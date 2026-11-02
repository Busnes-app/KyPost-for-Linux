#pragma once

#include "net/NativeRegistrationClient.h"

#include <QDateTime>
#include <QString>
#include <QUrl>

// The device side of new-device approval (kypost-server docs/device-approval.md,
// #516). A web-minted pairing token registers a PENDING device; the app polls
// pairing-status until the user approves or denies it on the web page that
// showed the code. These are the pure parts of that wait, so they are tested
// without a timer; PairingController owns the timer and the screen.
namespace DeviceApproval {

// The server advertises 3 s. A hostile or broken value is clamped, never trusted.
constexpr int kDefaultPollSeconds = 3;
constexpr int kMinPollSeconds = 1;
constexpr int kMaxPollSeconds = 30;
// The server keeps a pending device for 5 minutes. A deadline further out than
// this is clamped, so a server cannot keep the wait (and the registration's
// pin state, which PairAttempt holds until it ends) open indefinitely.
constexpr int kDefaultWaitSeconds = 5 * 60;
constexpr int kMaxWaitSeconds = 6 * 60;

// Milliseconds until the next poll. `failures` counts consecutive answers that
// were neither pending nor final (429, 503, transport errors): each doubles the
// interval up to kMaxPollSeconds. A Retry-After is honoured when it is longer,
// up to the same cap.
int nextPollDelayMs(int advertisedPollSeconds, int failures, int retryAfterSeconds);

// When to give up: the advertised expiresAt, clamped to (now, now + kMaxWaitSeconds];
// now + kDefaultWaitSeconds when absent or already past.
QDateTime deadline(const QDateTime& advertisedExpiresAt, const QDateTime& now);

// The advertised statusEndpoint only when it is same-origin with the server
// the user confirmed, else srv + /api/notifications/native/pairing-status --
// the rule the contract gives, and the one finishPair() applies to pullEndpoint.
QUrl statusEndpoint(const QString& advertised, const QUrl& serverBaseUrl);

// The Success a pending registration becomes once pairing-status answers 200:
// the secret from the 202, the deviceId and delivery fields from the 200 (the
// server may have merged the device into an existing row under another id),
// and the TLS evidence of the reply that approved it.
NativeRegistrationResult approvedRegistration(const NativeRegistrationResult& pending,
                                              const PairingStatusResult& approved);

} // namespace DeviceApproval
