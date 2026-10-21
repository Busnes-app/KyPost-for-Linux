import QtQuick
import "Fixture.js" as F

QtObject {
    property bool open: false
    property bool hasChallenge: true
    property bool inFlight: false
    property string challengeId: "mfa-1"
    property var choices: ["42", "17", "83"]
    property string ipAddress: "203.0.113.7"
    property string userAgent: "Firefox on Linux"
    property var issuedAt: F.now
    property int pendingCount: 1
    property int secondsRemaining: 94
    property bool resultIsError: false
    property string resultMessage: ""
    signal changed()
    signal showRequested()
    function approve() {}
    function deny() {}
    function close() { open = false }
}
