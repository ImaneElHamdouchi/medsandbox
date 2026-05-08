# MedSandbox

MedSandbox is an educational Linux sandbox written in C for safely executing experimental clinical decision-support plugins on synthetic FHIR data.

It combines Linux process management, resource limits, seccomp syscall filtering, `/proc` monitoring, cgroup support, JSON audit logs, and a healthcare-focused plugin demo using qSOFA.

> Disclaimer: MedSandbox is not a medical device and is not intended for real patient care. All clinical examples use synthetic data for educational and portfolio purposes only.

## Overview

Healthcare software may need to run external tools, research scripts, scoring algorithms, or clinical decision-support plugins. These programs can be useful, but they should not automatically be trusted.

An external plugin may accidentally or intentionally:

- consume too much memory or CPU;
- run forever;
- create too many processes;
- write large files;
- attempt network access;
- inspect other processes using `ptrace`;
- produce unaudited clinical output.

MedSandbox demonstrates how such programs can be executed inside a controlled Linux environment with security, resource control, and auditability in mind.

## Key Features

- Process execution with `fork`, `execvp`, and `waitpid`
- Wall-clock timeout enforcement
- Resource limits with `setrlimit`
  - memory limit
  - CPU time limit
  - file size limit
  - process count limit
- Seccomp syscall filtering
  - network syscall blocking
  - `ptrace` blocking
  - readonly mode
  - clinical allowlist mode
- `/proc/<pid>/status` monitoring
  - process state
  - memory usage
  - peak memory
  - thread count
- JSON audit logs using Jansson
- stdout and stderr capture
- Optional cgroup v2 support
- Optional Linux namespaces
  - network namespace
  - UTS namespace
  - PID namespace
  - mount namespace
- Optional `chroot` root filesystem
- Basic syscall tracing with `ptrace`
- Clinical plugin example: qSOFA on synthetic FHIR data

## Architecture

```text
               +-----------------------------+
               | Synthetic FHIR Patient JSON |
               +--------------+--------------+
                              |
                              v
+-----------------------------+-----------------------------+
|                         MedSandbox                         |
|-----------------------------------------------------------|
| fork/exec       setrlimit       seccomp       /proc monitor|
| cgroups         namespaces      stdout logs   JSON audit   |
+-----------------------------+-----------------------------+
                              |
                              v
               +-----------------------------+
               | Clinical Plugin: qSOFA      |
               +--------------+--------------+
                              |
                              v
               +-----------------------------+
               | JSON Clinical Result        |
               +-----------------------------+
```

## Project Structure

```text
.
├── medsandbox.c
├── syscall_names.h
├── Makefile
├── config.json
├── clinical_plugins/
│   └── qsofa.c
├── samples/
│   └── patient_qsofa.fhir.json
├── logs/
│   └── sandbox.log
├── out/
│   └── qsofa_result.json
├── fake_medical_app.c
├── memory_hog.c
├── file_writer.c
├── network_test.c
├── ptrace_test.c
├── fork_bomb.c
├── build_rootfs.sh
└── sandbox_root/
```

## Requirements

Tested on Debian/Linux.

Install dependencies:

```bash
sudo apt update
sudo apt install build-essential libseccomp-dev libcap-dev libjansson-dev
```

## Build

```bash
make clean
make
```

## Quick Clinical Demo

Run the qSOFA plugin inside MedSandbox clinical mode:

```bash
make demo-qsofa
```

Equivalent manual command:

```bash
./medsandbox --clinical \
  --stdout out/qsofa_result.json \
  --stderr out/qsofa_error.txt \
  ./clinical_plugins/qsofa samples/patient_qsofa.fhir.json
```

Example output:

```json
{
  "score_name": "qSOFA",
  "valid": true,
  "score": 3,
  "positive_screen": true,
  "interpretation": "requires clinical review",
  "inputs": {
    "respiratory_rate": 24.0,
    "systolic_bp": 92.0,
    "glasgow_coma_score": 14.0
  },
  "components": {
    "respiratory_rate_ge_22": true,
    "systolic_bp_le_100": true,
    "gcs_lt_15": true
  },
  "disclaimer": "synthetic data only, not for patient care"
}
```

## Security Demo

Run all security demonstrations:

```bash
make demo-security
```

This runs:

```text
network_test    -> blocked by seccomp
memory_hog      -> blocked by memory limit
file_writer     -> blocked by file size limit
ptrace_test     -> blocked by seccomp
```

Example observed results:

```text
Network syscall blocked: SIGSYS / Bad system call
Memory limit enforced: malloc failed
File size limit enforced: SIGXFSZ / File size limit exceeded
ptrace blocked: SIGSYS / Bad system call
```

## Example Commands

Run with strict profile:

```bash
./medsandbox --profile strict ./network_test
```

Limit memory to 50 MB:

```bash
./medsandbox -m 50 ./memory_hog
```

Limit output file size to 1 MB:

```bash
./medsandbox -f 1 ./file_writer
```

Capture stdout and stderr:

```bash
./medsandbox \
  --stdout out/stdout.txt \
  --stderr out/stderr.txt \
  ./fake_medical_app
```

Run with a JSON config:

```bash
./medsandbox --config config.json ./network_test
```

## Profiles

### basic

Default profile. Applies resource limits and monitoring, but does not block network or file writes unless configured.

```bash
./medsandbox ./fake_medical_app
```

### networkless

Blocks common network syscalls such as `socket`, `connect`, `bind`, `listen`, `sendto`, and `recvfrom`.

```bash
./medsandbox --profile networkless ./network_test
```

### readonly

Blocks common file creation, deletion, rename, truncate, and write-open operations.

```bash
./medsandbox --profile readonly ./file_writer
```

### strict

Combines network blocking, readonly mode, and additional dangerous syscall blocking such as `ptrace`, `mount`, `reboot`, `swapon`, and `kexec_load`.

```bash
./medsandbox --profile strict ./ptrace_test
```

### clinical

Designed for experimental clinical plugins.

It enables:

- network blocking;
- readonly mode;
- strict syscall filtering;
- seccomp allowlist behavior;
- resource limits;
- JSON audit logging.

```bash
./medsandbox --clinical ./clinical_plugins/qsofa samples/patient_qsofa.fhir.json
```

## JSON Audit Logs

MedSandbox writes JSON logs to:

```text
logs/sandbox.log
```

View recent logs:

```bash
tail -n 10 logs/sandbox.log
```

Example log line:

```json
{"schema_version":"1.0","timestamp":1778282094,"event":"signal","program":"./ptrace_test","pid":29672,"details":"signal=31 signal_name=Bad system call"}
```

Logged events include:

- configuration;
- process snapshots;
- normal exit;
- signal termination;
- timeouts;
- syscall traces.

## Synthetic FHIR Data

The clinical demo uses a synthetic FHIR Bundle containing observations for qSOFA:

- respiratory rate: LOINC `9279-1`
- systolic blood pressure: LOINC `8480-6`
- Glasgow Coma Score: LOINC `9269-2`

FHIR is used because it is a major healthcare interoperability standard for exchanging electronic health information.

## Clinical Plugin: qSOFA

The qSOFA plugin reads a FHIR Bundle and calculates:

```text
respiratory rate >= 22        -> 1 point
systolic blood pressure <=100 -> 1 point
Glasgow Coma Score < 15       -> 1 point
```

A score of 2 or more is reported as a positive screen requiring clinical review.

This is implemented only as an educational demonstration and must not be used for real clinical decisions.

## Threat Model

MedSandbox assumes the target program may be buggy, experimental, or untrusted.

It attempts to reduce risk by:

- limiting resource exhaustion;
- blocking common dangerous syscalls;
- preventing network access in restricted profiles;
- preventing many file-write operations in readonly profiles;
- recording execution behavior in audit logs.

MedSandbox does not claim to provide production-grade container isolation. It is an educational sandbox and security research project.

Known limitations:

- not a replacement for containers, VMs, or hardened production sandboxes;
- `chroot` is not a complete security boundary by itself;
- syscall allowlists may need tuning per binary and libc version;
- namespace features may require privileges depending on the host;
- cgroup support depends on host configuration;
- clinical examples are synthetic and educational.

## RootFS Demo

Create a minimal root filesystem:

```bash
./build_rootfs.sh
```

Run with `chroot`:

```bash
./medsandbox --rootfs sandbox_root --workdir / /bin/bash
```

Some namespace and chroot features may require elevated privileges depending on the system.

## Development Commands

Clean build outputs:

```bash
make clean
```

Build everything:

```bash
make
```

Run clinical demo:

```bash
make demo-qsofa
```

Run security demo:

```bash
make demo-security
```

Run all demos:

```bash
make demo-all
```

## Roadmap

Planned improvements:

- add more clinical plugins:
  - CHA2DS2-VASc
  - CURB-65
  - BMI
  - eGFR
- add plugin manifests;
- add FHIR validation;
- add Synthea-generated patient samples;
- add unit and integration tests;
- add GitHub Actions CI;
- add sanitizer targets:
  - AddressSanitizer
  - UndefinedBehaviorSanitizer
  - Valgrind
- add SHA-256 hashing of plugins and input files in logs;
- add stronger namespace and rootfs isolation;
- add cgroup CPU and IO controls;
- add a detailed threat model document;
- add a regulatory note explaining why this is not a medical device.

## Portfolio Summary

MedSandbox demonstrates a combination of:

- C systems programming;
- Linux process management;
- operating system security;
- seccomp syscall filtering;
- resource control;
- JSON audit logging;
- healthcare interoperability concepts;
- clinical decision-support safety awareness.

It is designed as a portfolio project connecting software engineering, Linux security, and medical knowledge.

## License

Educational project. Add your preferred open-source license before publishing.
