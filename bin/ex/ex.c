#include "exvi.h"

/*
 * ex: the shared editor, started in ex mode.  Its visual mode (`-v`,
 * `:visual`) is the library's own, run in this process on the same buffer;
 * ex no longer execs a `vi` -- found beside it in the source tree, or else
 * whatever `vi` was on PATH -- with the file's name.
 */
int
main(int argc, char **argv)
{
    return exvi_main(argc, argv, EXVI_FRONTEND_EX);
}
