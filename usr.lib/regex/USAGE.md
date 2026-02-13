# Usage

## Compile and Match

```c
regex_err_t err;
regex_t *re = regex_compile("foo[0-9]+", REGEX_FLAG_EXTENDED, &err);
if (!re) {
    /* handle err */
}

size_t caps[2];
ssize_t rc = regex_match(re, "foo123", 6, caps, 2, &err);
if (rc >= 0) {
    /* caps[0]..caps[1] */
}
regex_free(re);
```

## Limits

```c
regex_limits_t limits = regex_default_limits();
limits.match_steps = 50000;
regex_set_limits(re, &limits);
```

## Replace

```c
char *out = NULL;
size_t out_len = 0;
regex_replace(re, "foo123", 6, "bar$0", 0, &out, &out_len);
free(out);
```

## Streaming

```c
regex_iter_t *it = regex_iter_create(re, REGEX_ITER_DEFAULT, &err);
regex_iter_feed(it, chunk, len);
regex_iter_finish(it);
while (regex_iter_next(it, &s, &e, caps, cap_cap, &cap_count) > 0) {
    /* handle match */
}
regex_iter_destroy(it);
```
