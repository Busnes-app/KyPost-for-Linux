import QtQuick
import QtTest

// AppShell needs every C++ singleton, so its wiring is checked in the shipped
// source; the gesture it relies on is checked here on a bare, short ListView.
Item {
    width: 300
    height: 400

    ListView {
        id: list
        anchors.fill: parent
        model: 3
        delegate: Rectangle { width: 300; height: 50 }
        property real releasedAt: 0
        onDragEnded: releasedAt = verticalOvershoot
    }

    TestCase {
        name: "PullToRefresh"
        when: windowShown

        function read(path) {
            var source = new XMLHttpRequest()
            source.open("GET", path, false)
            source.send()
            compare(source.status, 200, path)
            return source.responseText
        }

        function drag(fromY, toY) {
            tryVerify(function() { return !list.moving })
            list.releasedAt = 0
            mousePress(list, 150, fromY)
            var step = toY > fromY ? 20 : -20
            for (var y = fromY + step; step > 0 ? y <= toY : y >= toY; y += step)
                mouseMove(list, 150, y, 16, Qt.LeftButton)
            mouseRelease(list, 150, toY)
            return list.releasedAt
        }

        function test_pulling_past_the_top_overshoots_at_release() {
            var overshoot = drag(20, 300)
            verify(overshoot < -100, "overshoot at release: " + overshoot)
        }

        function test_a_drag_up_does_not_overshoot_the_top() {
            var overshoot = drag(300, 40)
            verify(overshoot >= 0, "overshoot at release: " + overshoot)
        }

        function test_mobile_list_refreshes_on_pull() {
            var text = read("qrc:/qml/AppShell.qml")
            verify(text.indexOf("onDragEnded: {\n                                if (verticalOvershoot < -Kirigami.Units.gridUnit * 4)\n                                    MailApp.refresh()") !== -1)
        }
    }
}
