---
trigger: always_on
---

When working on the Substrate kernel in `sys/`, the following rules must be adhered to:

1. This is not Linux. If a file seems like it's in the wrong place in the filesystem hierarchy, it's likely not. Only move files if the user requests it. Attempt to match the existing hierarchy and do not try to emulate Linux. If you want to emulate an existing hierarchy, mimic one of the BSDs. 
2. NEVER use `extern`. If you're trying to pull in a function via `extern`, something has gone wrong. 
3. NEVER use relative paths in includes. If you're doing `#include "`, you've done goofed. Always give headers as "absolute" paths relative to `sys/` e.g. `<arch/i386/syscall.h>` is `sys/arch/i386/syscall.h`.
4. NEVER put things that should be in headers into source files. This includes preprocessor macro definitions, structure and type definitions.