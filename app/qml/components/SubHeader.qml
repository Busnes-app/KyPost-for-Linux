import QtQuick
import QtQuick.Layouts
import com.kysecurity.mail 1.0

// Title bar of a detail page (Android's "Email" / contact-name bar), with a
// back arrow when the page covers the list.
Rectangle {
    id: root

    property string title: ""
    property bool showBack: false
    default property alias actions: actionRow.data
    signal backRequested()

    implicitHeight: 64
    color: Theme.bg

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.showBack ? 4 : 20
        anchors.rightMargin: 12
        spacing: 8

        IconButton {
            visible: root.showBack
            icon: "arrow-back"
            tooltip: i18n("Back")
            onClicked: root.backRequested()
        }
        Text {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: root.title
            color: Theme.inkStrong
            font.family: Theme.fontUi
            font.pixelSize: Math.round(22 * Theme.fontScale)
            elide: Text.ElideRight
        }
        RowLayout {
            id: actionRow
            spacing: 4
        }
    }
}
