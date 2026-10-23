import QtQuick

QtObject {
    property bool locked: false
    property bool lockEnabled: true
    property bool lockPending: false
    property int failedAttempts: 0
    property int remainingLockoutSeconds: 0
    property int minimumPinLength: 6
    property string pinRejectionReason: ""
    property int backgroundGraceSeconds: 30
    property int wipeAfterAttempts: 30
    property bool credentialPinGateEnabled: false
    property bool hostileLocationEnabled: false
    property bool credentialsUnavailable: false
    property bool databaseMemoryOnly: false
    property bool databaseUnencrypted: false
    property bool dataDirectoryUnprotected: false
    property bool storeUnavailable: false
    property bool wipeIncomplete: false
    property bool lockOffered: false
    function takeLockOffer() {
        if (lockEnabled || lockOffered) return false
        lockOffered = true
        return true
    }
    function lockNow() { locked = true }
    function lockAfterGrace() {}
    function cancelPendingLock() {}
    function tryUnlock() { return false }
    function setPin() { return true }
    function disableLock() { return true }
    function setBackgroundGraceSeconds(s) { backgroundGraceSeconds = s }
    function setWipeAfterAttempts(n) { wipeAfterAttempts = n }
    function setCredentialPinGateEnabled(v) { credentialPinGateEnabled = v }
    function setHostileLocationEnabled(v) { hostileLocationEnabled = v }
}
