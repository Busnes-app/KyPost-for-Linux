import QtQuick 2.15
import QtTest 1.15
import com.kysecurity.mail 1.0
import "qrc:/qml/pages" as Pages

// The approval page must not single out the right number, must send exactly
// the number tapped, must render sign-in context as plain text, and must not
// answer while the app is locked. A press answers the challenge it started
// on, or nothing.
TestCase {
    id: testCase
    name: "MfaApproval"
    when: windowShown
    visible: true
    width: 420
    height: 720

    Component {
        id: pageComponent
        Pages.MfaApproval { width: testCase.width; height: testCase.height }
    }

    function init() {
        AppLock.locked = false
        Mfa.hasChallenge = true
        Mfa.challengeId = "0123456789abcdef0123456789abcdef0123456789abcdef"
        Mfa.choices = ["42", "07", "93"]
        Mfa.ipAddress = "203.0.113.9"
        Mfa.userAgent = "Firefox"
        Mfa.inFlight = false
        Mfa.secondsRemaining = 300
        Mfa.lastApproved = ""
        Mfa.denyCount = 0
        Mfa.lastAnsweredId = ""
    }

    function cleanup() {
        AppLock.locked = false
    }

    function collect(item, name, out) {
        if (item.objectName === name)
            out.push(item)
        for (let i = 0; i < item.children.length; ++i)
            collect(item.children[i], name, out)
        return out
    }

    function createPage() {
        const page = createTemporaryObject(pageComponent, testCase)
        verify(page !== null)
        verify(waitForRendering(page))
        return page
    }

    function test_three_choices_styled_identically() {
        const page = createPage()
        const buttons = collect(page, "mfaChoice", [])
        compare(buttons.length, 3)
        compare(buttons.map(b => b.text), ["42", "07", "93"], "shown in the order the controller shuffled")
        for (let i = 1; i < buttons.length; ++i) {
            compare(buttons[i].color, buttons[0].color, "no choice may be highlighted")
            compare(buttons[i].border.color, buttons[0].border.color)
            compare(buttons[i].opacity, buttons[0].opacity)
            compare(buttons[i].width, buttons[0].width)
        }
    }

    function test_tap_sends_that_number() {
        const page = createPage()
        const buttons = collect(page, "mfaChoice", [])
        mouseClick(buttons[2])
        compare(Mfa.lastApproved, "93")
        compare(Mfa.lastAnsweredId, Mfa.challengeId, "the answer names the challenge on screen")
    }

    function test_deny() {
        const page = createPage()
        mouseClick(collect(page, "mfaDeny", [])[0])
        compare(Mfa.denyCount, 1)
        compare(Mfa.lastApproved, "")
        compare(Mfa.lastAnsweredId, Mfa.challengeId, "the answer names the challenge on screen")
    }

    function test_context_is_plain_text() {
        Mfa.ipAddress = "<img src=\"http://example.invalid/x.png\">"
        Mfa.userAgent = "<a href=\"http://example.invalid\">Chrome</a>"
        const page = createPage()
        const ip = collect(page, "mfaIpAddress", [])[0]
        const ua = collect(page, "mfaUserAgent", [])[0]
        compare(ip.textFormat, Text.PlainText)
        compare(ua.textFormat, Text.PlainText)
        compare(ip.text, Mfa.ipAddress)
    }

    function test_locked_app_cannot_answer() {
        const page = createPage()
        AppLock.locked = true
        const buttons = collect(page, "mfaChoice", [])
        for (const b of buttons)
            verify(!b.enabled)
        const deny = collect(page, "mfaDeny", [])[0]
        verify(!deny.enabled)
        mouseClick(buttons[0])
        mouseClick(deny)
        compare(Mfa.lastApproved, "")
        compare(Mfa.denyCount, 0)
    }

    readonly property string replacementId: "fedcba9876543210fedcba9876543210fedcba9876543210"

    function test_replacement_during_held_choice_gets_no_decision() {
        const page = createPage()
        const buttons = collect(page, "mfaChoice", [])
        mousePress(buttons[0])
        // The shown challenge expires under the finger and a pending one with
        // the same numbers takes its place.
        Mfa.challengeId = testCase.replacementId
        Mfa.choices = ["42", "07", "93"]
        const after = collect(page, "mfaChoice", [])
        verify(after[0] === buttons[0], "unchanged choices keep the pressed button alive")
        mouseRelease(buttons[0])
        compare(Mfa.lastApproved, "", "the replacement must not receive the held tap")
        compare(Mfa.lastAnsweredId, "")
    }

    function test_replacement_during_held_deny_gets_no_decision() {
        const page = createPage()
        const deny = collect(page, "mfaDeny", [])[0]
        mousePress(deny)
        Mfa.challengeId = testCase.replacementId
        mouseRelease(deny)
        compare(Mfa.denyCount, 0, "the replacement must not receive the held deny")
        compare(Mfa.lastAnsweredId, "")
    }

    function test_expiry_during_held_gesture_gets_no_decision() {
        const page = createPage()
        const buttons = collect(page, "mfaChoice", [])
        mousePress(buttons[1])
        Mfa.secondsRemaining = 0
        mouseRelease(buttons[1])
        compare(Mfa.lastApproved, "")
        compare(Mfa.lastAnsweredId, "")
    }

    function test_voided_press_stays_void_if_challenge_returns() {
        const page = createPage()
        const original = Mfa.challengeId
        const deny = collect(page, "mfaDeny", [])[0]
        mousePress(deny)
        Mfa.challengeId = testCase.replacementId
        Mfa.challengeId = original
        mouseRelease(deny)
        compare(Mfa.denyCount, 0)
    }

    function test_new_press_after_replacement_answers_replacement() {
        const page = createPage()
        const buttons = collect(page, "mfaChoice", [])
        mousePress(buttons[0])
        Mfa.challengeId = testCase.replacementId
        mouseRelease(buttons[0])
        mouseClick(buttons[0])
        compare(Mfa.lastApproved, "42")
        compare(Mfa.lastAnsweredId, testCase.replacementId, "a fresh press after the swap answers what is shown")
    }
}
