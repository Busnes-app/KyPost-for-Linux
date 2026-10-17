#pragma once

#include "models/MailFolder.h"

#include <QString>
#include <QVector>

enum class StandardFolder
{
    Inbox,
    Drafts,
    Junk,
    Sent,
    Trash,
    Archive,
};

QString standardFolderWireName(StandardFolder folder);

// The account's path for `folder`: the top-level listing's folder whose
// `special` names its role, else the wire name. Two claimants resolve to the
// lowest path, so the answer does not depend on listing order.
QString standardFolderPath(StandardFolder folder, const QVector<MailFolder>& topLevel);

// Splits any folder path (not just the 6 standard folders) on both `/` and
// `.` hierarchy delimiters and returns the last segment.
QString standardFolderDisplayName(const QString& fullPath);
