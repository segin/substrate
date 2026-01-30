# POSIX lex Compliance Task List

> Reference: [IEEE Std 1003.1-2024 (SUSv4) lex](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/lex.html)

## 1. Command-Line Interface

### 1.1 Options
- [ ] `-t` - Write output to stdout instead of `lex.yy.c`
- [ ] `-n` - Suppress statistics summary
- [ ] `-v` - Write statistics to stdout/stderr
- [ ] `-n` implied if no table sizes specified and `-v` not specified

### 1.2 Operands
- [ ] Accept zero or more input files
- [ ] Concatenate multiple files into single lex program
- [ ] Use stdin if no files specified
- [ ] Use stdin if file operand is `-`

### 1.3 Exit Status
- [ ] Return 0 on successful completion
- [ ] Return >0 on error

---

## 2. Input File Format

### 2.1 Three-Section Structure
- [ ] Parse Definitions section (before first `%%`)
- [ ] Parse Rules section (between `%%` markers)
- [ ] Parse User Subroutines section (after second `%%`, optional)
- [ ] First `%%` required; second `%%` required only if subroutines follow

### 2.2 General Requirements
- [ ] Input is text file
- [ ] No C-language trigraphs in code
- [ ] Code within `%{ %}` must not contain lines of only `%}` or `%%`

---

## 3. Definitions Section

### 3.1 Substitution Strings
- [ ] `name substitute` - Define named pattern
- [ ] Name must be valid ISO C identifier
- [ ] `{name}` expands to `(substitute)` in rules
- [ ] No expansion within brackets or double-quotes

### 3.2 Start Conditions
- [ ] `%s name...` - Inclusive start conditions
- [ ] `%x name...` - Exclusive start conditions
- [ ] In `%s` state, unqualified patterns still active
- [ ] In `%x` state, only qualified patterns active
- [ ] Names follow same rules as definition names

### 3.3 yytext Type
- [ ] `%array` - Declare yytext as `char yytext[]`
- [ ] `%pointer` - Declare yytext as `char *yytext`
- [ ] Default type is implementation-defined
- [ ] Required for external references to yytext

### 3.4 Table Size Declarations
- [ ] `%p n` - Number of positions (min 2500)
- [ ] `%n n` - Number of states (min 500)
- [ ] `%a n` - Number of transitions (min 2000)
- [ ] `%e n` - Number of parse tree nodes (min 1000)
- [ ] `%k n` - Number of packed character classes (min 1000)
- [ ] `%o n` - Size of output array (min 3000)

### 3.5 C Code Blocks
- [ ] Lines starting with `<blank>` copied to external definitions
- [ ] `%{ ... %}` blocks copied unchanged to external definitions
- [ ] Code at Rules section start (before first rule) goes into yylex() local scope

---

## 4. Rules Section

### 4.1 Rule Format
- [ ] `ERE action` - Pattern followed by action
- [ ] ERE separated from action by one or more blanks
- [ ] Blanks in ERE: quote entire expression, use brackets, or escape each blank

### 4.2 Pattern Matching
- [ ] Match single longest possible string
- [ ] On ties, choose first rule in source order
- [ ] Default action: copy unmatched input to output
- [ ] Minimal `%%` program copies input to output unchanged

---

## 5. Regular Expressions

### 5.1 ERE Support
- [ ] Full extended regular expression support (XBD 9.4)
- [ ] Operator precedence specific to lex (see table below)

### 5.2 Lex-Specific ERE Extensions

#### Quoting
- [ ] `"..."` - Characters within quotes represent themselves
- [ ] Escape sequences recognized within quotes

#### Start Conditions
- [ ] `<state>r` - Match only in specified start condition
- [ ] `<s1,s2,...>r` - Match in multiple start conditions
- [ ] Start condition notation only at beginning of ERE

#### Trailing Context
- [ ] `r/x` - Match `r` only if followed by `x`
- [ ] Return only `r` portion in yytext
- [ ] No trailing context in `r` or `x`
- [ ] No `$` (end-of-line) in `r`
- [ ] No `^` (beginning-of-line) in `x`
- [ ] Unspecified if trailing portion of `r` matches beginning of `x`

#### Substitution
- [ ] `{name}` - Expand named definition
- [ ] Treated as if enclosed in parentheses
- [ ] No expansion within brackets or double-quotes

### 5.3 Escape Sequences
- [ ] `\\`, `\a`, `\b`, `\f`, `\n`, `\r`, `\t`, `\v` - Standard escapes
- [ ] `\digits` - Octal (1-3 digits, NUL undefined)
- [ ] `\xdigits` - Hexadecimal (NUL undefined)
- [ ] `\c` - Literal character `c` (for any other `c`)

### 5.4 Anchoring
- [ ] `^` - Beginning of line (only at ERE start)
- [ ] `$` - End of line (only at ERE end, equivalent to `/\n`)
- [ ] `^` and `$` apply to entire ERE
- [ ] No embedded anchoring (patterns like `(^abc)|(def$)` undefined)

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
- [ ] Single C statement
- [ ] Multiple statements in `{ ... }` braces
- [ ] Empty action `;` skips matched input
- [ ] Absent action is undefined behavior
- [ ] Braces must be balanced

### 6.2 Special Actions
- [ ] `|` - Use next rule's action (standalone, no semicolon/braces)
- [ ] `ECHO;` - Write yytext to output
- [ ] `REJECT;` - Try next matching rule (may not return)
- [ ] `BEGIN newstate;` - Switch to start condition

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
