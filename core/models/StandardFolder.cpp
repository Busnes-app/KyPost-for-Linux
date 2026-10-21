#include "models/StandardFolder.h"

#include <QStringList>

QString standardFolderWireName(StandardFolder folder)
{
    switch (folder) {
    case StandardFolder::Inbox:
        return QStringLiteral("INBOX");
    case StandardFolder::Drafts:
        return QStringLiteral("Drafts");
    case StandardFolder::Junk:
        return QStringLiteral("Junk");
    case StandardFolder::Sent:
        return QStringLiteral("Sent");
    case StandardFolder::Trash:
        return QStringLiteral("Trash");
    case StandardFolder::Archive:
        return QStringLiteral("Archive");
    }
    return QString();
}

QString standardFolderPath(StandardFolder folder, const QVector<MailFolder>& topLevel)
{
    // RFC 6154 has no role for INBOX, and the relay's roles are the wire
    // names lowercased.
    const QString role = standardFolderWireName(folder).toLower();
    QString path;
    for (const MailFolder& candidate : topLevel) {
        if (folder != StandardFolder::Inbox && candidate.special == role
            && (path.isEmpty() || candidate.path < path))
            path = candidate.path;
    }
    return path.isEmpty() ? standardFolderWireName(folder) : path;
}

QString standardFolderDisplayName(const QString& fullPath)
{
    // IMAP's INBOX is case-insensitive and spelled in capitals on the wire;
    // users see "Inbox", like every other folder name in sentence case.
    if (fullPath.compare(QLatin1String("INBOX"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Inbox");
    const QString normalized = QString(fullPath).replace(QLatin1Char('.'), QLatin1Char('/'));
    const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.isEmpty() ? fullPath : parts.last();
}
