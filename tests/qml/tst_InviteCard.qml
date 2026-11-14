import QtQuick 2.15
import QtTest 1.15
import com.kysecurity.mail 1.0
import "qrc:/qml/components" as Components

// The invite card and its RSVP confirmation (plan item L17): a tap only asks
// Invites, the confirmation answers only its own card, and nothing is offered
// or confirmed under the lock. The rules behind them are InviteControllerTest's.
TestCase {
    id: testCase
    name: "InviteCard"
    when: windowShown
    visible: true
    width: 480
    height: 640

    readonly property var card: ({
        token: 7, eyebrow: "Calendar invite · REQUEST", title: "<img src=\"http://beacon.example/x.png\">Launch",
        details: ["12 Nov 2026 15:00 – 16:00", "Organizer: boss@example.com"],
        notices: ["The organizer (boss@example.com) is not the sender of this email. Anyone can name any organizer."],
        offersRsvp: true })
    readonly property var confirmation: ({ token: 7, title: "Send RSVP unencrypted?", text: "Reply \"Accepted\"…",
                                           sendLabel: "Send unencrypted" })

    Column {
        width: parent.width
        Components.InviteCard {
            id: inviteCard
            width: parent.width
            invite: testCase.card
        }
    }

    SignalSpy {
        id: reloadSpy
        target: inviteCard
        signalName: "reloadRequested"
    }

    Components.RsvpConfirmDialog {
        id: dialog
        anchors.fill: parent
        z: 10
        token: 7
    }

    function init() {
        reloadSpy.clear()
        AppLock.locked = false
        Invites.reset()
        Invites.states = { "7": { answerable: true, sending: false, message: "" } }
        inviteCard.invite = testCase.card
        dialog.token = 7
    }

    function cleanup() {
        AppLock.locked = false
        Invites.reset()
    }

    function find(root, name) {
        if (root.objectName === name)
            return root
        const kids = root.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = find(kids[i], name)
            if (found)
                return found
        }
        return null
    }

    function lastCall() {
        return Invites.calls.length ? Invites.calls[Invites.calls.length - 1] : null
    }

    function test_the_card_shows_the_sender_data_as_plain_text() {
        verify(inviteCard.visible)
        const title = find(inviteCard, "inviteTitle")
        compare(title.textFormat, Text.PlainText)
        compare(title.text, testCase.card.title)
        compare(find(inviteCard, "inviteDetails").textFormat, Text.PlainText)
        compare(find(inviteCard, "inviteNotices").textFormat, Text.PlainText)
        verify(find(inviteCard, "inviteNotices").text.indexOf("is not the sender") >= 0)
    }

    function test_no_card_without_an_invite() {
        inviteCard.invite = {}
        verify(!inviteCard.visible)
        compare(inviteCard.implicitHeight, 0)
    }

    function test_accept_asks_with_the_cards_own_token() {
        const accept = find(inviteCard, "rsvp_ACCEPTED")
        verify(waitForRendering(inviteCard))
        mouseClick(accept)
        compare(lastCall().name, "requestRsvp")
        compare(lastCall().args[0], 7)
        compare(lastCall().args[1], "ACCEPTED")
    }

    function test_no_buttons_without_rsvp_support() {
        const noRsvp = Object.assign({}, testCase.card)
        noRsvp.offersRsvp = false
        inviteCard.invite = noRsvp
        verify(!find(inviteCard, "rsvp_ACCEPTED").visible)
    }

    function test_an_answered_revision_and_the_lock_disable_the_buttons() {
        verify(find(inviteCard, "rsvp_DECLINED").enabled)
        Invites.states = { "7": { answerable: false, sending: false, message: "RSVP sent" } }
        verify(!find(inviteCard, "rsvp_DECLINED").enabled)
        compare(find(inviteCard, "rsvpMessage").text, "RSVP sent")
        Invites.states = { "7": { answerable: true, sending: false, message: "" } }
        AppLock.locked = true
        verify(!find(inviteCard, "rsvp_DECLINED").enabled)
    }

    function test_the_dialog_answers_only_its_own_card() {
        verify(!dialog.visible)
        Invites.confirmation = Object.assign({}, testCase.confirmation, { token: 8 })
        verify(!dialog.visible)
        Invites.confirmation = testCase.confirmation
        verify(dialog.visible)
        compare(find(dialog, "rsvpConfirmText").textFormat, Text.PlainText)
        verify(waitForRendering(dialog))
        mouseClick(find(dialog, "rsvpSend"))
        compare(lastCall().name, "confirmRsvp")
        compare(lastCall().args[0], 7)
    }

    function test_cancel_and_the_backdrop_release_the_answer() {
        Invites.confirmation = testCase.confirmation
        verify(waitForRendering(dialog))
        mouseClick(find(dialog, "rsvpCancel"))
        compare(lastCall().name, "cancelRsvp")
        Invites.reset()
        Invites.confirmation = testCase.confirmation
        mouseClick(dialog, 4, 4)
        compare(lastCall().name, "cancelRsvp")
    }

    // C++ withdraws the confirmation when the app locks; the dialog does not
    // wait for that, so the prompt is never drawn or answered over the PIN.
    function test_the_dialog_is_closed_and_inert_under_the_lock() {
        Invites.confirmation = testCase.confirmation
        verify(dialog.visible)
        AppLock.locked = true
        verify(!dialog.visible)
        dialog.send()
        verify(Invites.calls.every(function (c) { return c.name !== "confirmRsvp" }))
    }

    function cardWith(extra) {
        return Object.assign({}, testCase.card, extra)
    }

    // KyPost-Server #606: a CANCEL says so and offers no answer.
    function test_a_cancellation_shows_cancelled_and_no_answers() {
        verify(!find(inviteCard, "inviteCancelled").visible)
        inviteCard.invite = cardWith({ cancelled: true, offersRsvp: false })
        verify(find(inviteCard, "inviteCancelled").visible)
        compare(find(inviteCard, "inviteCancelled").text, "Cancelled")
        verify(!find(inviteCard, "rsvp_ACCEPTED").visible)
    }

    function test_join_meeting_names_the_host() {
        verify(!find(inviteCard, "inviteJoin").visible)
        inviteCard.invite = cardWith({ joinUrl: "https://meet.example.com/j/1", joinHost: "meet.example.com" })
        const join = find(inviteCard, "inviteJoin")
        verify(join.visible)
        compare(join.text, "Join meeting (meet.example.com)")
        AppLock.locked = true
        verify(!join.enabled)
    }

    // An update to an answered invite: the notice says what changed, the
    // buttons ask again, and the answered revision shows its note.
    function test_an_update_asks_again_and_an_answer_is_noted() {
        inviteCard.invite = cardWith({ notices: ["Changed since you answered: place. You had accepted; answer again."] })
        verify(find(inviteCard, "inviteNotices").text.indexOf("Changed since you answered") === 0)
        verify(find(inviteCard, "rsvp_ACCEPTED").enabled)
        inviteCard.invite = cardWith({ notices: [], note: "You accepted." })
        compare(find(inviteCard, "rsvpMessage").text, "You accepted.")
        // The outcome of an answer wins over the note.
        Invites.states = { "7": { answerable: false, sending: false, message: "Answered through KyCalendar." } }
        compare(find(inviteCard, "rsvpMessage").text, "Answered through KyCalendar.")
    }

    function test_a_conflict_offers_a_reload() {
        verify(!find(inviteCard, "inviteReload").visible)
        Invites.states = { "7": { answerable: true, sending: false, reload: true,
                                  message: "The event changed since this invitation. Reload it and answer again." } }
        const reload = find(inviteCard, "inviteReload")
        verify(reload.visible)
        verify(waitForRendering(inviteCard))
        mouseClick(reload)
        compare(reloadSpy.count, 1)
    }
}
