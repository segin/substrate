# libevent on substrate

libevent 2.1.13-stable, the event loop library `tmux` is written on.

- `fetch.sh` downloads the release tarball from GitHub and checks it
  against the digest GitHub records for the asset.
- `build.sh` cross-builds shared and static libraries and stages them
  under `dist-overlay/dist-libevent/usr/`.

No patches.  Built without OpenSSL, the samples and the regression suite.

Of libevent's backends substrate offers `poll(2)` and `select(2)`;
configure finds no epoll, kqueue, `/dev/poll` or event ports, and a program
reports `using libevent 2.1.13-stable poll`.
