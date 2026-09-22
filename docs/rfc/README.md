# Vendored RFCs

The base protocol specifications for Substrate's IPv4 stack, kept in-tree so
the normative text is available offline and pinned: `sys/net/` cites these by
section number, and a citation is only checkable if the reader has the edition
the citation was written against.

| File | RFC | Title | Implemented by |
| --- | --- | --- | --- |
| `rfc791.txt` | 791 | Internet Protocol | `sys/net/inet.c` |
| `rfc793.txt` | 793 | Transmission Control Protocol | `sys/net/tcp.c`, `sys/include/netinet/tcp.h` |
| `rfc768.txt` | 768 | User Datagram Protocol | `sys/net/udp.c` |

## Which TCP edition

RFC 793 (1981), not RFC 9293 (2022).  RFC 9293 is the current Internet
Standard and obsoletes 793, but it renumbers everything: the kernel's comments
cite 793 section numbers (`RFC 793 3.4`, `RFC 793 3.9`), so 793 is
the edition that makes those comments verifiable.  Nothing here endorses 793
as the more current document -- consult 9293 for the standard as it stands
today, and in particular for the errata and clarifications 793 predates.

Note also that 793 is not the whole of TCP as implemented.  The requirements
that govern the stack's actual behaviour are spread over later RFCs, which are
cited in `sys/net/tcp.c` but not vendored here:

- **RFC 1122** -- host requirements; the "MUST" list 793 left implicit
- **RFC 5681** -- congestion control (slow start, congestion avoidance,
  fast retransmit/recovery)
- **RFC 6298** -- RTO computation
- **RFC 6928** -- initial window of 10 segments
- **RFC 5961** -- blind in-window attack hardening
- **RFC 6528** -- ISN generation

## Provenance

Retrieved 2026-09-22 from the RFC Editor, unmodified:

    https://www.rfc-editor.org/rfc/rfc791.txt
    https://www.rfc-editor.org/rfc/rfc793.txt
    https://www.rfc-editor.org/rfc/rfc768.txt

    6cfb387fcecfc1b72f2f69343c5b6951b5d263d708162e2b5c66ab8f394f6265  rfc791.txt
    e55b1faa35edbeecceb2400233a16ae81897bdf9c31d5b3031e91139e5ea8142  rfc793.txt
    7dc8880e1ecef9c3f9da0db4b876a16e96bfa4f0953cc9d977d414f8f680c2f0  rfc768.txt

These are the canonical plain-text editions and carry no copyright notice:
all three predate the IETF Trust and the RFC 5378 boilerplate.  They are
reproduced verbatim, trailing form feeds and pagination included.  Do not edit
them -- a vendored spec that has been "cleaned up" is no longer the spec.
