import QtQuick
import QtTest
import com.kysecurity.mail 1.0

// The reader's half of the invite card: it loads its own message's invite
// once per open, takes only its own request's card, and forgets the card when
// it moves on, so no answer is left being asked about for a card nobody shows.
TestCase {
    name: "ShellInvites"
    when: windowShown

    property var shell: null
    readonly property string first: "<playshot-0@kypost-demo.local>"
    readonly property string second: "<playshot-1@kypost-demo.local>"
    readonly property var listing: [{index: 1, name: "invite.ics", mimeType: "text/calendar", size: 900,
                                     calendarMethod: "REQUEST"}]
    readonly property var card: ({token: 7, eyebrow: "Calendar invite · REQUEST", title: "Launch review",
                                  details: [], notices: [], offersRsvp: true})

    function initTestCase() {
        const component = Qt.createComponent("qrc:/qml/AppShell.qml")
        compare(component.status, Component.Ready, component.errorString())
        shell = component.createObject(null, {width: 1920, height: 1080})
        shell.show()
        waitForRendering(shell.contentItem)
    }

    function cleanupTestCase() { shell.destroy() }

    function findReader(item, id) {
        if (item.replyQuote !== undefined && item.messageId === id)
            return item
        const kids = item.contentItem ? [item.contentItem] : (item.children || [])
        for (let i = 0; i < kids.length; ++i) {
            const found = findReader(kids[i], id)
            if (found) return found
        }
        return null
    }

    function loads() { return Invites.calls.filter(function (c) { return c.name === "load" }) }

    function open(id) {
        shell.selectEmail(id, "INBOX")
        let reader = null
        tryVerify(function () { reader = findReader(shell, id); return reader !== null })
        return reader
    }

    function test_a_reader_loads_takes_and_forgets_only_its_own_card() {
        Invites.calls = []
        const reader = open(first)
        MailApp.attachmentsListed("INBOX", first, listing)
        compare(loads().length, 1)
        compare(loads()[0].args[1], first)
        // Another reader listing the same message does not reopen this one's invite.
        MailApp.attachmentsListed("INBOX", first, listing)
        compare(loads().length, 1)
        // Nor does another message's listing.
        MailApp.attachmentsListed("INBOX", second, listing)
        compare(loads().length, 1)

        // Another reader's card is not this one's.
        Invites.inviteLoaded(reader.inviteRequest + 100, card)
        compare(reader.invite.token, undefined)
        Invites.inviteLoaded(reader.inviteRequest, card)
        compare(reader.invite.token, 7)

        // Moving on forgets the card, releasing anything still being asked.
        const moved = open(second)
        verify(Invites.calls.some(function (c) { return c.name === "forget" && c.args[0] === 7 }))
        compare(moved.invite.token, undefined)
        compare(moved.inviteRequest, 0)
    }
}
