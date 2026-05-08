#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <sys/resource.h>
#include <string.h>
#include <getopt.h>
#include <seccomp.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sched.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/mount.h>
#include <sys/select.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <pty.h>
#include <termios.h>
#include <linux/limits.h>
#include <sys/capability.h>
#include <jansson.h>
#include <limits.h>

#include "syscall_names.h"

#ifndef SCMP_ACT_KILL_PROCESS
#define SCMP_ACT_KILL_PROCESS SCMP_ACT_KILL
#endif

#define DEFAULT_LOG_FILE "logs/sandbox.log"

typedef struct {
    int wall_time_limit;
    int memory_limit_mb;
    int cpu_limit_seconds;
    int file_limit_mb;
    int max_processes;

    int verbose;
    int interactive;

    int network_block;
    int readonly;
    int strict;
    int clinical_mode;

    int isolate_net;
    int isolate_uts;
    int isolate_pid;
    int isolate_mount;

    int trace_syscalls;

    const char *hostname;
    const char *profile;

    const char *log_file;
    const char *stdout_file;
    const char *stderr_file;

    const char *rootfs;
    const char *workdir;

    const char *cgroup_dir;

    const char *program;
} SandboxConfig;

typedef struct {
    pid_t pid;
    int exit_code;
    int signal_number;
    int timed_out;
    long runtime_seconds;
    long peak_memory_kb;
} SandboxResult;

int parse_positive_int(const char *value, const char *name) {
    char *end = NULL;
    errno = 0;

    long number = strtol(value, &end, 10);

    if (errno != 0 || end == value || *end != '\0' || number <= 0 || number > INT_MAX) {
        fprintf(stderr, "Invalid %s: %s\n", name, value);
        exit(1);
    }

    return (int)number;
}

void print_usage(const char *program_name) {
    printf("Usage: %s [options] <program> [args...]\n", program_name);
    printf("\nOptions:\n");
    printf("  -h                     Show help\n");
    printf("  -v                     Verbose mode\n");
    printf("  -n                     Block network syscalls\n");
    printf("  -o file                JSON log file\n");
    printf("  -t seconds             Wall-clock time limit\n");
    printf("  -m MB                  Memory limit\n");
    printf("  -c seconds             CPU limit\n");
    printf("  -f MB                  File size limit\n");
    printf("  --profile name         basic, networkless, strict, readonly, clinical\n");
    printf("  --clinical             Clinical mode: strict readonly FHIR/plugin execution\n");
    printf("  --max-procs N          Process limit\n");
    printf("  --stdout file          Capture stdout\n");
    printf("  --stderr file          Capture stderr\n");
    printf("  --interactive          Run with pseudo-terminal\n");
    printf("  --isolate-net          Create network namespace\n");
    printf("  --isolate-uts          Create UTS namespace\n");
    printf("  --isolate-pid          Create PID namespace\n");
    printf("  --isolate-mount        Create mount namespace\n");
    printf("  --hostname name        Set hostname inside UTS namespace\n");
    printf("  --rootfs path          chroot into root filesystem\n");
    printf("  --workdir path         Working directory inside rootfs, default /\n");
    printf("  --trace-syscalls       Basic syscall tracing\n");
    printf("  --cgroup-dir path      Use cgroup v2 directory\n");
    printf("  --config file          Load JSON config\n");
}

void mkdir_p(const char *dir) {
    char path[PATH_MAX];

    if (dir == NULL || dir[0] == '\0') {
        return;
    }

    snprintf(path, sizeof(path), "%s", dir);

    size_t len = strlen(path);

    if (len == 0) {
        return;
    }

    if (path[len - 1] == '/') {
        path[len - 1] = '\0';
    }

    for (char *p = path + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';

            if (mkdir(path, 0755) != 0 && errno != EEXIST) {
                return;
            }

            *p = '/';
        }
    }

    mkdir(path, 0755);
}

void mkdir_parent_for_file(const char *file_path) {
    char path[PATH_MAX];

    if (file_path == NULL) {
        return;
    }

    snprintf(path, sizeof(path), "%s", file_path);

    char *slash = strrchr(path, '/');

    if (slash == NULL) {
        return;
    }

    if (slash == path) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }

    mkdir_p(path);
}

static json_t *json_string_safe(const char *value) {
    return json_string(value != NULL ? value : "");
}

void write_log_json(const char *log_file, const char *event, const char *program, pid_t pid, const char *details) {
    if (log_file == NULL) {
        return;
    }

    mkdir_parent_for_file(log_file);

    FILE *log = fopen(log_file, "a");

    if (log == NULL) {
        return;
    }

    json_t *root = json_object();

    json_object_set_new(root, "schema_version", json_string("1.0"));
    json_object_set_new(root, "timestamp", json_integer((json_int_t)time(NULL)));
    json_object_set_new(root, "event", json_string_safe(event));
    json_object_set_new(root, "program", json_string_safe(program));
    json_object_set_new(root, "pid", json_integer(pid));
    json_object_set_new(root, "details", json_string_safe(details));

    json_dumpf(root, log, JSON_COMPACT);
    fprintf(log, "\n");

    json_decref(root);
    fclose(log);
}

void apply_profile(SandboxConfig *config) {
    if (strcmp(config->profile, "basic") == 0) {
        return;
    }

    if (strcmp(config->profile, "networkless") == 0) {
        config->network_block = 1;
        return;
    }

    if (strcmp(config->profile, "readonly") == 0) {
        config->readonly = 1;
        return;
    }

    if (strcmp(config->profile, "clinical") == 0) {
        config->clinical_mode = 1;
        config->network_block = 1;
        config->readonly = 1;
        config->strict = 1;
        return;
    }

    if (strcmp(config->profile, "strict") == 0) {
        config->strict = 1;
        config->network_block = 1;
        config->readonly = 1;
        return;
    }

    fprintf(stderr, "Unknown profile: %s\n", config->profile);
    fprintf(stderr, "Available profiles: basic, networkless, strict, readonly, clinical\n");
    exit(1);
}

void apply_limits(const SandboxConfig *config) {
    struct rlimit limit;

    limit.rlim_cur = (rlim_t)config->memory_limit_mb * 1024 * 1024;
    limit.rlim_max = (rlim_t)config->memory_limit_mb * 1024 * 1024;
    if (setrlimit(RLIMIT_AS, &limit) != 0) {
        perror("setrlimit memory");
        exit(1);
    }

    limit.rlim_cur = config->cpu_limit_seconds;
    limit.rlim_max = config->cpu_limit_seconds;
    if (setrlimit(RLIMIT_CPU, &limit) != 0) {
        perror("setrlimit cpu");
        exit(1);
    }

    limit.rlim_cur = (rlim_t)config->file_limit_mb * 1024 * 1024;
    limit.rlim_max = (rlim_t)config->file_limit_mb * 1024 * 1024;
    if (setrlimit(RLIMIT_FSIZE, &limit) != 0) {
        perror("setrlimit file size");
        exit(1);
    }

    limit.rlim_cur = config->max_processes;
    limit.rlim_max = config->max_processes;
    if (setrlimit(RLIMIT_NPROC, &limit) != 0) {
        perror("setrlimit max processes");
        exit(1);
    }
}

void redirect_output(const char *path, int target_fd) {
    if (path == NULL) {
        return;
    }

    mkdir_parent_for_file(path);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0) {
        perror("open output file");
        exit(1);
    }

    if (dup2(fd, target_fd) < 0) {
        perror("dup2");
        close(fd);
        exit(1);
    }

    close(fd);
}

void apply_namespaces(const SandboxConfig *config) {
    if (config->isolate_net) {
        if (unshare(CLONE_NEWNET) != 0) {
            perror("unshare net");
            exit(1);
        }
    }

    if (config->isolate_uts) {
        if (unshare(CLONE_NEWUTS) != 0) {
            perror("unshare uts");
            exit(1);
        }

        if (config->hostname != NULL) {
            if (sethostname(config->hostname, strlen(config->hostname)) != 0) {
                perror("sethostname");
                exit(1);
            }
        }
    }

    if (config->isolate_mount) {
        if (unshare(CLONE_NEWNS) != 0) {
            perror("unshare mount");
            exit(1);
        }

        if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
            perror("mount private");
            exit(1);
        }
    }
}

void apply_rootfs(const SandboxConfig *config) {
    if (config->rootfs == NULL) {
        return;
    }

    if (chdir(config->rootfs) != 0) {
        perror("chdir rootfs");
        exit(1);
    }

    if (chroot(".") != 0) {
        perror("chroot");
        exit(1);
    }

    if (chdir(config->workdir) != 0) {
        perror("chdir workdir");
        exit(1);
    }
}

void set_no_new_privs_or_die(void) {
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        perror("prctl PR_SET_NO_NEW_PRIVS");
        exit(1);
    }
}

void drop_capabilities(void) {
    cap_t caps = cap_init();

    if (caps == NULL) {
        return;
    }

    cap_set_proc(caps);
    cap_free(caps);
}

void close_extra_fds(void) {
    long max_fd = sysconf(_SC_OPEN_MAX);

    if (max_fd < 0 || max_fd > 4096) {
        max_fd = 4096;
    }

    for (int fd = 3; fd < max_fd; fd++) {
        close(fd);
    }
}

void sanitize_environment(void) {
    clearenv();
    setenv("PATH", "/usr/bin:/bin", 1);
    setenv("LANG", "C", 1);
    setenv("LC_ALL", "C", 1);
}

void allow_syscall_or_die(scmp_filter_ctx ctx, int syscall_nr, const char *name) {
    int rc = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall_nr, 0);

    if (rc < 0) {
        fprintf(stderr, "seccomp allow failed for %s: %s\n", name, strerror(-rc));
        seccomp_release(ctx);
        exit(1);
    }
}

#define ALLOW(ctx, name) allow_syscall_or_die((ctx), SCMP_SYS(name), #name)

void add_network_rules(scmp_filter_ctx ctx) {
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(socket), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(connect), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(accept), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(accept4), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(bind), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(listen), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(sendto), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(recvfrom), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(sendmsg), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(recvmsg), 0);
}

void add_readonly_rules(scmp_filter_ctx ctx) {
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(creat), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(unlink), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(unlinkat), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(rename), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(renameat), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(renameat2), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(mkdir), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(mkdirat), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(rmdir), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(truncate), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(ftruncate), 0);

    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(open), 1,
        SCMP_A1(SCMP_CMP_MASKED_EQ, O_ACCMODE, O_WRONLY));
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(open), 1,
        SCMP_A1(SCMP_CMP_MASKED_EQ, O_ACCMODE, O_RDWR));
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(open), 1,
        SCMP_A1(SCMP_CMP_MASKED_EQ, O_CREAT, O_CREAT));
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(open), 1,
        SCMP_A1(SCMP_CMP_MASKED_EQ, O_TRUNC, O_TRUNC));
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(open), 1,
        SCMP_A1(SCMP_CMP_MASKED_EQ, O_APPEND, O_APPEND));

    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(openat), 1,
        SCMP_A2(SCMP_CMP_MASKED_EQ, O_ACCMODE, O_WRONLY));
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(openat), 1,
        SCMP_A2(SCMP_CMP_MASKED_EQ, O_ACCMODE, O_RDWR));
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(openat), 1,
        SCMP_A2(SCMP_CMP_MASKED_EQ, O_CREAT, O_CREAT));
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(openat), 1,
        SCMP_A2(SCMP_CMP_MASKED_EQ, O_TRUNC, O_TRUNC));
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(openat), 1,
        SCMP_A2(SCMP_CMP_MASKED_EQ, O_APPEND, O_APPEND));
}

void add_strict_rules(scmp_filter_ctx ctx) {
    add_network_rules(ctx);
    add_readonly_rules(ctx);

    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(ptrace), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(mount), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(umount2), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(reboot), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(kexec_load), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(swapon), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(swapoff), 0);
}

void allow_readonly_open_for_clinical(scmp_filter_ctx ctx) {
    int rc;

    rc = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(open), 1,
        SCMP_A1(SCMP_CMP_MASKED_EQ, O_ACCMODE, O_RDONLY));
    if (rc < 0) goto fail;

    rc = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(openat), 1,
        SCMP_A2(SCMP_CMP_MASKED_EQ, O_ACCMODE, O_RDONLY));
    if (rc < 0) goto fail;

    return;

fail:
    fprintf(stderr, "seccomp readonly open rule failed: %s\n", strerror(-rc));
    seccomp_release(ctx);
    exit(1);
}

void load_clinical_seccomp_filter(void) {
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL_PROCESS);

    if (ctx == NULL) {
        perror("seccomp_init clinical");
        exit(1);
    }

    seccomp_attr_set(ctx, SCMP_FLTATR_CTL_NNP, 1);

    ALLOW(ctx, read);
    ALLOW(ctx, write);
    ALLOW(ctx, writev);
    ALLOW(ctx, close);
    ALLOW(ctx, exit);
    ALLOW(ctx, exit_group);
    ALLOW(ctx, execve);

#ifdef __NR_execveat
    allow_syscall_or_die(ctx, __NR_execveat, "execveat");
#endif

    ALLOW(ctx, brk);
    ALLOW(ctx, mmap);
    ALLOW(ctx, mprotect);
    ALLOW(ctx, munmap);
    ALLOW(ctx, mremap);
    ALLOW(ctx, madvise);
    ALLOW(ctx, arch_prctl);
    ALLOW(ctx, set_tid_address);
    ALLOW(ctx, set_robust_list);
    ALLOW(ctx, futex);

#ifdef __NR_rseq
    allow_syscall_or_die(ctx, __NR_rseq, "rseq");
#endif

    ALLOW(ctx, fstat);
    ALLOW(ctx, newfstatat);
    ALLOW(ctx, lseek);
    ALLOW(ctx, pread64);
    ALLOW(ctx, access);
    ALLOW(ctx, faccessat);
    ALLOW(ctx, readlink);
    ALLOW(ctx, getpid);
    ALLOW(ctx, getrandom);
    ALLOW(ctx, rt_sigaction);
    ALLOW(ctx, rt_sigprocmask);
    ALLOW(ctx, clock_gettime);
    ALLOW(ctx, prlimit64);
    ALLOW(ctx, fcntl);

#ifdef __NR_readlinkat
    allow_syscall_or_die(ctx, __NR_readlinkat, "readlinkat");
#endif

#ifdef __NR_statx
    allow_syscall_or_die(ctx, __NR_statx, "statx");
#endif

    allow_readonly_open_for_clinical(ctx);

    if (seccomp_load(ctx) != 0) {
        perror("seccomp_load clinical");
        seccomp_release(ctx);
        exit(1);
    }

    seccomp_release(ctx);
}

void load_seccomp_filter(const SandboxConfig *config) {
    if (config->clinical_mode) {
        load_clinical_seccomp_filter();
        return;
    }

    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);

    if (ctx == NULL) {
        perror("seccomp_init");
        exit(1);
    }

    if (config->strict) {
        add_strict_rules(ctx);
    } else {
        if (config->network_block) {
            add_network_rules(ctx);
        }

        if (config->readonly) {
            add_readonly_rules(ctx);
        }
    }

    if (seccomp_load(ctx) != 0) {
        perror("seccomp_load");
        seccomp_release(ctx);
        exit(1);
    }

    seccomp_release(ctx);
}

void write_file_string(const char *path, const char *value) {
    FILE *file = fopen(path, "w");

    if (file == NULL) {
        return;
    }

    fprintf(file, "%s", value);
    fclose(file);
}

void setup_cgroup(const SandboxConfig *config, pid_t pid) {
    if (config->cgroup_dir == NULL) {
        return;
    }

    mkdir_p(config->cgroup_dir);

    char path[PATH_MAX];
    char value[128];

    snprintf(path, sizeof(path), "%s/memory.max", config->cgroup_dir);
    snprintf(value, sizeof(value), "%ld", (long)config->memory_limit_mb * 1024 * 1024);
    write_file_string(path, value);

    snprintf(path, sizeof(path), "%s/pids.max", config->cgroup_dir);
    snprintf(value, sizeof(value), "%d", config->max_processes);
    write_file_string(path, value);

    snprintf(path, sizeof(path), "%s/cgroup.procs", config->cgroup_dir);
    snprintf(value, sizeof(value), "%d", pid);
    write_file_string(path, value);
}

void cleanup_cgroup(const SandboxConfig *config) {
    if (config->cgroup_dir == NULL) {
        return;
    }

    char path[PATH_MAX];

    snprintf(path, sizeof(path), "%s/cgroup.kill", config->cgroup_dir);
    write_file_string(path, "1");

    rmdir(config->cgroup_dir);
}

void print_cgroup_stats(const SandboxConfig *config) {
    if (config->cgroup_dir == NULL) {
        return;
    }

    char path[PATH_MAX];
    char buffer[512];
    FILE *file;

    printf("\n=== Cgroup Stats ===\n");

    snprintf(path, sizeof(path), "%s/memory.current", config->cgroup_dir);
    file = fopen(path, "r");
    if (file != NULL) {
        if (fgets(buffer, sizeof(buffer), file) != NULL) {
            printf("Current memory: %s", buffer);
        }
        fclose(file);
    }

    snprintf(path, sizeof(path), "%s/pids.current", config->cgroup_dir);
    file = fopen(path, "r");
    if (file != NULL) {
        if (fgets(buffer, sizeof(buffer), file) != NULL) {
            printf("Current processes: %s", buffer);
        }
        fclose(file);
    }

    snprintf(path, sizeof(path), "%s/cpu.stat", config->cgroup_dir);
    file = fopen(path, "r");
    if (file != NULL) {
        printf("CPU stats:\n");
        while (fgets(buffer, sizeof(buffer), file)) {
            printf("%s", buffer);
        }
        fclose(file);
    }

    printf("====================\n");
}

long read_peak_memory_kb(pid_t pid) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    FILE *file = fopen(path, "r");

    if (file == NULL) {
        return 0;
    }

    char line[256];
    long peak = 0;

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "VmHWM:", 6) == 0) {
            sscanf(line, "VmHWM: %ld kB", &peak);
            break;
        }
    }

    fclose(file);
    return peak;
}

void monitor_process(pid_t pid, const char *log_file, int verbose, long *peak_memory_kb) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    FILE *file = fopen(path, "r");

    if (file == NULL) {
        return;
    }

    char line[256];
    char name[128] = "";
    char state[128] = "";
    char vmrss[128] = "";
    char vmhwm[128] = "";
    char threads[128] = "";

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "Name:", 5) == 0) {
            sscanf(line, "Name:%127[^\n]", name);
        } else if (strncmp(line, "State:", 6) == 0) {
            sscanf(line, "State:%127[^\n]", state);
        } else if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line, "VmRSS:%127[^\n]", vmrss);
        } else if (strncmp(line, "VmHWM:", 6) == 0) {
            sscanf(line, "VmHWM:%127[^\n]", vmhwm);
            long current_peak = read_peak_memory_kb(pid);
            if (current_peak > *peak_memory_kb) {
                *peak_memory_kb = current_peak;
            }
        } else if (strncmp(line, "Threads:", 8) == 0) {
            sscanf(line, "Threads:%127[^\n]", threads);
        }
    }

    fclose(file);

    if (verbose) {
        printf("\n--- Process monitor ---\n");
        printf("PID: %d\n", pid);
        if (strlen(name) > 0) printf("Name:%s\n", name);
        if (strlen(state) > 0) printf("State:%s\n", state);
        if (strlen(vmrss) > 0) printf("VmRSS:%s\n", vmrss);
        if (strlen(vmhwm) > 0) printf("VmHWM:%s\n", vmhwm);
        if (strlen(threads) > 0) printf("Threads:%s\n", threads);
        printf("-----------------------\n");
    }

    char details[512];
    snprintf(details, sizeof(details), "name=%s state=%s vmrss=%s vmhwm=%s threads=%s",
             name, state, vmrss, vmhwm, threads);

    write_log_json(log_file, "proc_snapshot", "process", pid, details);
}

int trace_syscalls_blocking(pid_t pid, const SandboxConfig *config, SandboxResult *result) {
    int status;
    int in_syscall = 0;
    time_t start = time(NULL);

    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) != 0) {
        perror("ptrace attach");
        return -1;
    }

    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid trace attach");
        return -1;
    }

    while (1) {
        result->runtime_seconds = time(NULL) - start;

        if (result->runtime_seconds >= config->wall_time_limit) {
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);

            result->timed_out = 1;
            result->signal_number = SIGKILL;
            result->runtime_seconds = time(NULL) - start;

            write_log_json(config->log_file, "timeout", config->program, pid,
                           "process group killed due to wall-clock timeout");
            return 0;
        }

        if (ptrace(PTRACE_SYSCALL, pid, NULL, NULL) != 0) {
            return 0;
        }

        if (waitpid(pid, &status, 0) < 0) {
            perror("waitpid trace");
            return 0;
        }

        result->runtime_seconds = time(NULL) - start;

        if (WIFEXITED(status)) {
            result->exit_code = WEXITSTATUS(status);

            char details[128];
            snprintf(details, sizeof(details), "exit_code=%d", result->exit_code);
            write_log_json(config->log_file, "exit", config->program, pid, details);
            return 0;
        }

        if (WIFSIGNALED(status)) {
            result->signal_number = WTERMSIG(status);

            char details[128];
            snprintf(details, sizeof(details), "signal=%d signal_name=%s",
                     result->signal_number, strsignal(result->signal_number));
            write_log_json(config->log_file, "signal", config->program, pid, details);
            return 0;
        }

        if (WIFSTOPPED(status) && !in_syscall) {
            struct user_regs_struct regs;

            if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) == 0) {
                char details[256];
                snprintf(details, sizeof(details), "syscall=%s number=%lld",
                         syscall_name((long)regs.orig_rax),
                         (long long)regs.orig_rax);

                printf("[SYSCALL] %s (%lld)\n",
                       syscall_name((long)regs.orig_rax),
                       (long long)regs.orig_rax);

                write_log_json(config->log_file, "syscall", config->program, pid, details);
            }
        }

        in_syscall = !in_syscall;
    }
}

void load_simple_config(SandboxConfig *config, const char *path) {
    json_error_t error;
    json_t *root = json_load_file(path, 0, &error);

    if (root == NULL) {
        fprintf(stderr, "open config: %s\n", error.text);
        exit(1);
    }

    json_t *value;

    value = json_object_get(root, "profile");
    if (json_is_string(value)) {
        config->profile = strdup(json_string_value(value));
    }

    value = json_object_get(root, "clinical");
    if (json_is_boolean(value) && json_boolean_value(value)) {
        config->profile = "clinical";
        config->clinical_mode = 1;
    }

    value = json_object_get(root, "network");
    if (json_is_boolean(value) && !json_boolean_value(value)) {
        config->network_block = 1;
    }

    value = json_object_get(root, "readonly");
    if (json_is_boolean(value)) {
        config->readonly = json_boolean_value(value);
    }

    value = json_object_get(root, "strict");
    if (json_is_boolean(value)) {
        config->strict = json_boolean_value(value);
    }

    value = json_object_get(root, "wall_time_limit");
    if (json_is_integer(value)) {
        config->wall_time_limit = (int)json_integer_value(value);
    }

    value = json_object_get(root, "memory_limit_mb");
    if (json_is_integer(value)) {
        config->memory_limit_mb = (int)json_integer_value(value);
    }

    value = json_object_get(root, "cpu_limit_seconds");
    if (json_is_integer(value)) {
        config->cpu_limit_seconds = (int)json_integer_value(value);
    }

    value = json_object_get(root, "file_limit_mb");
    if (json_is_integer(value)) {
        config->file_limit_mb = (int)json_integer_value(value);
    }

    value = json_object_get(root, "max_processes");
    if (json_is_integer(value)) {
        config->max_processes = (int)json_integer_value(value);
    }

    value = json_object_get(root, "log_file");
    if (json_is_string(value)) {
        config->log_file = strdup(json_string_value(value));
    }

    value = json_object_get(root, "stdout_file");
    if (json_is_string(value)) {
        config->stdout_file = strdup(json_string_value(value));
    }

    value = json_object_get(root, "stderr_file");
    if (json_is_string(value)) {
        config->stderr_file = strdup(json_string_value(value));
    }

    json_decref(root);
}

void run_child_payload(const SandboxConfig *config, char *argv[], int optind) {
    setpgid(0, 0);

    redirect_output(config->stdout_file, STDOUT_FILENO);
    redirect_output(config->stderr_file, STDERR_FILENO);

    apply_limits(config);
    apply_namespaces(config);
    apply_rootfs(config);

    set_no_new_privs_or_die();
    drop_capabilities();
    close_extra_fds();
    sanitize_environment();

    if (config->clinical_mode || config->network_block || config->readonly || config->strict) {
        load_seccomp_filter(config);
    }

    execvp(argv[optind], &argv[optind]);

    perror("execvp");
    exit(1);
}

void print_final_report(const SandboxConfig *config, const SandboxResult *result) {
    printf("\n=== MedSandbox Report ===\n");
    printf("Program: %s\n", config->program);
    printf("Profile: %s\n", config->profile);
    printf("Clinical mode: %s\n", config->clinical_mode ? "enabled" : "disabled");
    printf("PID: %d\n", result->pid);
    printf("Runtime: %ld seconds\n", result->runtime_seconds);
    printf("Peak memory: %ld KB\n", result->peak_memory_kb);
    printf("Max processes: %d\n", config->max_processes);
    printf("Network blocking: %s\n", config->network_block ? "enabled" : "disabled");
    printf("Readonly mode: %s\n", config->readonly ? "enabled" : "disabled");
    printf("Network namespace: %s\n", config->isolate_net ? "enabled" : "disabled");
    printf("PID namespace: %s\n", config->isolate_pid ? "enabled" : "disabled");
    printf("Mount namespace: %s\n", config->isolate_mount ? "enabled" : "disabled");
    printf("UTS namespace: %s\n", config->isolate_uts ? "enabled" : "disabled");

    if (result->timed_out) {
        printf("Status: killed\n");
        printf("Reason: wall-clock timeout / process group killed\n");
    } else if (result->signal_number != 0) {
        printf("Status: terminated by signal\n");
        printf("Signal: %d (%s)\n", result->signal_number, strsignal(result->signal_number));

        if (result->signal_number == SIGSYS) {
            printf("Reason: forbidden syscall blocked by seccomp\n");
        }
    } else {
        printf("Status: exited normally\n");
        printf("Exit code: %d\n", result->exit_code);
    }

    printf("Log file: %s\n", config->log_file);

    if (config->stdout_file != NULL) {
        printf("Stdout file: %s\n", config->stdout_file);
    }

    if (config->stderr_file != NULL) {
        printf("Stderr file: %s\n", config->stderr_file);
    }

    printf("=========================\n");
}

void handle_interactive_io(int master_fd) {
    fd_set readfds;
    struct timeval tv;
    char buffer[1024];

    FD_ZERO(&readfds);
    FD_SET(master_fd, &readfds);
    FD_SET(STDIN_FILENO, &readfds);

    int max_fd = master_fd > STDIN_FILENO ? master_fd : STDIN_FILENO;

    tv.tv_sec = 0;
    tv.tv_usec = 100000;

    int ready = select(max_fd + 1, &readfds, NULL, NULL, &tv);

    if (ready <= 0) {
        return;
    }

    if (FD_ISSET(master_fd, &readfds)) {
        ssize_t n = read(master_fd, buffer, sizeof(buffer));

        if (n > 0) {
            write(STDOUT_FILENO, buffer, n);
        }
    }

    if (FD_ISSET(STDIN_FILENO, &readfds)) {
        ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));

        if (n > 0) {
            write(master_fd, buffer, n);
        }
    }
}

int main(int argc, char *argv[]) {
    SandboxConfig config;

    config.wall_time_limit = 3;
    config.memory_limit_mb = 50;
    config.cpu_limit_seconds = 2;
    config.file_limit_mb = 1;
    config.max_processes = 50;

    config.verbose = 0;
    config.interactive = 0;

    config.network_block = 0;
    config.readonly = 0;
    config.strict = 0;
    config.clinical_mode = 0;

    config.isolate_net = 0;
    config.isolate_uts = 0;
    config.isolate_pid = 0;
    config.isolate_mount = 0;

    config.trace_syscalls = 0;

    config.hostname = NULL;
    config.profile = "basic";

    config.log_file = DEFAULT_LOG_FILE;
    config.stdout_file = NULL;
    config.stderr_file = NULL;

    config.rootfs = NULL;
    config.workdir = "/";
    config.cgroup_dir = NULL;

    config.program = NULL;

    static struct option long_options[] = {
        {"profile", required_argument, 0, 'p'},
        {"max-procs", required_argument, 0, 'P'},
        {"stdout", required_argument, 0, 1000},
        {"stderr", required_argument, 0, 1001},
        {"interactive", no_argument, 0, 1002},
        {"isolate-net", no_argument, 0, 1003},
        {"isolate-uts", no_argument, 0, 1004},
        {"isolate-pid", no_argument, 0, 1005},
        {"isolate-mount", no_argument, 0, 1006},
        {"hostname", required_argument, 0, 1007},
        {"rootfs", required_argument, 0, 1008},
        {"workdir", required_argument, 0, 1009},
        {"trace-syscalls", no_argument, 0, 1010},
        {"cgroup-dir", required_argument, 0, 1011},
        {"config", required_argument, 0, 1012},
        {"clinical", no_argument, 0, 1013},
        {0, 0, 0, 0}
    };

    int opt;

    while ((opt = getopt_long(argc, argv, "hvno:t:m:c:f:p:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h':
                print_usage(argv[0]);
                return 0;

            case 'v':
                config.verbose = 1;
                break;

            case 'n':
                config.network_block = 1;
                break;

            case 'o':
                config.log_file = optarg;
                break;

            case 't':
                config.wall_time_limit = parse_positive_int(optarg, "time limit");
                break;

            case 'm':
                config.memory_limit_mb = parse_positive_int(optarg, "memory limit");
                break;

            case 'c':
                config.cpu_limit_seconds = parse_positive_int(optarg, "CPU limit");
                break;

            case 'f':
                config.file_limit_mb = parse_positive_int(optarg, "file limit");
                break;

            case 'p':
                config.profile = optarg;
                break;

            case 'P':
                config.max_processes = parse_positive_int(optarg, "max processes");
                break;

            case 1000:
                config.stdout_file = optarg;
                break;

            case 1001:
                config.stderr_file = optarg;
                break;

            case 1002:
                config.interactive = 1;
                break;

            case 1003:
                config.isolate_net = 1;
                break;

            case 1004:
                config.isolate_uts = 1;
                break;

            case 1005:
                config.isolate_pid = 1;
                break;

            case 1006:
                config.isolate_mount = 1;
                break;

            case 1007:
                config.hostname = optarg;
                config.isolate_uts = 1;
                break;

            case 1008:
                config.rootfs = optarg;
                break;

            case 1009:
                config.workdir = optarg;
                break;

            case 1010:
                config.trace_syscalls = 1;
                break;

            case 1011:
                config.cgroup_dir = optarg;
                break;

            case 1012:
                load_simple_config(&config, optarg);
                break;

            case 1013:
                config.profile = "clinical";
                config.clinical_mode = 1;
                config.network_block = 1;
                config.readonly = 1;
                config.strict = 1;
                break;

            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    apply_profile(&config);

    if (optind >= argc) {
        print_usage(argv[0]);
        return 1;
    }

    config.program = argv[optind];

    char details[1024];

    snprintf(details, sizeof(details),
             "profile=%s clinical=%d wall_time=%ds memory=%dMB cpu=%ds file=%dMB max_procs=%d verbose=%d network_block=%d readonly=%d strict=%d",
             config.profile,
             config.clinical_mode,
             config.wall_time_limit,
             config.memory_limit_mb,
             config.cpu_limit_seconds,
             config.file_limit_mb,
             config.max_processes,
             config.verbose,
             config.network_block,
             config.readonly,
             config.strict);

    write_log_json(config.log_file, "config", "medsandbox", getpid(), details);

    int master_fd = -1;
    pid_t pid;

    if (config.interactive) {
        pid = forkpty(&master_fd, NULL, NULL, NULL);
    } else {
        pid = fork();
    }

    if (pid < 0) {
        perror("fork");
        write_log_json(config.log_file, "error", "medsandbox", getpid(), "fork failed");
        return 1;
    }

    if (pid == 0) {
        if (config.isolate_pid) {
            if (unshare(CLONE_NEWPID) != 0) {
                perror("unshare pid");
                exit(1);
            }

            pid_t inner = fork();

            if (inner < 0) {
                perror("fork inner pid namespace");
                exit(1);
            }

            if (inner > 0) {
                int inner_status;
                waitpid(inner, &inner_status, 0);

                if (WIFEXITED(inner_status)) {
                    exit(WEXITSTATUS(inner_status));
                }

                if (WIFSIGNALED(inner_status)) {
                    kill(getpid(), WTERMSIG(inner_status));
                }

                exit(1);
            }
        }

        run_child_payload(&config, argv, optind);
    }

    setpgid(pid, pid);
    setup_cgroup(&config, pid);

    SandboxResult result;
    result.pid = pid;
    result.exit_code = -1;
    result.signal_number = 0;
    result.timed_out = 0;
    result.runtime_seconds = 0;
    result.peak_memory_kb = 0;

    if (config.verbose) {
        printf("[INFO] Starting MedSandbox\n");
        printf("[INFO] Program: %s\n", config.program);
        printf("[INFO] Profile: %s\n", config.profile);
        printf("[INFO] Clinical mode: %s\n", config.clinical_mode ? "enabled" : "disabled");
        printf("[INFO] Child PID: %d\n", pid);
        printf("[INFO] Interactive: %s\n", config.interactive ? "enabled" : "disabled");
    }

    int trace_handled = 0;

    if (config.trace_syscalls) {
        if (trace_syscalls_blocking(pid, &config, &result) == 0) {
            trace_handled = 1;
        }
    }

    if (!trace_handled) {
        int status;
        time_t start = time(NULL);

        while (1) {
            pid_t wait_result = waitpid(pid, &status, WNOHANG);

            if (wait_result == -1) {
                perror("waitpid");
                write_log_json(config.log_file, "error", config.program, pid, "waitpid failed");
                return 1;
            }

            if (wait_result == pid) {
                result.runtime_seconds = time(NULL) - start;

                if (WIFEXITED(status)) {
                    result.exit_code = WEXITSTATUS(status);
                    snprintf(details, sizeof(details), "exit_code=%d", result.exit_code);
                    write_log_json(config.log_file, "exit", config.program, pid, details);
                } else if (WIFSIGNALED(status)) {
                    result.signal_number = WTERMSIG(status);
                    snprintf(details, sizeof(details), "signal=%d signal_name=%s",
                             result.signal_number, strsignal(result.signal_number));
                    write_log_json(config.log_file, "signal", config.program, pid, details);
                }

                break;
            }

            result.runtime_seconds = time(NULL) - start;

            if (result.runtime_seconds >= config.wall_time_limit) {
                if (config.verbose) {
                    printf("[WARN] Wall-clock time limit exceeded. Killing process group...\n");
                }

                kill(-pid, SIGKILL);
                waitpid(pid, &status, 0);

                result.timed_out = 1;
                result.signal_number = SIGKILL;
                result.runtime_seconds = time(NULL) - start;

                write_log_json(config.log_file, "timeout", config.program, pid,
                               "process group killed due to wall-clock timeout");
                break;
            }

            monitor_process(pid, config.log_file, config.verbose, &result.peak_memory_kb);

            if (config.interactive && master_fd >= 0) {
                handle_interactive_io(master_fd);
                usleep(100000);
            } else {
                sleep(1);
            }
        }
    }

    print_cgroup_stats(&config);
    cleanup_cgroup(&config);

    if (master_fd >= 0) {
        close(master_fd);
    }

    print_final_report(&config, &result);

    return 0;
}
