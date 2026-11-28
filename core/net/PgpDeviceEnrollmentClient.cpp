#include "net/PgpDeviceEnrollmentClient.h"

#include "net/ContactSyncClient.h"
#include "net/HttpClient.h"
#include "net/RelayAuth.h"

#include "pgp/PgpFingerprint.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cmath>

namespace {
DeviceEnrollmentCallResult statusOf(const HttpClient::HttpResult& result)
{
    DeviceEnrollmentCallResult out;
    if (!result.error.has_value()) {
        out.status = DeviceEnrollmentCallStatus::Ok;
        return out;
    }
    switch (result.statusCode) {
    case 401: out.status = DeviceEnrollmentCallStatus::Unauthorized; break;
    case 404: out.status = DeviceEnrollmentCallStatus::NotFound; break;
    case 429: out.status = DeviceEnrollmentCallStatus::RateLimited; break;
    default: out.status = DeviceEnrollmentCallStatus::Failed; break;
    }
    out.detail = result.detail;
    return out;
}

qint64 generationOf(const QJsonValue& value)
{
    const double number = value.toDouble(-1);
    return value.isDouble() && number >= 0 && number <= 9007199254740991.0 && std::floor(number) == number
        ? static_cast<qint64>(number) : -1;
}

// The snapshot importPrivateKeyring checks the decrypted ring against. Only
// when the delivery was prepared from the account's current material.
std::optional<PgpKeyringSnapshot> keyringOf(const QJsonObject& response)
{
    const QJsonObject ring = response.value(QStringLiteral("keyring")).toObject();
    PgpKeyringSnapshot out;
    out.activeFingerprint = normalizedFingerprint(response.value(QStringLiteral("fingerprint")).toString().toUtf8());
    const auto activeValue = ring.value(QStringLiteral("active"));
    if (!activeValue.isUndefined()) {
        if (!activeValue.isObject() || activeValue.toObject().isEmpty()) return std::nullopt;
        const auto active = activeValue.toObject();
        for (auto it = active.begin(); it != active.end(); ++it) {
            const auto fingerprint = normalizedFingerprint(it.value().toString().toUtf8());
            if (it.key() == QStringLiteral("v4") && fingerprint.size() == 40) out.activeV4 = fingerprint;
            else if (it.key() == QStringLiteral("v6") && fingerprint.size() == 64) out.activeV6 = fingerprint;
            else return std::nullopt;
        }
    }
    out.materialGeneration = generationOf(response.value(QStringLiteral("materialGeneration")));
    if (ring.isEmpty() || out.activeFingerprint.isEmpty() || out.materialGeneration < 1
        || generationOf(ring.value(QStringLiteral("materialGeneration"))) != out.materialGeneration)
        return std::nullopt;
    const auto inventory = ring.value(QStringLiteral("keyFingerprints"));
    if (!inventory.isArray() || inventory.toArray().isEmpty() || inventory.toArray().size() > 256) return std::nullopt;
    for (const QJsonValue& value : inventory.toArray())
        out.keyFingerprints.append(value.toString());
    return out;
}
// Sent on every key and state write: the server reads an omitted list as ["v4"].
QJsonArray openPgpCapabilities()
{
    return QJsonArray{QStringLiteral("v4"), QStringLiteral("v6"), QStringLiteral("rfc9980")};
}
}

PgpDeviceEnrollmentClient::PgpDeviceEnrollmentClient(HttpClient& httpClient) : m_httpClient(httpClient) {}

DeviceEnrollmentCallResult PgpDeviceEnrollmentClient::publishKey(
    const QUrl& serverBaseUrl, const RelayAuth& auth, const QByteArray& publicKeyBase64) const
{
    return statusOf(m_httpClient.post(
        joinUrlPath(serverBaseUrl, QStringLiteral("api/pgp/device/enrollment-key")), {},
        QJsonObject{{QStringLiteral("publicKey"), QString::fromLatin1(publicKeyBase64)},
                    {QStringLiteral("envelopeVersions"), publicKeyBase64.size() == 1624 ? QJsonArray{6} : QJsonArray{4, 5}},
                    {QStringLiteral("openpgp"), openPgpCapabilities()}},
        auth.headerItems()));
}

DeviceEnrollmentCallResult PgpDeviceEnrollmentClient::fetchEnvelope(
    const QUrl& serverBaseUrl, const RelayAuth& auth) const
{
    const HttpClient::HttpResult result = m_httpClient.get(
        joinUrlPath(serverBaseUrl, QStringLiteral("api/pgp/device/envelope")), {}, auth.headerItems());
    DeviceEnrollmentCallResult out = statusOf(result);
    if (out.status != DeviceEnrollmentCallStatus::Ok)
        return out;
    QString decodeError;
    const std::optional<QJsonObject> decoded = decodeJsonObject(result.body, &decodeError);
    if (!decoded.has_value()) {
        out.status = DeviceEnrollmentCallStatus::Failed;
        out.detail = decodeError;
        return out;
    }
    out.envelope = decoded->value(QStringLiteral("envelope")).toString().toUtf8();
    out.version = decoded->value(QStringLiteral("version")).toInt();
    out.keyring = keyringOf(*decoded);
    if (out.version == 6 && generationOf(decoded->value(QStringLiteral("materialGeneration"))) > 0
        && !out.keyring.has_value()) {
        out.status = DeviceEnrollmentCallStatus::Failed;
        out.detail = QStringLiteral("invalid converted device delivery snapshot");
        return out;
    }
    if (out.envelope.isEmpty()) {
        out.status = DeviceEnrollmentCallStatus::Failed;
        out.detail = QStringLiteral("empty device envelope");
    }
    return out;
}

DeviceEnrollmentCallResult PgpDeviceEnrollmentClient::reportState(
    const QUrl& serverBaseUrl, const RelayAuth& auth, bool enrolled) const
{
    return statusOf(m_httpClient.post(
        joinUrlPath(serverBaseUrl, QStringLiteral("api/pgp/device/enrollment-state")), {},
        QJsonObject{{QStringLiteral("encryptionEnrolled"), enrolled}, {QStringLiteral("openpgp"), openPgpCapabilities()}},
        auth.headerItems()));
}

DeviceEnrollmentCallResult PgpDeviceEnrollmentClient::acknowledgeKeyring(
    const QUrl& serverBaseUrl, const RelayAuth& auth, int version, const PgpKeyringSnapshot& held) const
{
    return statusOf(m_httpClient.post(
        joinUrlPath(serverBaseUrl, QStringLiteral("api/pgp/device/enrollment-state")), {},
        QJsonObject{{QStringLiteral("encryptionEnrolled"), true},
                    {QStringLiteral("envelopeVersion"), version},
                    {QStringLiteral("materialGeneration"), held.materialGeneration},
                    {QStringLiteral("fingerprint"), held.activeFingerprint}, {QStringLiteral("openpgp"), openPgpCapabilities()}},
        auth.headerItems()));
}
