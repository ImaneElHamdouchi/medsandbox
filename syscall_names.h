#ifndef SYSCALL_NAMES_H
#define SYSCALL_NAMES_H

#include <sys/syscall.h>

static inline const char *syscall_name(long syscall) {
    switch (syscall) {
        case SYS_read: return "read";
        case SYS_write: return "write";
        case SYS_open: return "open";
        case SYS_close: return "close";
        case SYS_mmap: return "mmap";
        case SYS_mprotect: return "mprotect";
        case SYS_munmap: return "munmap";
        case SYS_brk: return "brk";
        case SYS_getpid: return "getpid";
        case SYS_socket: return "socket";
        case SYS_connect: return "connect";
        case SYS_clone: return "clone";
        case SYS_fork: return "fork";
        case SYS_execve: return "execve";
        case SYS_exit: return "exit";
        case SYS_setpgid: return "setpgid";
        case SYS_prctl: return "prctl";
        case SYS_arch_prctl: return "arch_prctl";
        case SYS_futex: return "futex";
        case SYS_exit_group: return "exit_group";
        case SYS_openat: return "openat";
        case SYS_newfstatat: return "newfstatat";
        case SYS_set_robust_list: return "set_robust_list";
        case SYS_prlimit64: return "prlimit64";
        case SYS_seccomp: return "seccomp";
        case SYS_getrandom: return "getrandom";
        case SYS_rseq: return "rseq";
        default: return "unknown";
    }
}

#endif
