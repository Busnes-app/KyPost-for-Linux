import QtQuick
import QtTest
import com.kysecurity.mail 1.0

Item {
    id: host
    width: 400
    height: 600

    // Stands in for Settings: answers historyWanted with whatever the
    // database would hold for the current account.
    property var accountHistory: []
    property bool answers: true

    Loader {
        id: loader
        width: 400
        source: "qrc:/qml/components/ConnectionStatus.qml"
    }

    Connections {
        target: loader.item
        function onHistoryWanted() {
            if (host.answers)
                loader.item.history = host.accountHistory
        }
    }

    SignalSpy {
        id: wantedSpy
        target: loader.item
        signalName: "historyWanted"
    }

    SignalSpy {
        id: openSpy
        target: loader.item
        signalName: "openRequested"
    }

    TestCase {
        name: "ConnectionStatus"
        when: windowShown

        function row(name) {
            var r = findChild(loader.item, name)
            verify(r !== null, name)
            return r
        }

        function value(name) {
            return findChild(row(name), "value").text
        }

        function init() {
            var s = loader.item
            host.answers = true
            host.accountHistory = []
            s.active = false
            s.distributorActive = false
            s.pushServerHost = ""
            s.pushEncrypted = false
            s.lastSyncAt = new Date(NaN)
            s.lastError = ""
            s.history = []
            openSpy.clear()
            wantedSpy.clear()
        }

        function previousAccount() {
            return [{ "messageId": "old1", "receivedAt": new Date(2026, 9, 11, 7, 5),
                      "sender": "a@old.example", "subject": "Previous account subject" }]
        }

        // Settings -> Connection stays selected across the replacement, so
        // nothing about the pane changes; only the pairing does.
        function test_replacing_the_account_clears_history_while_shown() {
            var s = loader.item
            host.accountHistory = previousAccount()
            s.active = true
            var list = findChild(s, "historyList")
            compare(list.count, 1)
            compare(list.itemAt(0).text, "Oct 11, 07:05  Previous account subject")

            host.accountHistory = [] // the replacement purged the table
            Pairing.pairingChanged()
            verify(s.active)
            compare(list.count, 0)
            verify(findChild(s, "historyEmpty").visible)
        }

        function test_the_new_accounts_history_replaces_the_old() {
            var s = loader.item
            host.accountHistory = previousAccount()
            s.active = true
            host.accountHistory = [{ "messageId": "new1", "receivedAt": new Date(2026, 9, 12, 8, 0),
                                     "sender": "b@new.example", "subject": "New account subject" }]
            Pairing.pairingChanged()
            var list = findChild(s, "historyList")
            compare(list.count, 1)
            compare(list.itemAt(0).text, "Oct 12, 08:00  New account subject")
        }

        // The clear does not depend on the reload: a host that supplies
        // nothing still leaves no previous-account row behind.
        function test_a_pairing_change_clears_history_even_without_a_reload() {
            var s = loader.item
            host.accountHistory = previousAccount()
            s.active = true
            compare(findChild(s, "historyList").count, 1)
            host.answers = false
            Pairing.pairingChanged()
            compare(findChild(s, "historyList").count, 0)
        }

        function test_history_is_read_only_while_shown() {
            var s = loader.item
            host.accountHistory = previousAccount()
            Pairing.pairingChanged()
            compare(wantedSpy.count, 0)
            compare(findChild(s, "historyList").count, 0)
            s.active = true
            compare(wantedSpy.count, 1)
            compare(findChild(s, "historyList").count, 1)
            s.active = false
            compare(findChild(s, "historyList").count, 0)
        }

        function test_polling_is_named_as_polling() {
            loader.item.pushServerHost = "ntfy.example" // stale token from an earlier distributor
            compare(value("pushRow"), "Checking the relay every 90 seconds")
        }

        function test_distributor_shows_its_push_server_host() {
            loader.item.distributorActive = true
            loader.item.pushServerHost = "ntfy.example"
            compare(value("pushRow"), "UnifiedPush via ntfy.example")
        }

        function test_encryption_is_stated_either_way() {
            compare(value("encryptedRow"), "No")
            loader.item.pushEncrypted = true
            compare(value("encryptedRow"), "Yes")
        }

        function test_last_sync_says_never_until_a_sync() {
            compare(value("syncRow"), "Not yet this session")
            loader.item.lastSyncAt = new Date(2026, 9, 11, 7, 5)
            compare(value("syncRow"), "Oct 11, 07:05")
        }

        function test_error_row_only_when_there_is_an_error() {
            verify(!row("errorRow").visible)
            loader.item.lastError = "Refresh failed"
            verify(row("errorRow").visible)
            compare(value("errorRow"), "Refresh failed")
        }

        function test_history_lists_and_opens_messages() {
            verify(findChild(loader.item, "historyEmpty").visible)
            loader.item.history = [
                { "messageId": "m1", "receivedAt": new Date(2026, 9, 11, 7, 5), "sender": "a@b", "subject": "Hello" },
                { "messageId": "m2", "receivedAt": new Date(2026, 9, 11, 6, 0), "sender": "", "subject": "" }
            ]
            verify(!findChild(loader.item, "historyEmpty").visible)
            var list = findChild(loader.item, "historyList")
            compare(list.count, 2)
            compare(list.itemAt(0).text, "Oct 11, 07:05  Hello")
            compare(list.itemAt(1).text, "Oct 11, 06:00  (message not synced yet)")
            mouseClick(list.itemAt(1))
            compare(openSpy.count, 1)
            compare(openSpy.signalArguments[0][0], "m2")
        }
    }
}
