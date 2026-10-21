import QtQuick
import com.kysecurity.mail 1.0

// The mail list's search field (GET /api/mail/search), one definition for
// both roots. Results replace the list while there is a query; Escape or
// emptying the field returns to the folder.
//
// Disabled while locked. A disabled item neither holds nor takes focus, so
// neither Find nor any other route can put the keyboard into the field under
// the PIN overlay to edit, select or copy the query it still shows.
// MailController refuses query changes while locked as the backstop.
ThemedTextField {
    id: field

    // AppShell turns this on for the mail section.
    property bool findShortcutEnabled: false
    readonly property alias findShortcut: findShortcut

    // Escape cleared the search; the old MobileRoot also hides the field.
    signal dismissed()

    enabled: !AppLock.locked

    Shortcut {
        id: findShortcut
        sequences: [StandardKey.Find]
        enabled: field.findShortcutEnabled && !AppLock.locked
        onActivated: field.inputField.forceActiveFocus()
    }

    Shortcut {
        sequence: "Esc"
        enabled: field.inputField.activeFocus && !AppLock.locked
        onActivated: {
            MailApp.clearSearch()
            field.dismissed()
        }
    }

    Connections {
        target: field.inputField
        function onTextEdited() { MailApp.setSearchQuery(field.text) }
    }

    Connections {
        target: MailApp
        function onSearchChanged() {
            if (field.text !== MailApp.searchQuery)
                field.text = MailApp.searchQuery
        }
    }
}
