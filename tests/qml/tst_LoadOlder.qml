import QtQuick
import QtTest

// The roots need every C++ singleton, so their wiring is checked in the
// shipped source; the signal it relies on is checked here on a bare ListView.
// MailController decides whether a page is due (tested in C++).
Item {
    width: 300
    height: 400

    ListModel { id: rows }

    ListView {
        id: list
        anchors.fill: parent
        model: rows
        delegate: Rectangle { width: 300; height: 50 }
        property int asked: 0
        onAtYEndChanged: if (atYEnd) asked++
    }

    TestCase {
        name: "LoadOlder"
        when: windowShown

        function read(path) {
            var source = new XMLHttpRequest()
            source.open("GET", path, false)
            source.send()
            compare(source.status, 200, path)
            return source.responseText
        }

        function fill(count) {
            for (var i = 0; i < count; ++i)
                rows.append({})
        }

        function test_reaching_the_end_asks_once_and_new_rows_rearm_it() {
            rows.clear()
            fill(20)
            list.positionViewAtBeginning()
            list.asked = 0
            list.positionViewAtEnd()
            tryCompare(list, "asked", 1)
            // The page lands below the fold: no longer at the end, so the
            // next scroll to the end asks again rather than now.
            fill(20)
            tryVerify(function() { return !list.atYEnd })
            compare(list.asked, 1)
            list.positionViewAtEnd()
            tryCompare(list, "asked", 2)
        }

        function test_the_shell_asks_at_the_end_of_the_list() {
            var line = "onAtYEndChanged: if (atYEnd) MailApp.loadOlder()"
            verify(read("qrc:/qml/AppShell.qml").indexOf(line) !== -1)
        }
    }
}
