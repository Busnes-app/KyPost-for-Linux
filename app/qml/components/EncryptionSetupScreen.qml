import QtQuick
import QtQuick.Layouts
import com.kysecurity.mail 1.0

// Shown over the mail section while the relay withholds mail until encryption
// setup is finished on the web (MailController::encryptionSetupRequired).
// Not an error and not a sign-out: the pairing and any open draft stay.
Rectangle {
    id: root

    // The relay's sentence; the default wording when it sent none.
    property string message: ""
    // <server>/setup/encryption; empty hides the button (non-https pairing).
    property url setupUrl: ""
    property bool checking: false

    signal openRequested()
    signal checkRequested()

    color: Theme.bg

    // Swallows clicks meant for the mail list underneath.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 420)
        spacing: 16

        Text {
            objectName: "encryptionSetupTitle"
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: i18n("Finish encryption setup")
            color: Theme.ink
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontSubject
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            objectName: "encryptionSetupBody"
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: root.message !== "" ? root.message
                : i18n("Finish email encryption setup in KyPost on the web, then come back.")
            color: Theme.ink
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontBody
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        PrimaryButton {
            objectName: "encryptionSetupOpen"
            Layout.alignment: Qt.AlignHCenter
            visible: root.setupUrl.toString() !== ""
            text: i18n("Open KyPost on the web")
            onClicked: root.openRequested()
        }

        GhostButton {
            objectName: "encryptionSetupCheck"
            Layout.alignment: Qt.AlignHCenter
            enabled: !root.checking
            text: root.checking ? i18n("Checking…") : i18n("Check again")
            onClicked: root.checkRequested()
        }
    }
}
