# MedSandbox

[![CI](https://github.com/ImaneElHamdouchi/medsandbox/actions/workflows/ci.yml/badge.svg)](https://github.com/ImaneElHamdouchi/medsandbox/actions/workflows/ci.yml)

MedSandbox is an educational Linux sandbox written in C for running and supervising experimental programs in a constrained environment.

The current healthcare demo runs a small qSOFA clinical scoring plugin on synthetic FHIR data. The goal is not to build a medical device, but to explore how clinical or research plugins could be executed with resource limits, syscall filtering, output capture, and audit logs.

> Disclaimer: MedSandbox is not a medical device and is not intended for real patient care. All clinical examples use synthetic data for educational and portfolio purposes only.

## Why I Built This

I started MedSandbox to practice Linux sandboxing in C and to connect it with my medical background.

The idea is simple: clinical scoring tools or research plugins may be useful, but they should not automatically be trusted. Before running an external clinical plugin, I wanted to explore how to limit its resources, block dangerous system calls, capture its output, and keep an audit trail.

The current demo runs a qSOFA plugin on synthetic FHIR data inside a restricted Linux environment.

## Current State

Implemented and tested:

- process execution with `fork`, `execvp`, and `waitpid`;
- memory, CPU, file size, and process limits with `setrlimit`;
- network syscall blocking with seccomp;
- `ptrace` blocking in strict mode;
- readonly restrictions for common file write operations;
- `/proc/<pid>/status` monitoring;
- stdout and stderr capture;
- JSON audit logs using Jansson;
- qSOFA clinical plugin using synthetic FHIR data.

Experimental or partially implemented:

- namespace isolation;
- cgroup v2 support;
- `chroot` rootfs mode;
- ptrace-based syscall tracing;
- clinical seccomp allowlist tuning across different Linux/glibc versions.

## Main Features

MedSandbox currently focuses on four things:

1. Running a target program under supervision.
2. Applying resource limits before execution.
3. Blocking selected risky syscalls with seccomp.
4. Producing readable reports and JSON audit logs.

The clinical demo adds a small qSOFA plugin that reads a synthetic FHIR Bundle and returns a JSON result.

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
├── docs/
│   ├── threat_model.md
│   └── medical_safety.md
├── fake_medical_app.c
├── memory_hog.c
├── file_writer.c
├── network_test.c
├── ptrace_test.c
├── fork_bomb.c
└── build_rootfs.sh
```

Generated at runtime and ignored by Git:

```text
logs/
out/
sandbox_root/
big_output.txt
compiled binaries
```

## Requirements

Tested on Debian/Linux.

Install dependencies:

```bash
sudo apt update
sudo apt install build-essential libseccomp-dev libcap-dev libjansson-dev
```

## Tested Environment

The project was tested locally on a Debian Linux environment with:

- GCC;
- glibc;
- libseccomp;
- libcap;
- Jansson.

You can check your local versions with:

```bash
gcc --version
ldd --version
dpkg -l | grep -E 'libseccomp-dev|libcap-dev|libjansson-dev'
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

Run the security demonstrations:

```bash
make demo-security
```

Current observed results:

```text
network_test  -> blocked with SIGSYS / Bad system call
memory_hog    -> malloc failed under memory limit
file_writer   -> terminated with SIGXFSZ / File size limit exceeded
ptrace_test   -> blocked with SIGSYS / Bad system call
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

FHIR is used here as a simple healthcare data format for the demo. The sample file is synthetic and does not contain real patient information.

## Clinical Plugin: qSOFA

The qSOFA plugin reads a FHIR Bundle and calculates:

```text
respiratory rate >= 22        -> 1 point
systolic blood pressure <=100 -> 1 point
Glasgow Coma Score < 15       -> 1 point
```

A score of 2 or more is reported as a positive screen requiring clinical review.

This is implemented only as an educational demonstration and must not be used for real clinical decisions.

## Known Limitations

MedSandbox is not production-grade isolation.

Known limitations:

- the seccomp allowlist may need adjustments between Linux distributions or glibc versions;
- `chroot` alone is not a complete security boundary;
- namespace support is still experimental;
- cgroup support depends on the host system configuration;
- the clinical plugin uses synthetic data only;
- qSOFA is included as an educational example, not as a medical recommendation engine.

One issue observed during testing: `/proc` snapshots may sometimes show the child process before `execvp()` fully replaces it, so the process name in logs can briefly appear as `medsandbox`.

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

## Fresh Clone Test

To verify the repository from a clean clone:

```bash
cd /tmp
git clone git@github.com:ImaneElHamdouchi/medsandbox.git
cd medsandbox
make
make demo-qsofa
make demo-security
```

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
- improve namespace and rootfs isolation;
- add cgroup CPU and IO controls;
- add a more detailed threat model document;
- add a regulatory note explaining why this is not a medical device.

## What This Project Shows

This project helped me practice:

- Linux process control in C;
- resource limiting;
- seccomp filtering;
- JSON logging;
- basic clinical data processing;
- thinking about safety boundaries around clinical software.

It reflects my interest in building software at the intersection of systems programming, security, and healthcare.

## License

This project is licensed under the MIT License.

MedSandbox is an educational project and is not intended for real patient care or production clinical use.
