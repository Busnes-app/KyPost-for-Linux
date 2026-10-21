import QtQuick

QtObject {
    property var checkedAt: new Date(2026, 9, 20, 9, 0)
    property string installedVersion: "0.4.5"
    property string latestVersion: "0.4.5"
    property string releaseUrl: ""
    property bool updateAvailable: false
    signal updateBecameAvailable()
}
