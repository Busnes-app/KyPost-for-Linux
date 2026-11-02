#include "domain/DeviceApproval.h"

#include "net/HttpClient.h"

#include <algorithm>

namespace DeviceApproval {

int nextPollDelayMs(int advertisedPollSeconds, int failures, int retryAfterSeconds)
{
    int seconds = advertisedPollSeconds > 0 ? advertisedPollSeconds : kDefaultPollSeconds;
    seconds = std::clamp(seconds, kMinPollSeconds, kMaxPollSeconds);
    for (int i = 0; i < failures && seconds < kMaxPollSeconds; ++i)
        seconds = std::min(seconds * 2, kMaxPollSeconds);
    seconds = std::max(seconds, std::min(retryAfterSeconds, kMaxPollSeconds));
    return seconds * 1000;
}

QDateTime deadline(const QDateTime& advertisedExpiresAt, const QDateTime& now)
{
    if (!advertisedExpiresAt.isValid() || advertisedExpiresAt <= now)
        return now.addSecs(kDefaultWaitSeconds);
    return std::min(advertisedExpiresAt, now.addSecs(kMaxWaitSeconds));
}

QUrl statusEndpoint(const QString& advertised, const QUrl& serverBaseUrl)
{
    const QUrl candidate(advertised);
    if (!advertised.isEmpty() && candidate.isValid() && sameUrlOrigin(candidate, serverBaseUrl))
        return candidate;
    QUrl derived;
    derived.setScheme(serverBaseUrl.scheme());
    derived.setHost(serverBaseUrl.host());
    if (serverBaseUrl.port() != -1)
        derived.setPort(serverBaseUrl.port());
    derived.setPath(QStringLiteral("/api/notifications/native/pairing-status"));
    return derived;
}

NativeRegistrationResult approvedRegistration(const NativeRegistrationResult& pending,
                                              const PairingStatusResult& approved)
{
    NativeRegistrationResult out;
    out.outcome = RegistrationOutcome::Success;
    out.response = approved.response;
    out.response.ok = true;
    out.response.deviceSecret = pending.response.deviceSecret;
    // Without fresh handshake evidence on the poll (a pooled connection), the
    // key that served the registration is the one to pin.
    out.peerSpkiSha256 = !approved.peerSpkiSha256.isEmpty() ? approved.peerSpkiSha256 : pending.peerSpkiSha256;
    out.certificatePinVerified = approved.certificatePinVerified;
    return out;
}

} // namespace DeviceApproval
