/* src/mp/wasm_compat.h — the browser build (MERAKIPLUS_WASM, Sim Studio: sim/studio/README.md) on wasm32-wasi.
 * Included by src/merakiplus.c BEFORE the system headers, only when MERAKIPLUS_WASM is defined.
 *
 * The browser build IS the SIM build (SIM_RT: sandbox paths, state JSON, test hooks) compiled for WASI, with
 * src/mp/wasm_hal.c replacing main()'s blocking loop by exports. WASI (wasi-libc) has no processes, no signals,
 * no termios and no unix sockets; this header turns on wasi-libc's emulations and declares the few POSIX calls the
 * SIM build references, so the SAME source compiles unchanged. Their stub bodies are in src/mp/wasm_hal.c. None of
 * them is reached at run time in the browser: the pty becomes a WASI device file provided by the JS shim
 * (sim/studio/wasi.js), the ctrl socket is not opened (no MERAKIPLUS_SIM_CTRL; hooks go through mp_ctrl()), and
 * fork/exec only exist on paths the SIM build never takes (stock-UI launch, sound helper, wifi daemons). */
#ifndef MERAKIPLUS_WASM_COMPAT_H
#define MERAKIPLUS_WASM_COMPAT_H
#ifndef MERAKIPLUS_SIM
#define MERAKIPLUS_SIM 1                   /* -> SIM_RT */
#endif
#define _WASI_EMULATED_SIGNAL 1            /* signal(): link -lwasi-emulated-signal */
#define _WASI_EMULATED_MMAN 1              /* mmap(MAP_ANON) for the fb buffer: -lwasi-emulated-mman */
#define _WASI_EMULATED_PROCESS_CLOCKS 1    /* <sys/resource.h>: -lwasi-emulated-process-clocks */
#define _WASI_EMULATED_GETPID 1            /* getpid(): -lwasi-emulated-getpid */
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <signal.h>
/* <sys/wait.h> (musl's generic header) names siginfo_t, which wasi-libc's emulated <signal.h> does not define */
typedef struct { int si_signo, si_code, si_errno; } siginfo_t;
/* wasi-libc's struct sockaddr_un has no sun_path (WASI has no unix sockets): use the POSIX layout instead */
#include <__typedef_sa_family_t.h>
#define __wasilibc___struct_sockaddr_un_h 1
struct sockaddr_un { sa_family_t sun_family; char sun_path[108]; };
struct sockaddr;
/* not declared by wasi-libc at all (no processes, no fifos, no socket()) — stubs in wasm_hal.c fail with ENOSYS */
pid_t fork(void);
pid_t setsid(void);
int dup2(int,int);
int execl(const char*,const char*,...);
int mkfifo(const char*,mode_t);
int socket(int,int,int);
int bind(int,const struct sockaddr*,unsigned);
int listen(int,int);
#endif
