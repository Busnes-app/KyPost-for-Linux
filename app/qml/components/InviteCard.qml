import QtQuick
import QtQuick.Layouts
import com.kysecurity.mail 1.0

// The calendar-invite card on the reader (plan item L17): the event read from
// the message's own calendar part by Invites.load(), and Accept/Maybe/Decline.
// Every value here is the sender's, so every Text is PlainText. A tap only
// asks Invites; the confirmation is RsvpConfirmDialog, which the host owns.
Card {
    id: root

    // Invites.inviteLoaded's card: {token, eyebrow, title, details, notices,
    // offersRsvp}, or {} for none.
    property var invite: ({})
    readonly property bool shown: root.invite.token !== undefined
    // {answerable, sending, message} for this card, from whichever reader
    // last answered the same invite.
    readonly property var rsvp: root.shown ? (Invites.states[String(root.invite.token)] || ({})) : ({})

    visible: root.shown
    implicitHeight: visible ? column.implicitHeight + 28 : 0
    elevated: false

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 6

        SectionLabel {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: root.invite.eyebrow || ""
        }
        Text {
            objectName: "inviteTitle"
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: root.invite.title || ""
            color: Theme.inkStrong
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontSubject
            font.bold: true
            wrapMode: Text.Wrap
        }
        Text {
            objectName: "inviteDetails"
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: (root.invite.details || []).join("\n")
            color: Theme.ink
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontSender
            wrapMode: Text.Wrap
        }

        Rectangle {
            Layout.fillWidth: true
            visible: (root.invite.notices || []).length > 0
            implicitHeight: visible ? noticeText.implicitHeight + 20 : 0
            radius: Theme.shapeButton
            color: Theme.warningActionFill
            border.width: 1
            border.color: Theme.warningActionBorder

            Text {
                id: noticeText
                objectName: "inviteNotices"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: 10
                textFormat: Text.PlainText
                text: (root.invite.notices || []).join("\n\n")
                color: Theme.warningActionText
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontPreview
                wrapMode: Text.Wrap
            }
        }

        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 4
            visible: root.invite.offersRsvp === true
            spacing: 8

            Repeater {
                model: [
                    { partstat: "ACCEPTED", label: i18n("Accept") },
                    { partstat: "TENTATIVE", label: i18n("Maybe") },
                    { partstat: "DECLINED", label: i18n("Decline") },
                ]

                delegate: GhostButton {
                    objectName: "rsvp_" + modelData.partstat
                    text: modelData.label
                    enabled: root.rsvp.answerable === true && !AppLock.locked
                    onClicked: Invites.requestRsvp(root.invite.token, modelData.partstat)
                }
            }
        }

        Text {
            objectName: "rsvpMessage"
            Layout.fillWidth: true
            visible: text !== ""
            textFormat: Text.PlainText
            text: root.rsvp.sending ? i18n("Sending RSVP…") : (root.rsvp.message || "")
            color: Theme.ink
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontPreview
            wrapMode: Text.Wrap
        }
    }
}
