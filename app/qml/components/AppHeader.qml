import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import com.kysecurity.mail 1.0

// The suite brand row: the logo tile and the "KyPost" wordmark in ink,
// nothing under it, actions at the right. Where you are ("Inbox") is a
// separate sentence-case title below the brand row.
Item {
    id: root

    property string place: ""
    default property alias actions: actionRow.data

    implicitHeight: column.implicitHeight

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            Layout.leftMargin: 20
            Layout.rightMargin: 12
            spacing: 10

            Image {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                // Rendered from the 256 px envelope at the screen's density (at
                // least 2x); the display size stays the explicit 28 above.
                source: "qrc:/appicon/256.png"
                sourceSize: Qt.size(Math.ceil(28 * Math.max(2, Screen.devicePixelRatio)),
                                    Math.ceil(28 * Math.max(2, Screen.devicePixelRatio)))
                smooth: true
                mipmap: true
                Accessible.ignored: true
            }

            Text {
                Layout.fillWidth: true
                text: "KyPost" // product name, not translated
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: Math.round(20 * Theme.fontScale)
                font.weight: Font.Bold
            }

            RowLayout {
                id: actionRow
                spacing: 4
            }
        }

        Text {
            objectName: "headerPlace"
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 12
            textFormat: Text.PlainText
            text: root.place
            color: Theme.inkStrong
            font.family: Theme.fontUi
            font.pixelSize: Math.round(24 * Theme.fontScale)
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
    }
}
