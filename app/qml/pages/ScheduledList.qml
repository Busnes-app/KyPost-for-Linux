import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.kysecurity.mail 1.0
import "../components"

// Mail the relay is holding for later. A waiting message can be canceled,
// which returns it to Drafts; the rest show what became of them.
Item {
    id: root

    signal closed()

    // The waiting message whose cancel is being confirmed.
    property string confirmingId: ""

    implicitWidth: 480
    implicitHeight: 560

    Component.onCompleted: Scheduled.refresh()

    // A confirmation must not outlive the lock gate.
    Connections {
        target: AppLock
        function onLockedChanged() { root.confirmingId = "" }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: i18n("Scheduled")
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: 18
                font.bold: true
            }
            GhostButton {
                text: i18n("Refresh")
                enabled: !Scheduled.busy
                onClicked: Scheduled.refresh()
            }
            GhostButton {
                text: i18n("Done")
                onClicked: root.closed()
            }
        }

        Text {
            objectName: "scheduledMessage"
            Layout.fillWidth: true
            visible: text !== ""
            textFormat: Text.PlainText
            text: Scheduled.message !== "" ? Scheduled.message
                : (!Scheduled.busy && Scheduled.items.length === 0 ? i18n("Nothing is scheduled.") : "")
            color: Theme.ink
            font.family: Theme.fontUi
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 12
            model: Scheduled.items
            ScrollBar.vertical: ThemedScrollBar {}

            delegate: ColumnLayout {
                objectName: "scheduledRow"
                width: list.width
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    text: modelData.subject
                    color: Theme.inkStrong
                    font.family: Theme.fontUi
                    font.pixelSize: 14
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    text: modelData.to
                    color: Theme.ink
                    font.family: Theme.fontUi
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    text: [modelData.when, modelData.status, modelData.reason].filter(s => s !== "").join(" · ")
                    color: Theme.ink
                    font.family: Theme.fontUi
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    visible: modelData.cancellable
                    spacing: 8

                    GhostButton {
                        objectName: "cancelScheduled"
                        // The second tap confirms; the button stays where it was.
                        text: root.confirmingId === modelData.id ? i18n("Return to Drafts") : i18n("Cancel Send")
                        enabled: !Scheduled.busy
                        onClicked: {
                            if (root.confirmingId !== modelData.id) {
                                root.confirmingId = modelData.id
                                return
                            }
                            root.confirmingId = ""
                            Scheduled.cancel(modelData.id)
                        }
                    }
                    GhostButton {
                        visible: root.confirmingId === modelData.id
                        text: i18n("Keep")
                        onClicked: root.confirmingId = ""
                    }
                }
            }
        }
    }
}
