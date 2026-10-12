import QtQuick
import QtTest

Item {
    width: 400
    height: 600

    Loader {
        id: loader
        width: 400
        source: "qrc:/qml/components/ConnectionStatus.qml"
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
            s.distributorActive = false
            s.pushServerHost = ""
            s.pushEncrypted = false
            s.lastSyncAt = new Date(NaN)
            s.lastError = ""
            s.history = []
            openSpy.clear()
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
