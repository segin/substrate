# Yacc Implementation Specification

## PART 1: BASIC UNIX YACC FEATURES (CLASSIC BEHAVIOR)

### 1.1 INPUT LANGUAGE
1. Grammar sections: Declarations (%%), Rules (%%), User subroutines (%%)
2. Token declarations: %token, %left, %right, %nonassoc with optional integer precedence
3. Union type declaration: %union with C-style member declarations
4. Type association: %type <tag> symbols
5. Start symbol: %start symbol
6. Precedence resolution: explicit precedence declarations and implicit rule precedence
7. Literal tokens: Single-quoted characters in rules
8. Special directives: %prec for overriding precedence, %pure-parser, %parse-param
9. Embedded actions: C code in { } within rules, including mid-rule actions
10. Value references: $$ (LHS), $1, $2... (RHS symbols)
11. Lexical interface: extern int yylex(), yyerror(const char *)

### 1.2 OUTPUT PARSER
12. Parser skeleton: Default skeleton with yyparse(), yydebug, YYDEBUG
13. State machine: LALR(1) construction with deterministic conflict resolution
14. Conflict reporting: "shift/reduce" and "reduce/reduce" warnings with verbose state reports
15. Parse tables: Compressed tables (yyact, yypact, yypgoto, yys, yydef, yyr2, yychk, yyr1)
16. Error recovery: Error productions with 'error' symbol, YYRECOVERING()
17. Lookahead token: yychar variable, YYEMPTY state
18. Stack management: yyval (value stack), yylval (lookahead value), yyloc (location tracking if enabled)
19. Debug output: YYDEBUG or #define DEBUG, yydebug variable, -t runtime flag behavior
20. Parser return: 0 (success), 1 (syntax error), 2 (memory exhaustion)

### 1.3 CODE GENERATION
21. Header file: -d flag generates y.tab.h with token definitions
22. Multiple files: -b flag for base filename, default y.tab.c and y.tab.h
23. Verbose file: -v flag generates y.output with state machine description
24. Graph output: -g flag (System V) for state graph in DOT/GRAP format
25. Line directives: #line directives in generated code preserving original locations

### 1.4 ALGORITHMIC BEHAVIOR
26. State reduction: Default reductions only when no shift possible
27. Conflict resolution: Shift over reduce (shift/reduce), first rule (reduce/reduce)
28. Table compression: Packed representation with negative states as default actions
29. Lookahead computation: Spontaneous lookaheads for ε-productions in error states
30. Default start symbol: First non-terminal in grammar specification

## PART 2: POSIX.1-2017 MINUTIAE (MANDATORY CONFORMANCE)

### 2.1 COMMAND LINE OPTIONS (IEEE Std 1003.1-2017, XCU § 4)
31. -b file_prefix: Use 'file_prefix' instead of 'y' for output files
32. -d: Write header file with token definitions (y.tab.h or file_prefix.tab.h)
33. -l: Omit #line directives in generated code
34. -t: Modify runtime debugging in generated code (default no debugging)
35. -v: Write verbose description to y.output or file_prefix.output
36. Grammar_file: Optional input file (default stdin)
37. Exit status: 0=success, >0=error, 1=invalid arguments, 2=input error, 3=grammar error, 4=system error

### 2.2 INPUT FILE FORMAT (XCU § 4, Grammar § 2)
38. Three sections separated by '%%' lines: Declarations, Rules, Programs
39. Declaration section: C declarations enclosed in '%{' and '%}', token declarations
40. Token declaration syntax: %token [<type>] token... (optional type in brackets)
41. Precedence syntax: %left token..., %right token..., %nonassoc token...
42. Start declaration: %start non-terminal
43. Union syntax: %union { body } where body is C member declarations
44. Type declaration: %type <type> symbol...
45. Grammar rules: non-terminal : symbol-list [precedence] ;
46. Symbol-list: Empty or symbol... with optional semantic action
47. Semantic action: { C statement... } returning value via $$
48. Precedence override: %prec token at end of rule
49. Literal token: 'c' (single character in single quotes)
50. Programs section: C code copied verbatim after second '%%'

### 2.3 GENERATED PARSER INTERFACE
51. yyparse(): External entry point returning 0 (accept) or 1 (error)
52. yylex(): Externally provided lexical analyzer returning integer token
53. yyerror(s): Externally provided error reporter (const char * argument)
54. yylval: External union YYSTYPE for lexical value
55. yychar: External int containing current lookahead (optional)
56. yydebug: External int enabling debug output (if YYDEBUG nonzero)
57. YYSTYPE: Union type defined in header (from %union)
58. YYLTYPE: Structure type for location tracking (not required but if used)

### 2.4 GENERATED HEADER FILE (-d)
59. #define YYSTYPE union-type if %union used, otherwise int
60. #define tokens: Token numbers for declared tokens (except literal tokens)
61. Token numbering: User-defined tokens >255, others <256
62. Reserved ranges: 0-255 single-character tokens, 257 upward named tokens
63. YYERRCODE: Token number for error (typically 256)
64. End-of-file marker: 0 token number

### 2.5 CONFLICT RESOLUTION SPECIFICS
65. Shift/reduce: Prefer shift when both shift and reduce possible
66. Reduce/reduce: Choose rule appearing first in grammar specification
67. Precedence resolution: Compare rule precedence (from %prec or last terminal) vs token precedence
68. Associativity: %left (reduce), %right (shift), %nonassoc (error on conflict)
69. Default precedence: Rule inherits precedence of rightmost terminal, or none

### 2.6 ERROR RECOVERY REQUIREMENTS
70. Error symbol: Special 'error' non-terminal in grammar rules
71. Recovery action: Discard tokens until finding synchronization point
72. YYRECOVERING macro: Nonzero during error recovery phase
73. Error count: yynerrs variable tracking number of errors
74. Default error rule: If no error rule, parser immediately returns on error

### 2.7 PORTABILITY CONSTRAINTS
75. Character set: Input files as text files in POSIX locale
76. Output format: Generated C code conforming to ISO C standard
77. Name reservations: yy* and YY* prefixes reserved for implementation
78. No limits: No arbitrary limits on grammar size (within system memory)
79. Reentrancy: Optional via %pure-parser with altered interface

### 2.8 BEHAVIORAL CORNER CASES
80. Empty rules: A : ; is valid empty production
81. Multiple actions: Rules may have multiple embedded actions with values
82. Mid-rule actions: Treated as separate productions with generated non-terminals
83. Value types: $$ and $n references follow %type declarations or default union member
84. Undefined symbols: Warning for undefined non-terminals (no rules)
85. Unused terminals: Warning for declared but unused tokens
86. Unused non-terminals: Warning for non-terminals defined but not reachable from start
87. Type conflicts: Error if symbol used with inconsistent value types
88. Precedence conflicts: Only warned when actually ambiguous, not when resolved by precedence

### 2.9 OUTPUT FILE SPECIFICS
89. y.tab.c: Generated parser (or file_prefix.tab.c)
90. y.tab.h: Generated header with token definitions (with -d)
91. y.output: State listing with lookaheads (with -v)
92. File creation: Output files created even on error (possibly empty)
93. Line numbering: #line directives unless -l option or system doesn't support

### 2.10 EXTENSION CONSTRAINTS
94. Extensions permitted only if: Not change behavior of conforming grammar, documented
95. Conflicts with POSIX: POSIX behavior takes precedence
96. Additional warnings: Allowed beyond required diagnostics
97. Additional options: Must not conflict with standard options

## PART 3: IMPLEMENTATION REQUIREMENTS

### 3.1 COMPLIANCE VALIDATION
98. Must pass POSIX YACC test suite (if available)
99. Must parse legacy yacc grammar files without modification
100. Must generate parsers compatible with lex/flex lexical analyzers

### 3.2 ERROR HANDLING
101. Syntax errors: Line number and character position in input grammar
102. Grammar errors: Multiple definitions, type mismatches, illegal symbols
103. Resource errors: Memory exhaustion with graceful failure
104. File errors: Permission denied, disk full with appropriate exit codes

### 3.2.1 Reader Invariants
1. Mid-rule action synthetic symbols (`$@N`) are interned in the global symbol table.
2. Synthetic symbols participate in symbol packing and verbose output like normal nonterminals.
3. Reader output (`plhs`, `rrhs`, `ritem`) must not contain unresolved temporary indices.

### 3.3 PERFORMANCE CHARACTERISTICS
105. Time complexity: O(n³) worst-case for state construction (typical yacc algorithm)
106. Memory: Efficient table compression equivalent to historical implementations
107. Parser speed: Generated parser should match typical LALR(1) performance

### 3.3.1 LR(0) Item-Set Determinism
1. Closure item vectors are sorted before GOTO partitioning.
2. Kernel items are grouped by shift symbol in deterministic symbol-index order.
3. Kernel slices are sorted and deduplicated before state interning.
4. Repeated parser generation over the same grammar must produce byte-identical state listings.

### 3.3.2 Closure Semantics
1. Closure expansion is transitive over nonterminals reachable after the dot.
2. Each production contributes at most one `B -> .γ` item per closure set.
3. Closure output is sorted and deduplicated before LR(0) kernel partitioning.

### 3.3.3 GOTO Graph Correctness
1. Shift and reduction records are keyed by the source LR state being processed.
2. GOTO edge construction preserves deterministic symbol ordering per state.
3. No shift edge is emitted for states whose closure contains only completed items.

### 3.4 DOCUMENTATION
108. Manual page: Conforming to POSIX man page format
109. Extension documentation: Clearly marked implementation extensions
110. Conformance statement: Explicit claim of POSIX conformance

### 3.5 TESTABILITY
111. Self-test: Ability to parse its own grammar specification
112. Regression suite: Historical yacc test cases (calc, mfcalc, etc.)
113. Boundary tests: Maximum grammar sizes, edge case productions

## PART 4: GENERATION CONSTRAINTS

The implementation must:
114. Compile cleanly under -Wall -Wextra -pedantic in C99/C11 environments
115. Avoid undefined behavior per ISO C standard
116. Be portable across 32-bit and 64-bit systems
117. Handle large grammars (up to system memory limits)
118. Produce identical behavior for equivalent grammars regardless of input order
119. Maintain bug-for-bug compatibility with classic yacc for historical grammars
120. Include all necessary skeleton files and runtime library components
