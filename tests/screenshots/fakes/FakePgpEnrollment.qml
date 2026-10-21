import QtQuick

QtObject {
    property bool busy: false
    property int state: 0
    property string status: ""
    property string verificationCode: ""
    function start() {}
    function cancel() {}
    function checkAgain() {}
}
