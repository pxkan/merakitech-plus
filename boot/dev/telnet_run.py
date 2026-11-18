#!/usr/bin/env python3
"""Kør ÉN kommando på kaffemaskinens root-telnet (<display-ip>:23, from $TELNET_HOST).
Logger kommando + råt svar til telnet_session.log. Ingen writes uden eksplicit kommando.
Brug: python3 telnet_run.py "<shell-kommando>" [timeout_sek]
"""
import socket, sys, time, pathlib, datetime, re, os

HOST, PORT = os.environ["TELNET_HOST"], int(os.environ.get("TELNET_PORT", "23"))  # set TELNET_HOST=<display-ip>
HERE = pathlib.Path(__file__).resolve().parent
LOG = HERE / "telnet_session.log"
IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240

def strip_iac(sock, chunk):
    """Fjern telnet-IAC-sekvenser; svar WONT/DONT på forhandlinger."""
    clean = bytearray()
    i = 0
    while i < len(chunk):
        b = chunk[i]
        if b == IAC:
            if i + 1 >= len(chunk):
                break
            cmd = chunk[i + 1]
            if cmd in (DO, DONT, WILL, WONT) and i + 2 < len(chunk):
                resp = WONT if cmd in (DO, DONT) else DONT
                try:
                    sock.sendall(bytes([IAC, resp, chunk[i + 2]]))
                except OSError:
                    pass
                i += 3
                continue
            if cmd == SB:
                j = chunk.find(bytes([IAC, SE]), i)
                i = j + 2 if j >= 0 else len(chunk)
                continue
            if cmd == IAC:
                clean.append(IAC)
                i += 2
                continue
            i += 2
            continue
        clean.append(b)
        i += 1
    return bytes(clean)

def main():
    cmd = sys.argv[1]
    timeout = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
    tag = f"{int(time.time()*1000) % 100000000:08d}"
    start, endm = f"__WT_S_{tag}__", f"__WT_E_{tag}__"
    wire = f"echo {start}; {cmd}; echo {endm}:$?\r\n".encode()

    sock = socket.create_connection((HOST, PORT), timeout=10)
    sock.settimeout(0.5)
    # dræn indledende forhandling + prompt
    t0 = time.time()
    while time.time() - t0 < 1.0:
        try:
            strip_iac(sock, sock.recv(65536))
        except socket.timeout:
            break
    sock.sendall(wire)
    data = b""
    deadline = time.time() + timeout
    # Slut-markøren skal stå ALENE på en linje med numerisk exit-kode.
    # (Input-ekkoet indeholder markøren som del af en længere linje med '$rc' — må ikke matche.)
    end_re = re.compile(rb"(?:^|\n)" + endm.encode() + rb":[0-9]+\r?\n")
    while time.time() < deadline:
        try:
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += strip_iac(sock, chunk)
            if end_re.search(data):
                break
        except socket.timeout:
            continue
    sock.close()

    text = data.decode("utf-8", errors="replace")
    (HERE / "last_raw.bin").write_bytes(data)
    lines = text.splitlines()
    out, exitcode, started = [], None, False
    for ln in lines:
        s = ln.strip("\r")
        if not started:
            if s == start:
                started = True
            continue
        if s.startswith(endm + ":") and s.split(":", 1)[1].strip().isdigit():
            exitcode = s.split(":", 1)[1]
            break
        out.append(s)
    body = "\n".join(out)
    ts = datetime.datetime.now().isoformat(timespec="seconds")
    with LOG.open("a") as f:
        f.write(f"\n===== {ts} | exit={exitcode} | timeout={timeout}s =====\n$ {cmd}\n{body}\n")
    print(body)
    print(f"[exit={exitcode}]", file=sys.stderr)
    if exitcode is None:
        print("[ADVARSEL: slut-markør ikke set — svar kan være ufuldstændigt]", file=sys.stderr)
        sys.exit(2)

if __name__ == "__main__":
    main()