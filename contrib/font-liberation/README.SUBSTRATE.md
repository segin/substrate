# Liberation 2.1.5 — substrate port

Liberation Sans, Serif and Mono in regular, bold, italic and bold italic:
12 TrueType fonts with the same advance widths as Arial, Times New Roman
and Courier New, so text laid out for those fonts does not reflow.
Licence: SIL Open Font License 1.1.

## Build

```sh
./fetch.sh        # SHA-256 matches FreeBSD's x11-fonts/liberation-fonts-ttf
./build.sh        # -> dist-font-liberation/usr/share/fonts/liberation/*.ttf
```

No compile step and no dependencies.

## Notes

- fontconfig's stock `30-metric-aliases.conf`, already enabled, maps the
  Microsoft names to these, so `fc-match Arial` gives Liberation Sans and a
  program or document asking for "Times New Roman" gets Liberation Serif.
- Licence, authors and README under `/usr/share/doc/font-liberation/`.
