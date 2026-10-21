import QtQuick
import KyPostScreens

QtObject {
    property bool isDesktopMode: ScreenRun.desktop
    property string preferredMode: "auto"
    property bool trayIconEnabled: true
    property bool minimizeToTrayOnClose: false
    function setPreferredMode(m) { preferredMode = m }
    function setTrayIconEnabled(v) { trayIconEnabled = v }
    function setMinimizeToTrayOnClose(v) { minimizeToTrayOnClose = v }
}
