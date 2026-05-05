# cal Design

## Scope

`bin/cal/` provides a production rewrite of `cal(1)` with a small modular split:

- `cal_opts.c`: CLI parsing and display-mode selection
- `cal_math.c`: Gregorian/Julian arithmetic, reform handling, ISO week numbers
- `cal_render.c`: locale-aware month/day naming and multi-month layout rendering
- `cal.c`: main program flow

## Calendar Model

The implementation supports three chronology modes:

- Mixed mode: Julian dates before the configured reform date, Gregorian dates on and after it
- Pure Gregorian mode
- Pure Julian mode

The reform date is interpreted as the first Gregorian day in the local transition. The code validates a textual date by checking whether its Gregorian absolute day falls on or after the reform or its Julian absolute day falls before the reform. Dates that satisfy neither rule are part of the transition gap and are omitted from the rendered month.

The default reform is `1752-09-14`, which produces the British/American September 1752 gap.

## Rendering

Months are rendered into fixed line blocks and then printed one or three columns at a time depending on the view mode. In year mode the block titles omit the year because the overall view has a centered year header. Other views include the year in each block title to keep cross-year ranges unambiguous.

`-j` widens each day cell to three digits and renders day-of-year ordinals instead of month-local dates.

`-w` prefixes each rendered week with an ISO week number derived from the week row's Thursday.

## Highlighting And Locale

Month and weekday names are obtained through `strftime(3)` after `setlocale(LC_TIME, "")`, with simple English fallbacks if formatting fails.

Today highlighting uses reverse-video ANSI escape sequences and is enabled only when:

- highlighting is not disabled with `-h` / `--no-highlight`, and
- color mode is `always`, or color mode is `auto` and standard output is a TTY

## Validation Strategy

`bin/cal/tests/` covers:

- option parsing
- leap-year and reform-gap math
- CLI integration smoke checks
- reference cases for September 1752 and Julian/Gregorian February behavior
- randomized property checks for month day counts
- fuzz-smoke option handling