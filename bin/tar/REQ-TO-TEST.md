# tar Requirement-to-Test Matrix

| Requirement | Test | Coverage |
|---|---|---|
| R1 PAX-compatible | `test_tar.sh` `test_pax_interop` | Verifies `--format=pax` output is listable/extractable by host `tar`. |
| R2 Safe-extract | `test_tar.sh` `test_safe_extract` | Verifies absolute/`..` entries are rejected with `--safe-extract`. |
| R3 Sparse | `test_tar.sh` `test_sparse_roundtrip` | Verifies extracted sparse file has similar apparent size and hole behavior. |
| R4 Incremental | `test_tar.sh` `test_incremental` | Verifies snapshot-based incremental archive includes changed files. |
| R5 Interop | `test_tar.sh` `test_roundtrip_stream` and `test_pax_interop` | Verifies stream mode and host tool interoperability. |
