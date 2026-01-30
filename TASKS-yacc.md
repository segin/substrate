# POSIX yacc Compliance Task List

> Reference: [IEEE Std 1003.1-2024 (SUSv4) yacc](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/yacc.html)

## 1. Command-Line Interface

### 1.1 Options
- [x] `-b file_prefix` - Use `file_prefix` instead of `y` for output filenames
- [x] `-d` - Generate header file (`y.tab.h` or `file_prefix.tab.h`)
- [x] `-l` - Suppress `#line` directives in generated code
- [x] `-p sym_prefix` - Replace `yy` prefix with `sym_prefix` in external names
- [x] `-t` - Enable runtime debugging code compilation (`YYDEBUG`)
- [x] `-v` - Generate description file (`y.output` or `file_prefix.output`)

### 1.2 Operands
- [x] Accept single grammar file operand
- [x] Report error if no grammar file specified

### 1.3 Exit Status
- [x] Return 0 on successful completion
- [x] Return >0 on error

---

## 2. Input File Format

### 2.1 Three-Section Structure
- [x] Parse declarations section (before first `%%`)
- [x] Parse grammar rules section (between `%%` markers)
- [x] Parse programs/epilogue section (after second `%%`, optional)
- [x] Second `%%` is optional if programs section is empty

### 2.2 Lexical Structure
- [x] Ignore `<blank>`, `<newline>`, `<formfeed>` except in names/symbols
- [x] Support C-style comments `/* ... */`
- [x] Names: letters, `.`, `_`, non-initial digits (arbitrary length)
- [x] Case-sensitive names
- [x] Character literals: single character in single quotes
- [x] Support all ISO C escape sequences in literals (`\n`, `\t`, `\r`, `\\`, `\'`, etc.)
- [x] Reject NUL character in grammar rules or literals

---

## 3. Declarations Section

### 3.1 Token Declarations
- [x] `%token [<tag>] name [number] [name [number]]...`
- [x] Optional `<tag>` for C union member type
- [x] Optional `number` to assign explicit token value
- [x] Tokens not explicitly numbered get values > 256

### 3.2 Precedence/Associativity Declarations
- [x] `%left [<tag>] name [number]...` - Left associative
- [x] `%right [<tag>] name [number]...` - Right associative
- [x] `%nonassoc [<tag>] name [number]...` - Non-associative (error on associative use)
- [x] Lines in order of increasing precedence
- [x] All tokens on same line have same precedence

### 3.3 Type Declarations
- [x] `%type <tag> name...` - Declare non-terminal types
- [x] Require `<tag>` for `%type`
- [x] Prohibit token numbers or literals with `%type`
- [x] Enable type checking when `%type` is used

### 3.4 Start Symbol
- [x] `%start name` - Declare start symbol
- [x] Default to LHS of first grammar rule if not specified

### 3.5 Union Declaration
- [x] `%union { body }` - Declare YYSTYPE union
- [x] Body must not contain unbalanced curly braces
- [x] Generate YYSTYPE typedef from union

### 3.6 C Code Blocks
- [x] `%{ ... %}` - Copy enclosed C code to output file
- [x] Code has global scope in output
- [x] Must not contain `%}` outside comment/string/literal
- [x] Terminate declarations section with `%%`

---

## 4. Grammar Rules Section

### 4.1 Rule Syntax
- [x] `A : BODY ;` format
- [x] `|` for multiple alternatives with same LHS
- [x] Empty BODY for epsilon productions
- [x] Assign unique number to each rule

### 4.2 Semantic Actions
- [x] `{ C-code }` - Arbitrary C statements
- [x] Actions can appear anywhere in rule (mid-rule actions)
- [x] Mid-rule actions create anonymous nonterminals
- [x] `$$` - Access/set rule result value (translates to `yyval`)
- [x] `$N` - Access Nth RHS symbol value (translates to `yyvsp[offset]`)
- [x] `$-N` - Access symbol before current rule
- [x] `$<tag>N` - Typed access with explicit union member
- [x] Default `$$ = $1` if no action specified

### 4.3 Precedence Override
- [x] `%prec token` - Override rule precedence with token's precedence
- [x] Rule precedence defaults to last token/literal in body

### 4.4 Error Token
- [x] Reserved `error` token for error recovery
- [x] Default value 256 (configurable via `%token`)
- [x] Lexer should not return `error` value

---

## 5. Programs/Epilogue Section

### 5.1 Epilogue Handling
- [x] Copy all code after second `%%` to end of output file
- [x] Code appears after `yyparse()` definition
- [x] Placement relative to semantic actions is unspecified

---

## 6. Output Files

### 6.1 Code File (`y.tab.c`)
- [x] Generate C source conforming to ISO C standard
- [x] No undefined/unspecified/implementation-defined behavior (except copied code)
- [x] Include `extern int yychar` or `int yychar` definition
- [x] Include function prototypes:
  - [x] `void yyerror(const char *);`
  - [x] `int yylex(void);`
  - [x] `int yyparse(void);`
- [x] Protect `yyerror`/`yylex` declarations with `#ifndef` macros
- [x] Include `#define` statements for tokens (same as header)
- [x] Include YYSTYPE definition if `%union` used
- [x] Include `extern YYSTYPE yylval` or `YYSTYPE yylval` definition
- [x] NO declaration of `main()` unless in `%{ %}` block
- [x] Copy `%{ %}` code before semantic actions
- [x] Generate `yyparse()` function

### 6.2 Header File (`y.tab.h`)
- [x] Generate only with `-d` option
- [x] `#define` statements for token names/values
- [x] YYSTYPE declaration if `%union` used
- [x] `extern YYSTYPE yylval` declaration
- [x] May declare `yyparse()` with prototype
- [x] Must NOT declare `yyerror()` or `yylex()`

### 6.3 Description File (`y.output`)
- [x] Generate only with `-v` option
- [x] State machine description (format unspecified)
- [x] Internal table limits report

---

## 7. Generated Parser Requirements

### 7.1 Parser Function
- [x] `int yyparse(void)` - Main parser function
- [x] Return 0 on successful parse (YYACCEPT)
- [x] Return non-zero on error (YYABORT or unrecoverable)
- [x] Implement LALR(1) parsing algorithm

### 7.2 External Variables
- [x] `int yychar` - Current lookahead token
- [x] `YYSTYPE yylval` - Semantic value of lookahead
- [x] `int yydebug` - Runtime debug flag (initial value 0)
- [x] `int yynerrs` - Syntax error count
- [x] `int yyerrflag` - Error recovery state

### 7.3 Required Macros
- [x] `YYEOF` - End-of-file token value
- [x] `YYEMPTY` - No lookahead token indicator
- [x] `YYERRCODE` - Error token value (default 256)
- [x] `YYMAXDEPTH` - Maximum parse stack depth
- [x] `yyerrok` - Clear error state: `(yyerrflag = 0)`
- [x] `yyclearin` - Discard lookahead: `(yychar = YYEMPTY)` or `(yychar = -1)`
- [x] `YYACCEPT` - Return 0 from parser
- [x] `YYABORT` - Return non-zero from parser
- [x] `YYERROR` - Initiate error handling from semantic action
- [x] `YYRECOVERING()` - Return 1 if recovering from error, 0 otherwise

### 7.4 Debugging
- [x] `YYDEBUG` preprocessor symbol controls debug code
- [x] `-t` sets `YYDEBUG` to 1 if not already defined
- [x] Without `-t`, set `YYDEBUG` to 0 if not defined
- [x] Debug output includes shift/reduce actions, input symbols, error recovery

### 7.5 Lexer Interface
- [x] Call `yylex()` to get next token
- [x] `yylex()` returns token number > 0, or ≤ 0 for EOF
- [x] Assign return value to `yychar`
- [x] Associated value in `yylval`
- [x] Convert ≤ 0 returns to `YYEOF`
- [x] Single-byte literals: token = character value
- [x] Never request token when only reduction possible

---

## 8. Error Handling

### 8.1 Error Detection
- [x] Detect syntax error when action is `error`
- [x] Call `yyerror("syntax error")` on first error
- [x] Do NOT call `yyerror()` if recovering (< 3 shifts since last error)
- [x] Do NOT call `yyerror()` when `YYERROR` executed

### 8.2 Error Recovery
- [x] Pop stack until state allows shift on `error` token
- [x] If stack empties, return non-zero
- [x] Shift `error` token and resume parsing
- [x] Discard lookahead if error during recovery and not endmarker
- [x] Return non-zero if endmarker during recovery
- [x] Parser fully recovered after 3 normal shifts or `yyerrok`

### 8.3 YYERROR Macro
- [x] Semantic actions can execute `YYERROR`
- [x] `YYERROR` passes control back to parser
- [x] Cannot be used outside semantic actions

---

## 9. Conflict Resolution

### 9.1 Shift/Reduce Conflicts
- [x] Use precedence/associativity if both rule and token have them
- [x] Higher precedence wins
- [x] Same precedence: left→reduce, right→shift, nonassoc→error
- [x] Default: shift (count as conflict)

### 9.2 Reduce/Reduce Conflicts
- [x] Reduce by earlier rule in input sequence
- [x] Count as conflict

### 9.3 Conflict Reporting
- [x] Report conflicts to stderr (format unspecified)
- [x] Conflicts resolved by precedence NOT counted
- [x] Report in description file with `-v`

---

## 10. Symbol Prefix (`-p` Option)

### 10.1 Required Substitutions
- [x] `yychar` → `{prefix}char`
- [x] `yydebug` → `{prefix}debug`
- [x] `yyerror` → `{prefix}error`
- [x] `yylex` → `{prefix}lex`
- [x] `yylval` → `{prefix}lval`
- [x] `yynerrs` → `{prefix}nerrs`
- [x] `yyparse` → `{prefix}parse`

---

## 11. Yacc Library (`-ly`)

### 11.1 Library Functions
- [x] `int main(void)` - Calls `yyparse()`, returns 0
- [x] `void yyerror(const char *msg)` - Prints message to stderr
- [x] `main()` calls `setlocale(LC_ALL, "")`

### 11.2 Library Precedence
- [x] `-ly` must precede `-ll` for correct `main()`

---

## 12. Internal Limits

### 12.1 Minimum Maximums
- [x] `{NTERMS}` ≥ 126 tokens
- [x] `{NNONTERM}` ≥ 200 non-terminals
- [x] `{NPROD}` ≥ 300 rules
- [x] `{NSTATES}` ≥ 600 states
- [x] `{MEMSIZE}` ≥ 5200 rule length (names)
- [x] `{ACTSIZE}` ≥ 4000 parser actions

---

## 13. Environment Variables

- [x] `LANG` - Default internationalization
- [x] `LC_ALL` - Override all LC_* variables
- [x] `LC_CTYPE` - Character interpretation
- [x] `LC_MESSAGES` - Diagnostic message format
- [x] `NLSPATH` - Message catalog location (XSI)

---

## 14. Test Suite

### 14.1 Basic Tests
- [x] Minimal grammar (single rule)
- [x] Token declarations with explicit numbers
- [x] Precedence and associativity
- [x] `%type` declarations with type checking
- [x] `%union` declaration
- [x] `%start` symbol override

### 14.2 Grammar Tests
- [x] Empty productions
- [x] Mid-rule actions
- [x] `$$`, `$N`, `$-N` references
- [x] `%prec` precedence override
- [x] Multiple alternatives with `|`

### 14.3 Error Recovery Tests
- [x] `error` token in grammar
- [x] `yyerrok` macro
- [x] `yyclearin` macro
- [x] `YYERROR` from semantic action
- [x] `YYRECOVERING()` macro

### 14.4 Output Tests
- [x] Code file structure
- [x] Header file with `-d`
- [x] Description file with `-v`
- [x] `-b` prefix option
- [x] `-p` symbol prefix
- [x] `-l` suppress line directives
- [x] `-t` debug mode

### 14.5 Conflict Tests
- [x] Shift/reduce conflict resolution
- [x] Reduce/reduce conflict resolution
- [x] Precedence-based resolution
- [x] Conflict reporting

### 14.6 Integration Tests
- [x] Calculator with variables
- [x] Expression parser
- [x] C-like grammar subset
- [x] Link with lex output
