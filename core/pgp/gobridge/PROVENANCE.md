# Shared fixtures

All keys and X-Wing seeds here are **public test secrets**.

Copied byte for byte from KyPost-Server worktree `spec-a-p2`, commit
`4eb06d13e8b04993e74deacc1b1888f8e4e556e0` (server PR #755).
`testdata/openpgp-v6/SHA256SUMS` is the upstream manifest; the adjacent README
records generation and Bouncy Castle 1.87 cross-checks. The Go and C++ tests
consume those exact messages and signatures. `testdata/SHA256SUMS` pins the
X-Wing server- and browser-sealed fixtures.

Re-copy fixture changes from upstream; do not regenerate or edit them here.

`LICENSES/` preserves the notices for Go and every statically linked module.
Refresh the matching notice when a dependency's license changes; CMake installs
them with the application.
