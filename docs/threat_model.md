# Threat Model

This document describes the current security assumptions and limitations of MedSandbox.

MedSandbox is an educational Linux sandbox. It is designed to reduce risk when running experimental programs, but it is not production-grade isolation.

## Assets Protected

MedSandbox tries to protect:

- host CPU and memory resources;
- host filesystem from common write operations in readonly/strict modes;
- network access in restricted profiles;
- auditability of program execution through JSON logs;
- basic process safety by limiting execution time and process count.

## Assumed Threat

The target program may be:

- buggy;
- experimental;
- downloaded from an external source;
- too resource-intensive;
- attempting network access;
- attempting to inspect other processes using `ptrace`;
- attempting to write large files.

The target program is not assumed to be fully trusted.

## Current Controls

Current implemented controls include:

- `fork` / `execvp` process execution;
- `waitpid` supervision;
- wall-clock timeout;
- `setrlimit` limits for memory, CPU time, file size, and process count;
- seccomp filters for network syscalls, `ptrace`, and other risky syscalls;
- readonly syscall restrictions for common file write operations;
- `/proc/<pid>/status` monitoring;
- stdout and stderr capture;
- JSON audit logs.

## Clinical Mode

Clinical mode is intended for running experimental clinical plugins on synthetic FHIR data.

It enables:

- network blocking;
- readonly behavior;
- strict syscall filtering;
- resource limits;
- JSON audit logging.

The goal is to prevent simple classes of unsafe behavior such as network exfiltration attempts, excessive resource use, and uncontrolled file writes.

## Out of Scope

MedSandbox does not currently protect against all possible sandbox escapes.

Out of scope:

- production-grade container isolation;
- kernel-level vulnerabilities;
- side-channel attacks;
- malicious code exploiting privileged host configuration;
- full filesystem virtualization;
- full network namespace hardening;
- complete seccomp allowlist portability across distributions;
- real patient data protection.

## Known Limitations

Known limitations:

- `chroot` is not a complete security boundary by itself;
- namespace support is experimental;
- cgroup support depends on the host system configuration;
- seccomp rules may require adjustments across Linux distributions and glibc versions;
- readonly mode blocks common write paths but is not a full filesystem policy engine;
- `/proc` monitoring may briefly observe the child before `execvp()` completes.

## Future Improvements

Planned security improvements:

- add SHA-256 hashing of plugins and input files;
- add better cgroup CPU and IO controls;
- improve namespace setup;
- mount a minimal `/proc` inside PID namespace;
- add automated security tests;
- add fuzzing for clinical input parsing;
- add CI checks with sanitizers.
