#pragma once

#include "models/MfaChallenge.h"

#include <QString>
#include <QStringList>
#include <optional>

// Fields from the backend's verified `buildNativePushData` envelope
// (backend/internal/processor/poller.go): the push `data` object carries
// `messageId`, `sender`, `subject`, `senderName`, `emailSubject`, a
// comma-joined `Keywords` string (capital K on the wire, parsed here into
// `keywords`), `title`, `body`, `url`.
struct PushNotification
{
    QString messageId;
    QString sender;
    QString subject;
    QString senderName;
    QString emailSubject;
    QStringList keywords;
    QString title;
    QString body;
    QString url;
    // Set for a data.type "mfa_challenge" push, which is routed to the
    // approval page and never shown or stored as mail.
    std::optional<MfaChallenge> mfaChallenge;

    bool operator==(const PushNotification&) const = default;
};
