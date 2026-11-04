#pragma once

#include "pgp/PgpSendPlanner.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>

// The request body, as a value.
//
// Separated from send() so the exact bytes this client would put on the wire
// can be handed to the RELAY'S OWN decoder and validators without standing up
// a socket -- see scripts/verify-pgp-against-relay.sh. A wire format verified
// against a second opinion written here is worth much less than one verified
// against the code that will actually read it.
// materialGeneration: the enrolled keyring generation, which a converted
// account's relay requires (KyPost-Server requireCurrentGeneration).
QJsonObject pgpSendRequestBody(const QString& from, const PgpSendPlan& plan, const QStringList& to,
                                const QStringList& cc, const QStringList& bcc, const QString& mode,
                                const QDateTime& sendAt = {}, const QByteArray& draftCopy = {},
                                std::optional<qint64> materialGeneration = std::nullopt,
                                const QString& replyToMessageId = {}, const QString& replyToMailbox = {});
