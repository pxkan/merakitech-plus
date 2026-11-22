// sim/studio/wasi.js — a small WASI (wasi_snapshot_preview1) host for the Meraki+ browser build.
//
// Just enough of WASI for src/merakiplus.c built with -DMERAKIPLUS_WASM (src/mp/wasm_hal.c): an in-memory file system
// (directories, files, symlinks, device nodes), environment variables, and clocks that the HOST owns (the studio can
// pause the UI, run it at 1x or 20x, and set the time of day). Runs in browsers and in node >= 18; no dependencies.
//
// Device nodes and sinks replace the SIM build's host files:
//   * a device node (fs.mkdev) is a byte pipe: the UART (/dev/ttyS1) is wired to the JS machine model, /dev/null is empty.
//   * a sink (fs.sink) is a path prefix whose writes go to a JS callback instead of memory: the SIM's state JSONL
//     (only the latest line is kept) and the fb dump file (discarded — the canvas reads the framebuffer directly).
//
// Written for this project (no vendored code). Error numbers and struct layouts follow the WASI preview1 spec.

const E = { SUCCESS: 0, ACCES: 2, AGAIN: 6, BADF: 8, EXIST: 20, INVAL: 28, IO: 29, ISDIR: 31, LOOP: 32, NOENT: 44,
  NOSYS: 52, NOTDIR: 54, NOTEMPTY: 55, NOTSUP: 58, PERM: 63, SPIPE: 70 };
const FT = { UNKNOWN: 0, CHAR: 2, DIR: 3, FILE: 4, LINK: 7 };   // wasi filetype
const OFLAG = { CREAT: 1, DIRECTORY: 2, EXCL: 4, TRUNC: 8 };
const FDFLAG = { APPEND: 1, NONBLOCK: 4 };
const LOOKUP_SYMLINK_FOLLOW = 1;
const ALL_RIGHTS = 0x1fffffffn;

export class WasiExit extends Error {
  constructor(code) { super(`proc_exit(${code})`); this.code = code; }
}

let inoSeq = 1;
const now64 = () => BigInt(Date.now()) * 1000000n;
function mkdirNode() { return { type: 'dir', ino: inoSeq++, children: new Map(), mtime: now64() }; }
function mkfileNode(data) { return { type: 'file', ino: inoSeq++, data: data || new Uint8Array(0), size: data ? data.length : 0, mtime: now64() }; }

// ------------------------------------------------------------------ in-memory file system
export class MemFS {
  constructor() {
    this.root = mkdirNode();
    this.sinks = [];              // [{prefix, write(bytes, path), discardRename}]
  }
  _split(path) { return path.split('/').filter(p => p && p !== '.'); }
  // resolve a path to {parent, name, node}; follow symlinks on intermediate components (and the last one if follow)
  resolve(path, base = this.root, follow = true, depth = 0) {
    if (depth > 16) return { err: E.LOOP };
    let dir = path.startsWith('/') ? this.root : base;
    const parts = this._split(path);
    const stack = [];             // parents for '..'
    for (let i = 0; i < parts.length; i++) {
      const name = parts[i], last = i === parts.length - 1;
      if (name === '..') { dir = stack.pop() || this.root; if (last) return { parent: stack[stack.length - 1] || this.root, name: '.', node: dir }; continue; }
      if (dir.type !== 'dir') return { err: E.NOTDIR };
      let node = dir.children.get(name);
      if (node && node.type === 'link' && (!last || follow)) {
        const r = this.resolve(node.target, dir, true, depth + 1);
        if (r.err) return r;
        node = r.node;
        if (!node) return last ? { parent: r.parent, name: r.name, node: null } : { err: E.NOENT };
      }
      if (last) return { parent: dir, name, node: node || null };
      if (!node) return { err: E.NOENT };
      stack.push(dir); dir = node;
    }
    return { parent: dir, name: '.', node: dir };
  }
  sinkFor(path) { return this.sinks.find(s => path.startsWith(s.prefix)) || null; }
  mkdirp(path) {
    let dir = this.root;
    for (const name of this._split(path)) {
      let n = dir.children.get(name);
      if (!n) { n = mkdirNode(); dir.children.set(name, n); }
      dir = n;
    }
    return dir;
  }
  writeFile(path, data) {
    const bytes = typeof data === 'string' ? new TextEncoder().encode(data) : new Uint8Array(data);
    const parts = this._split(path), name = parts.pop();
    const dir = this.mkdirp(parts.join('/'));
    dir.children.set(name, mkfileNode(bytes.slice()));
  }
  readFile(path) {
    const r = this.resolve(path);
    if (r.err || !r.node || r.node.type !== 'file') return null;
    return r.node.data.slice(0, r.node.size);
  }
  readText(path) { const b = this.readFile(path); return b ? new TextDecoder().decode(b) : null; }
  exists(path) { const r = this.resolve(path); return !r.err && !!r.node; }
  unlink(path) { const r = this.resolve(path, this.root, false); if (r.node) r.parent.children.delete(r.name); }
  symlink(target, path) {
    const parts = this._split(path), name = parts.pop();
    this.mkdirp(parts.join('/')).children.set(name, { type: 'link', ino: inoSeq++, target, mtime: now64() });
  }
  // a character device: {read(maxBytes) -> Uint8Array, write(Uint8Array)}
  mkdev(path, dev) {
    const parts = this._split(path), name = parts.pop();
    this.mkdirp(parts.join('/')).children.set(name, { type: 'dev', ino: inoSeq++, dev, mtime: now64() });
  }
  sink(prefix, write) { this.sinks.push({ prefix, write }); }
  // a snapshot of the tree as {path: text|Uint8Array} (files only) — for the studio's "files" view and exports
  list(dir = this.root, prefix = '') {
    const out = {};
    for (const [name, n] of dir.children) {
      const p = `${prefix}/${name}`;
      if (n.type === 'dir') Object.assign(out, this.list(n, p));
      else if (n.type === 'file') out[p] = n.data.slice(0, n.size);
    }
    return out;
  }
}

// ------------------------------------------------------------------ the WASI host
export class Wasi {
  // opts: {fs: MemFS, env: {K: V}, args: [..], clock: {mono_ns: BigInt, wall_offset_ns: BigInt}, log(stream, line)}
  constructor(opts = {}) {
    this.fs = opts.fs || new MemFS();
    this.env = opts.env || {};
    this.args = opts.args || ['merakiplus'];
    this.clock = opts.clock || { mono_ns: 1000n * 1000000000n, wall_offset_ns: null };
    this.log = opts.log || ((s, l) => (s === 2 ? console.warn : console.log)(`[ui] ${l}`));
    this.fds = new Map();
    this.fds.set(0, { kind: 'stdio', n: 0 });
    this.fds.set(1, { kind: 'stdio', n: 1, buf: '' });
    this.fds.set(2, { kind: 'stdio', n: 2, buf: '' });
    this.fds.set(3, { kind: 'node', node: this.fs.root, path: '/', preopen: '/', pos: 0, flags: 0 });
    this.nextFd = 4;
    this.memory = null;
    this.imports = { wasi_snapshot_preview1: this._imports() };
  }
  setMemory(mem) { this.memory = mem; }
  // wall clock: UTC ns = mono + offset (the studio sets the offset to move the time of day)
  wallNs() {
    if (this.clock.wall_offset_ns === null) this.clock.wall_offset_ns = now64() - this.clock.mono_ns;
    return this.clock.mono_ns + this.clock.wall_offset_ns;
  }

  _dv() { return new DataView(this.memory.buffer); }
  _u8() { return new Uint8Array(this.memory.buffer); }
  _str(ptr, len) { return new TextDecoder().decode(this._u8().subarray(ptr, ptr + len)); }
  _fdPath(fd, rel) {             // absolute path of rel inside directory fd
    const f = this.fds.get(fd);
    const base = f && f.path ? f.path : '/';
    if (rel.startsWith('/')) return rel;
    return (base.endsWith('/') ? base : base + '/') + rel;
  }
  _iovs(iovs, n) {
    const dv = this._dv(), out = [];
    for (let i = 0; i < n; i++) out.push([dv.getUint32(iovs + i * 8, true), dv.getUint32(iovs + i * 8 + 4, true)]);
    return out;
  }
  _stdio(f, bytes) {
    f.buf += new TextDecoder().decode(bytes);
    let k;
    while ((k = f.buf.indexOf('\n')) >= 0) { this.log(f.n, f.buf.slice(0, k)); f.buf = f.buf.slice(k + 1); }
    if (f.buf.length > 4096) { this.log(f.n, f.buf); f.buf = ''; }
  }
  _grow(node, need) {
    if (node.data.length >= need) return;
    const nd = new Uint8Array(Math.max(need, node.data.length * 2, 256));
    nd.set(node.data.subarray(0, node.size));
    node.data = nd;
  }
  _writeStat(ptr, node) {         // filestat: dev, ino, filetype(u8)+pad, nlink, size, atim, mtim, ctim
    const dv = this._dv();
    const ft = node.type === 'dir' ? FT.DIR : node.type === 'file' ? FT.FILE : node.type === 'link' ? FT.LINK : FT.CHAR;
    dv.setBigUint64(ptr, 1n, true);
    dv.setBigUint64(ptr + 8, BigInt(node.ino || 0), true);
    dv.setUint8(ptr + 16, ft);
    for (let i = 17; i < 24; i++) dv.setUint8(ptr + i, 0);
    dv.setBigUint64(ptr + 24, 1n, true);
    dv.setBigUint64(ptr + 32, BigInt(node.type === 'file' ? node.size : node.type === 'link' ? node.target.length : 0), true);
    const t = node.mtime || 0n;
    dv.setBigUint64(ptr + 40, t, true); dv.setBigUint64(ptr + 48, t, true); dv.setBigUint64(ptr + 56, t, true);
  }

  _imports() {
    const self = this;
    const w = {
      args_sizes_get(pc, ps) {
        const dv = self._dv(), enc = self.args.map(a => new TextEncoder().encode(a + '\0'));
        dv.setUint32(pc, enc.length, true); dv.setUint32(ps, enc.reduce((s, b) => s + b.length, 0), true); return E.SUCCESS;
      },
      args_get(argv, buf) {
        const dv = self._dv(), u8 = self._u8();
        self.args.forEach((a, i) => { const b = new TextEncoder().encode(a + '\0'); dv.setUint32(argv + i * 4, buf, true); u8.set(b, buf); buf += b.length; });
        return E.SUCCESS;
      },
      environ_sizes_get(pc, ps) {
        const dv = self._dv(), enc = Object.entries(self.env).map(([k, v]) => new TextEncoder().encode(`${k}=${v}\0`));
        dv.setUint32(pc, enc.length, true); dv.setUint32(ps, enc.reduce((s, b) => s + b.length, 0), true); return E.SUCCESS;
      },
      environ_get(envp, buf) {
        const dv = self._dv(), u8 = self._u8();
        Object.entries(self.env).forEach(([k, v], i) => { const b = new TextEncoder().encode(`${k}=${v}\0`); dv.setUint32(envp + i * 4, buf, true); u8.set(b, buf); buf += b.length; });
        return E.SUCCESS;
      },
      clock_res_get(id, ptr) { self._dv().setBigUint64(ptr, 1000000n, true); return E.SUCCESS; },
      clock_time_get(id, _prec, ptr) {
        // 0 = realtime (host wall clock, movable), 1 = monotonic (host sim clock), 2/3 = cpu time -> monotonic
        self._dv().setBigUint64(ptr, id === 0 ? self.wallNs() : self.clock.mono_ns, true);
        return E.SUCCESS;
      },
      fd_advise() { return E.SUCCESS; },
      fd_allocate() { return E.SUCCESS; },
      fd_datasync() { return E.SUCCESS; },
      fd_sync(fd) { return self.fds.has(fd) ? E.SUCCESS : E.BADF; },
      fd_close(fd) { if (!self.fds.has(fd) || fd < 3) return E.BADF; self.fds.delete(fd); return E.SUCCESS; },
      fd_fdstat_get(fd, ptr) {
        const f = self.fds.get(fd); if (!f) return E.BADF;
        const dv = self._dv();
        let ft = FT.CHAR;
        if (f.kind === 'node') ft = f.node.type === 'dir' ? FT.DIR : f.node.type === 'file' ? FT.FILE : FT.CHAR;
        dv.setUint8(ptr, ft); dv.setUint16(ptr + 2, f.flags || 0, true);
        dv.setBigUint64(ptr + 8, ALL_RIGHTS, true); dv.setBigUint64(ptr + 16, ALL_RIGHTS, true);
        return E.SUCCESS;
      },
      fd_fdstat_set_flags(fd, flags) { const f = self.fds.get(fd); if (!f) return E.BADF; f.flags = flags; return E.SUCCESS; },
      fd_fdstat_set_rights() { return E.SUCCESS; },
      fd_filestat_get(fd, ptr) {
        const f = self.fds.get(fd); if (!f) return E.BADF;
        self._writeStat(ptr, f.kind === 'node' ? f.node : { type: 'dev', ino: 0 }); return E.SUCCESS;
      },
      fd_filestat_set_size(fd, size) {
        const f = self.fds.get(fd); if (!f || f.kind !== 'node' || f.node.type !== 'file') return E.BADF;
        const n = Number(size); self._grow(f.node, n); if (n > f.node.size) f.node.data.fill(0, f.node.size, n); f.node.size = n; return E.SUCCESS;
      },
      fd_filestat_set_times() { return E.SUCCESS; },
      fd_prestat_get(fd, ptr) {
        const f = self.fds.get(fd); if (!f || !f.preopen) return E.BADF;
        const dv = self._dv(); dv.setUint8(ptr, 0); dv.setUint32(ptr + 4, new TextEncoder().encode(f.preopen).length, true); return E.SUCCESS;
      },
      fd_prestat_dir_name(fd, ptr, len) {
        const f = self.fds.get(fd); if (!f || !f.preopen) return E.BADF;
        self._u8().set(new TextEncoder().encode(f.preopen).subarray(0, len), ptr); return E.SUCCESS;
      },
      fd_read(fd, iovs, n, pnread) { return self._read(fd, iovs, n, pnread, null); },
      fd_pread(fd, iovs, n, off, pnread) { return self._read(fd, iovs, n, pnread, Number(off)); },
      fd_write(fd, iovs, n, pnw) { return self._write(fd, iovs, n, pnw, null); },
      fd_pwrite(fd, iovs, n, off, pnw) { return self._write(fd, iovs, n, pnw, Number(off)); },
      fd_seek(fd, off, whence, pnew) {
        const f = self.fds.get(fd); if (!f) return E.BADF;
        if (f.kind !== 'node' || f.node.type !== 'file') return f.kind === 'sink' ? (self._dv().setBigUint64(pnew, 0n, true), E.SUCCESS) : E.SPIPE;
        const o = Number(off), base = whence === 0 ? 0 : whence === 1 ? f.pos : f.node.size;
        if (base + o < 0) return E.INVAL;
        f.pos = base + o; self._dv().setBigUint64(pnew, BigInt(f.pos), true); return E.SUCCESS;
      },
      fd_tell(fd, ptr) { const f = self.fds.get(fd); if (!f) return E.BADF; self._dv().setBigUint64(ptr, BigInt(f.pos || 0), true); return E.SUCCESS; },
      fd_readdir(fd, buf, len, cookie, pused) {
        const f = self.fds.get(fd); if (!f || f.kind !== 'node' || f.node.type !== 'dir') return E.BADF;
        const ents = [['.', f.node], ['..', f.node], ...f.node.children];
        const dv = self._dv(), u8 = self._u8(); let off = 0;
        for (let i = Number(cookie); i < ents.length && off < len; i++) {
          const [name, n] = ents[i], nb = new TextEncoder().encode(name);
          const hdr = new Uint8Array(24), hv = new DataView(hdr.buffer);
          hv.setBigUint64(0, BigInt(i + 1), true); hv.setBigUint64(8, BigInt(n.ino || 0), true); hv.setUint32(16, nb.length, true);
          hv.setUint8(20, n.type === 'dir' ? FT.DIR : n.type === 'file' ? FT.FILE : n.type === 'link' ? FT.LINK : FT.CHAR);
          const ent = new Uint8Array(24 + nb.length); ent.set(hdr); ent.set(nb, 24);
          const k = Math.min(ent.length, len - off); u8.set(ent.subarray(0, k), buf + off); off += k;
        }
        dv.setUint32(pused, off, true); return E.SUCCESS;
      },
      fd_renumber() { return E.NOSYS; },
      path_create_directory(fd, p, l) {
        const r = self.fs.resolve(self._fdPath(fd, self._str(p, l)));
        if (r.err) return r.err; if (r.node) return E.EXIST;
        r.parent.children.set(r.name, mkdirNode()); return E.SUCCESS;
      },
      path_filestat_get(fd, flags, p, l, ptr) {
        const path = self._fdPath(fd, self._str(p, l));
        const sk = self.fs.sinkFor(path);
        if (sk) { self._writeStat(ptr, { type: 'file', size: 0, ino: 0 }); return E.SUCCESS; }
        const r = self.fs.resolve(path, self.fs.root, !!(flags & LOOKUP_SYMLINK_FOLLOW));
        if (r.err) return r.err; if (!r.node) return E.NOENT;
        self._writeStat(ptr, r.node); return E.SUCCESS;
      },
      path_filestat_set_times() { return E.SUCCESS; },
      path_link() { return E.NOTSUP; },
      path_open(dirfd, dirflags, p, l, oflags, _rb, _ri, fdflags, pfd) {
        const path = self._fdPath(dirfd, self._str(p, l));
        const sk = self.fs.sinkFor(path);
        let f;
        if (sk) f = { kind: 'sink', sink: sk, path, pos: 0, flags: fdflags };
        else {
          const r = self.fs.resolve(path, self.fs.root, true);   // the SIM never opens a symlink itself (O_NOFOLLOW unused)
          if (r.err) return r.err;
          let node = r.node;
          if (node && (oflags & OFLAG.EXCL) && (oflags & OFLAG.CREAT)) return E.EXIST;
          if (!node) {
            if (!(oflags & OFLAG.CREAT)) return E.NOENT;
            if (!r.parent || r.parent.type !== 'dir') return E.NOTDIR;
            node = mkfileNode(); r.parent.children.set(r.name, node);
          }
          if ((oflags & OFLAG.DIRECTORY) && node.type !== 'dir') return E.NOTDIR;
          if (node.type === 'file' && (oflags & OFLAG.TRUNC)) { node.size = 0; node.mtime = now64(); }
          f = { kind: 'node', node, path, pos: 0, flags: fdflags };
        }
        const fd = self.nextFd++; self.fds.set(fd, f);
        self._dv().setUint32(pfd, fd, true); return E.SUCCESS;
      },
      path_readlink(fd, p, l, buf, blen, pused) {
        const r = self.fs.resolve(self._fdPath(fd, self._str(p, l)), self.fs.root, false);
        if (r.err) return r.err; if (!r.node) return E.NOENT; if (r.node.type !== 'link') return E.INVAL;
        const b = new TextEncoder().encode(r.node.target).subarray(0, blen);
        self._u8().set(b, buf); self._dv().setUint32(pused, b.length, true); return E.SUCCESS;
      },
      path_remove_directory(fd, p, l) {
        const r = self.fs.resolve(self._fdPath(fd, self._str(p, l)), self.fs.root, false);
        if (r.err) return r.err; if (!r.node) return E.NOENT; if (r.node.type !== 'dir') return E.NOTDIR;
        if (r.node.children.size) return E.NOTEMPTY; r.parent.children.delete(r.name); return E.SUCCESS;
      },
      path_rename(fd, p, l, fd2, p2, l2) {
        const from = self._fdPath(fd, self._str(p, l)), to = self._fdPath(fd2, self._str(p2, l2));
        if (self.fs.sinkFor(from) || self.fs.sinkFor(to)) return E.SUCCESS;     // fb dump: tmp + rename into a sink
        const a = self.fs.resolve(from, self.fs.root, false), b = self.fs.resolve(to, self.fs.root, false);
        if (a.err) return a.err; if (b.err) return b.err; if (!a.node) return E.NOENT;
        if (b.node && b.node.type === 'dir' && b.node.children.size) return E.NOTEMPTY;
        a.parent.children.delete(a.name); b.parent.children.set(b.name, a.node); a.node.mtime = now64(); return E.SUCCESS;
      },
      path_symlink(p, l, fd, p2, l2) {
        const target = self._str(p, l), r = self.fs.resolve(self._fdPath(fd, self._str(p2, l2)), self.fs.root, false);
        if (r.err) return r.err; if (r.node) return E.EXIST;
        r.parent.children.set(r.name, { type: 'link', ino: inoSeq++, target, mtime: now64() }); return E.SUCCESS;
      },
      path_unlink_file(fd, p, l) {
        const path = self._fdPath(fd, self._str(p, l));
        if (self.fs.sinkFor(path)) return E.SUCCESS;
        const r = self.fs.resolve(path, self.fs.root, false);
        if (r.err) return r.err; if (!r.node) return E.NOENT; if (r.node.type === 'dir') return E.ISDIR;
        r.parent.children.delete(r.name); return E.SUCCESS;
      },
      poll_oneoff(_in, _out, _n, pn) { self._dv().setUint32(pn, 0, true); return E.SUCCESS; },
      proc_exit(code) { throw new WasiExit(code); },
      proc_raise() { return E.NOSYS; },
      sched_yield() { return E.SUCCESS; },
      random_get(buf, len) {
        const u8 = self._u8().subarray(buf, buf + len);
        if (globalThis.crypto && crypto.getRandomValues) for (let o = 0; o < len; o += 65536) crypto.getRandomValues(u8.subarray(o, Math.min(len, o + 65536)));
        else for (let i = 0; i < len; i++) u8[i] = (Math.random() * 256) | 0;
        return E.SUCCESS;
      },
      sock_accept() { return E.NOSYS; }, sock_recv() { return E.NOSYS; }, sock_send() { return E.NOSYS; }, sock_shutdown() { return E.NOSYS; },
    };
    return w;
  }

  _read(fd, iovs, n, pnread, at) {
    const f = this.fds.get(fd); if (!f) return E.BADF;
    const dv = this._dv(), u8 = this._u8();
    let total = 0;
    if (f.kind === 'stdio' || f.kind === 'sink') { dv.setUint32(pnread, 0, true); return E.SUCCESS; }
    const node = f.node;
    if (node.type === 'dir') return E.ISDIR;
    if (node.type === 'dev') {
      for (const [ptr, len] of this._iovs(iovs, n)) {
        const b = node.dev.read ? node.dev.read(len) : null;
        if (!b || !b.length) break;
        u8.set(b, ptr); total += b.length; if (b.length < len) break;
      }
      if (!total && (f.flags & FDFLAG.NONBLOCK) && node.dev.nonblockEagain) return E.AGAIN;
      dv.setUint32(pnread, total, true); return E.SUCCESS;
    }
    let pos = at === null ? f.pos : at;
    for (const [ptr, len] of this._iovs(iovs, n)) {
      const k = Math.max(0, Math.min(len, node.size - pos));
      u8.set(node.data.subarray(pos, pos + k), ptr); pos += k; total += k; if (k < len) break;
    }
    if (at === null) f.pos = pos;
    dv.setUint32(pnread, total, true); return E.SUCCESS;
  }
  _write(fd, iovs, n, pnw, at) {
    const f = this.fds.get(fd); if (!f) return E.BADF;
    const dv = this._dv(), u8 = this._u8();
    const parts = this._iovs(iovs, n);
    const total = parts.reduce((s, [, l]) => s + l, 0);
    if (f.kind === 'stdio') { for (const [p, l] of parts) this._stdio(f, u8.subarray(p, p + l)); }
    else if (f.kind === 'sink') { if (f.sink.write) for (const [p, l] of parts) if (l) f.sink.write(u8.subarray(p, p + l), f.path); }
    else {
      const node = f.node;
      if (node.type === 'dir') return E.ISDIR;
      if (node.type === 'dev') { for (const [p, l] of parts) if (l && node.dev.write) node.dev.write(u8.slice(p, p + l)); }
      else {
        let pos = at === null ? ((f.flags & FDFLAG.APPEND) ? node.size : f.pos) : at;
        this._grow(node, pos + total);
        for (const [p, l] of parts) { node.data.set(u8.subarray(p, p + l), pos); pos += l; }
        if (pos > node.size) node.size = pos;
        node.mtime = now64();
        if (at === null) f.pos = pos;
      }
    }
    dv.setUint32(pnw, total, true); return E.SUCCESS;
  }
}
