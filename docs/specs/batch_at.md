# `batch` and `at` Execution Back-end Specification

## 1. Top Actions
1. Treat `batch` as a thin frontend over a shared `at` submission backend, because POSIX defines `batch` as equivalent to `at -q b -m now`, and the execution model, job IDs, shell invocation, retained environment/cwd/umask, and access-control rules all come from the `at` machinery.
2. Split behavior into a strict POSIX core plus compatibility profiles, because current BSD and GNU interfaces add useful features but also diverge from POSIX and from each other. OpenBSD documents direct `batch` options and a default that does **not** mail by default; Debian GNU/Linux documents `batch` as a bare command / `at -b`, with GNU options largely living on `at`/`atd`.

## 2. Scope and conflict policy
This spec assumes a C implementation for the Substrate OS. It covers the normative POSIX.1-2024 `batch` command, plus documented GNU/Linux and BSD-family extensions that are relevant to `batch`. OpenBSD is the primary BSD-extension source explicitly documenting the POSIX delta for `batch`; NetBSD is used to confirm BSD-family variance.

**Non-negotiable baseline:** in the POSIX form, `batch` reads shell commands from standard input, has no standard options or operands, schedules execution in the special batch queue, is equivalent to `at -q b -m now`, prints `job %s at %s\n` to stderr on successful submission, returns 0 on success and >0 on error, and must not schedule a job on error.

Execution semantics are inherited from `at`: the job runs via a separate shell invocation, in a separate process group, with no controlling terminal, while retaining the submission-time environment, current working directory, umask, and other implementation-defined execution-time attributes. Job ID format must be restricted to alphanumerics plus periods.

**Conflict-resolution policy:** 
1. POSIX behavior strictly wins for standard invocation forms.
2. BSD extension semantics apply when specifically invoking BSD paths/flags.
3. For pure extensions that conflict, BSD wins over GNU only for extension-on-extension conflicts. Mixed profile scenarios are blocked. Ambiguous nonstandard inputs must fail deterministically rather than silently reinterpret across profiles.

## 3. Compatibility Profiles Model
Use three profiles:
* **POSIX strict**: default for conformance testing; bare `batch` must behave exactly like `at -q b -m now`.
* **BSD extended**: accept direct `batch [-m] [-f file] [-q queue] [timespec]`, `teatime`, BSD queue semantics, and BSD-style direct submission UX. Prefer OpenBSD behavior when GNU and BSD extensions conflict.
* **GNU extended**: support `at -b`, GNU `at` options that affect batch operation (`-M`, `-u`, `-o`, `-v`, `-c`), GNU queue `"="`, and GNU/Linux daemon tunables such as load threshold and batch interval.

## 4. Phase 1 - Truth Table (Behavior Matrix)

| Feature | POSIX Strict (P1003.1-2024) | OpenBSD Compatibility | NetBSD (BSD Family) | GNU/Linux (Debian) |
| :--- | :--- | :--- | :--- | :--- |
| **CLI Forms** | `batch` (no options, stdin only) | `batch [-m] [-f file] [-q queue] [timespec]` | `batch [-m] [-f file] [-q queue] [timespec]` | `batch` / `at -b` (often just wrapper or symlink) |
| **Mail Default** | Implies `-m` (mails completion regardless of output) | Does **not** mail by default, mails only if output | Dependent on output unless `-m` | Mails if output is produced, use `-M` to silence |
| **Queue Default** | Queue `b` | Queue `E` (batch lowercase or specific defaults) | Queue `b` or `E` (depends on alias/implementation) | Queue `b` |
| **Time Syntax** | Equivalent to `now` | Accepts `[timespec]`, e.g., `teatime`, tomorrow | extended time/date forms compatible | GNU time forms, though bare `batch` assumes now |
| **ACL Rule base** | Uses `at.allow` and `at.deny` | Uses `at.allow` and `at.deny` | Uses `at.allow` and `at.deny` | Uses `at.allow` and `at.deny` |
| **Exec Shell** | User's shell or `/bin/sh` | Passed `SHELL` or defaults to `/bin/sh` | `SHELL` / `/bin/sh` | Requires `/bin/sh` used by `atd` execution |
| **stderr format** | `job %s at %s\n` | `job %s at %s\n` | `job %s at %s\n` | `warning: ... job %s at %s\n` |
| **Special queues** | `a` (at), `b` (batch) | extended alphabetical queues with limits | Alphabetical queues (higher letter = higher nice) | includes `=` for running |

---
## 5. INCOSE/EARS Requirements

### 5.1 Core conformance requirements
* **REQ-BATCH-001** (Ubiquitous): When the utility is invoked as `batch` with no nonstandard options or operands, the system shall behave as if `at -q b -m now` had been invoked.
* **REQ-BATCH-002** (Ubiquitous): When submitting a job in POSIX mode, the utility shall read standard input as a text file containing commands accepted by the POSIX shell command language.
* **REQ-BATCH-003** (Event-driven): When a submission succeeds, the utility shall write `job %s at %s\n` to standard error using a unique job identifier and a user-timezone-adjusted date string.
* **REQ-BATCH-004** (Ubiquitous): When a submitted job later executes, the execution engine shall invoke a shell in a separate process group with no controlling terminal.
* **REQ-BATCH-005** (Ubiquitous): When a submitted job later executes, the execution engine shall retain the submission-time current working directory, file creation mask, and retained environment/implementation-defined execution attributes.
* **REQ-BATCH-006** (Ubiquitous): The job identifier shall consist only of alphanumeric characters and period characters.
* **REQ-BATCH-007** (Unwanted behavior): If submission fails at any point, the system shall not leave a runnable scheduled job behind.
* **REQ-BATCH-008** (Ubiquitous): The utility shall return exit status 0 for successful completion and a value greater than 0 on error.

### 5.2 Access-control and shell-selection requirements
* **REQ-BATCH-009** (State-driven): While `at.allow` exists, the system shall permit use of `batch` only to listed users.
* **REQ-BATCH-010** (State-driven): While `at.allow` does not exist and `at.deny` exists, the system shall deny use of `batch` to listed users and permit all others.
* **REQ-BATCH-011** (State-driven): While neither `at.allow` nor `at.deny` exists, the system shall permit only appropriately privileged users to submit jobs.
* **REQ-BATCH-012** (State-driven): While only `at.deny` exists and is empty, the system shall permit all users to submit jobs.
* **REQ-BATCH-013** (Optional-feature): When `SHELL` is unset or null, the system shall invoke `sh`.
* **REQ-BATCH-014** (Optional-feature): When `SHELL` is set to a non-`sh` value, the system shall use one POSIX-permitted behavior: use that shell, use `sh`, or use the login shell, and shall optionally emit a warning that does not affect exit status.

### 5.3 Output, mail, and execution requirements
* **REQ-BATCH-015** (Ubiquitous): In strict POSIX mode, the system shall treat `batch` as implying `-m`, and shall mail completion even when the job produces no output.
* **REQ-BATCH-016** (Event-driven): When job standard output or standard error is produced and not redirected elsewhere, the system shall deliver it to the user via the configured mail path.
* **REQ-BATCH-017** (Unwanted behavior): If mail delivery fails after successful job execution, the system shall record the failure and shall not mark the job as unexecuted.
* **REQ-BATCH-018** (Ubiquitous): The implementation shall record enough submission metadata to reconstruct execution context, output routing, and completion reporting.

### 5.4 BSD-extension requirements
* **REQ-BATCH-019** (Optional-feature): In BSD compatibility mode, the `batch` frontend shall accept `[-m] [-f file] [-q queue] [timespec]`.
* **REQ-BATCH-020** (Optional-feature): In BSD compatibility mode, the parser shall accept the `teatime` token and BSD-style extended time/date forms.
* **REQ-BATCH-021** (Optional-feature): In BSD compatibility mode, the scheduler shall support BSD queue semantics, including uppercase queues behaving batch-like and increased niceness for higher letters.
* **REQ-BATCH-022** (Optional-feature): In BSD compatibility mode, the implementation shall support BSD-style direct file submission with `-f`.
* **REQ-BATCH-023** (Optional-feature): In BSD compatibility mode, the implementation shall allow batch submission to a caller-selected queue via `-q`.
* **REQ-BATCH-024** (Optional-feature): In BSD compatibility mode, the implementation shall support an OpenBSD-compatible profile where bare `batch` does not imply mail and may default to queue `E`.

### 5.5 GNU-extension requirements
* **REQ-BATCH-025** (Optional-feature): The implementation shall support GNU-compatible invocation through `at -b`.
* **REQ-BATCH-026** (Optional-feature): The shared backend shall support GNU `at` options that materially affect batch behavior, including `-M`, `-u`, `-o`, `-v`, and `-c`.
* **REQ-BATCH-027** (Optional-feature): The scheduler/daemon shall support GNU/Linux load-threshold tuning and minimum batch-start interval tuning.
* **REQ-BATCH-028** (Optional-feature): The implementation shall support GNU queue `"="` as reserved for currently running jobs.
* **REQ-BATCH-029** (State-driven): In GNU/Linux compatibility mode, the implementation shall either require a usable Linux load-average source or fail clearly/documentedly when batch-load semantics cannot be computed.

### 5.6 Security and robustness requirements
* **REQ-BATCH-030** (Unwanted behavior): If spool-file creation, rename, lock, fsync, or metadata persistence fails, the system shall fail submission atomically.
* **REQ-BATCH-031** (Ubiquitous): The implementation shall isolate privileged spool/ACL operations from untrusted command content.
* **REQ-BATCH-032** (Optional-feature): The implementation shall expose environment-retention policy as a compatibility-profile setting, because documented BSD-family and GNU/Linux filtered-variable sets differ.
* **REQ-BATCH-033** (Unwanted behavior): The implementation shall reject malformed or ambiguous nonstandard inputs in a deterministic way and shall not silently reinterpret them across profiles.
* **REQ-BATCH-034** (Ubiquitous): The implementation shall be testable without real-time waiting by providing a controllable scheduler clock or injectable readiness predicate.

---
## 6. Architecture & Deliverables

### C Library Modules
* `libatjob`: job model, job-id generation, queue metadata, submission result formatting.
* `libatacl`: `at.allow` / `at.deny` parsing and authorization.
* `libatspool`: atomic spool write, lock, fsync, rename, recovery scanning.
* `libatexec`: shell selection, process-group/session setup, cwd/umask/env restoration, stdio capture.
* `libatmail`: completion mail routing and error recording.
* `libatparse`: extended time parser for BSD/GNU forms; not required for strict bare POSIX `batch`, but required for extension coverage.
* `libatcompat`: profile selection and policy switches.
