#include "models/MfaChallenge.h"

#include <QRegularExpression>

namespace {

QString field(const QJsonObject& data, const char* key)
{
    return data.value(QLatin1String(key)).toString().trimmed();
}

bool isTwoDigits(const QString& value)
{
    static const QRegularExpression re(QStringLiteral("^[0-9]{2}$"));
    return re.match(value).hasMatch();
}

// Shown on the approval page. Control and format characters go, so a bidi
// override cannot reorder an IP address and a newline cannot fake a second line.
QString context(const QJsonObject& data, const char* key)
{
    QString out;
    for (const QChar c : field(data, key)) {
        const QChar::Category cat = c.category();
        if (cat != QChar::Other_Control && cat != QChar::Other_Format)
            out.append(c);
    }
    out.truncate(MfaChallenge::kMaxContextLength);
    if (!out.isEmpty() && out.at(out.size() - 1).isHighSurrogate())
        out.chop(1);
    return out;
}

} // namespace

bool MfaChallenge::isChallengeType(const QJsonObject& data)
{
    return data.value(QStringLiteral("type")).toString() == QLatin1String("mfa_challenge");
}

std::optional<MfaChallenge> MfaChallenge::fromPushData(const QJsonObject& data)
{
    if (!isChallengeType(data))
        return std::nullopt;

    static const QRegularExpression idRe(QStringLiteral("^[0-9a-f]{48}$"));
    static const QRegularExpression msRe(QStringLiteral("^[1-9][0-9]{0,15}$"));

    MfaChallenge c;
    c.challengeId = field(data, "challengeId");
    if (!idRe.match(c.challengeId).hasMatch())
        return std::nullopt;

    const QString issuedAt = field(data, "issuedAt");
    if (!msRe.match(issuedAt).hasMatch())
        return std::nullopt;
    c.issuedAtMs = issuedAt.toLongLong();

    c.matchDigits = field(data, "matchDigits");
    if (!isTwoDigits(c.matchDigits))
        return std::nullopt;

    for (const QString& part : field(data, "decoyDigits").split(QLatin1Char(',')))
        c.decoyDigits.append(part.trimmed());
    if (c.decoyDigits.size() != 2 || c.decoyDigits[0] == c.decoyDigits[1])
        return std::nullopt;
    for (const QString& decoy : c.decoyDigits) {
        if (!isTwoDigits(decoy) || decoy == c.matchDigits)
            return std::nullopt;
    }

    c.ipAddress = context(data, "ipAddress");
    c.userAgent = context(data, "userAgent");
    return c;
}
