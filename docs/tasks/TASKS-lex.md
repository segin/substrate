# POSIX lex Compliance Task List

> Reference: [IEEE Std 1003.1-2024 (SUSv4) lex](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/lex.html)

## 1. Command-Line Interface

### 1.1 Options
- [x] `-t` - Write output to stdout instead of `lex.yy.c`
- [x] `-n` - Suppress statistics summary
- [x] `-v` - Write statistics to stdout/stderr
- [x] `-n` implied if no table sizes specified and `-v` not specified

### 1.2 Operands
- [x] Accept zero or more input files
- [x] Concatenate multiple files into single lex program
- [x] Use stdin if no files specified
- [x] Use stdin if file operand is `-`

### 1.3 Exit Status
- [x] Return 0 on successful completion
- [x] Return >0 on error

---

## 2. Input File Format

### 2.1 Three-Section Structure
- [x] Parse Definitions section (before first `%%`)
- [x] Parse Rules section (between `%%` markers)
- [x] Parse User Subroutines section (after second `%%`, optional)
- [x] First `%%` required; second `%%` required only if subroutines follow

### 2.2 General Requirements
- [x] Input is text file
- [x] No C-language trigraphs in code
- [x] Code within `%{ %}` must not contain lines of only `%}` or `%%`

---

## 3. Definitions Section

### 3.1 Substitution Strings
- [x] `name substitute` - Define named pattern
- [x] Name must be valid ISO C identifier
- [x] `{name}` expands to `(substitute)` in rules
- [x] No expansion within brackets or double-quotes

### 3.2 Start Conditions
- [x] `%s name...` - Inclusive start conditions
- [x] `%x name...` - Exclusive start conditions
- [x] In `%s` state, unqualified patterns still active
- [x] In `%x` state, only qualified patterns active
- [x] Names follow same rules as definition names

### 3.3 yytext Type
- [x] `%array` - Declare yytext as `char yytext[]`
- [x] `%pointer` - Declare yytext as `char *yytext`
- [x] Default type is implementation-defined
- [x] Required for external references to yytext

### 3.4 Table Size Declarations
- [x] `%p n` - Number of positions (min 2500)
- [x] `%n n` - Number of states (min 500)
- [x] `%a n` - Number of transitions (min 2000)
- [x] `%e n` - Number of parse tree nodes (min 1000)
- [x] `%k n` - Number of packed character classes (min 1000)
- [x] `%o n` - Size of output array (min 3000)

### 3.5 C Code Blocks
- [x] Lines starting with `<blank>` copied to external definitions
- [x] `%{ ... %}` blocks copied unchanged to external definitions
- [x] Code at Rules section start (before first rule) goes into yylex() local scope

---

## 4. Rules Section

### 4.1 Rule Format
- [x] `ERE action` - Pattern followed by action
- [x] ERE separated from action by one or more blanks
- [x] Blanks in ERE: quote entire expression, use brackets, or escape each blank

### 4.2 Pattern Matching
- [x] Match single longest possible string
- [x] On ties, choose first rule in source order
- [x] Default action: copy unmatched input to output
- [x] Minimal `%%` program copies input to output unchanged

---

## 5. Regular Expressions

### 5.1 ERE Support
- [ ] Full extended regular expression support (XBD 9.4)
- [ ] Operator precedence specific to lex (see table below)

### 5.2 Lex-Specific ERE Extensions

#### Quoting
- [x] `"..."` - Characters within quotes represent themselves
- [x] Escape sequences recognized within quotes

#### Start Conditions
- [x] `<state>r` - Match only in specified start condition
- [x] `<s1,s2,...>r` - Match in multiple start conditions
- [x] Start condition notation only at beginning of ERE

#### Trailing Context
- [ ] `r/x` - Match `r` only if followed by `x`
- [ ] Return only `r` portion in yytext
- [ ] No trailing context in `r` or `x`
- [ ] No `$` (end-of-line) in `r`
- [ ] No `^` (beginning-of-line) in `x`
- [ ] Unspecified if trailing portion of `r` matches beginning of `x`

#### Substitution
- [x] `{name}` - Expand named definition
- [x] Treated as if enclosed in parentheses
- [x] No expansion within brackets or double-quotes

### 5.3 Escape Sequences
- [x] `\\`, `\a`, `\b`, `\f`, `\n`, `\r`, `\t`, `\v` - Standard escapes
- [x] `\digits` - Octal (1-3 digits, NUL undefined)
- [x] `\xdigits` - Hexadecimal (NUL undefined)
- [ ] `\c` - Literal character `c` (for any other `c`)

### 5.4 Anchoring
- [x] `^` - Beginning of line (only at ERE start)
- [x] `$` - End of line (only at ERE end, equivalent to `/\n`)
- [x] `^` and `$` apply to entire ERE
- [x] No embedded anchoring (patterns like `(^abc)|(def$)` undefined)

### 5.5 Operator Precedence (High to Low)
1. Collation symbols: `[= =]`, `[: :]`, `[. .]`
2. Escaped characters: `\<special>`
3. Bracket expression: `[ ]`
4. Quoting: `"..."`
5. Grouping: `( )`
6. Definition: `{name}`
7. Single-char duplication: `*`, `+`, `?`
8. Concatenation
9. Interval expression: `{m,n}`
10. Alternation: `|`

---

## 6. Actions

### 6.1 Action Syntax
- [x] Single C statement
- [x] Multiple statements in `{ ... }` braces
- [x] Empty action `;` skips matched input
- [x] Absent action is undefined behavior
- [x] Braces must be balanced

### 6.2 Special Actions
- [x] `|` - Use next rule's action (standalone, no semicolon/braces)
- [x] `ECHO;` - Write yytext to output
- [x] `REJECT;` - Try next matching rule (may not return)
- [x] `BEGIN newstate;` - Switch to start condition

---

## 7. User Subroutines Section

- [ ] Copy all code after second `%%` to end of lex.yy.c
- [ ] Code appears after yylex() definition

---

## 8. Output File (`lex.yy.c`)

### 8.1 Generated Code
- [ ] Conform to ISO C standard
- [ ] No undefined/unspecified/implementation-defined behavior (except copied code)
- [ ] Write to `lex.yy.c` (or stdout with `-t`)
- [ ] File state unspecified on non-zero exit

### 8.2 External Variables
- [ ] `char *yytext` or `char yytext[]` - Matched text (null-terminated)
- [ ] `int yyleng` - Length of matched text
- [ ] `FILE *yyin` - Input file pointer
- [ ] `FILE *yyout` - Output file pointer

### 8.3 Naming Convention
- [ ] All external/static names begin with `yy` or `YY`
- [ ] Exceptions: `input()`, `unput()`, `main()`

---

## 9. Generated Scanner Requirements

### 9.1 Functions in lex.yy.c (or library)
- [ ] `int yylex(void)` - Main lexer, returns token or 0 on EOF
- [ ] `int yymore(void)` - Append next match to yytext
- [ ] `int yyless(int n)` - Retain first n chars, push back rest
- [ ] `int input(void)` - Read next character (0 on EOF)
- [ ] `int unput(int c)` - Push character back to input

### 9.2 Functions in lex library only (`-ll`)
- [ ] `int yywrap(void)` - Called at EOF, return 1 to stop, 0 to continue
- [ ] `int main(int argc, char *argv[])` - Calls yylex() and exits

### 9.3 Default Behavior
- [ ] Read from yyin (default stdin)
- [ ] Copy unmatched input to yyout (default stdout)
- [ ] yywrap() returns 1 (scanner stops at EOF)

---

## 10. Lex Library (`-ll`)

### 10.1 Library Contents
- [ ] Default `main()` - Calls yylex()
- [ ] Default `yywrap()` - Returns 1

### 10.2 Library Redefinition
- [ ] Application can provide own `main()`
- [ ] Application can provide own `yywrap()`
- [ ] Library functions can be reliably overridden

---

## 11. Environment Variables

- [ ] `LANG` - Default internationalization
- [ ] `LC_ALL` - Override all LC_* variables
- [ ] `LC_COLLATE` - Collation for ranges, equivalence classes
- [ ] `LC_CTYPE` - Character interpretation
- [ ] `LC_MESSAGES` - Diagnostic message format
- [ ] `NLSPATH` - Message catalog location (XSI)
- [ ] Behavior unspecified if LC_CTYPE/LC_COLLATE not POSIX locale

---

## 12. Test Suite

### 12.1 Basic Tests
- [ ] Minimal scanner (`%%` only)
- [ ] Single pattern/action
- [ ] Multiple patterns
- [ ] Pattern matching order (longest match, first rule)

### 12.2 Definition Tests
- [ ] Named definitions (`{name}`)
- [ ] Start conditions (`%s`, `%x`)
- [ ] `%array` vs `%pointer`
- [ ] Table size declarations

### 12.3 Pattern Tests
- [ ] Character classes `[abc]`
- [ ] Ranges `[a-z]`
- [ ] Negated classes `[^abc]`
- [ ] Repetition `*`, `+`, `?`
- [ ] Interval `{n,m}`
- [ ] Alternation `|`
- [ ] Grouping `( )`
- [ ] Quoting `"..."`
- [ ] All escape sequences
- [ ] Anchoring `^`, `$`
- [ ] Trailing context `r/x`

### 12.4 Action Tests
- [ ] Empty action `;`
- [ ] Multi-line actions `{ ... }`
- [ ] `|` action chaining
- [ ] `ECHO`
- [ ] `REJECT`
- [ ] `BEGIN`/start conditions

### 12.5 Function Tests
- [ ] `yytext` and `yyleng` access
- [ ] `yymore()` append behavior
- [ ] `yyless()` pushback
- [ ] `input()` character read
- [ ] `unput()` pushback

### 12.6 Integration Tests
- [ ] User subroutines section
- [ ] `%{ %}` code blocks
- [ ] Link with yacc output
- [ ] Custom yywrap()
- [ ] Custom main()

### 12.7 Option Tests
- [ ] `-t` output to stdout
- [ ] `-v` statistics
- [ ] `-n` suppress statistics
- [ ] Multiple input files

---

## 13. Example Grammar Coverage

The following example from POSIX should work:

```lex
%{
#include <math.h>
#include <stdio.h>
%}
DIGIT    [0-9]
ID       [a-z][a-z0-9]*
%%
{DIGIT}+           { printf("integer: %s\n", yytext); }
{DIGIT}+"."{DIGIT}* { printf("float: %s\n", yytext); }
if|then|begin|end|procedure|function { printf("keyword: %s\n", yytext); }
{ID}               printf("identifier: %s\n", yytext);
"+"|"-"|"*"|"/"    printf("operator: %s\n", yytext);
"{"[^}\n]*"}"      /* comment */
[ \t\n]+           /* whitespace */
.                  printf("unrecognized: %s\n", yytext);
%%
int main(int argc, char *argv[]) {
    yyin = (argc > 1) ? fopen(argv[1], "r") : stdin;
    yylex();
}
```
