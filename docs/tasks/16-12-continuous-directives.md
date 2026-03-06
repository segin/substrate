# 12. Continuous Directives

> This file was seeded from `TASKS.md` using a fork-copy (rename+restore) workflow to preserve lineage.
> Source span in original monolith: lines 9652-9664.

## Reimplemented Checklist (All Open)

### 12. Continuous Directives
> [!IMPORTANT]
> These are not one-off tasks but ongoing directives to be performed regularly.

- [ ] **Documentation Maintenance:** (REQ: REQ-16-0001)
    - [ ] **`ARCHITECTURE.md`:** Update whenever major structural changes or design decisions are made. (REQ: REQ-16-0002)
    - [ ] **`AGENTS.md` / `GEMINI.md`:** Update to reflect current project status, context, and new capabilities. (REQ: REQ-16-0003)
    - [ ] **Temporary component tasklists policy:** For active deep work, keep temporary `TASKLIST_*.md` files inside the owning component directory (for example `usr.bin/as/`, `usr.bin/ld/`, `usr.lib/elf/`). Do not track these in `ARCHITECTURE.md`; remove each tasklist when completed. (REQ: REQ-16-0004)
- [ ] **Testing & Quality:** (REQ: REQ-16-0005)
    - [ ] **Regression Tests:** Ensure `make test` (or equivalent) passes before committing. (REQ: REQ-16-0006)
    - [ ] **Code Style:** adhere to kernel coding standards (KNF/Linux-style). (REQ: REQ-16-0007)



## User Stories

- **US-16-0001**: As a Substrate contributor working on 12. Continuous Directives, I want to documentation Maintenance: so that this capability is implemented with clear verification evidence.
- **US-16-0002**: As a Substrate contributor working on 12. Continuous Directives, I want to aRCHITECTURE.md: Update whenever major structural changes or design decisions are made so that this capability is implemented with clear verification evidence.
- **US-16-0003**: As a Substrate contributor working on 12. Continuous Directives, I want to aGENTS.md / GEMINI.md: Update to reflect current project status, context, and new capabilities so that this capability is implemented with clear verification evidence.
- **US-16-0004**: As a Substrate contributor working on 12. Continuous Directives, I want to temporary component tasklists policy: For active deep work, keep temporary TASKLIST_*.md files inside the owning component directory (for example usr.bin/as/, usr.bin/ld/, usr.lib/elf/). Do not track these in ARCHITECTURE.md; remove each tasklist when completed so that this capability is implemented with clear verification evidence.
- **US-16-0005**: As a Substrate contributor working on 12. Continuous Directives, I want to testing & Quality: so that this capability is implemented with clear verification evidence.
- **US-16-0006**: As a Substrate contributor working on 12. Continuous Directives, I want to regression Tests: Ensure make test (or equivalent) passes before committing so that this capability is implemented with clear verification evidence.
- **US-16-0007**: As a Substrate contributor working on 12. Continuous Directives, I want to code Style: adhere to kernel coding standards (KNF/Linux-style) so that this capability is implemented with clear verification evidence.

## INCOSE/EARS Requirements

- **REQ-16-0001** (EARS/Ubiquitous): The Substrate system shall documentation Maintenance:.
  - Context: 12. Continuous Directives
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-16-0002** (EARS/Ubiquitous): The Substrate system shall aRCHITECTURE.md: Update whenever major structural changes or design decisions are made.
  - Context: 12. Continuous Directives
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-16-0003** (EARS/Ubiquitous): The Substrate system shall aGENTS.md / GEMINI.md: Update to reflect current project status, context, and new capabilities.
  - Context: 12. Continuous Directives
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-16-0004** (EARS/Ubiquitous): The Substrate system shall temporary component tasklists policy: For active deep work, keep temporary TASKLIST_*.md files inside the owning component directory (for example usr.bin/as/, usr.bin/ld/, usr.lib/elf/). Do not track these in ARCHITECTURE.md; remove each tasklist when completed.
  - Context: 12. Continuous Directives
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-16-0005** (EARS/Ubiquitous): The Substrate system shall testing & Quality:.
  - Context: 12. Continuous Directives
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-16-0006** (EARS/Ubiquitous): The Substrate system shall regression Tests: Ensure make test (or equivalent) passes before committing.
  - Context: 12. Continuous Directives
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-16-0007** (EARS/Ubiquitous): The Substrate system shall code Style: adhere to kernel coding standards (KNF/Linux-style).
  - Context: 12. Continuous Directives
  - Verification: design review + implementation evidence + test/doc update.
