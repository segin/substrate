# 7. Core Utilities (`bin/`)

> This file was seeded from `TASKS.md` using a fork-copy (rename+restore) workflow to preserve lineage.
> Source span in original monolith: lines 8931-8948.
> Standards basis for this section: POSIX.1-2024 Issue 8 where the utility is standardized, plus GNU and BSD behavior/extensions where applicable. When GNU and BSD behaviors conflict, BSD semantics, flags, output, and error handling take precedence unless a Substrate-specific requirement explicitly says otherwise.
> Existing unique Substrate checks are preserved, including `ps` bitness support via `-b`, `setldt`/`ldtctl`, shared `libbc`, and ext2-native `mkfs`/`fsck` goals.

## Reimplemented Checklist

### 7. Core Utilities (`bin/`)
- [ ] **Compliance Policy:** (REQ: REQ-11-0001)
    - [ ] Implement full POSIX.1-2024 behavior for utilities in this file that are standardized by POSIX (`ps`, `bc`, `compress`). (REQ: REQ-11-0002)
    - [ ] For utilities in this file that are not standardized by POSIX.1-2024 (`dc`, `cpio`, `mkfs`, `fsck`, `setldt`, `ldtctl`, `libbc`), implement BSD behavior as the primary compatibility target and GNU behavior as a secondary compatibility target where non-conflicting. (REQ: REQ-11-0003)
    - [ ] Preserve Substrate-specific behavior as first-class requirements instead of hiding it behind compatibility shims. (REQ: REQ-11-0004)

- [x] **Process Tools:** (REQ: REQ-11-0005)
    - [x] **`ps` Existing Substrate Support:** (REQ: REQ-11-0006)
        - [x] Keep `-b` as an explicit bitness option fed by `sys_proc_info`, and preserve its current formatting contract while standards work proceeds. (REQ: REQ-11-0007)
    - [ ] **`ps` POSIX.1-2024 Baseline:** (REQ: REQ-11-0008)
        - [ ] Implement the POSIX/XSI option surface required by Issue 8: `-a`, `-A`, `-d`, `-e`, `-f`, `-g`, `-G`, `-l`, `-n`, `-o`, `-p`, `-t`, `-u`, `-U`, `-w`. (REQ: REQ-11-0009)
        - [ ] Match POSIX default selection semantics: same effective UID and controlling terminal as the invoker when no selectors are provided. (REQ: REQ-11-0010)
        - [ ] Match POSIX inclusive-OR selection behavior when multiple selector classes are combined. (REQ: REQ-11-0011)
        - [ ] Implement POSIX `-o` formatting semantics, including repeated fields, header control, width handling, and stable keyword parsing. (REQ: REQ-11-0012)
        - [ ] Match POSIX output requirements for default, full, and long listings, including field meaning, ordering, truncation, and wide output behavior. (REQ: REQ-11-0013)
    - [ ] **`ps` BSD/GNU Extension Surface:** (REQ: REQ-11-0014)
        - [ ] Support BSD-style invocation forms and field conventions expected by common scripts (`aux`, `ax`, `u`, `l`, `ww`, BSD field aliases, BSD header conventions). (REQ: REQ-11-0015)
        - [ ] Prefer BSD semantics when BSD and GNU `ps` option behavior conflicts, while still accepting non-conflicting GNU-compatible forms where practical. (REQ: REQ-11-0016)
        - [ ] Integrate Substrate-specific columns such as bitness and personality without breaking POSIX or BSD output modes. (REQ: REQ-11-0017)
    - [ ] **`ps` Verification & Documentation:** (REQ: REQ-11-0018)
        - [ ] Add integration tests covering default output, POSIX selectors, BSD-style invocations, and the existing `-b` bitness view. (REQ: REQ-11-0019)
        - [ ] Add or update the `ps(1)` manual page to document standards compliance, BSD precedence, Substrate-specific fields, and output differences from Linux procps where relevant. (REQ: REQ-11-0020)

- [x] **LDT Tools:** (REQ: REQ-11-0021)
    - [x] **`setldt` / `ldtctl` Existing Bring-Up:** CLI tool to inspect/manipulate LDT entries. (REQ: REQ-11-0022)
    - [ ] **`setldt` / `ldtctl` CLI Contract:** (REQ: REQ-11-0023)
        - [ ] Define stable inspect/list/show/set/clear operations with explicit selector, base, limit, privilege, and descriptor-type semantics. (REQ: REQ-11-0024)
        - [ ] Reject invalid descriptors, nonsensical limits, malformed selectors, and privilege-escalating combinations with deterministic diagnostics and exit status. (REQ: REQ-11-0025)
        - [ ] Provide scriptable output modes suitable for regression tests and system integration. (REQ: REQ-11-0026)
    - [ ] **`setldt` / `ldtctl` Verification & Documentation:** (REQ: REQ-11-0027)
        - [ ] Add integration tests covering safe inspection, mutation, invalid input rejection, and kernel-visible effects. (REQ: REQ-11-0028)
        - [ ] Add man pages documenting the Substrate-only nature of these utilities and their privilege/safety model. (REQ: REQ-11-0029)

- [ ] **Calculator Tools:** (REQ: REQ-11-0030)
    - [ ] **`libbc`:** Shared bignum/runtime library for `bc` and `dc`. (REQ: REQ-11-0031)
        - [ ] Provide arbitrary-precision integer/decimal primitives: add, sub, mul, div, mod, pow, compare, scale handling, and base conversion. (REQ: REQ-11-0032)
        - [ ] Centralize parser/runtime-independent facilities so `bc` and `dc` share number semantics, diagnostics, and memory management rules. (REQ: REQ-11-0033)
    - [ ] **`bc` POSIX.1-2024 Baseline:** (REQ: REQ-11-0034)
        - [ ] Implement Issue 8 `bc` grammar, execution model, input sequencing (files then stdin), interactive behavior, and `-l` math-library mode. (REQ: REQ-11-0035)
        - [ ] Implement POSIX special variables and numeric semantics, including `ibase`, `obase`, `scale`, expression printing, and diagnostics/exit behavior. (REQ: REQ-11-0036)
    - [ ] **`bc` GNU/BSD Extensions:** (REQ: REQ-11-0037)
        - [ ] Support GNU-compatible extension controls such as `-s`/strict POSIX mode, `-w`/extension warnings, `-q`, and version/help handling where adopted. (REQ: REQ-11-0038)
        - [ ] Support GNU/BSD extension syntax and behavior intentionally selected for Substrate, such as multi-character names and extension comments, while allowing strict POSIX rejection/warning modes. (REQ: REQ-11-0039)
    - [ ] **`dc` BSD/GNU-Compatible RPN Engine:** (REQ: REQ-11-0040)
        - [ ] Implement the historical/BSD/GNU `dc` stack model, registers, strings/macros, radix/output control, and file/stdin execution flow. (REQ: REQ-11-0041)
        - [ ] Support the selected GNU/BSD extension set for `dc`, including `#` comments, `n`-style output behavior, and compatibility switches where appropriate. (REQ: REQ-11-0042)
    - [ ] **Calculator Verification & Documentation:** (REQ: REQ-11-0043)
        - [ ] Add cross-validation tests that exercise `libbc` via both `bc` and `dc` and verify shared arithmetic behavior. (REQ: REQ-11-0044)
        - [ ] Add man pages for `bc(1)`, `dc(1)`, and `libbc(3)` documenting POSIX baseline, BSD/GNU extensions, and strict-vs-extension modes. (REQ: REQ-11-0045)

- [ ] **Filesystem Tools (`sbin/`):** (REQ: REQ-11-0046)
    - [ ] **`mkfs` (`ext2`):** Native ext2 filesystem creation tool. (REQ: REQ-11-0047)
        - [ ] Create valid ext2 images with superblock, group descriptors, block/inode bitmaps, inode tables, root directory, and `lost+found`. (REQ: REQ-11-0048)
        - [ ] Support configurable filesystem geometry and metadata such as block size, inode ratio/count, reserved blocks, label, UUID, revision, and selected feature flags. (REQ: REQ-11-0049)
        - [ ] Expose a BSD-first CLI contract when BSD and GNU/e2fsprogs conventions differ, while keeping the generated ext2 image broadly interoperable. (REQ: REQ-11-0050)
    - [ ] **`fsck` (`ext2`):** Native ext2 consistency checker/repair tool. (REQ: REQ-11-0051)
        - [ ] Verify and repair superblocks, group descriptors, bitmaps, inode/block references, directory structure, connectivity, link counts, and orphaned objects. (REQ: REQ-11-0052)
        - [ ] Support operational modes analogous to BSD/e2fsck practice: preen/non-interactive, prompt/yes-no repair, alternate superblocks, and deterministic exit status reporting. (REQ: REQ-11-0053)
        - [ ] Prefer conservative repair behavior with clear dry-run/report capabilities before destructive repair paths. (REQ: REQ-11-0054)
    - [ ] **Filesystem Tool Verification & Documentation:** (REQ: REQ-11-0055)
        - [ ] Add image-creation, mount/round-trip, corruption-repair, and feature-matrix regression tests for ext2 tooling. (REQ: REQ-11-0056)
        - [ ] Add man pages for the ext2 `mkfs` and `fsck` utilities documenting on-disk guarantees, compatibility limits, and recovery policy. (REQ: REQ-11-0057)

- [ ] **Compression & Archive Tools:** (REQ: REQ-11-0058)
    - [x] **`compress` / `uncompress` / `zcat` Existing Bring-Up:** Implemented LZW-based commands. (REQ: REQ-11-0059)
    - [ ] **`compress` POSIX/BSD Compliance:** (REQ: REQ-11-0060)
        - [ ] Match POSIX `compress` baseline behavior for stream/file handling, diagnostics, exit status, and `.Z` interoperability. (REQ: REQ-11-0061)
        - [ ] Add the selected BSD extension surface for `compress`, `uncompress`, and `zcat` such as `-b`, `-c`, `-f`, `-v`, suffix/output controls, and compatible quiet/verbose behaviors. (REQ: REQ-11-0062)
        - [ ] Add regression vectors for valid `.Z` streams, truncated/corrupt input, streaming mode, and in-place file replacement policy. (REQ: REQ-11-0063)
    - [x] **`cpio` Existing Bring-Up:** Implement POSIX-compatible archive utility (`usr.bin/cpio`). (REQ: REQ-11-0064)
    - [ ] **`cpio` BSD-First Interoperability:** (REQ: REQ-11-0065)
        - [ ] Support create, extract, and pass-through workflows with correct pathname handling, hardlink/symlink/device preservation, metadata restore, and archive traversal behavior. (REQ: REQ-11-0066)
        - [ ] Support the archive formats needed for practical BSD/GNU interoperability (`odc`, `newc`, `crc`, and any additional explicitly selected interchange format). (REQ: REQ-11-0067)
        - [ ] Prefer BSD option semantics when BSD and GNU `cpio` conflict, while still accepting non-conflicting GNU-compatible options where practical. (REQ: REQ-11-0068)
        - [ ] Add archive interoperability tests and a man page documenting the supported format/option matrix. (REQ: REQ-11-0069)

- [ ] **Cross-Cutting Utility Requirements:** (REQ: REQ-11-0070)
    - [ ] Implement consistent locale, message-catalog, and character-handling behavior wherever the referenced standard or chosen BSD/GNU contract requires it. (REQ: REQ-11-0071)
    - [ ] Enforce correct stdout/stderr discipline, machine-parseable exit statuses, and deterministic diagnostics across all utilities in this section. (REQ: REQ-11-0072)
    - [ ] Provide `--help`/`--version` or BSD-equivalent self-description behavior where the selected GNU/BSD contract requires it, without violating POSIX mode for standardized utilities. (REQ: REQ-11-0073)
    - [ ] Ship or update man pages for every user-facing utility and library named in this section. (REQ: REQ-11-0074)

## User Stories

- **US-11-0001**: As a Substrate contributor implementing core utilities, I want the tasklist to distinguish POSIX-baseline behavior from BSD/GNU extension behavior so that compatibility work is explicit instead of implicit.
- **US-11-0002**: As a Substrate contributor implementing `ps`, I want a BSD-first but POSIX-complete process-reporting utility so that scripts and interactive users both get predictable behavior.
- **US-11-0003**: As a Substrate contributor maintaining `ps`, I want the existing `-b` bitness display preserved as a named requirement so that Substrate-specific observability is not lost during standards work.
- **US-11-0004**: As a Substrate contributor maintaining `setldt` and `ldtctl`, I want a safe, scriptable LDT control surface so that low-level descriptor debugging is possible without ad hoc tooling.
- **US-11-0005**: As a Substrate contributor implementing calculator tooling, I want `libbc` shared by `bc` and `dc` so that numeric behavior is consistent and duplication is minimized.
- **US-11-0006**: As a Substrate contributor implementing `bc`, I want full POSIX.1-2024 behavior plus carefully chosen GNU/BSD extensions so that strict and extended modes are both well-defined.
- **US-11-0007**: As a Substrate contributor implementing `dc`, I want BSD/GNU-compatible RPN behavior so that traditional scripts and test vectors run correctly.
- **US-11-0008**: As a Substrate contributor implementing filesystem utilities, I want native ext2 `mkfs` support so that Substrate can create its own bootable/testable filesystem images.
- **US-11-0009**: As a Substrate contributor implementing filesystem repair tooling, I want ext2 `fsck` support so that damaged images can be diagnosed and repaired inside the system.
- **US-11-0010**: As a Substrate contributor maintaining `compress`, `uncompress`, and `zcat`, I want standards-compliant `.Z` interoperability and BSD-compatible flags so that legacy archives remain usable.
- **US-11-0011**: As a Substrate contributor maintaining `cpio`, I want BSD-first interoperability with GNU archives so that packaging and archive workflows remain portable.
- **US-11-0012**: As a Substrate contributor maintaining the whole utility set, I want consistent diagnostics, exit statuses, and man pages so that the tools feel like one coherent operating system rather than a pile of unrelated commands.

## INCOSE/EARS Requirements

- **REQ-11-0001** (EARS/Ubiquitous): The Substrate system shall define a compliance policy for all utilities in this task section.
- **REQ-11-0002** (EARS/Ubiquitous): The Substrate system shall implement full POSIX.1-2024 behavior for the utilities in this file that are standardized by POSIX.
- **REQ-11-0003** (EARS/Ubiquitous): The Substrate system shall implement BSD behavior as the primary compatibility target for utilities in this file that are not standardized by POSIX.1-2024, and GNU behavior as a secondary compatibility target where non-conflicting.
- **REQ-11-0004** (EARS/Ubiquitous): The Substrate system shall preserve explicit Substrate-specific utility behavior rather than hiding it behind generic compatibility logic.
- **REQ-11-0005** (EARS/Ubiquitous): The Substrate system shall provide process tools in this task section.
- **REQ-11-0006** (EARS/Ubiquitous): The Substrate system shall preserve the existing `ps` bring-up work as part of the task plan.
- **REQ-11-0007** (EARS/Ubiquitous): The Substrate system shall keep `ps -b` as an explicit bitness option backed by `sys_proc_info`.
- **REQ-11-0008** (EARS/Ubiquitous): The Substrate system shall implement the POSIX.1-2024 baseline for `ps`.
- **REQ-11-0009** (EARS/Ubiquitous): The Substrate system shall support the Issue 8/XSI `ps` option surface required by POSIX.
- **REQ-11-0010** (EARS/Ubiquitous): The Substrate system shall match POSIX default process-selection behavior for `ps`.
- **REQ-11-0011** (EARS/Ubiquitous): The Substrate system shall match POSIX inclusive-OR selector combination behavior for `ps`.
- **REQ-11-0012** (EARS/Ubiquitous): The Substrate system shall implement POSIX `ps -o` formatting semantics.
- **REQ-11-0013** (EARS/Ubiquitous): The Substrate system shall match POSIX output requirements for the default, full, and long `ps` listings.
- **REQ-11-0014** (EARS/Ubiquitous): The Substrate system shall define a BSD/GNU extension surface for `ps`.
- **REQ-11-0015** (EARS/Ubiquitous): The Substrate system shall support BSD-style `ps` invocation forms and field conventions expected by common scripts.
- **REQ-11-0016** (EARS/Ubiquitous): The Substrate system shall prefer BSD semantics when BSD and GNU `ps` behavior conflicts.
- **REQ-11-0017** (EARS/Ubiquitous): The Substrate system shall integrate Substrate-specific `ps` columns without breaking standards or BSD compatibility modes.
- **REQ-11-0018** (EARS/Ubiquitous): The Substrate system shall verify and document `ps` behavior.
- **REQ-11-0019** (EARS/Ubiquitous): The Substrate system shall add integration tests for `ps` default, POSIX, BSD, and bitness output modes.
- **REQ-11-0020** (EARS/Ubiquitous): The Substrate system shall document `ps` standards compliance, BSD precedence, and Substrate-specific fields in the manual page.
- **REQ-11-0021** (EARS/Ubiquitous): The Substrate system shall provide LDT tooling in this task section.
- **REQ-11-0022** (EARS/Ubiquitous): The Substrate system shall preserve the existing `setldt`/`ldtctl` bring-up work as part of the task plan.
- **REQ-11-0023** (EARS/Ubiquitous): The Substrate system shall define a stable CLI contract for `setldt` and `ldtctl`.
- **REQ-11-0024** (EARS/Ubiquitous): The Substrate system shall provide explicit inspect/list/show/set/clear operations for LDT descriptors.
- **REQ-11-0025** (EARS/Ubiquitous): The Substrate system shall reject invalid or privilege-escalating LDT manipulations with deterministic diagnostics.
- **REQ-11-0026** (EARS/Ubiquitous): The Substrate system shall provide scriptable output modes for LDT tooling.
- **REQ-11-0027** (EARS/Ubiquitous): The Substrate system shall verify and document `setldt` and `ldtctl`.
- **REQ-11-0028** (EARS/Ubiquitous): The Substrate system shall add integration tests for safe and unsafe `setldt`/`ldtctl` operations.
- **REQ-11-0029** (EARS/Ubiquitous): The Substrate system shall add man pages documenting the Substrate-only nature and privilege model of `setldt` and `ldtctl`.
- **REQ-11-0030** (EARS/Ubiquitous): The Substrate system shall provide calculator tooling in this task section.
- **REQ-11-0031** (EARS/Ubiquitous): The Substrate system shall provide a shared `libbc` runtime for `bc` and `dc`.
- **REQ-11-0032** (EARS/Ubiquitous): The Substrate system shall provide arbitrary-precision arithmetic and base-conversion primitives in `libbc`.
- **REQ-11-0033** (EARS/Ubiquitous): The Substrate system shall centralize shared calculator runtime behavior in `libbc`.
- **REQ-11-0034** (EARS/Ubiquitous): The Substrate system shall implement the POSIX.1-2024 baseline for `bc`.
- **REQ-11-0035** (EARS/Ubiquitous): The Substrate system shall implement the Issue 8 `bc` grammar, execution model, input sequencing, interactive behavior, and `-l` mode.
- **REQ-11-0036** (EARS/Ubiquitous): The Substrate system shall implement POSIX `bc` numeric semantics, special variables, diagnostics, and exit behavior.
- **REQ-11-0037** (EARS/Ubiquitous): The Substrate system shall define the GNU/BSD extension surface for `bc`.
- **REQ-11-0038** (EARS/Ubiquitous): The Substrate system shall support the selected GNU-compatible `bc` extension controls such as strict and warning modes.
- **REQ-11-0039** (EARS/Ubiquitous): The Substrate system shall support the selected GNU/BSD extension syntax for `bc` while preserving strict POSIX operation.
- **REQ-11-0040** (EARS/Ubiquitous): The Substrate system shall implement a BSD/GNU-compatible `dc` engine.
- **REQ-11-0041** (EARS/Ubiquitous): The Substrate system shall implement the historical/BSD/GNU `dc` stack, register, macro, and execution model.
- **REQ-11-0042** (EARS/Ubiquitous): The Substrate system shall support the selected GNU/BSD `dc` extension behaviors.
- **REQ-11-0043** (EARS/Ubiquitous): The Substrate system shall verify and document the calculator toolchain.
- **REQ-11-0044** (EARS/Ubiquitous): The Substrate system shall add cross-validation tests for `libbc`, `bc`, and `dc`.
- **REQ-11-0045** (EARS/Ubiquitous): The Substrate system shall add man pages for `bc`, `dc`, and `libbc` documenting standards and extension modes.
- **REQ-11-0046** (EARS/Ubiquitous): The Substrate system shall provide filesystem utilities in this task section.
- **REQ-11-0047** (EARS/Ubiquitous): The Substrate system shall provide native ext2 `mkfs` functionality.
- **REQ-11-0048** (EARS/Ubiquitous): The Substrate system shall create structurally valid ext2 images including required metadata and root structures.
- **REQ-11-0049** (EARS/Ubiquitous): The Substrate system shall expose configurable ext2 geometry and metadata creation parameters.
- **REQ-11-0050** (EARS/Ubiquitous): The Substrate system shall prefer BSD-style `mkfs` CLI behavior when BSD and GNU/e2fsprogs conventions differ.
- **REQ-11-0051** (EARS/Ubiquitous): The Substrate system shall provide native ext2 `fsck` functionality.
- **REQ-11-0052** (EARS/Ubiquitous): The Substrate system shall verify and repair ext2 structural and metadata consistency defects.
- **REQ-11-0053** (EARS/Ubiquitous): The Substrate system shall support operational `fsck` modes analogous to BSD/e2fsck practice, including deterministic exit status reporting.
- **REQ-11-0054** (EARS/Ubiquitous): The Substrate system shall prefer conservative repair behavior and clear dry-run/report capabilities for ext2 `fsck`.
- **REQ-11-0055** (EARS/Ubiquitous): The Substrate system shall verify and document ext2 `mkfs` and `fsck` behavior.
- **REQ-11-0056** (EARS/Ubiquitous): The Substrate system shall add regression coverage for ext2 image creation, round-trip use, corruption, and repair.
- **REQ-11-0057** (EARS/Ubiquitous): The Substrate system shall add man pages for ext2 `mkfs` and `fsck` utilities.
- **REQ-11-0058** (EARS/Ubiquitous): The Substrate system shall provide compression and archive tooling in this task section.
- **REQ-11-0059** (EARS/Ubiquitous): The Substrate system shall preserve the existing `compress`/`uncompress`/`zcat` bring-up work as part of the task plan.
- **REQ-11-0060** (EARS/Ubiquitous): The Substrate system shall define the standards and extension compliance target for `compress`.
- **REQ-11-0061** (EARS/Ubiquitous): The Substrate system shall match POSIX `compress` baseline behavior and `.Z` interoperability.
- **REQ-11-0062** (EARS/Ubiquitous): The Substrate system shall support the selected BSD extension surface for `compress`, `uncompress`, and `zcat`.
- **REQ-11-0063** (EARS/Ubiquitous): The Substrate system shall add regression vectors for `.Z` compatibility and error handling.
- **REQ-11-0064** (EARS/Ubiquitous): The Substrate system shall preserve the existing `cpio` bring-up work as part of the task plan.
- **REQ-11-0065** (EARS/Ubiquitous): The Substrate system shall define a BSD-first interoperability target for `cpio`.
- **REQ-11-0066** (EARS/Ubiquitous): The Substrate system shall support `cpio` create, extract, and pass-through workflows with correct metadata and special-file handling.
- **REQ-11-0067** (EARS/Ubiquitous): The Substrate system shall support the archive formats needed for BSD/GNU `cpio` interoperability.
- **REQ-11-0068** (EARS/Ubiquitous): The Substrate system shall prefer BSD option semantics when BSD and GNU `cpio` behavior conflicts.
- **REQ-11-0069** (EARS/Ubiquitous): The Substrate system shall add interoperability tests and manual-page coverage for `cpio`.
- **REQ-11-0070** (EARS/Ubiquitous): The Substrate system shall define cross-cutting utility requirements for this task section.
- **REQ-11-0071** (EARS/Ubiquitous): The Substrate system shall implement locale and character-handling behavior required by the referenced standards or selected compatibility contracts.
- **REQ-11-0072** (EARS/Ubiquitous): The Substrate system shall enforce correct stdout/stderr discipline, deterministic diagnostics, and meaningful exit status behavior across this utility set.
- **REQ-11-0073** (EARS/Ubiquitous): The Substrate system shall provide help/version or BSD-equivalent self-description behavior where the selected compatibility contract requires it.
- **REQ-11-0074** (EARS/Ubiquitous): The Substrate system shall ship or update man pages for every user-facing utility and library named in this task section.
