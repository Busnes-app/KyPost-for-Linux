import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.kysecurity.mail 1.0
import "../utils/format.js" as Format

// One mail row as Android draws it (item_email.xml): a rounded panel card,
// 8px margin and 16px padding, an accent dot and bold subject when unread,
// subject 18 / sender 14 / preview 13 / date 12, no avatar. Swipe right
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
    padding: 24
    topPadding: 24
    bottomPadding: 24
    Accessible.name: model.accessibleName

    Connections {
        target: MailApp
        function onActionCompleted(action, messageIds, ok) {
            if (!ok && messageIds.indexOf(model.messageId) !== -1)
                card.actionTriggered = false
        }
    }

    // A control's background takes no anchors, so the 8px card margin lives
    // on a child.
    background: Item {
        Card {
            anchors.fill: parent
            anchors.margins: 8
            color: card.selected ? Theme.selection : (card.down ? Qt.darker(Theme.panel, 1.03) : Theme.panel)

            FocusRing {
                cornerRadius: Theme.shapePanel
                visible: card.visualFocus
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                visible: model.unread
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 8
                implicitHeight: 8
                radius: 4
                color: Theme.accent
            }
            Text {
                visible: !!model.pgpMarker
                textFormat: Text.PlainText
                text: model.pgpMarker || ""
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontSubject
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
                font.pixelSize: Theme.fontSubject
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
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }

    swipe.left: Card {
        anchors.fill: parent
        anchors.margins: 8
        elevated: false
        color: Theme.dangerColor

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            text: i18n("Delete")
            color: Theme.readableOn(Theme.dangerColor)
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

    swipe.right: Card {
        anchors.fill: parent
        anchors.margins: 8
        elevated: false
        color: Theme.warningColor

        Text {
            readonly property bool inSnoozed: model.folder === MailApp.snoozedFolder
            anchors.right: archiveLabel.left
            anchors.rightMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: inSnoozed ? i18n("Unsnooze") : i18n("Snooze")
            color: Theme.readableOn(Theme.warningColor)
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
            color: Theme.readableOn(Theme.warningColor)
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
