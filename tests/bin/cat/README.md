# cat Tests

Run the full host suite:

```sh
make -C tests/bin/cat test
```

Run sanitizer suite:

```sh
make -C tests/bin/cat san
```

Run fuzz smoke:

```sh
make -C tests/bin/cat fuzz-smoke
```

The regression suite uses a hook-enabled build (`CAT_TEST_HOOKS`) to inject
short reads/writes, `EINTR`, lock interruptions, and malloc failure paths.
