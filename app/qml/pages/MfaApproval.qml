import QtQuick
import QtQuick.Layouts
import com.kysecurity.mail 1.0
import "../components"

// Sign-in approval, shown over the window while Mfa.open. Both roots host it
// below LockOverlay (z 1000), so a locked app shows the PIN screen first.
// MfaController refuses to answer while locked regardless.
//
// The three numbers are styled identically and arrive pre-shuffled; nothing
// here may single out the right one.
Item {
    id: root

    readonly property bool canAnswer: Mfa.hasChallenge && !Mfa.inFlight && !AppLock.locked

    // The challenge on screen when the current press began. A tap answers
    // that challenge or nothing: if it was replaced or expired while the
    // finger was down, the press is void, even when the replacement shows
    // the same numbers.
    property string gestureChallengeId: ""

    function beginGesture() {
        root.gestureChallengeId = Mfa.hasChallenge && Mfa.secondsRemaining > 0 ? Mfa.challengeId : ""
    }

    function stillShown(id) {
        return id !== "" && Mfa.hasChallenge && Mfa.challengeId === id && Mfa.secondsRemaining > 0
    }

    // Returns the challenge the finished gesture may answer, or "".
    function endGesture() {
        const id = root.gestureChallengeId
        root.gestureChallengeId = ""
        return root.stillShown(id) ? id : ""
    }

    // Cancel as soon as the challenge changes, so a later return to the same
    // ID cannot revive a press that was voided.
    Connections {
        target: Mfa
        function onChanged() {
            if (root.gestureChallengeId !== "" && !root.stillShown(root.gestureChallengeId))
                root.gestureChallengeId = ""
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(400, root.width - 48)
        spacing: 14

        Text {
            Layout.fillWidth: true
            text: i18n("Approve sign-in")
            color: Theme.inkStrong
            font.family: Theme.fontUi
            font.pixelSize: 20
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            Layout.fillWidth: true
            visible: Mfa.hasChallenge
            text: i18n("Someone is signing in to your account. If this was not you, deny it.")
            color: Theme.ink
            font.family: Theme.fontUi
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: Mfa.pendingCount > 1
            textFormat: Text.PlainText
            text: i18n("%1 sign-in requests are waiting. If you did not start them, deny each one and change your password.",
                       Mfa.pendingCount)
            color: Theme.dangerColor
            font.family: Theme.fontUi
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        GridLayout {
            Layout.fillWidth: true
            visible: Mfa.hasChallenge
            columns: 2
            columnSpacing: 12
            rowSpacing: 6

            Text {
                text: i18n("When")
                color: Theme.ink
                font.family: Theme.fontUi
                font.pixelSize: 13
            }
            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: Mfa.issuedAt.toLocaleString(Qt.locale(), Locale.ShortFormat)
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: 13
                wrapMode: Text.WrapAnywhere
            }

            Text {
                visible: Mfa.ipAddress !== ""
                text: i18n("IP address")
                color: Theme.ink
                font.family: Theme.fontUi
                font.pixelSize: 13
            }
            Text {
                objectName: "mfaIpAddress"
                Layout.fillWidth: true
                visible: Mfa.ipAddress !== ""
                textFormat: Text.PlainText
                text: Mfa.ipAddress
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: 13
                wrapMode: Text.WrapAnywhere
            }

            Text {
                visible: Mfa.userAgent !== ""
                text: i18n("Browser")
                color: Theme.ink
                font.family: Theme.fontUi
                font.pixelSize: 13
            }
            Text {
                objectName: "mfaUserAgent"
                Layout.fillWidth: true
                visible: Mfa.userAgent !== ""
                textFormat: Text.PlainText
                text: Mfa.userAgent
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: 13
                wrapMode: Text.WrapAnywhere
            }

            Text {
                text: i18n("Expires in")
                color: Theme.ink
                font.family: Theme.fontUi
                font.pixelSize: 13
            }
            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: Math.floor(Mfa.secondsRemaining / 60) + ":" + ("0" + (Mfa.secondsRemaining % 60)).slice(-2)
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: 13
            }
        }

        MutedHint {
            Layout.fillWidth: true
            visible: Mfa.hasChallenge && Mfa.ipAddress === "" && Mfa.userAgent === ""
            text: i18n("Your server did not say where this sign-in comes from. It sends that only when notification previews are on.")
        }

        Text {
            Layout.fillWidth: true
            visible: Mfa.hasChallenge
            text: i18n("Tap the number shown on the screen you are signing in from. One wrong tap locks push approval for this sign-in.")
            color: Theme.inkStrong
            font.family: Theme.fontUi
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            visible: Mfa.hasChallenge
            spacing: 12

            Repeater {
                model: Mfa.choices
                delegate: GhostButton {
                    required property string modelData
                    objectName: "mfaChoice"
                    implicitWidth: 72
                    text: modelData
                    enabled: root.canAnswer
                    onPressStarted: root.beginGesture()
                    onClicked: {
                        const id = root.endGesture()
                        if (id !== "")
                            Mfa.approve(id, modelData)
                    }
                }
            }
        }

        DangerButton {
            objectName: "mfaDeny"
            Layout.fillWidth: true
            visible: Mfa.hasChallenge
            text: i18n("Deny, this was not me")
            enabled: root.canAnswer
            onPressStarted: root.beginGesture()
            onClicked: {
                const id = root.endGesture()
                if (id !== "")
                    Mfa.deny(id)
            }
        }

        Text {
            objectName: "mfaResult"
            Layout.fillWidth: true
            visible: text !== ""
            textFormat: Text.PlainText
            text: Mfa.resultMessage !== "" ? Mfa.resultMessage
                : (Mfa.hasChallenge ? "" : i18n("No sign-in request is waiting. It expired or was already answered."))
            color: Mfa.resultIsError ? Theme.dangerColor : Theme.inkStrong
            font.family: Theme.fontUi
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: Mfa.inFlight
            text: i18n("Sending…")
            color: Theme.ink
            font.family: Theme.fontUi
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
        }

        GhostButton {
            objectName: "mfaClose"
            Layout.fillWidth: true
            text: i18n("Close")
            onClicked: Mfa.close()
        }
    }
}
