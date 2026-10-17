#pragma once

#include <QString>
#include <QStringList>

// The address in one header value: the LAST angle-addr (RFC 5322), else the
// bare value. "" when it has no "@". Same rule as format.js addressFromHeader.
QString addressFromHeader(const QString& raw);

// Reply All's recipients: the sender, then To and Cc (wire strings joined by
// "," or ";" outside quotes and angle brackets), as bare addresses, deduplicated case-insensitively. The
// account's own addresses are dropped, unless nobody else is left -- a note to
// self still answers somebody. Same rule as Android's replyAllRecipients.
QStringList replyAllRecipients(const QString& sender, const QString& to, const QString& cc,
                               const QStringList& own);
