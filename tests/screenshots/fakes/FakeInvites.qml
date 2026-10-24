import QtQuick

// The "email-invite" screen hands the reader a card directly; token 7 is
// answerable. "rsvp-confirm" sets the confirmation the real controller raises.
// ShellInvites drives the reader's wiring through `calls` and inviteLoaded.
QtObject {
    property var states: ({"7": {answerable: true, sending: false, message: ""}})
    property var confirmation: ({})
    property var calls: []
    property int lastRequest: 0
    signal inviteLoaded(int request, var card)
    function record(name, args) { calls = calls.concat([{name: name, args: args}]) }
    function load(mailbox, messageId) { record("load", [mailbox, messageId]); return ++lastRequest }
    function forget(token) { record("forget", [token]) }
    function requestRsvp() {}
    function confirmRsvp() {}
    function cancelRsvp(token) { record("cancelRsvp", [token]) }
}
