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
- [ ] `%token [<tag>] name [number] [name [number]]...`
- [ ] Optional `<tag>` for C union member type
- [ ] Optional `number` to assign explicit token value
- [ ] Tokens not explicitly numbered get values > 256

### 3.2 Precedence/Associativity Declarations
- [ ] `%left [<tag>] name [number]...` - Left associative
- [ ] `%right [<tag>] name [number]...` - Right associative
- [ ] `%nonassoc [<tag>] name [number]...` - Non-associative (error on associative use)
- [ ] Lines in order of increasing precedence
- [ ] All tokens on same line have same precedence

### 3.3 Type Declarations
- [ ] `%type <tag> name...` - Declare non-terminal types
- [ ] Require `<tag>` for `%type`
- [ ] Prohibit token numbers or literals with `%type`
- [ ] Enable type checking when `%type` is used

### 3.4 Start Symbol
- [ ] `%start name` - Declare start symbol
- [ ] Default to LHS of first grammar rule if not specified

### 3.5 Union Declaration
- [x] `%union { body }` - Declare YYSTYPE union
- [x] Body must not contain unbalanced curly braces
- [x] Generate YYSTYPE typedef from union

### 3.6 C Code Blocks
- [ ] `%{ ... %}` - Copy enclosed C code to output file
- [ ] Code has global scope in output
- [ ] Must not contain `%}` outside comment/string/literal
- [ ] Terminate declarations section with `%%`

---

## 4. Grammar Rules Section

### 4.1 Rule Syntax
- [ ] `A : BODY ;` format
- [ ] `|` for multiple alternatives with same LHS
- [ ] Empty BODY for epsilon productions
- [ ] Assign unique number to each rule

### 4.2 Semantic Actions
- [ ] `{ C-code }` - Arbitrary C statements
- [ ] Actions can appear anywhere in rule (mid-rule actions)
- [ ] Mid-rule actions create anonymous nonterminals
- [ ] `$$` - Access/set rule result value (translates to `yyval`)
- [ ] `$N` - Access Nth RHS symbol value (translates to `yyvsp[offset]`)
- [ ] `$-N` - Access symbol before current rule
- [ ] `$<tag>N` - Typed access with explicit union member
- [ ] Default `$$ = $1` if no action specified

### 4.3 Precedence Override
- [ ] `%prec token` - Override rule precedence with token's precedence
- [ ] Rule precedence defaults to last token/literal in body

### 4.4 Error Token
- [ ] Reserved `error` token for error recovery
- [ ] Default value 256 (configurable via `%token`)
- [ ] Lexer should not return `error` value

---

## 5. Programs/Epilogue Section

### 5.1 Epilogue Handling
- [ ] Copy all code after second `%%` to end of output file
- [ ] Code appears after `yyparse()` definition
- [ ] Placement relative to semantic actions is unspecified

---

## 6. Output Files

### 6.1 Code File (`y.tab.c`)
- [ ] Generate C source conforming to ISO C standard
- [ ] No undefined/unspecified/implementation-defined behavior (except copied code)
- [ ] Include `extern int yychar` or `int yychar` definition
- [ ] Include function prototypes:
  - [ ] `void yyerror(const char *);`
  - [ ] `int yylex(void);`
  - [ ] `int yyparse(void);`
- [ ] Protect `yyerror`/`yylex` declarations with `#ifndef` macros
- [ ] Include `#define` statements for tokens (same as header)
- [ ] Include YYSTYPE definition if `%union` used
- [ ] Include `extern YYSTYPE yylval` or `YYSTYPE yylval` definition
- [ ] NO declaration of `main()` unless in `%{ %}` block
- [ ] Copy `%{ %}` code before semantic actions
- [ ] Generate `yyparse()` function

### 6.2 Header File (`y.tab.h`)
- [ ] Generate only with `-d` option
- [ ] `#define` statements for token names/values
- [ ] YYSTYPE declaration if `%union` used
- [ ] `extern YYSTYPE yylval` declaration
- [ ] May declare `yyparse()` with prototype
- [ ] Must NOT declare `yyerror()` or `yylex()`

### 6.3 Description File (`y.output`)
- [ ] Generate only with `-v` option
- [ ] State machine description (format unspecified)
- [ ] Internal table limits report

---

## 7. Generated Parser Requirements

### 7.1 Parser Function
- [ ] `int yyparse(void)` - Main parser function
- [ ] Return 0 on successful parse (YYACCEPT)
- [ ] Return non-zero on error (YYABORT or unrecoverable)
- [ ] Implement LALR(1) parsing algorithm

### 7.2 External Variables
- [ ] `int yychar` - Current lookahead token
- [ ] `YYSTYPE yylval` - Semantic value of lookahead
- [ ] `int yydebug` - Runtime debug flag (initial value 0)
- [ ] `int yynerrs` - Syntax error count
- [ ] `int yyerrflag` - Error recovery state

### 7.3 Required Macros
- [ ] `YYEOF` - End-of-file token value
- [ ] `YYEMPTY` - No lookahead token indicator
- [ ] `YYERRCODE` - Error token value (default 256)
- [ ] `YYMAXDEPTH` - Maximum parse stack depth
- [ ] `yyerrok` - Clear error state: `(yyerrflag = 0)`
- [ ] `yyclearin` - Discard lookahead: `(yychar = YYEMPTY)` or `(yychar = -1)`
- [ ] `YYACCEPT` - Return 0 from parser
- [ ] `YYABORT` - Return non-zero from parser
- [ ] `YYERROR` - Initiate error handling from semantic action
- [ ] `YYRECOVERING()` - Return 1 if recovering from error, 0 otherwise

### 7.4 Debugging
- [ ] `YYDEBUG` preprocessor symbol controls debug code
- [ ] `-t` sets `YYDEBUG` to 1 if not already defined
- [ ] Without `-t`, set `YYDEBUG` to 0 if not defined
- [ ] Debug output includes shift/reduce actions, input symbols, error recovery

### 7.5 Lexer Interface
- [ ] Call `yylex()` to get next token
- [ ] `yylex()` returns token number > 0, or ≤ 0 for EOF
- [ ] Assign return value to `yychar`
- [ ] Associated value in `yylval`
- [ ] Convert ≤ 0 returns to `YYEOF`
- [ ] Single-byte literals: token = character value
- [ ] Never request token when only reduction possible

---

## 8. Error Handling

### 8.1 Error Detection
- [ ] Detect syntax error when action is `error`
- [ ] Call `yyerror("syntax error")` on first error
- [ ] Do NOT call `yyerror()` if recovering (< 3 shifts since last error)
- [ ] Do NOT call `yyerror()` when `YYERROR` executed

### 8.2 Error Recovery
- [ ] Pop stack until state allows shift on `error` token
- [ ] If stack empties, return non-zero
- [ ] Shift `error` token and resume parsing
- [ ] Discard lookahead if error during recovery and not endmarker
- [ ] Return non-zero if endmarker during recovery
- [ ] Parser fully recovered after 3 normal shifts or `yyerrok`

### 8.3 YYERROR Macro
- [ ] Semantic actions can execute `YYERROR`
- [ ] `YYERROR` passes control back to parser
- [ ] Cannot be used outside semantic actions

---

## 9. Conflict Resolution

### 9.1 Shift/Reduce Conflicts
- [ ] Use precedence/associativity if both rule and token have them
- [ ] Higher precedence wins
- [ ] Same precedence: left→reduce, right→shift, nonassoc→error
- [ ] Default: shift (count as conflict)

### 9.2 Reduce/Reduce Conflicts
- [ ] Reduce by earlier rule in input sequence
- [ ] Count as conflict

### 9.3 Conflict Reporting
- [ ] Report conflicts to stderr (format unspecified)
- [ ] Conflicts resolved by precedence NOT counted
- [ ] Report in description file with `-v`

---

## 10. Symbol Prefix (`-p` Option)

### 10.1 Required Substitutions
- [ ] `yychar` → `{prefix}char`
- [ ] `yydebug` → `{prefix}debug`
- [ ] `yyerror` → `{prefix}error`
- [ ] `yylex` → `{prefix}lex`
- [ ] `yylval` → `{prefix}lval`
- [ ] `yynerrs` → `{prefix}nerrs`
- [ ] `yyparse` → `{prefix}parse`

---

## 11. Yacc Library (`-ly`)

### 11.1 Library Functions
- [ ] `int main(void)` - Calls `yyparse()`, returns 0
- [ ] `void yyerror(const char *msg)` - Prints message to stderr
- [ ] `main()` calls `setlocale(LC_ALL, "")`

### 11.2 Library Precedence
- [ ] `-ly` must precede `-ll` for correct `main()`

---

## 12. Internal Limits

### 12.1 Minimum Maximums
- [ ] `{NTERMS}` ≥ 126 tokens
- [ ] `{NNONTERM}` ≥ 200 non-terminals
- [ ] `{NPROD}` ≥ 300 rules
- [ ] `{NSTATES}` ≥ 600 states
- [ ] `{MEMSIZE}` ≥ 5200 rule length (names)
- [ ] `{ACTSIZE}` ≥ 4000 parser actions

---

## 13. Environment Variables

- [ ] `LANG` - Default internationalization
- [ ] `LC_ALL` - Override all LC_* variables
- [ ] `LC_CTYPE` - Character interpretation
- [ ] `LC_MESSAGES` - Diagnostic message format
- [ ] `NLSPATH` - Message catalog location (XSI)

---

## 14. Test Suite

### 14.1 Basic Tests
- [ ] Minimal grammar (single rule)
- [ ] Token declarations with explicit numbers
- [ ] Precedence and associativity
- [ ] `%type` declarations with type checking
- [ ] `%union` declaration
- [ ] `%start` symbol override

### 14.2 Grammar Tests
- [ ] Empty productions
- [ ] Mid-rule actions
- [ ] `$$`, `$N`, `$-N` references
- [ ] `%prec` precedence override
- [ ] Multiple alternatives with `|`

### 14.3 Error Recovery Tests
- [ ] `error` token in grammar
- [ ] `yyerrok` macro
- [ ] `yyclearin` macro
- [ ] `YYERROR` from semantic action
- [ ] `YYRECOVERING()` macro

### 14.4 Output Tests
- [ ] Code file structure
- [ ] Header file with `-d`
- [ ] Description file with `-v`
- [ ] `-b` prefix option
- [ ] `-p` symbol prefix
- [ ] `-l` suppress line directives
- [ ] `-t` debug mode

### 14.5 Conflict Tests
- [ ] Shift/reduce conflict resolution
- [ ] Reduce/reduce conflict resolution
- [ ] Precedence-based resolution
- [ ] Conflict reporting

### 14.6 Integration Tests
- [ ] Calculator with variables
- [ ] Expression parser
- [ ] C-like grammar subset
- [ ] Link with lex output
