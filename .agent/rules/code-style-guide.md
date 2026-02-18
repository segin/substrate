---
trigger: always_on
---

# C Coding Style Ruleset

Follow these rules when generating or modifying C code.

---

## 1. Indentation
- Prefer **tabs**, tab width = **4 spaces**.
- Spaces allowed if tabs are impractical.
- Keep indentation consistent.

---

## 2. Braces

### Placement
- Opening brace goes on the **same line** as the function/control statement.
- Applies to functions, structs, enums, etc.

Example:
    int foo(void) {
        return(0);
    }

### Single-statement bodies
- Omit braces when safe and clear.
- Include braces if readability would improve.

Example:
    if(x)
        do_stuff();
    else {
        do_other();
        log_event();
    }

---

## 3. Spacing

- No space after control keywords:
      if(x)
      while(y)
- Compact `for` formatting:
      for(int i=0;i<n;i++)
- Keep spacing minimal unless readability suffers.

---

## 4. Function Signatures

- Keep the **entire declaration on one line whenever possible**, even if >120 columns.
- Only wrap when readability truly suffers.
- When wrapping:
  - Align parameters under the first parameter.
  - Keep the opening brace on the same line.

Examples:

Single line:
    int process_items(int a, char *items[]) {

Wrapped:
    int process_items(int a,
                      char *items[],
                      int flags) {

- Rule applies to **long return types as well**.

---

## 5. Variable Declarations
- Prefer declarations at **top of block**.
- Multiple variables per line allowed.

Example:
    int x, y;

---

## 6. Pointer and `const` Style
- Asterisk attaches to the variable name:
      char *buf;
      int *ptr, *next;
- `const` goes before the type:
      const char *name;

---

## 7. Naming
- Use **snake_case** for functions and variables.
- Macros may be ALL_CAPS but are discouraged unless appropriate.

---

## 8. Comments
- `//` for single-line comments.
- `/* ... */` for block comments.

---

## 9. Return Statements
- Always use parentheses:
      return(0);
      return(x);

---

## 10. Header Includes
- System headers first.
- Project headers second.

Example:
    #include <stdio.h>
    #include <stdlib.h>

    #include "project.h"

---

## 11. Structs / Enums
- Same brace rules as functions.

Example:
    struct foo {
        int a;
        int b;
    };

---

## 12. Line Length
- No strict maximum.
- Prefer single-line definitions unless readability suffers.

---

## 13. General Philosophy
- Prefer compact, dense code.
- Avoid unnecessary vertical space.
- Allow long lines.
- Expand formatting only when clarity improves.