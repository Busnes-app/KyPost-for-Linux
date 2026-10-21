import QtQuick
import QtTest
import "qrc:/qml/utils/format.js" as Format

// Folder names are sentence case: the raw IMAP "INBOX" never reaches the UI.
TestCase {
    name: "FolderNames"

    function test_relay_display_name_wins() {
        const folders = [{wireName: "INBOX", displayName: "Inbox"}, {wireName: "Archive/2024", displayName: "2024"}]
        compare(Format.folderDisplayName(folders, "INBOX"), "Inbox")
        compare(Format.folderDisplayName(folders, "Archive/2024"), "2024")
    }

    function test_notification_choices_show_the_whole_path() {
        compare(Format.folderPath("INBOX"), "Inbox")
        compare(Format.folderPath("Archive/Work"), "Archive / Work")
        compare(Format.folderPath("Projects.Work"), "Projects / Work")
        compare(Format.folderPath("INBOX.Receipts"), "Inbox / Receipts")
    }

    function test_fallback_never_shows_raw_inbox() {
        compare(Format.folderDisplayName([], "INBOX"), "Inbox")
        compare(Format.folderDisplayName([], "inbox"), "Inbox")
        compare(Format.folderDisplayName([], "Receipts"), "Receipts")
    }
}
