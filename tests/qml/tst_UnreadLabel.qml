import QtQuick 2.15
import QtTest 1.15
import "qrc:/qml/utils/format.js" as Format

// Tab and folder labels carry their unread count as Android's do: "Work · 3",
// and the bare label when nothing is unread.
TestCase {
    name: "UnreadLabel"

    function test_a_count_follows_the_isolated_label() {
        compare(Format.withUnread("Work", 3), "⁨Work⁩ · 3")
    }

    function test_nothing_unread_is_the_bare_label() {
        compare(Format.withUnread("Inbox", 0), "Inbox")
        // A folder absent from MailApp.unreadCounts (Drafts, Sent, Snoozed).
        compare(Format.withUnread("Drafts", undefined), "Drafts")
        compare(Format.withUnread("Junk", -1), "Junk")
    }

    function test_no_cap() {
        compare(Format.withUnread("Inbox", 1234), "⁨Inbox⁩ · 1234")
    }
}
