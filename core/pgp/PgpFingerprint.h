#pragma once

#include <QByteArray>
#include <QString>

// Full OpenPGP primary fingerprints only; never a GnuPG key pattern.
inline QString normalizedFingerprint(const QByteArray& bytes)
{
    if (bytes.size() != 40 && bytes.size() != 64)
        return {};
    for (char c : bytes) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return {};
    }
    return QString::fromLatin1(bytes).toUpper();
}
