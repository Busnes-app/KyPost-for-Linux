# Security Policy

KyPost for Linux is the desktop companion to a self-hosted KyPost server. It
holds a per-device pairing credential for someone's mail account, caches their
mail, and holds the PGP private key sealed to this device. This document covers
how to report a vulnerability and where the boundary with the server lies.

For **server-side** security — TLS termination, reverse proxies, key custody at
rest, deployment hardening — see
[SECURITY.md in the server repository](https://github.com/Busnes-app/KyPost-Server/blob/main/SECURITY.md).
Report server vulnerabilities there, not here.

## Reporting a vulnerability

Report privately through GitHub Security Advisories rather than opening a public
issue.

1. Go to the [Security Advisories](https://github.com/Busnes-app/KyPost-for-Linux/security/advisories/new) page
2. Click **Report a vulnerability**
3. Give a description, the affected versions, and reproduction steps if you have them
4. Please do not disclose publicly until a fix is available

Private vulnerability reporting is enabled on this repository, so that form works
for anyone — you do not need to be a maintainer.

If you are unsure which repository a finding belongs to, file it here and it will
be moved. A misfiled report is better than an unfiled one.

## What this client is responsible for

- **The device credential.** `X-Kypost-Device-Id` / `X-Kypost-Device-Secret` are
  minted once at pairing and authenticate every later request. They are this
  device's identity to the server.
- **The sealed PGP envelope.** Enrollment seals the account key to a key in this
  device's own store. The verification code shown during enrollment is the only
  thing standing between you and a server that substituted its own key — see
  [PLATFORM_BASELINE.md](https://github.com/Busnes-app/KyPost-Server/blob/main/docs/PLATFORM_BASELINE.md)
  section 2, which is normative across every client.
- **Rendering hostile mail.** Message bodies are attacker-controlled input.
- **Key strength.** Every HTTPS connection and every new OpenPGP signature or
  encryption refuses keys below RSA-2048/EC-224, including weak trusted roots.
  [docs/STRONG-CRYPTO.md](docs/STRONG-CRYPTO.md) lists each boundary, the
  deliberate exceptions and what is out of scope.

## Pairing and certificate pinning

Pairing pins the relay's certificate, and later calls to the relay must match the pin. The
stored pin is the SHA-256 of the public key of the certificate that issued the relay's leaf
(`core/net/HttpClient.cpp`, `pinnedSpkiFromChain`).

- **With `pin=` in the pairing link**, the registration handshake must match it. On a new
  connection the check runs when the TLS handshake completes, before the pairing token or
  push credentials are sent. A mismatch, or a `pin=` that cannot be parsed, refuses the
  pairing.
- **Without `pin=`, pairing is trust-on-first-use.** The client pins the certificate that
  served registration. It detects a certificate that changes afterwards, but not an
  interceptor already in place at pairing. On a network with a locally trusted CA, such as
  enterprise MDM, a user-installed root or a hostile captive portal, that interceptor can
  read the pairing token. Pair over a network you trust.
- **Automatic re-registration is trust-on-first-use again.** At startup, and when the push
  endpoint or transport changes, the client re-registers without the stored pin and pins the
  key that answers, so the same interceptor can take over then. Qt's normal certificate
  validation still applies. The reasons are recorded at
  `DeviceRegistrationService::reregistrationParams`.
- Because the pin names the issuing CA's key, not the leaf's, a certificate for the relay's
  name from the same issuing CA also matches.

How these protections fit together is argued in [docs/ASSURANCE-CASE.md](docs/ASSURANCE-CASE.md).
How to check a release is in [docs/VERIFYING-RELEASES.md](docs/VERIFYING-RELEASES.md).

## Disclosure timeline

The same commitments as the server repository, so a finding that spans both is
not governed by two different clocks:

- Acknowledge receipt within **2 business days**
- Provide a timeline and severity assessment
- Develop and test a patch
- Coordinate a release that includes the fix before the deadline
- Credit you in the release notes, unless you prefer anonymity

Public disclosure timelines are by severity — **critical** 30 days, **high** 60,
**moderate** 90 — as set out in
[the server's SECURITY.md](https://github.com/Busnes-app/KyPost-Server/blob/main/SECURITY.md).
This assumes good-faith coordination, and patches are publicly available before
any advisory is published.

## Contact

- **Vulnerability reports:** [GitHub Security Advisories](https://github.com/Busnes-app/KyPost-for-Linux/security/advisories/new)
- **Maintainer:** [Yoshiofthewire](https://github.com/Yoshiofthewire)
