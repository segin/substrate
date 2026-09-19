# sqlite3

SQLite — a self-contained SQL database engine.

Upstream: <https://www.sqlite.org/>
Pinned version: **3.53.1** (autoconf tarball `sqlite-autoconf-3530100`)
License: public domain.
Substrate vendoring: tarball + patch series.

## Build

```
./fetch.sh
./build.sh
```

Produces `dist-sqlite3/usr/` with `bin/sqlite3` (the CLI),
`lib/libsqlite3.{a,so,so.0,so.0.8.6}`, `include/{sqlite3.h,sqlite3ext.h}`
and a hand-written `lib/pkgconfig/sqlite3.pc`.

The amalgamation is compiled twice, plain for the static library and
`-fPIC` for the shared one; the soname is `libsqlite3.so.0` and the file
`libsqlite3.so.0.8.6`, matching upstream's libtool build.  The `sqlite3`
CLI links the shared library (`DT_NEEDED libsqlite3.so.0`).  The static
library stays for anything that wants to link SQLite in.

## Notes

SQLite ships as a single-file amalgamation, so `build.sh` compiles
`sqlite3.c` directly with the cross compiler — no need to run the
(autosetup) configure under cross-compilation.

Built **thread-safe** (`SQLITE_THREADSAFE=1`).  substrate's
`pthread_mutex_t` is a bare futex word with no room for an owner id
or recursion count, so `SQLITE_HOMEGROWN_RECURSIVE_MUTEX` has
SQLite implement recursive-mutex semantics itself over plain
(non-recursive) pthread mutexes.

Required by `contrib/elinks` whenever its ECMAScript backend is
enabled (elinks stores JavaScript `localStorage` in SQLite).
