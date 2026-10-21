import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.kysecurity.mail 1.0
import "../components"
import "../utils/format.js" as Format

// Task 36 -- plain reusable Item, deliberately NOT a Kirigami.Page (see
// Phase 6 global constraint 4): MobileRoot wraps this in a thin
// Kirigami.Page shell when it pushes it (Task 38); DesktopRoot embeds it
// directly inside its list-pane Item (Task 39). Navigation is signal-based,
// same "don't assume how the host navigates" shape as EmailDetail/Compose
// (Task 35) -- tapping a row or "Add" both just report a uid, they never
// push/pop anything themselves.
Item {
    id: root

    // uid === "" means "Add" (create mode) -- ContactDetail.qml's own `uid`
    // property already treats an empty string this way (see its doc
    // comment), so "Add" reuses this single signal with the "" sentinel
    // rather than adding a second addRequested() signal that would carry no
    // real behavioral difference for whichever root ends up handling it.
    signal contactSelected(string uid)
    // PGP QR key exchange: same "let the host decide how to navigate" shape
    // as contactSelected() -- the host pushes/opens PgpScanContactKey.qml
    // and, on a successful scan, creates a brand-new contact from it (see
    // MobileRoot.qml/DesktopRoot.qml's wiring).
    signal scanPgpKeyRequested()

    implicitWidth: 360
    implicitHeight: 640

    Component.onCompleted: ContactsApp.load()

    // Shown for 3s after a Sync tap, then hidden again -- ContactsApp.
    // statusMessage/lastError are ContactsApp's own persistent last-outcome
    // state (they don't clear themselves), so "briefly after" is
    // implemented as a local visibility flag here rather than this file
    // reaching in and blanking properties it doesn't own.
    property bool showSyncStatus: false

    function sync() {
        ContactsApp.sync()
        root.showSyncStatus = true
        syncStatusTimer.restart()
    }

    function findDuplicates() {
        ContactsApp.dedupe()
        root.showSyncStatus = true
        syncStatusTimer.restart()
    }

    // Sync, Find duplicates and Scan PGP key, Android's overflow menu. The
    // host's header owns the "more" button and anchors the menu to it.
    function openActions(anchor) {
        actionsMenu.popup(anchor, 0, anchor.height)
    }

    // A Menu stacks above the lock overlay, so it closes when the app locks
    // and its items refuse while locked.
    Connections {
        target: AppLock
        function onLockedChanged() {
            if (AppLock.locked)
                actionsMenu.close()
        }
    }

    Menu {
        id: actionsMenu

        MenuItem {
            text: i18n("Sync")
            enabled: !ContactsApp.isBusy && !AppLock.locked
            onTriggered: root.sync()
        }
        MenuItem {
            text: i18n("Find duplicates")
            enabled: !ContactsApp.isBusy && !AppLock.locked
            onTriggered: root.findDuplicates()
        }
        MenuItem {
            text: i18n("Scan PGP key")
            enabled: !AppLock.locked
            onTriggered: root.scanPgpKeyRequested()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            visible: root.showSyncStatus && (ContactsApp.lastError !== "" || ContactsApp.statusMessage !== "")
            textFormat: Text.PlainText
            text: ContactsApp.lastError !== "" ? ContactsApp.lastError : ContactsApp.statusMessage
            color: ContactsApp.lastError !== "" ? Theme.dangerActionText : Theme.ink
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontDate
            wrapMode: Text.WordWrap
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            visible: listView.count === 0
            text: i18n("No contacts yet — sync or add one.")
        }

        // Android's item_contact.xml: a card per contact with a 40px
        // avatar, the name over the organisation (or email), and the key
        // chip on the right.
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: count > 0
            clip: true
            spacing: 12
            topMargin: 12
            bottomMargin: 88 // clear of the add button
            leftMargin: 16
            rightMargin: 16
            model: ContactsApp.contactModel
            ScrollBar.vertical: ThemedScrollBar {}

            delegate: Card {
                id: contactCard
                width: listView.width - 32
                height: Math.max(76, rowContent.implicitHeight + 28)
                color: tapHandler.pressed ? Theme.selection : Theme.panel

                RowLayout {
                    id: rowContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 14

                    Avatar {
                        initials: Format.initialsFromName(model.fn)
                        // Lazy photo fetch as the row scrolls into view; rows
                        // with no photoRef never call ContactsApp at all.
                        // ContactsApp.photoRevision is read for its binding
                        // dependency, not its value: photoPathFor() is
                        // cache-only, returns "" on a miss and starts a
                        // background fetch, so without something notifying
                        // this binding would never re-run.
                        photoSource: model.photoRef !== "" && ContactsApp.photoRevision >= 0
                                     ? ContactsApp.photoPathFor(model.uid) : ""
                        size: 40
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            text: (model.fn && model.fn.length > 0 ? model.fn : i18n("Unnamed"))
                                + (model.isSelf ? " · " + i18n("You") : "")
                            color: Theme.inkStrong
                            font.family: Theme.fontUi
                            font.pixelSize: Math.round(18 * Theme.fontScale)
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: text !== ""
                            textFormat: Text.PlainText
                            text: model.org !== "" ? model.org : model.primaryEmail
                            color: Theme.ink
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fontSender
                            elide: Text.ElideRight
                        }
                    }

                    StatusBadge {
                        active: model.hasPgpKey
                        text: model.hasPgpKey ? i18n("Secure key") : i18n("No key")
                    }
                }

                TapHandler {
                    id: tapHandler
                    onTapped: root.contactSelected(model.uid)
                }
            }
        }
    }

    // Android's add FAB.
    Card {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        width: 56
        height: 56
        radius: 28
        color: Theme.accent

        IconButton {
            anchors.fill: parent
            objectName: "addContact"
            icon: "add"
            variant: "primary"
            tooltip: i18n("Add contact")
            onClicked: root.contactSelected("")
        }
    }

    Timer {
        id: syncStatusTimer
        interval: 3000
        onTriggered: root.showSyncStatus = false
    }
}
