#pragma once

#include <QString>

#include "net/NetworkError.h"
#include "pgp/OpenPgpKeyImporter.h"

#include <QByteArray>
#include <QUrl>
#include <optional>

class HttpClient;
struct RelayAuth;

enum class DeviceEnrollmentCallStatus { Ok, NotFound, Unauthorized, RateLimited, Failed };

struct DeviceEnrollmentCallResult
{
    DeviceEnrollmentCallStatus status = DeviceEnrollmentCallStatus::Failed;
    QByteArray envelope;
    // The version the server recorded at delivery; the device opens exactly this.
    int version = 0;
    // Converted accounts only: the delivered generation, the account's active
    // fingerprint and its current inventory. Absent when the delivery is stale.
    std::optional<PgpKeyringSnapshot> keyring;
    QString detail;
};

class PgpDeviceEnrollmentClient
{
public:
    explicit PgpDeviceEnrollmentClient(HttpClient& httpClient);
    // Advertises RFC 9180 envelope versions [4,5] only with the key; the server stores both atomically.
    DeviceEnrollmentCallResult publishKey(const QUrl& serverBaseUrl, const RelayAuth& auth,
                                           const QByteArray& publicKeyBase64) const;
    DeviceEnrollmentCallResult fetchEnvelope(const QUrl& serverBaseUrl, const RelayAuth& auth) const;
    DeviceEnrollmentCallResult reportState(const QUrl& serverBaseUrl, const RelayAuth& auth,
                                            bool enrolled) const;
    // A converted account's acknowledgement: exactly what was delivered and imported.
    DeviceEnrollmentCallResult acknowledgeKeyring(const QUrl& serverBaseUrl, const RelayAuth& auth,
                                                   int version, const PgpKeyringSnapshot& held) const;

private:
    HttpClient& m_httpClient;
};
