import QtQuick

QtObject {
    property string pairingState: "idle"
    property bool isPaired: true
    property string pendingPairOrigin: "https://relay.kypost-demo.local"
    property bool pendingPairInsecure: false
    property string pairingError: ""
    property string deviceId: "linux-7f3a2c"
    property string deliveryMode: "push"
    property string transport: "UnifiedPush"
    property bool distributorActive: true
    property bool pushEncrypted: true
    property string pushServerHost: "ntfy.kypost-demo.local"
    property string pairedServerHost: "relay.kypost-demo.local"
    property bool certificateMismatch: false
    property string expectedCertificateFingerprint: "AA:BB"
    property string observedCertificateFingerprint: "CC:DD"
    property bool reregistrationRejected: false
    signal pairingChanged()
    signal pairedFromLink()
    function confirmPendingPair() {}
    function cancelPendingPair() { pairingState = "idle" }
    function pairFromDeepLink() {}
    function pairFromPastedLink() {}
    function reset() {}
    function removePairing() {}
    function reconnectToServer() {}
}
