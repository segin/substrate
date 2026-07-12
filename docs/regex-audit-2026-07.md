# usr.lib/regex audit - 2026-07

Full read of the Substrate regex library (~5,000 lines) at commit
8ebc61bca: the core dispatch (compile.c, exec.c, util.c, posix_compat.c),
the default self-contained **safe engine** (engine_safe.c, 3,088 lines),
and the two optional adapters (engine_posix.c host-passthrough,
engine_pcre2_adapter.c).  This is the `libregex` that `bin/vi`/`bin/ex`
and other tools link; it matches UNTRUSTED patterns and text, so
memory-safety and DoS-resistance are the priority.

The large safe engine was read by parallel auditors; findings were then
verified against a host build of `libregex.a` (a probe harness plus an
AddressSanitizer/UBSan build).  Findings numbered REGEX-NN by severity.
Status: **all fixed** - REGEX-01 through REGEX-20 are each fixed and
verified in their own commit (build + AddressSanitizer/UBSan + a
behavioural check, GNU-sed/grep parity where applicable).  Two further
defects found while verifying fixes are also fixed: **REGEX-21**
(match_queue ring-buffer corruption when the streaming queue grows past
its first capacity, found verifying REGEX-07) and **REGEX-22** ($N group
offsets not adjusted by match position in replace, found verifying
REGEX-08).  REGEX-17's fix lives in `include/sys/types.h` (ssize_t width);
REGEX-12 also required making the never-compiled PCRE2 adapter build.
`[VERIFIED]` = reproduced against the host binary (ASan or observed
behavior); `[code]` = confirmed by inspection.

What's solid: no forbidden `strcpy`/`strcat`/`sprintf`/`vsprintf`/`gets`
anywhere; the realloc-grow sites all use the temp-pointer pattern (no
clobber-on-failure); UTF-8 decode bounds-checks continuation bytes and
rejects overlong/out-of-range; `find_all`, `split`, and `match` handle
zero-width matches safely (only `replace` does not - REGEX-01); the
parser bounds hex reads and swaps inverted class ranges.  The
backtracker shares one step budget across restarts.

## High

### REGEX-01: `regex_replace` heap-overflows on any empty-matchable pattern (memcpy size = -1)
`safe_regex_replace` ends with an UNCONDITIONAL "copy the rest of the
text" (engine_safe.c:2595-2601): `memcpy(out+out_len, text+pos,
text_len - pos)`.  After a zero-width match at end of text the loop does
`pos++` (2590) making `pos == text_len + 1`, so `text_len - pos`
underflows to `SIZE_MAX`.  **[VERIFIED - AddressSanitizer]**
`regex_replace(/a*/, ..., global)` - i.e. `s/a*/X/g`, `s/.*/Y/g`,
`s/x*/-/g`, or an empty pattern - aborts with
`negative-size-param (size=-1) ... engine_safe.c:2601` (a heap buffer
overflow of unbounded size).  `find_all`/`split`/`match` guard this
correctly; only `replace` is missing the `if (pos <= text_len)` guard
the other engines have.  Fix: guard the final copy (copy 0 and just
write the NUL when `pos > text_len`).

### REGEX-02: `{m,n}` compilation is a DoS - the whole NFA is built before the state limit is checked
`compile_node` materializes the entire NFA, and `state_count >
limits.max_states` is only tested AFTER (engine_safe.c:3017 build,
:3031 check), while `{m,n}` expansion (repeat loop ~1274-1285) with no
count cap allocates `n` copies.  **[VERIFIED - timeout]** `a{100000000}`
does not fail fast - it hangs for many seconds building ~10^8 states
before the limit fires (`a{500}` is instant; `a{100000}` already
fails).  `(a{1000}){1000}` reaches ~10^6 states; another nesting level
~10^9.  Fix: enforce `max_states` INSIDE the expansion/allocation loop,
and reject `{m,n}` whose `n` exceeds a RE_DUP_MAX-style cap up front.

### REGEX-03: POSIX character classes `[[:alpha:]]` etc. are not implemented (silently mis-parsed)
`parse_charclass` (engine_safe.c:442) has no `[:...:]` handling, so
`[[:alpha:]]` is parsed as a bracket set of the literals `[ : a l p h`
terminated by the first `]`, leaving a stray `]`.  **[VERIFIED]**
`[[:alpha:]]+` and `[[:space:]]` both FAIL to match ("hello123", "a b")
while plain `[abc]+` matches - so every POSIX-class pattern silently
never matches.  ex/vi and scripts pass these routinely.  Fix: implement
`[:alpha:]`/`[:digit:]`/`[:space:]`/… inside bracket expressions.

### REGEX-04: `{m,n}` counts have no overflow guard
`min = min*10 + digit` / `max = …` accumulate with no cap or overflow
check (engine_safe.c:~898-907), so `a{4000000000}` wraps `size_t`
and/or drives the REGEX-02 expansion with a garbage count.  Cap the
digits and reject overflow with `REGEX_ERR_SYNTAX`.  (Root cause shared
with REGEX-02.)

### REGEX-05: `frag_*` builders dereference `nfa_state_new()` without a NULL check
`frag_literal` (`s->ch = cp`, engine_safe.c:1115), `frag_dot`,
`frag_class`, `frag_anchor`, `frag_star/plus/qmark`, `frag_group`, and
`compile_node`'s empty case all use the returned state immediately, but
`nfa_state_new` returns NULL on allocation failure (1096/1103).  Under
the large `{m,n}` expansion of REGEX-02/04 the allocator eventually
fails mid-compile and `s->ch = cp` writes through NULL (or `patch()`
later stores through a NULL+offset) - turning OOM into a crash /
arbitrary write.  Check every `nfa_state_new` result.

### REGEX-06: unbounded parser and compile-node recursion (no depth cap)
`parse_regex -> parse_concat -> parse_repeat -> parse_atom ->
parse_group -> parse_regex` (engine_safe.c:~994) and `compile_node`
(:1214) recurse once per nesting level with no depth counter; a deeply
nested pattern `((((…))))` overflows the C stack.  **[partly VERIFIED]**
up to ~20,000-deep via argv it fails cleanly (the state-count limit
trips first), but a pattern delivered from a file (an `ex :s//` pattern,
a config) with deeper nesting can overflow the stack before the state
limit.  Add an explicit recursion-depth cap in both the parser and
compile_node.

### REGEX-07: streaming never trims consumed bytes, so `max_stream_buffer` makes long streams impossible
`iter_feed` only grows `it->buffer` and hard-rejects once
`buf_len + len > max_stream_buffer` (engine_safe.c:~2795), but nothing
trims bytes below the scan position and `base_offset` is never advanced
(dead, 2842).  **[code]** Any stream longer than `max_stream_buffer`
eventually returns a permanent error even though earlier matches were
already emitted - streaming over large inputs is impossible, and memory
grows to the cap regardless of how much has been consumed.  Fix: drop
consumed prefix bytes and track a running base offset.

## Medium

### REGEX-08: a `$N` referencing a non-existent group aborts the whole replace
In `safe_regex_replace`, `$5` when the pattern has fewer groups calls
`replace_append_capture` which returns `REGEX_ERR_INVALID_ARGUMENT`
(engine_safe.c:~2485/2563), failing the entire operation.
**[VERIFIED]** `regex_replace(/a/, "aaa", "$9", global)` returns rc=5
with NULL output.  POSIX/ed/sed treat an out-of-range backref group as
empty (or literal).  Fix: substitute empty for an out-of-range group
rather than erroring.

### REGEX-09: captures silently dropped when the caller's buffer is smaller than the group count, but success is still returned
`nfa_capture_match` (1949), `safe_regex_backtrack` (2309), and
`iter_next` (2901) only copy offsets when `max_captures >= cap_count`;
a caller that provides a smaller buffer gets it left untouched
(uninitialized) while the function returns `capture_count` > what it
filled.  **[code]** The caller then reads garbage offsets it believes
were written.  Fix: copy `min(max_captures, cap_count)` and return the
number actually written (or document the contract and require the full
size).

### REGEX-10: POSIX `regexec` ignores `eflags` (`REG_NOTBOL`/`REG_NOTEOL`) and `REG_NOSUB`
`regexec` does `(void)eflags;` (posix_compat.c:61), so `^`/`$`
anchoring can't be disabled for mid-buffer matches, and `regcomp`
ignores `REG_NOSUB` (still allocates/report captures).  **[code]**
Iterated line matching that relies on `REG_NOTBOL` misbehaves.

### REGEX-11: host POSIX engine's `^`-anchored `find_all`/`replace` match at every offset
`posix_match_from` copies the substring from each offset and matches it
from position 0 with `eflags=0` (engine_posix.c:129-132), so `^` matches
at EVERY restart, not just real line starts - `find_all(/^a/, "aaa")`
reports 3 matches.  **[code]** Only built under `REGEX_USE_HOST_POSIX`
(host tests), not the shipped substrate engine.  Fix: pass
`REG_NOTBOL` when `offset > 0` (and match in place rather than copying).

### REGEX-12: PCRE2 adapter leaks the split array on OOM and mis-detects non-matching replace
`pcre2_split` frees `caps` but never frees `items` (or the already-built
segments) on any `malloc`/`realloc` failure (engine_pcre2_adapter.c:
260/268/287/295) - a leak cluster.  `pcre2_replace` treats anything
other than `PCRE2_ERROR_NOMEMORY` from the size-probe as an error
(199-201), so a pattern that does not match returns `REGEX_ERR_INTERNAL`
instead of the unchanged text.  **[code]** Only built under
`REGEX_USE_PCRE2` (optional).

## Low

### REGEX-13: `{m,n}` with `n < m` is accepted, not rejected (engine_safe.c:~922)
`a{5,2}` compiles as `a{5}` (the `max > min` branch never runs) instead
of `REGEX_ERR_SYNTAX`.

### REGEX-14: `\U` accepts 8 hex digits with no codepoint-range check (engine_safe.c:731,543)
Values up to `0xFFFFFFFF` are admitted into the program with no
`> 0x10FFFF` rejection.

### REGEX-15: `match_steps` is per-scan-position, not global, for the non-backref path
`nfa_capture_match`/`dfa_match_span` reset `steps=0` at each of up to
`text_len` start positions (engine_safe.c:1854/1985, loop 2068), so
worst-case work is `O(text_len * match_steps)` rather than the intended
`O(match_steps)` - a weaker DoS bound on very long inputs.  (The
backtracker correctly shares one budget.)

### REGEX-16: streaming `max_matches` bounds queued, not cumulative, matches (engine_safe.c:2863)
The limit checks `queue.count`; a consumer draining between feeds can
receive unlimited total matches - weak DoS bound on match volume.

### REGEX-17: `regex_match` no-match return is `0xFFFFFFFF` (not sign-extended) on LP64
**[VERIFIED - host]** on a 64-bit build the substrate `regex_match` API
returns `4294967295` for no-match instead of `-1`, so a caller doing
`rc < 0` (find_all/match) treats no-match as a huge match.  On 32-bit
substrate `ssize_t` is 32-bit so `0xFFFFFFFF == -1` (correct), and the
POSIX `regexec` path is unaffected - hence host-test-only, but the
truncation in `safe_regex_match_internal`'s return should be fixed for
portability.

### REGEX-18: compile.c `fail:` path may leak engine `impl` (compile.c:105-110)
On `engine->compile` returning an error after it allocated `re->impl`,
the label does `free(re)` without `engine->destroy(re)`.  Depends on
each engine freeing its own partial state on error; verify and, to be
safe, call destroy on the fail path.

### REGEX-19: `REG_NEWLINE` is mapped to `MULTILINE` only (posix_compat.c:27-28)
`REG_NEWLINE` also requires that `.` and negated classes not match
newline; mapping it to `REGEX_FLAG_MULTILINE` alone leaves `.` matching
`\n`, so `REG_NEWLINE` is partially honored.

### REGEX-20: latent `size_t` underflow in split segment length (engine_safe.c:~2657)
`seg_len = start - pos` is unchecked; currently `start >= pos` always
holds, but a future anchored/zero-width change producing `start < pos`
would underflow into a huge `malloc`+`memcpy`.  Add an assert/guard.

## Verification method
Built `libregex.a` (native) and a probe harness; ran an
AddressSanitizer + UBSan build over replace/find_all/split/match with
adversarial inputs.  Reproduced: REGEX-01 (`s/a*/X/g`, `s/.*/Y/g`,
`s/x*/-/g`, empty pattern all abort at engine_safe.c:2601 with
`negative-size-param`), REGEX-02 (`a{10^8}` compile timeout while
`a{500}` is instant), REGEX-03 (`[[:alpha:]]+`/`[[:space:]]` never
match), REGEX-08 (`$9` -> rc=5, NULL output), REGEX-17 (no-match ->
4294967295 on the 64-bit host).  `find_all`/`split`/`match` were
verified zero-width-safe under ASan.  The remaining findings are on
OOM / very-deep-nesting / streaming / optional-engine paths confirmed
by source inspection.
