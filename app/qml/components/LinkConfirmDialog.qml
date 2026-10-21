import QtQuick
import QtQuick.Layouts
import com.kysecurity.mail 1.0

// Asks before a link in a message leaves the app. Mail links lie about where
// they go, so this names the host the browser will really load -- parsed in
// C++ (MailApp.linkTarget, domain/LinkTarget.h), punycode for IDNs -- and
// opens exactly the URL string it showed.
//
// Same overlay-Item shape as CertificateChangeDialog.qml, not a Popup, so the
// app-lock overlay covers it (AGENTS.md 6d).
Item {
    id: root

    property bool isOpen: false
    // MailApp.linkTarget()'s map: host, shownUrl, mismatchHost.
    property var target: ({})
    property bool phishing: false

    signal openRequested(string url)

    function show(linkTarget, flaggedPhishing) {
        root.target = linkTarget
        root.phishing = flaggedPhishing
        root.isOpen = true
        panel.forceActiveFocus()
    }

    function close() {
        root.isOpen = false
        root.target = ({})
    }

    function cancel() {
        root.close()
    }

    function confirm() {
        const url = root.target.shownUrl
        root.close()
        root.openRequested(url)
    }

    Connections {
        target: AppLock
        function onLockedChanged() {
            if (AppLock.locked)
                root.close()
        }
    }

    visible: root.isOpen
    anchors.fill: parent
    z: 960

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.4)

        TapHandler {
            onTapped: root.cancel()
        }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 32 : 460, 460)
        implicitHeight: content.implicitHeight + 32
        radius: Theme.shapeSheet
        color: Theme.panel
        border.width: 1
        border.color: Theme.line

        // Escape cancels; no button takes focus, so no keystroke opens.
        Keys.onEscapePressed: function (event) {
            root.cancel()
            event.accepted = true
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: function (wheel) { wheel.accepted = true }
        }

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            Text {
                text: i18n("Open this link?")
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: Math.round(16 * Theme.fontScale)
                font.weight: Font.Medium
                textFormat: Text.PlainText
            }

            SectionLabel { text: i18n("It goes to") }
            Text {
                objectName: "linkHost"
                Layout.fillWidth: true
                text: root.target.host || ""
                color: Theme.inkStrong
                font.family: Theme.fontMono
                font.pixelSize: Math.round(15 * Theme.fontScale)
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
            }

            Text {
                objectName: "linkMismatch"
                Layout.fillWidth: true
                visible: !!root.target.mismatchHost
                text: i18n("The link text names a different address. Check the host above, not the text.")
                color: Theme.dangerActionText
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontPreview
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
            }

            Text {
                objectName: "linkPhishing"
                Layout.fillWidth: true
                visible: root.phishing
                text: i18n("This message was flagged as possible phishing. Check the address above, not the link text.")
                color: Theme.dangerActionText
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontPreview
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
            }

            SectionLabel { text: i18n("Full address") }
            Text {
                objectName: "linkUrl"
                Layout.fillWidth: true
                // Capped at 300 like Android; the host line is the check.
                text: (root.target.shownUrl || "").slice(0, 300)
                color: Theme.ink
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontEyebrow
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                GhostButton {
                    text: i18n("Cancel")
                    onClicked: root.cancel()
                }
                PrimaryButton {
                    text: i18n("Open")
                    onClicked: root.confirm()
                }
            }
        }
    }
}
