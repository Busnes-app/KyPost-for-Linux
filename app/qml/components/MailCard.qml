import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.kysecurity.mail 1.0
import "../utils/format.js" as Format

// One mail row, flat on the page surface as Gmail's list: a hairline under
// each row, about 72px tall with the preview and 64px without it. An accent
// dot in the left gutter and a bold subject mark unread; the selected row is
// accent-soft with an accent bar. Subject 16 / sender 14 / preview 13 /
// date 12, no avatar. Swipe right
// deletes, swipe left offers Snooze and Archive; right-click opens the menu.
// Used as a ListView delegate, so the row's roles arrive as `model.*`.
SwipeDelegate {
    id: card

    property bool selected: false
    // Narrow windows show subject and sender only, as Android's phone list.
    property bool showDetails: true
    property date now: new Date()
    signal moveRequested(var messageIds, string folder)
    signal snoozeRequested(var messageIds)

    // Swipes and the menu dispatch and return; a second gesture on the same
    // row before the reply lands must not send the action twice.
    property bool actionTriggered: false

    width: ListView.view ? ListView.view.width : implicitWidth
    leftPadding: 36
    rightPadding: 20
    topPadding: showDetails ? 8 : 12
    bottomPadding: showDetails ? 8 : 12
    Accessible.name: model.accessibleName

    Connections {
        target: MailApp
        function onActionCompleted(action, messageIds, ok) {
            if (!ok && messageIds.indexOf(model.messageId) !== -1)
                card.actionTriggered = false
        }
    }

    background: Rectangle {
        color: card.selected ? Theme.accentSoft
            : (card.down || card.hovered ? Qt.rgba(Theme.ink.r, Theme.ink.g, Theme.ink.b, 0.06) : Theme.bg)

        Rectangle {
            // the selection bar
            visible: card.selected
            width: 3
            height: parent.height
            color: Theme.accent
        }
        Rectangle {
            // unread, in the gutter, centred on the subject line
            visible: model.unread
            x: 20
            y: card.topPadding + Math.round(subjectLine.height / 2) - 4
            width: 8
            height: 8
            radius: 4
            color: Theme.accent
        }
        Rectangle {
            // the hairline between rows
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.line
        }
        Item {
            anchors.fill: parent
            anchors.margins: 3
            FocusRing {
                cornerRadius: 4
                visible: card.visualFocus
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 1

        RowLayout {
            id: subjectLine
            Layout.fillWidth: true
            spacing: 8

            Text {
                visible: !!model.pgpMarker
                textFormat: Text.PlainText
                text: model.pgpMarker || ""
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontBody
                Accessible.role: Accessible.StaticText
                Accessible.name: model.pgpMarkerAccessibleName || ""
            }
            Text {
                objectName: "mailSubject"
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: model.subject
                color: model.unread ? Theme.inkStrong : Theme.ink
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontBody
                font.weight: model.unread ? Font.Bold : Font.Normal
                elide: Text.ElideRight
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: model.sender
                color: Theme.ink
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontSender
                elide: Text.ElideRight
            }
            StatusBadge {
                visible: !!model.woken
                text: i18nc("@label mail list badge, message returned from snooze", "Snoozed")
            }
            Text {
                visible: card.showDetails
                textFormat: Text.PlainText
                text: Format.rowDate(model.atUtc, card.now,
                                     model.atUtc.toLocaleTimeString(Qt.locale(), Locale.ShortFormat),
                                     Qt.formatDate(model.atUtc, "MMM d"))
                color: Theme.ink
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontDate
            }
        }
        Text {
            visible: card.showDetails && text.length > 0
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: model.preview
            color: Theme.ink
            opacity: 0.85
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontPreview
            maximumLineCount: 1
            elide: Text.ElideRight
        }
    }

    swipe.left: Rectangle {
        anchors.fill: parent
        color: Theme.danger

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            text: i18n("Delete")
            color: Theme.readableOn(Theme.danger)
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontSender
            font.weight: Font.Medium

            SwipeDelegate.onClicked: {
                if (card.actionTriggered)
                    return
                card.actionTriggered = true
                MailApp.deleteEmails([model.messageId])
            }
        }
    }

    swipe.right: Rectangle {
        anchors.fill: parent
        color: Theme.warning

        Text {
            readonly property bool inSnoozed: model.folder === MailApp.snoozedFolder
            anchors.right: archiveLabel.left
            anchors.rightMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: inSnoozed ? i18n("Unsnooze") : i18n("Snooze")
            color: Theme.readableOn(Theme.warning)
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontSender
            font.weight: Font.Medium

            SwipeDelegate.onClicked: {
                if (card.actionTriggered)
                    return
                if (inSnoozed) {
                    card.actionTriggered = true
                    MailApp.unsnoozeEmails([model.messageId])
                } else {
                    card.swipe.close()
                    card.snoozeRequested([model.messageId])
                }
            }
        }
        Text {
            id: archiveLabel
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            text: i18n("Archive")
            color: Theme.readableOn(Theme.warning)
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontSender
            font.weight: Font.Medium

            SwipeDelegate.onClicked: {
                if (card.actionTriggered)
                    return
                card.actionTriggered = true
                MailApp.archiveEmails([model.messageId])
            }
        }
    }

    function openMenu() { if (!AppLock.locked) contextMenu.popup() }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: card.openMenu()
    }
    // The menu without a pointer: the Menu key or Shift+F10.
    Keys.onMenuPressed: card.openMenu()
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) {
            card.openMenu()
            event.accepted = true
        }
    }

    // A Menu paints above the lock overlay.
    Connections {
        target: AppLock
        function onLockedChanged() { if (AppLock.locked) contextMenu.close() }
    }

    readonly property alias menuOpen: contextMenu.opened

    Menu {
        id: contextMenu
        MenuItem {
            text: model.unread ? i18n("Mark as read") : i18n("Mark as unread")
            onTriggered: model.unread ? MailApp.markRead(model.folder, [model.messageId])
                                      : MailApp.markUnread(model.folder, [model.messageId])
        }
        MenuItem {
            text: i18n("Move to…")
            enabled: !MailApp.isBusy
            onTriggered: card.moveRequested([model.messageId], model.folder)
        }
        MenuItem {
            readonly property bool inSnoozed: model.folder === MailApp.snoozedFolder
            text: inSnoozed ? i18n("Unsnooze") : i18n("Snooze…")
            enabled: !MailApp.isBusy
            onTriggered: inSnoozed ? MailApp.unsnoozeEmails([model.messageId]) : card.snoozeRequested([model.messageId])
        }
        MenuItem {
            text: i18n("Delete")
            onTriggered: MailApp.deleteEmails([model.messageId])
        }
    }
}
