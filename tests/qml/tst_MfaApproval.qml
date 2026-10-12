import QtQuick 2.15
import QtTest 1.15
import com.kysecurity.mail 1.0
import "qrc:/qml/pages" as Pages

// The approval page must not single out the right number, must send exactly
// the number tapped, must render sign-in context as plain text, and must not
// answer while the app is locked.
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
        Mfa.choices = ["42", "07", "93"]
        Mfa.ipAddress = "203.0.113.9"
        Mfa.userAgent = "Firefox"
        Mfa.inFlight = false
        Mfa.lastApproved = ""
        Mfa.denyCount = 0
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
    }

    function test_deny() {
        const page = createPage()
        mouseClick(collect(page, "mfaDeny", [])[0])
        compare(Mfa.denyCount, 1)
        compare(Mfa.lastApproved, "")
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
}
