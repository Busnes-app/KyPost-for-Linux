import QtQuick
import QtTest

// The roots need every C++ singleton, so their search wiring is checked in
// the shipped source, like tst_ComposeSeeding. Behaviour is in MailControllerTest.
TestCase {
    name: "SearchAndDrafts"

    function read(path) {
        var source = new XMLHttpRequest()
        source.open("GET", path, false)
        source.send()
        compare(source.status, 200, path)
        return source.responseText
    }

    function test_both_roots_show_search_results_in_the_mail_list() {
        for (var root of ["DesktopRoot", "MobileRoot"]) {
            var text = read("qrc:/qml/" + root + ".qml")
            verify(text.indexOf("model: MailApp.searchActive ? MailApp.searchModel : MailApp.emailModel") !== -1, root)
            // The lock gating lives in the component; tst_SearchLock tests it.
            verify(text.indexOf("MailSearchField {") !== -1, root)
            verify(text.indexOf("visible: !MailApp.searchActive // keywords filter the folder") !== -1, root)
        }
        verify(read("qrc:/qml/DesktopRoot.qml").indexOf("findShortcutEnabled: root.currentSection === \"mail\"") !== -1)
        var field = read("qrc:/qml/components/MailSearchField.qml")
        verify(field.indexOf("function onTextEdited() { MailApp.setSearchQuery(field.text) }") !== -1)
        verify(field.indexOf("MailApp.clearSearch()") !== -1)
    }

    // A window shortcut fires under the PIN overlay whatever has focus.
    function test_no_root_shortcut_fires_while_locked() {
        var text = read("qrc:/qml/DesktopRoot.qml")
        var blocks = text.split("Shortcut {").slice(1)
        verify(blocks.length >= 3)
        for (var block of blocks) {
            var head = block.slice(0, block.indexOf("onActivated"))
            verify(head.indexOf("!AppLock.locked") !== -1, head)
        }
        var mobile = read("qrc:/qml/MobileRoot.qml")
        verify(mobile.indexOf('objectName: "mobileSearchToggle"\n                    enabled: !AppLock.locked') !== -1)
    }

    function test_reader_loads_search_bodies_and_reopens_plaintext_drafts() {
        var text = read("qrc:/qml/pages/EmailDetail.qml")
        verify(text.indexOf("MailApp.loadSearchBody(root.folder, root.messageId)") !== -1)
        verify(text.indexOf("function onSearchBodyLoaded(folder, messageId)") !== -1)
        verify(text.indexOf("const draft = MailApp.reopenDraft(root.folder, root.messageId)") !== -1)
        // Drafts do not reopen under the PIN overlay; a held-back body loads on unlock.
        verify(text.split("enabled: !MailApp.isBusy && !AppLock.locked").length === 3)
        verify(text.indexOf("if (root.email.bodyPending)\n                MailApp.loadSearchBody(root.folder, root.messageId)") !== -1)
        // A search hit with no readable body may be an encrypted draft.
        verify(text.indexOf('root.folder === "Drafts" && root.email.pgpState === 0 && (!root.email.searchOnly || !!root.email.body)') !== -1)
    }

    function test_compose_no_longer_claims_saving_makes_a_copy() {
        var text = read("qrc:/qml/pages/Compose.qml")
        verify(text.indexOf("Saving creates another draft. Inline images") === -1)
        verify(text.indexOf("The first save or send replaces the original draft") !== -1)
    }
}
