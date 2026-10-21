import QtQuick
import KyPostScreens

QtObject {
    property bool isBusy: false
    property string lastError: ""
    property string myFingerprint: "3F2A 91C4 7D10 B8E5 6A0C  4E21 9B7F 02D3 C845 1E6A"
    property bool myFingerprintIsLocal: true
    property var myQrExpiresAt: new Date(2026, 9, 20, 14, 33)
    function myQrImageDataUrl() { return ScreenRun.qrDataUrl }
    property string myQrUrl: "kypost://pgp-key?fp=3F2A91C47D10B8E5"
    property string scannedFingerprint: ""
    property string scannedName: ""
    property string scannedPublicKey: ""
    property bool scannedVerified: false
    signal myQrDataChanged()
    function refreshMyQrCode() { myQrDataChanged() }
    function scanQrPayload() {}
    function clearScanResult() {}
    function scannedContactCardFields() { return ({}) }
}
