foo = 1 + 2 * 3;
ASSERT(foo == 7, "bad basic assignment");
SECTIONS { .text : { *(.text) } }
