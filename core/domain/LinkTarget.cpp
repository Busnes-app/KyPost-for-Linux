#include "domain/LinkTarget.h"

#include <QRegularExpression>

std::optional<LinkTarget> linkTargetOf(const QUrl& raw)
{
    if (!raw.isValid() || (raw.scheme() != QLatin1String("http") && raw.scheme() != QLatin1String("https")))
        return std::nullopt;
    // Browsers end the host at a backslash; QUrl reads it as userinfo.
    if ((raw.userName(QUrl::FullyDecoded) + raw.password(QUrl::FullyDecoded)).contains(QLatin1Char('\\')))
        return std::nullopt;
    const QUrl url = raw.adjusted(QUrl::RemoveUserInfo);
    QString host = url.host(QUrl::FullyEncoded);
    if (host.isEmpty())
        return std::nullopt;
    if (host.contains(QLatin1Char(':')))
        host = QLatin1Char('[') + host + QLatin1Char(']');
    return LinkTarget{host, url};
}

static QString siteOf(QString host)
{
    if (host.endsWith(QLatin1Char('.')))
        host.chop(1);
    if (host.startsWith(QLatin1String("www.")))
        host.remove(0, 4);
    return host;
}

QString linkTextMismatch(const QString& text, const QUrl& href)
{
    const auto real = linkTargetOf(href);
    if (!real)
        return {};
    // Text that reads as an address: an explicit http(s) URL, or a bare
    // dotted name ending in a letters-only label. Words, sentences and e-mail
    // addresses are not.
    static const QRegularExpression withScheme(QStringLiteral("^https?://"),
                                               QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bareName(
        QStringLiteral(R"(^([\p{L}\p{M}\p{N}-]+\.)+[\p{L}\p{M}]{2,}\.?(:\d+)?([/?#]\S*)?$)"));
    const QString shown = text.trimmed();
    QUrl claimedUrl;
    if (withScheme.match(shown).hasMatch())
        claimedUrl = QUrl(shown);
    else if (bareName.match(shown).hasMatch())
        claimedUrl = QUrl(QStringLiteral("https://") + shown);
    else
        return {};
    // Address-shaped text that does not parse names no site we can match, so
    // it counts as naming another one.
    const auto claimed = linkTargetOf(claimedUrl);
    const QString realSite = siteOf(real->host);
    if (claimed) {
        const QString claimedSite = siteOf(claimed->host);
        if (realSite == claimedSite || realSite.endsWith(QLatin1Char('.') + claimedSite))
            return {};
    }
    return real->host;
}

QString linkTextMismatch(const QVariantList& anchors, const QUrl& href)
{
    for (const QVariant& anchor : anchors) {
        const QVariantList pair = anchor.toList();
        if (pair.size() != 2 || QUrl(pair.at(0).toString()) != href)
            continue;
        const QString real = linkTextMismatch(pair.at(1).toString(), href);
        if (!real.isEmpty())
            return real;
    }
    return {};
}
