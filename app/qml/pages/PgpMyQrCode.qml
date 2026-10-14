import QtQuick
import QtQuick.Layouts
import com.kysecurity.mail 1.0
import "../components"

// PGP QR key exchange (Client_PGP_Update.md): displays a QR code encoding
// this device's pairing account's PGP-key pickup URL (PgpQr.myQrUrl), for
// someone else's device to scan via PgpScanContactKey.qml. Plain reusable
// Item, same "parent-agnostic" shape as every other page component in this
// directory (Compose/ContactDetail/Settings) -- MobileRoot pushes this via
// pageStack, DesktopRoot hosts it in a Kirigami.OverlaySheet (same choice
// Settings.qml itself already made).
Item {
    id: root

    signal closed()

    implicitWidth: 360
    implicitHeight: 480

    // PgpQr.myQrImageDataUrl()/myQrExpiresAt are plain Q_INVOKABLE/QString
    // reads, not NOTIFY-bound to the underlying token data itself -- this
    // local property is refreshed explicitly (via the Connections block
    // below) whenever PgpQr.myQrDataChanged fires, so Image.source stays a
    // plain property binding QML can react to.
    property string qrDataUrl: ""

    function doRefresh() {
        PgpQr.refreshMyQrCode()
    }

    Component.onCompleted: root.doRefresh()

    // The code is the previous account's after a pairing change, and a failed
    // refresh never replaces it (myQrDataChanged fires on success only), so
    // drop it before asking again. DesktopRoot keeps this page alive in a sheet.
    Connections {
        target: Pairing
        function onPairingChanged() {
            refreshTimer.stop()
            root.qrDataUrl = ""
            root.doRefresh()
        }
    }

    Connections {
        target: PgpQr
        function onMyQrDataChanged() {
            root.qrDataUrl = PgpQr.myQrImageDataUrl()

            // Auto-refresh ~15s before the token's 2-minute expiry -- only
            // re-armed after a successful fetch (myQrDataChanged only fires
            // on Success, see PgpQrController::refreshMyQrCode()), never
            // after a NoPgpIdentity/ServiceUnavailable failure, which
            // should show a static message and stop retrying instead.
            const expiresMs = Date.parse(PgpQr.myQrExpiresAt)
            if (!isNaN(expiresMs)) {
                const delay = expiresMs - Date.now() - 15000
                refreshTimer.interval = Math.max(1000, delay)
                refreshTimer.restart()
            }
        }
    }

    Timer {
        id: refreshTimer
        repeat: false
        onTriggered: root.doRefresh()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: i18n("My PGP QR Code")
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: 20
                font.weight: Font.Bold
            }
            GhostButton {
                text: i18n("Close")
                onClicked: root.closed()
            }
        }

        Text {
            Layout.fillWidth: true
            visible: PgpQr.lastError !== ""
            textFormat: Text.PlainText
            text: PgpQr.lastError
            color: Theme.dangerColor
            font.family: Theme.fontUi
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        Item { Layout.fillHeight: true; Layout.fillWidth: true; visible: root.qrDataUrl === "" }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            visible: root.qrDataUrl !== ""
            width: 240
            height: 240
            color: "white"
            radius: Theme.shapePanel

            Image {
                anchors.fill: parent
                anchors.margins: 12
                source: root.qrDataUrl
                fillMode: Image.PreserveAspectFit
                smooth: false // crisp QR modules, no blur
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            visible: root.qrDataUrl !== ""
            textFormat: Text.PlainText
            text: i18n("Expires %1", PgpQr.myQrExpiresAt)
            color: Theme.ink
            font.family: Theme.fontUi
            font.pixelSize: 12
        }

        SectionLabel {
            visible: root.qrDataUrl !== "" && PgpQr.myFingerprint !== ""
            text: PgpQr.myFingerprintIsLocal ? i18n("Your key fingerprint") : i18n("Fingerprint reported by the server")
        }
        Text {
            Layout.fillWidth: true
            visible: root.qrDataUrl !== "" && PgpQr.myFingerprint !== ""
            textFormat: Text.PlainText
            text: PgpQr.myFingerprint
            color: Theme.inkStrong
            font.family: Theme.fontMono
            font.pixelSize: 13
            wrapMode: Text.WrapAnywhere
        }
        Text {
            Layout.fillWidth: true
            visible: root.qrDataUrl !== "" && !PgpQr.myFingerprintIsLocal
            textFormat: Text.PlainText
            text: i18n("This device holds no private key for your account, so this code can't be verified in person.")
            color: Theme.warningColor
            font.family: Theme.fontUi
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            GhostButton {
                text: i18n("Refresh")
                enabled: !PgpQr.isBusy
                onClicked: root.doRefresh()
            }
            Item { Layout.fillWidth: true }
        }
    }
}
