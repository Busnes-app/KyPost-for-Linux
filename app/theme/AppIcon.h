#pragma once

#include <QIcon>

// The KyPost envelope from the binary (app/resources/appicon.qrc, which
// points at packaging/flatpak/icons/hicolor), never from the icon theme: an
// older icon installed under com.kysecurity.mail by a stale Flatpak won the
// theme lookup and put the wrong logo in the title bar and taskbar.
inline QIcon bundledAppIcon()
{
    QIcon icon;
    for (int size : {16, 22, 24, 32, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/appicon/%1.png").arg(size), QSize(size, size));
    return icon;
}
