#include "domain/ReplyAll.h"

#include <QSet>

QString addressFromHeader(const QString& raw)
{
    const QString value = raw.trimmed();
    QString candidate = value;
    const qsizetype close = value.lastIndexOf(QLatin1Char('>'));
    const qsizetype open = close < 0 ? -1 : value.lastIndexOf(QLatin1Char('<'), close);
    if (open >= 0 && close > open)
        candidate = value.mid(open + 1, close - open - 1).trimmed();
    return candidate.contains(QLatin1Char('@')) ? candidate : QString();
}

namespace {

// Splits on "," or ";" outside quotes and angle brackets: a display name such
// as "evil@attacker.tld, x" <bob@corp.com> is one recipient, not two.
QStringList splitAddressList(const QString& list)
{
    QStringList parts;
    QString current;
    bool quoted = false, angle = false, escaped = false;
    for (const QChar c : list) {
        if (escaped) {
            escaped = false;
        } else if (quoted && c == QLatin1Char('\\')) {
            escaped = true;
        } else if (c == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (!quoted && (c == QLatin1Char('<') || c == QLatin1Char('>'))) {
            angle = c == QLatin1Char('<');
        } else if (!quoted && !angle && (c == QLatin1Char(',') || c == QLatin1Char(';'))) {
            parts.append(current);
            current.clear();
            continue;
        }
        current.append(c);
    }
    parts.append(current);
    return parts;
}

} // namespace

QStringList replyAllRecipients(const QString& sender, const QString& to, const QString& cc,
                               const QStringList& own)
{
    const QStringList headers = QStringList{ sender } + splitAddressList(to) + splitAddressList(cc);

    QSet<QString> mine;
    for (const QString& address : own)
        mine.insert(addressFromHeader(address).toLower());

    QStringList all;
    QStringList others;
    QSet<QString> seen;
    for (const QString& header : headers) {
        const QString address = addressFromHeader(header);
        const QString key = address.toLower();
        if (address.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        all.append(address);
        if (!mine.contains(key))
            others.append(address);
    }
    return others.isEmpty() ? all : others;
}
