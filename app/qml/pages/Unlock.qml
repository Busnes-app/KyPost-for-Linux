import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.kysecurity.mail 1.0
import "../components"

// Full-window PIN gate shown whenever AppLock.locked is true.
//
// A plain Item, following the EmailDetail.qml/Compose.qml convention that a
// page does not assume how it is presented -- both roots overlay it at the
// top of their own stacking order rather than pushing it as a page, so
// nothing behind it is reachable while it is up.
Item {
    id: root

    // Emitted once the correct PIN has been accepted. Hosts don't strictly
    // need it (AppLock.locked drives visibility on its own), but it gives
    // them a hook for post-unlock work.
    signal unlocked()

    // Opaque by construction: this sits over the real UI, so any
    // transparency here would leak the mail it exists to hide.
    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    // Swallows every click and key that would otherwise reach the UI behind
    // this overlay.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
    }

    Timer {
        // Drives the lockout countdown. Only runs while a backoff is
        // actually in force, so an unlocked-but-idle app isn't waking up
        // once a second for nothing.
        id: countdownTimer
        interval: 1000
        repeat: true
        running: root.visible && AppLock.remainingLockoutSeconds > 0
        onTriggered: root.secondsLeft = AppLock.remainingLockoutSeconds
    }

    property int secondsLeft: AppLock.remainingLockoutSeconds
    readonly property bool lockedOut: secondsLeft > 0

    onVisibleChanged: {
        if (visible) {
            root.secondsLeft = AppLock.remainingLockoutSeconds
            pinField.text = ""
            root.errorText = ""
            pinField.inputField.forceActiveFocus()
        }
    }

    property string errorText: ""

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(320, root.width - 48)
        spacing: 16

        MaterialIcon {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            width: 40
            height: 40
            name: "lock"
            tint: Theme.accent
        }

        Text {
            Layout.fillWidth: true
            text: i18n("KyPost is locked")
            color: Theme.inkStrong
            font.family: Theme.fontUi
            font.pixelSize: Math.round(20 * Theme.fontScale)
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            Layout.fillWidth: true
            visible: !AppLock.storeUnavailable
            text: i18n("Enter your PIN to continue.")
            color: Theme.ink
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontPreview
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        // The lock fails closed when the secret store cannot be read, which
        // is the right answer and a baffling one on its own: the stored PIN
        // hash lives in that same store, so verifyPin() cannot succeed
        // either and the correct PIN is refused like every other. Without
        // this the screen silently implies the user has forgotten it.
        Text {
            Layout.fillWidth: true
            visible: AppLock.storeUnavailable
            text: i18n("KyPost cannot reach your system keyring, so it cannot check your PIN. "
                       + "Start a keyring service (GNOME Keyring or KWallet), unlock it, then "
                       + "restart KyPost.")
            color: Theme.dangerActionText
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontPreview
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        ThemedTextField {
            id: pinField
            objectName: "pinField"
            Layout.fillWidth: true
            enabled: !root.lockedOut && !AppLock.storeUnavailable
            placeholderText: i18n("PIN")

            // echoMode/inputMethodHints live on the inner TextField --
            // ThemedTextField is a Rectangle wrapper and exposes only
            // text/placeholderText/inputField.
            Component.onCompleted: {
                inputField.echoMode = TextInput.Password
                inputField.inputMethodHints = Qt.ImhDigitsOnly
                // Android's unlock field: large centred digits.
                inputField.horizontalAlignment = TextInput.AlignHCenter
                inputField.font.pixelSize = Math.round(28 * Theme.fontScale)
                // Keys typed after the lock land here, never in whatever had
                // focus underneath.
                inputField.forceActiveFocus()
            }
        }

        Connections {
            target: pinField.inputField
            function onAccepted() { root.submit() }
        }

        Text {
            Layout.fillWidth: true
            visible: root.errorText !== ""
            textFormat: Text.PlainText
            text: root.errorText
            color: Theme.dangerActionText
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontDate
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: root.lockedOut
            textFormat: Text.PlainText
            text: i18n("Too many attempts. Try again in %1 seconds.", root.secondsLeft)
            color: Theme.ink
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontDate
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            // Warn before the wipe rather than after. The threshold is the
            // policy's, not a number duplicated here.
            visible: !root.lockedOut && AppLock.failedAttempts >= 5
            text: i18n("Warning: after 10 failed attempts, all local data is erased.")
            color: Theme.dangerActionText
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontDate
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        PrimaryButton {
            Layout.fillWidth: true
            text: i18n("Unlock")
            enabled: !root.lockedOut && pinField.text.length > 0
            onClicked: root.submit()
        }
    }

    function submit() {
        if (root.lockedOut)
            return
        if (AppLock.tryUnlock(pinField.text)) {
            pinField.text = ""
            root.errorText = ""
            root.unlocked()
            return
        }
        pinField.text = ""
        root.secondsLeft = AppLock.remainingLockoutSeconds
        root.errorText = root.lockedOut ? "" : i18n("Incorrect PIN.")
        pinField.inputField.forceActiveFocus()
    }
}
