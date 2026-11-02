# New-device approval screenshots, 2026-11-01

`kypost-linux-21-{before,after}-<layout>-<view>-<light|dark>.png`: before is origin/main
f1dbf58, after is branch `fix/516-device-approval`. Rendered offscreen by `KyPostScreens` with
fixture data in the `kypost-ci:f44` container, phone 412x850 (`--mobile`) and desktop 1440x900,
Busnes Light and Busnes Dark. `pairing-approval` is the pairing page after register answers 202:
before, the old client's failure; after, the waiting screen. `pairing-not-approved` (after only)
is the start page after a 410 or expiry.

# Council round 1 screenshots, 2026-11-01

`kypost-linux-15-{before,after}-<layout>-<view>-<light|dark|pair>.png`: before is origin/main
6399b27, after is branch `design/council-r1`. Rendered offscreen by `KyPostScreens` with fixture
data in the `kypost-ci:f44` container: phone 412x850 (`--mobile`; inbox, Settings and Appearance)
and desktop 1440x900 (inbox, a selected message, the folder drawer, Appearance, Security and
Contacts). `light` and `dark` are Busnes Light and Busnes Dark; `pair` is Forest following the
system (light under the offscreen platform).

# Theme screenshots, 2026-10-31

Before (origin/main 2fe0ab01) and after the Ky suite theme pairs, rendered offscreen by
`KyPostScreens` with fixture data: phone 412x850 (`--mobile`) and desktop 1440x900, inbox
and Settings → Appearance. The theme is the saved `appearance/themeId` value, so "after"
shots show the migration: `Busnes Light`/`Busnes Dark` open as Busnes with that mode,
`Ocean` as Ocean Dark, and `forest-pair` (after only) is Forest following the system
(light under the offscreen platform).
