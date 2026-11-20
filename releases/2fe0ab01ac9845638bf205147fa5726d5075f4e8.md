# KyPost source 2fe0ab01ac98

Source: https://github.com/Busnes-app/KyPost-for-Linux/commit/2fe0ab01ac9845638bf205147fa5726d5075f4e8

[Full release history](https://github.com/Busnes-app/KyPost-for-Linux/blob/2fe0ab01ac9845638bf205147fa5726d5075f4e8/CHANGELOG.md)

### Changes

- Documentation only: the README's Features, Building and Architecture sections
  moved verbatim to [docs/FEATURES.md](docs/FEATURES.md),
  [docs/BUILDING.md](docs/BUILDING.md) and
  [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Links to the old README sections
  land on its Documentation list, which links the new pages.
- Repository hygiene: a stray compiled `scripts/__pycache__` file is no longer
  tracked, and `__pycache__/` is ignored.
- CI-only: the Scorecard workflow's `github/codeql-action/upload-sarif` moves
  from 4.39.0 to 4.39.1.
- CI-only: the release-notes check no longer demands new notes for a change
  confined to `.github/`, such as a Dependabot Actions bump.

### Upgrade impact

- None. Nothing shipped changes.
