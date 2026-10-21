.pragma library
// Android's store-screenshot mailbox (kypost-android scripts/store-screenshots/seed-mail.mjs),
// on a fixed clock so every run renders the same times.
var now = new Date(2026, 9, 20, 14, 23, 0)
var me = "Sam Rivera <sam@kypost-demo.local>"

function ago(min) { return new Date(now.getTime() - min * 60000) }

// [minutesAgo, folder, keywords, unread, flagged, from, subject, body]
var mail = [
    [12, "INBOX", ["Primary"], true, false, "Maya Chen <maya.chen@example.com>", "Saturday hike: trailhead at 8?",
     "Hey Sam,\n\nForecast looks clear for Saturday. Want to do the Ridge Loop? I can pick you up around 7:30 and we'd be at the trailhead by 8.\n\nI'll bring coffee if you bring the good trail mix.\n\nMaya"],
    [47, "INBOX", ["Primary"], true, false, "Jordan Ellis <jordan@northwind.example>", "Re: Q4 roadmap draft",
     "Thanks for pulling this together. Two notes:\n\n1. Let's move the self-hosted backup work up to October. It unblocks the migration.\n2. The mobile release can slip a week if QA needs it.\n\nOtherwise this looks great. Can we review Thursday at 2?\n\nJordan"],
    [95, "INBOX", ["Updates"], true, false, "Parcelwise <tracking@parcelwise.example>", "Your package arrives today",
     "Good news! Your order is out for delivery and should arrive by 6 PM.\n\nItem: 2x 8TB NAS drives\nCarrier: Parcelwise Ground\n\nTrack it any time from your Parcelwise account."],
    [140, "INBOX", ["Primary"], false, false, "Leo Martins <leo@martins.example>", "Re: NAS upgrade parts list",
     "That list looks solid. I'd swap the 4GB stick for 8GB if the board supports it. ZFS will happily use the extra RAM.\n\nHappy to come over and help with the build next weekend.\n\nLeo"],
    [210, "INBOX", ["Social"], true, false, "Elm Street Garden Club <hello@elmgarden.example>", "Seed swap this Sunday 🌱",
     "Hi neighbors,\n\nOur fall seed swap is this Sunday from 10 to 1 at the community plot. Bring labeled seeds, cuttings or just your curiosity.\n\nThere will be cider!\n\nThe Garden Club"],
    [300, "INBOX", ["Updates"], false, false, "Harbor Credit Union <statements@harborcu.example>", "Your October statement is ready",
     "Your monthly statement for checking account ending in 4821 is now available.\n\nSign in to online banking to view it. We will never ask for your password by email."],
    [420, "INBOX", ["Primary"], false, true, "Dr. Okafor's Office <frontdesk@okaforclinic.example>", "Appointment reminder: Thu 10:30 AM",
     "This is a reminder of your appointment on Thursday at 10:30 AM.\n\nPlease arrive 10 minutes early. Reply to this email if you need to reschedule."],
    [560, "INBOX", ["Promotions"], true, false, "Fieldnotes Coffee <roasts@fieldnotes.example>", "20% off fall roasts this week",
     "Our Maple Ridge and Harvest Blend are back.\n\nUse code FALL20 for 20% off whole beans through Sunday."],
    [720, "INBOX", ["Updates"], false, false, "Homelab Git <git@homelab.example>", "[homelab/infra] PR #42 merged: rotate backup keys",
     "Pull request #42 \"rotate backup keys\" was merged into main by sam.\n\n3 files changed, 41 insertions(+), 12 deletions(-)"],
    [900, "INBOX", ["Social"], false, false, "Riverside Book Club <club@riversidebooks.example>", "Next month's pick is in",
     "We voted, and next month we're reading \"The Ministry for the Future.\"\n\nWe meet the 3rd Tuesday at 7 PM at the library. See you there!"],
    [1300, "INBOX", ["Promotions"], false, false, "Trailhead Outfitters <news@trailhead.example>", "New arrivals: insulated layers for fall",
     "Stay warm on the trail. Our new insulated jackets and fleece layers just landed.\n\nFree shipping on orders over $75."],
    [1500, "INBOX", ["Primary"], false, false, "Priya Raman <priya@raman.example>", "Cabin photos are up",
     "Finally uploaded everything from the lake weekend. The sunset ones came out amazing.\n\nShared album link is in our group chat. Same time next year?\n\nPriya"],
    [1600, "Sent", [], false, false, me, "Q4 roadmap draft",
     "Hi Jordan,\n\nAttached is the first pass at the Q4 roadmap. Let me know what you'd shuffle.\n\nSam"],
]

function row(m, i) {
    var body = m[7]
    return {
        messageId: "<playshot-" + i + "@kypost-demo.local>", folder: m[1], sender: m[5],
        sentTo: m[1] === "Sent" ? "Jordan Ellis <jordan@northwind.example>" : me,
        cc: "", bcc: "", subject: m[6], preview: body.replace(/\s+/g, " ").slice(0, 140),
        body: body, bodyMode: "plain", label: m[2].length ? m[2][0] : "",
        keywords: m[2], status: m[3] ? "unread" : "seen", atUtc: ago(m[0]),
        hasAttachments: false, sourceMode: "", pgpMarker: "", pgpMarkerAccessibleName: "",
        unread: m[3], woken: false, accessibleName: m[6] + ", " + m[5], pgpState: 0,
        searchOnly: false, bodyPending: false, bodyNotice: "", canDecryptHere: false,
        pgpBannerTitle: "", pgpBannerBody: "", pgpReadAction: "", webmailUrl: ""
    }
}

function rows(folder, keyword) {
    var out = []
    for (var i = 0; i < mail.length; ++i) {
        var m = mail[i]
        if (m[1] !== folder) continue
        if (keyword && m[2].indexOf(keyword) < 0) continue
        out.push(row(m, i))
    }
    return out
}

function find(messageId) {
    for (var i = 0; i < mail.length; ++i)
        if ("<playshot-" + i + "@kypost-demo.local>" === messageId) return row(mail[i], i)
    return ({})
}

var keywords = ["Primary", "Updates", "Social", "Promotions"]

function unread(keyword) {
    var n = 0
    for (var i = 0; i < mail.length; ++i)
        if (mail[i][1] === "INBOX" && mail[i][3] && (!keyword || mail[i][2].indexOf(keyword) >= 0)) ++n
    return n
}

// Android's demo address book, with Secure key / No key as in the store shots.
var contacts = [
    {uid: "c1", fn: "Amara Okafor", givenName: "Amara", familyName: "Okafor", org: "Okafor Family Medicine", email: "amara@okaforclinic.example", phone: "+1 555 0101", key: true},
    {uid: "c2", fn: "Dana Whitfield", givenName: "Dana", familyName: "Whitfield", org: "Elm Street Garden Club", email: "dana@elmgarden.example", phone: "+1 555 0142", key: false},
    {uid: "c3", fn: "Ethan Brooks", givenName: "Ethan", familyName: "Brooks", org: "Harbor Credit Union", email: "ethan@harborcu.example", phone: "+1 555 0163", key: false},
    {uid: "c4", fn: "Jordan Ellis", givenName: "Jordan", familyName: "Ellis", org: "Northwind", title: "Engineering Manager", email: "jordan@northwind.example", phone: "+1 555 0177", key: true},
    {uid: "c5", fn: "Leo Martins", givenName: "Leo", familyName: "Martins", org: "", email: "leo@martins.example", phone: "", key: true},
    {uid: "c6", fn: "Maya Chen", givenName: "Maya", familyName: "Chen", org: "Ridgeline Design", email: "maya.chen@example.com", phone: "+1 555 0188", key: true},
]

function contact(uid) {
    for (var i = 0; i < contacts.length; ++i) {
        var c = contacts[i]
        if (c.uid !== uid) continue
        return {
            uid: c.uid, rev: "1", fn: c.fn, givenName: c.givenName, familyName: c.familyName,
            middleName: "", prefix: "", suffix: "", nickname: "", org: c.org, title: c.title || "",
            department: "", notes: "", birthday: "", pronouns: "", isSelf: false, photoRef: "",
            phoneticGivenName: "", phoneticFamilyName: "",
            pgpKey: c.key ? "-----BEGIN PGP PUBLIC KEY BLOCK-----\n(fixture)\n-----END PGP PUBLIC KEY BLOCK-----" : "",
            emails: c.email ? [{label: "work", value: c.email}] : [],
            phones: c.phone ? [{label: "mobile", value: c.phone}] : [],
            addresses: [], ims: [], websites: [], events: [], relations: [], customFields: [], groupIds: []
        }
    }
    return ({})
}
