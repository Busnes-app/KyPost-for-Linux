import QtQuick
import QtQuick.Layouts
import com.kysecurity.mail 1.0

// The calendar-invite card on the reader (plan item L17): the event read from
// the message's own calendar part by Invites.load(), and Accept/Maybe/Decline,
// answered through KyCalendar first. Every value here is the sender's, so
// every Text is PlainText. A tap only asks Invites; the mail fallback's
// confirmation is RsvpConfirmDialog, which the host owns.
Card {
    id: root

    // Invites.inviteLoaded's card: {token, eyebrow, title, details, notices,
    // note, cancelled, joinUrl, joinHost, offersRsvp}, or {} for none.
    property var invite: ({})
    readonly property bool shown: root.invite.token !== undefined
    // {answerable, sending, message, reload} for this card, from whichever
    // reader last answered the same invite.
    readonly property var rsvp: root.shown ? (Invites.states[String(root.invite.token)] || ({})) : ({})

    // KyCalendar said the event changed: the host reads the invite again.
    signal reloadRequested()

    visible: root.shown
    implicitHeight: visible ? column.implicitHeight + 28 : 0

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
            objectName: "inviteCancelled"
            visible: root.invite.cancelled === true
            textFormat: Text.PlainText
            text: i18n("Cancelled")
            color: Theme.danger
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontSender
            font.bold: true
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
            color: Theme.warningSoft
            border.width: 1
            border.color: Theme.warningBorder

            Text {
                id: noticeText
                objectName: "inviteNotices"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: 10
                textFormat: Text.PlainText
                text: (root.invite.notices || []).join("\n\n")
                color: Theme.warning
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontPreview
                wrapMode: Text.Wrap
            }
        }

        // https only, checked in C++; the host is named so the user sees
        // where the sender is sending them.
        PrimaryButton {
            objectName: "inviteJoin"
            Layout.topMargin: 4
            visible: (root.invite.joinUrl || "") !== ""
            text: i18n("Join meeting (%1)", root.invite.joinHost || "")
            enabled: !AppLock.locked
            onClicked: Qt.openUrlExternally(root.invite.joinUrl)
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
            text: root.rsvp.sending ? i18n("Sending RSVP…") : (root.rsvp.message || root.invite.note || "")
            color: Theme.ink
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontPreview
            wrapMode: Text.Wrap
        }

        GhostButton {
            objectName: "inviteReload"
            visible: root.rsvp.reload === true
            text: i18n("Reload invitation")
            enabled: !AppLock.locked
            onClicked: root.reloadRequested()
        }
    }
}
