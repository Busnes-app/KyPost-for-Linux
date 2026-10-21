import QtQuick
import QtQuick.Layouts
import com.kysecurity.mail 1.0

// Android's header: launcher logo, the "KyPost" wordmark in accent and,
// under it, where you are in monospace caps ("INBOX").
Item {
    id: root

    property string place: ""
    default property alias actions: actionRow.data

    implicitHeight: 88

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 12
        spacing: 14

        Image {
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44
            source: "qrc:/qml/icons/logo.png"
            sourceSize: Qt.size(88, 88)
            smooth: true
            mipmap: true
            Accessible.ignored: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Text {
                text: "KyPost" // product name, not translated
                color: Theme.accent
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontWordmark
                font.weight: Font.Bold
            }
            Text {
                objectName: "headerPlace"
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: root.place
                color: Theme.ink
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontDate
                font.capitalization: Font.AllUppercase
                font.letterSpacing: Math.round(Theme.fontDate * 0.18 * 10) / 10
                elide: Text.ElideRight
            }
        }

        RowLayout {
            id: actionRow
            spacing: 4
        }
    }
}
