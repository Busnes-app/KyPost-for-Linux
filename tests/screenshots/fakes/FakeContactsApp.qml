import QtQuick
import "Fixture.js" as F

QtObject {
    property ListModel contactModel: ListModel {}
    property bool isBusy: false
    property string lastError: ""
    property string statusMessage: ""
    property int photoRevision: 0
    property bool empty: false
    function reload() {
        contactModel.clear()
        if (empty) return
        for (var i = 0; i < F.contacts.length; ++i) {
            var c = F.contacts[i]
            contactModel.append({uid: c.uid, rev: "1", fn: c.fn, givenName: c.givenName, familyName: c.familyName,
                                 org: c.org, notes: "", primaryEmail: c.email, primaryPhone: c.phone,
                                 synced: true, photoRef: "", isSelf: false, hasPgpKey: c.key})
        }
    }
    Component.onCompleted: reload()
    function allGroups() { return [] }
    function contactAt(uid) { return F.contact(uid) }
    function createContact() { return "c-new" }
    function updateContact() { return true }
    function deleteContact() { return true }
    function dedupe() {}
    function isSynced() { return true }
    function load() {}
    function sync() {}
    function searchContacts(q) {
        return F.contacts.filter(function (c) { return c.fn.toLowerCase().indexOf(q.toLowerCase()) >= 0 })
                         .map(function (c) { return {name: c.fn, email: c.email} })
    }
    function photoPathFor() { return "" }
}
