import QtQuick
import "Fixture.js" as F
import KyPostScreens

// Fixture stand-in for MailController: same members app/qml reads.
QtObject {
    id: app

    property ListModel emailModel: ListModel {}
    property ListModel searchModel: ListModel {}
    property bool searchActive: false
    property bool searchBusy: false
    property string searchQuery: ""
    property bool isBusy: false
    property string lastError: ""
    property var lastSyncAt: new Date()
    property string selectedKeyword: ""
    property string currentFolder: "INBOX"
    property string draftsFolder: "Drafts"
    property string archiveFolder: "Archive"
    property string snoozedFolder: "Snoozed"
    property var keywordTabs: F.keywords.map(function (k) { return {name: k, unread: empty ? 0 : F.unread(k)} })
    property int currentFolderUnread: empty ? 0 : F.unread("")
    property var unreadCounts: ({"INBOX": F.unread(""), "Drafts": 0, "Junk": 0, "Sent": 0, "Trash": 0, "Archive": 0, "Snoozed": 0})
    property bool empty: false

    property bool decryptBusy: false
    property var decryptedAttachments: []
    property string decryptedFolder: ""
    property string decryptedHtml: ""
    property string decryptedImageBase: ""
    property string decryptedMessageId: ""
    property string decryptedPlain: ""
    property string decryptedSignature: ""
    property bool decryptedSignatureIsWarning: false
    property string decryptedSubject: ""
    property bool decryptedHasSubject: false
    property string decryptedToken: ""
    property string decryptFailure: ""
    property bool decryptRetryable: false
    property bool pgpCanEncrypt: false
    property bool pgpCanSign: false
    property bool pgpCanSendFromThisDevice: false
    property var pgpKeylessRecipients: []
    property bool lastSendMissingSentCopy: false
    property string linkAnchorScript: ""
    property var linkTarget: ({})


    signal actionCompleted(string action, var ids, bool ok)
    signal attachmentDownloaded()
    signal attachmentsListed()
    signal decryptedChanged()
    signal draftSaveCompleted()
    signal folderMutationCompleted(bool ok)
    signal foldersChanged()
    signal forwardComposeRequested(var draft)
    signal forwardFinished(bool composerBusy)
    signal notificationEmailAmbiguous(string messageId, var folders)
    signal notificationEmailResolved(string messageId, string folder)
    signal openEmailRequested(string messageId)
    signal pickupFallbackRequired()
    signal readStateChanged()
    signal replyThreadingRefused()
    signal searchBodyLoaded(string folder, string messageId)
    signal searchChanged()
    signal sendCompleted()
    signal sendWarning(string warning)

    function reload() {
        emailModel.clear()
        if (empty) return
        var r = F.rows(currentFolder, selectedKeyword)
        for (var i = 0; i < r.length; ++i) emailModel.append(r[i])
    }
    Component.onCompleted: reload()

    function refresh() {}
    function refreshFolders() {}
    function selectFolder(f) { currentFolder = f; selectedKeyword = ""; reload() }
    function selectKeyword(k) { selectedKeyword = k; reload() }
    function mailFolders() {
        var names = ["INBOX", "Drafts", "Junk", "Sent", "Trash", "Archive"]
        var out = names.map(function (n) {
            return {wireName: n, displayName: ScreenRun.folderDisplayName(n), depth: 0, deletable: false, isStandard: true}
        })
        out.push({wireName: "Snoozed", displayName: "Snoozed", depth: 0, deletable: false, isStandard: true})
        return out
    }
    function loadOlder() {}
    function setSearchQuery(q) {
        searchQuery = q; searchActive = q.length > 0; searchModel.clear()
        var r = F.rows("INBOX", "")
        for (var i = 0; i < r.length; ++i)
            if ((r[i].subject + r[i].sender + r[i].body).toLowerCase().indexOf(q.toLowerCase()) >= 0) searchModel.append(r[i])
        searchChanged()
    }
    function clearSearch() { setSearchQuery("") }
    function loadSearchBody() {}
    function findByMessageId(id) { return F.find(id) }
    property string openedFromNotification: ""
    function openFromNotification(id) { openedFromNotification = id }
    function markRead() {}
    function markUnread() {}
    function markSpam() {}
    function archiveEmails() {}
    function deleteEmails() {}
    function moveEmails() {}
    function snoozeEmails() {}
    function unsnoozeEmails() {}
    function snoozePresets() { return [{label: "Later today", when: "18:00"}, {label: "Tomorrow", when: "08:00"}] }
    function snoozeCustomTime() { return F.now }
    function createFolder() {}
    function renameFolder() {}
    function deleteFolder() {}
    function sendMail() { return true }
    function sendClientEncrypted() { return true }
    function saveDraft() {}
    function reopenDraft() {}
    function reopenDecryptedDraft() {}
    function releaseDraft() {}
    function forwardWithAttachments() { return false }
    function discardPendingSend() {}
    function confirmPickupFallbackSend() {}
    function preflightRecipients() { return ({}) }
    function replyAllRecipients() { return ({to: [], cc: []}) }
    function loadAccountAddresses() { return [F.me] }
    function listAttachments() {}
    function downloadAttachment() {}
    function saveDecryptedAttachment() {}
    function openDecryptedAttachmentTemporarily() {}
    function decryptMessage() {}
    function forgetDecrypted() {}
    function refreshPgpComposeState() {}
    function pgpHandoffToWebmail() {}
    function openWebmailDrafts() {}
    function notificationHistory() { return [] }
    function allKeywordSettings() { return F.keywords.map(function (k) { return {keyword: k, visible: true} }) }
    function setKeywordOrder() {}
    function setKeywordVisible() {}
}
