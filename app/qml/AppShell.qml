import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami 2.20 as Kirigami
import com.kysecurity.mail 1.0
import "components"
import "pages"
import "utils/format.js" as Format
import "utils/layout.js" as Breakpoints

// The one window, Android's adaptive shell. The layout follows the window's
// width at runtime (utils/layout.js): a bottom bar below 600px, a left rail
// from 600, and list | detail panes at 0.4 from 840. Each destination keeps
// Android's header (logo, wordmark, current place in monospace caps).
//
// General.isDesktopMode no longer picks a layout. It still picks platform
// behaviour: pop-out windows and window-visibility locking on the desktop,
// application-state locking on a phone.
Kirigami.ApplicationWindow {
    id: root
    width: 1024
    height: 768
    color: Theme.bg
    minimumWidth: 320
    minimumHeight: 480
    visible: true
    title: "KyPost" // product name -- not translated
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.None

    readonly property string layoutMode: Breakpoints.mode(width)
    // Read by tests: these popups paint above the lock overlay.
    readonly property bool foldersOpen: folderDrawer.opened
    readonly property bool pairingConfirmOpen: pairingConfirmPopup.opened
    readonly property bool folderActionsOpen: folderActionsMenu.opened
    readonly property bool scheduledOpen: scheduledSheet.opened
    readonly property bool pgpMyQrCodeOpen: pgpMyQrCodeSheet.opened
    readonly property bool pgpScanOpen: pgpScanContactKeySheet.opened
    readonly property bool split: layoutMode === "expanded"

    // Minimize to tray instead of quitting, when both are enabled (System
    // Tray settings pane; TrayController only exists in desktop mode).
    onClosing: (close) => {
        if (General.trayIconEnabled && General.minimizeToTrayOnClose) {
            close.accepted = false
            root.visible = false
            // Hiding to tray is the desktop equivalent of Android's onStop.
            // lockAfterGrace(), not lockNow(): the grace period defaults to 0.
            AppLock.lockAfterGrace()
            return
        }
        // Closing the window quits: a message in the compose pane is asked
        // about first, as on every other way out (owner decision Q1). The
        // close is tried again once the composer is gone.
        if (root.detailMode === "compose" && composeLoader.item && composeLoader.item.mayHaveContent()) {
            close.accepted = false
            root.leaveCompose(function () { root.close() })
        }
    }

    // Re-lock whenever the window is hidden or minimised by any route, not
    // only the minimise-to-tray path above.
    onVisibleChanged: {
        if (!General.isDesktopMode)
            return
        if (!root.visible)
            AppLock.lockAfterGrace()
        else
            AppLock.cancelPendingLock()
    }
    onWindowStateChanged: {
        if (!General.isDesktopMode)
            return
        if (root.windowState === Qt.WindowMinimized)
            AppLock.lockAfterGrace()
        else if (root.visible)
            AppLock.cancelPendingLock()
    }

    // A phone backgrounds the app instead of hiding a window: lock when it
    // leaves the foreground, as Android's onStop does.
    Connections {
        target: Qt.application
        enabled: !General.isDesktopMode
        function onStateChanged() {
            if (Qt.application.state !== Qt.ApplicationActive)
                AppLock.lockAfterGrace()
            else
                AppLock.cancelPendingLock()
        }
    }

    // ---- navigation and selection state ----------------------------------
    property string destination: "inbox" // "inbox" | "contacts" | "settings"
    readonly property string currentSection: destination === "contacts" ? "contacts" : "mail"
    property string currentFolder: "INBOX" // wire folder name
    property string selectedMessageId: ""
    property string selectedEmailFolder: ""
    property string selectedContactUid: ""
    // "empty" | "email" | "contact" | "compose": what the detail pane shows.
    // Independent of the selection ids so an in-progress Compose is not
    // clobbered by whatever selection happens to be sitting there.
    property string detailMode: "empty"
    property var composeSeed: ({ to: "", subject: "", body: "" })
    property bool searchOpen: false
    // Below 840 the detail covers the list; at 840 and up they sit side by side.
    readonly property bool detailOpen: detailMode !== "empty"
    readonly property string navCurrent: detailMode === "compose" ? "compose" : destination

    function folderDisplayName(wireName) {
        return Format.folderDisplayName(MailApp.mailFolders(), wireName)
    }

    // What the header's monospace line says.
    // At 840px+ compose opens beside the list, so the header keeps naming
    // the list; below that compose covers it and the header says so.
    readonly property string place: detailMode === "compose" && !split ? i18n("Compose")
        : destination === "contacts" ? i18n("Contacts")
        : destination === "settings" ? i18n("Settings")
        : root.folderDisplayName(root.currentFolder)

    // Runs proceed() at once, unless the compose pane holds something the
    // user wrote: then it asks "Discard this message?" first (owner decision
    // Q1). Save draft leaves once the draft has landed, Discard leaves now,
    // Keep writing stays and runs cancel(). Either way out closes the composer
    // BEFORE proceed(), so a guarded function called from proceed() passes.
    property var pendingLeave: null
    property var pendingLeaveCancel: null
    function leaveCompose(proceed, cancel) {
        const composer = composeLoader.item
        if (root.detailMode !== "compose" || !composer) {
            proceed()
            return
        }
        composer.checkHasContent(function (hasContent) {
            if (composeLoader.item !== composer || root.detailMode !== "compose") {
                if (cancel) cancel() // the pane changed while this was asked
                return
            }
            if (!hasContent) {
                root.detailMode = "empty"
                proceed()
                return
            }
            if (root.pendingLeaveCancel) root.pendingLeaveCancel() // superseded
            root.pendingLeave = proceed
            root.pendingLeaveCancel = cancel || null
            leaveComposeDialog.open()
        })
    }

    function navigate(target) {
        if (target === "lock") {
            AppLock.lockNow()
            return
        }
        if (root.detailMode === "compose" && target !== "compose") {
            root.leaveCompose(function () { root.navigate(target) })
            return
        }
        if (target === "compose") {
            root.openCompose("", "", "")
            return
        }
        if (target === "inbox" && root.destination === "inbox" && !root.detailOpen) {
            MailApp.refresh() // re-tapping Inbox at the top refreshes, as on Android
            return
        }
        root.destination = target
        root.closeDetail()
        if (target === "settings")
            settingsPane.refreshSnapshots()
    }

    function selectFolder(wireName) {
        if (root.detailMode === "compose") {
            root.leaveCompose(function () { root.selectFolder(wireName) })
            return
        }
        root.destination = "inbox"
        root.currentFolder = wireName
        root.closeDetail()
        MailApp.selectFolder(wireName)
    }

    function selectContactsSection() {
        if (root.detailMode === "compose") {
            root.leaveCompose(root.selectContactsSection)
            return
        }
        root.destination = "contacts"
        root.closeDetail()
    }

    function selectEmail(messageId, folder) {
        if (root.detailMode === "compose") {
            root.leaveCompose(function () { root.selectEmail(messageId, folder) })
            return
        }
        root.destination = "inbox"
        root.selectedMessageId = messageId
        root.selectedEmailFolder = folder
        root.detailMode = "email"
    }

    // A notification names a message in any folder. The reader's mailbox
    // actions act on MailApp.currentFolder and are off anywhere else (F3), so
    // the list moves to the message's folder first.
    function openEmailInItsFolder(messageId, folder) {
        if (root.detailMode === "compose") {
            root.leaveCompose(function () { root.openEmailInItsFolder(messageId, folder) })
            return
        }
        if (folder !== MailApp.currentFolder)
            root.selectFolder(folder)
        root.selectEmail(messageId, folder)
    }

    function selectContact(uid) {
        if (root.detailMode === "compose") {
            root.leaveCompose(function () { root.selectContact(uid) })
            return
        }
        root.destination = "contacts"
        root.selectedContactUid = uid
        root.detailMode = "contact"
    }

    // Seeds composeSeed BEFORE flipping detailMode: the compose Loader reads
    // the seed only when it instantiates a fresh Compose.
    function openCompose(to, subject, body, replyTo, notice) {
        if (root.detailMode === "compose") {
            root.leaveCompose(function () { root.openCompose(to, subject, body, replyTo, notice) })
            return
        }
        if (root.destination === "settings")
            root.destination = "inbox"
        root.detailMode = "empty" // recreate the editor before applying another seed
        root.composeSeed = { to: to || "", subject: subject || "", body: body || "", replyTo: replyTo || ({}),
                             notice: notice || "" }
        root.detailMode = "compose"
    }

    // Keep writing releases the session it was handed.
    function openRestoredDraft(draft) {
        if (root.detailMode === "compose") {
            root.leaveCompose(function () { root.openRestoredDraft(draft) },
                              function () { MailApp.releaseDraft(draft.token) })
            return
        }
        root.detailMode = "empty" // recreate the editor before applying another seed
        root.composeSeed = { to: "", subject: "", body: "", draft: draft }
        root.detailMode = "compose"
    }

    function forwardNotOpened() {
        sendWarningToast.show(i18n("The forward was not opened: finish or close the message you are writing first."))
    }

    function closeDetail() {
        root.detailMode = "empty"
        root.selectedMessageId = ""
        root.selectedContactUid = ""
    }

    function closeOverlays() {
        settingsPane.closePopups()
        pairingConfirmPopup.close()
        folderDrawer.close()
        folderActionsMenu.close()
        scheduledSheet.close()
        pgpMyQrCodeSheet.close()
        pgpScanContactKeySheet.close()
    }

    // Entry points the drawer, header and the screenshot harness share.
    function openFolders() { folderDrawer.open() }
    function openScheduled() { scheduledSheet.open() }
    function openPgpMyQrCode() { pgpMyQrCodeSheet.open() }
    function openPgpScan() {
        pgpScanContactKeySheet.targetContactDetail = null
        pgpScanContactKeySheet.open()
    }
    function openSettingsPane(pane) {
        root.navigate("settings")
        settingsPane.showPane(pane)
    }
    // Once out of any composer: the pairing popup would cover its prompt.
    function openPairing() {
        if (root.detailMode === "compose") {
            root.leaveCompose(root.openPairing)
            return
        }
        root.navigate("settings")
        settingsPane.openPairing()
    }

    function openFolderActions(folder) {
        folderActionsMenu.folder = folder
        folderActionsMenu.popup()
    }

    Component.onCompleted: {
        if (root.carryAsk)
            carryContactsDialog.open()
        MailApp.refresh()
        // Populates the folder drawer's Archive subfolders. A failure leaves
        // the six standard mailboxes showing (MailController::refreshFolders).
        MailApp.refreshFolders()
    }
    // ---- notification tap-through ---------------------------------------
    // MailController::openEmailRequested (Task 42) is forwarded from
    // NotificationDispatcher::openRequested (Task 40) via a direct
    // signal-to-signal connect in main.cpp -- see that connect's own
    // comment. Reuses selectEmail() unchanged (the exact function the mail
    // list's own TapHandler below already calls) rather than a second,
    // parallel way to set detail-pane selection state; currentSection is
    // set to "mail" first since selectEmail() alone only ever fires today
    // from within the mail list (visible only when currentSection ===
    // "mail" already), so a tap-through arriving while the Contacts section
    // is showing needs that switch made explicit here. messageId is bare
    // (no folder), so this hydrates the full cached email via
    // MailApp.findByMessageId() the same way EmailDetail.qml itself already
    // does, purely to recover the folder -- an empty folder (message not
    // yet locally cached) is not an error, see EmailDetail.qml's own
    // reload(), which already handles a miss gracefully.
    //
    // raise()/requestActivate() run unconditionally here because this
    // handler only ever fires from a genuine user click on the
    // notification's "View" action -- never from NotificationDispatcher.
    // notify() itself, which only builds/sends the KNotification and
    // touches no window state (confirmed by reading
    // NotificationDispatcher.h/.cpp and main.cpp's Task 41 wiring).
    Connections {
        target: MailApp
        function onOpenEmailRequested(messageId) {
            // Handed to C++ rather than hydrated from the cache here: a push
            // is generated the instant mail arrives and this client may not
            // have synced it yet, which used to select a message that was not
            // there and show an empty detail pane. openFromNotification()
            // refreshes first and answers on onNotificationEmailResolved.
            root.destination = "inbox"
            MailApp.openFromNotification(messageId)
            root.raise()
            root.requestActivate()
        }
        // Cached in several mailboxes and the notification did not say which.
        // Only the user can answer this, so ask instead of rendering the
        // refusal as a blank detail page.
        function onNotificationEmailAmbiguous(messageId, folders) {
            root.destination = "inbox"
            notificationFolderDialog.open(messageId, folders)
        }

        // A Forward whose attachments are now held in memory (EmailDetail's
        // Forward). Here, not in EmailDetail: the reader may have moved on.
        // Never over a message already being written: that would lose it.
        // MailController refuses first when that message holds a draft
        // session; over a plain compose pane, openRestoredDraft() asks.
        function onForwardComposeRequested(draft) {
            root.openRestoredDraft(draft)
        }
        function onForwardFinished(composerBusy) {
            if (composerBusy)
                root.forwardNotOpened()
        }

        function onNotificationEmailResolved(messageId, folder) {
            // Empty folder: not found even after a refresh. MailController
            // has set lastError, and selecting nothing leaves that visible
            // instead of covering it with an empty pane.
            if (folder.length > 0)
                root.openEmailInItsFolder(messageId, folder)
        }

        // The mail list's context-menu Delete dispatches and returns, so the
        // "was it the message the detail pane is showing?" test has to live
        // somewhere that outlives the row -- the optimistic removal destroys
        // that delegate before the reply lands.
        //
        // Only the embedded pane is closed here. The popped-out Windows
        // handle their own EmailDetail's actionCompleted (see
        // emailWindowComponent), which is why this checks
        // root.selectedMessageId rather than acting on any deleted id.
        function onActionCompleted(action, messageIds, ok) {
            if (ok && action === "delete" && root.detailMode === "email"
                && messageIds.indexOf(root.selectedMessageId) !== -1) {
                root.closeDetail()
            }
        }
    }

    // ---- keyboard shortcuts ----------------------------------------------
    // Small, non-exhaustive set per the task-39 brief ("don't feel obligated
    // to replicate Mac's full shortcut set") -- refresh-current-section and
    // new-compose only. Ctrl+Shift+C is Mac's own Compose shortcut, added
    // alongside the more standard Ctrl+N rather than instead of it. Find
    // lives in MailSearchField.
    //
    // All of them are off while locked. A window shortcut fires whatever has
    // focus, so the PIN overlay does not stop them: Ctrl+N would replace an
    // open composition and put focus in the editor underneath it.
    Shortcut {
        sequence: "Ctrl+R"
        enabled: !AppLock.locked
        onActivated: {
            if (root.currentSection === "mail")
                MailApp.refresh()
            else
                ContactsApp.sync()
        }
    }
    Shortcut {
        sequence: "Ctrl+N"
        enabled: !AppLock.locked
        onActivated: root.openCompose("", "", "")
    }
    Shortcut {
        sequence: "Ctrl+Shift+C"
        enabled: !AppLock.locked
        onActivated: root.openCompose("", "", "")
    }

    Popup {
        id: scheduledSheet
        focus: true // Escape closes it and Tab stays inside
        contentWidth: 480
        contentHeight: scheduledLoader.item ? scheduledLoader.item.implicitHeight : 200
        // A plain centred Popup: Kirigami.OverlaySheet sizes itself from its
        // own y and its y from its size (a binding loop on every load), and
        // the page inside carries its own title and Close/Done anyway.
        modal: true
        padding: 0
        anchors.centerIn: Overlay.overlay
        width: Math.min(implicitWidth, root.width - 32)
        height: Math.min(implicitHeight, root.height - 32)
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.4) }

        background: Rectangle {
            color: Theme.panel
            radius: Theme.shapeSheet
            border.width: 1
            border.color: Theme.line
        }

        Loader {
            id: scheduledLoader
            active: scheduledSheet.opened
            anchors.fill: parent // the popup clamps to small windows
            sourceComponent: ScheduledList {
                onClosed: scheduledSheet.close()
            }
        }
    }

    // ---- pairing confirmation ------------------------------------------
    // VibeSec fix: this app is registered as the OS-wide handler for the
    // kypost:// scheme (packaging/flatpak/com.kysecurity.mail.desktop's
    // MimeType), so a kypost://native-pair link clicked anywhere on
    // the system reaches PairingController without any of this app's own
    // UI ever having been on screen -- including here, where (unlike
    // the old MobileRoot.qml's pageStack) there's no page to auto-push. This is a
    // small dedicated Popup rather than reusing the full Pairing.qml/
    // Settings flow (Settings.qml's own pairingPopup is nested inside
    // the Settings destination, which isn't shown by default): it only needs to show
    // which server is asking and let the user accept or reject before
    // Pairing.confirmPendingPair() makes any network call. See
    // PairingController::pairFromDeepLink's doc comment for the full gate.
    Popup {
        id: pairingConfirmPopup
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        // Dim what is behind, so it reads as a question, not a stray card.
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.4) }
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        width: Math.min(380, root.width - 32)
        padding: 20

        background: Rectangle {
            color: Theme.panel
            radius: Theme.shapeSheet
            border.width: 1
            border.color: Theme.line
        }

        // Escape (or any other close not routed through the buttons below)
        // must fail closed -- discard the pending request rather than
        // leaving it sitting around for a later, unrelated
        // confirmPendingPair() call to unexpectedly complete. The state
        // check avoids double-cancelling when this popup is closed *because*
        // confirmPendingPair()/the Cancel button already moved
        // pairingState away from "confirm" themselves.
        onClosed: if (Pairing.pairingState === "confirm") Pairing.cancelPendingPair()

        ColumnLayout {
            width: parent.width
            spacing: 16

            Text {
                Layout.fillWidth: true
                text: i18n("Pair this device?")
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontSubject
                font.weight: Font.Bold
            }
            Text {
                Layout.fillWidth: true
                text: i18n("A pairing request wants to connect this device to:")
                color: Theme.ink
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontSender
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: Pairing.pendingPairOrigin
                color: Theme.inkStrong
                font.family: Theme.fontMono
                font.pixelSize: Math.round(15 * Theme.fontScale)
                font.weight: Font.Bold
                wrapMode: Text.WordWrap
            }
            // Only reachable for loopback, but cleartext must never look the
            // same as TLS in the one confirmation the user gets -- the pairing
            // token and the real push device token both travel over this.
            Text {
                Layout.fillWidth: true
                visible: Pairing.pendingPairInsecure
                text: i18n("This connection is not encrypted (http).")
                color: Theme.danger
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontDate
                wrapMode: Text.WordWrap
            }
            // DECISIONS.md: a link that re-pins this account is never
            // confirmed silently.
            Text {
                Layout.fillWidth: true
                objectName: "repinWarning"
                visible: Pairing.pendingPairRepins || Pairing.certificateMismatch
                text: i18n("This server's certificate changed since you paired. Only continue if you just opened this link from your own KyPost web page.")
                color: Theme.danger
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontDate
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: i18n("Only confirm this if you expected it. Once paired, this server can deliver mail notifications to this device.")
                color: Theme.ink
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontDate
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Item { Layout.fillWidth: true }
                GhostButton {
                    text: i18n("Cancel")
                    onClicked: Pairing.cancelPendingPair()
                }
                PrimaryButton {
                    text: i18n("Pair")
                    onClicked: Pairing.confirmPendingPair()
                }
            }
        }
    }

    Connections {
        target: Pairing
        function onPairingStateChanged() {
            if (Pairing.pairingState === "confirm") {
                // Same raise()+requestActivate() as onOpenEmailRequested
                // above -- a kypost://native-pair link can arrive via
                // KDBusService while this window is minimized/behind
                // others, and Popup.open() alone doesn't bring the window
                // forward, so the confirm prompt would open invisibly and
                // the pairing attempt would silently stall on this step
                // forever.
                root.raise()
                root.requestActivate()
                pairingConfirmPopup.open()
            } else if (Pairing.pairingState === "approval") {
                // A deep-link pairing was confirmed in the popup above, so
                // the pairing page is not on screen: the code must be (#516).
                pairingConfirmPopup.close()
                if (!settingsPane.pairingOpen)
                    root.openPairing()
            } else {
                pairingConfirmPopup.close()
            }
        }
        // Android #181: after a pairing from a link, never after Reconnect.
        // takeLockOffer() is true once per install and only with no lock set.
        // Settings' pairing page is a Popup and would paint over the offer,
        // so it gives way.
        function onPairedFromLink() {
            if (AppLock.takeLockOffer()) {
                settingsPane.closePopups()
                appLockOfferDialog.open()
            }
        }
    }

    // ---- PGP QR key exchange: two more popups, same choice as
    // the scheduled sheet above (no second top-level window/event-loop to manage).
    Popup {
        id: pgpMyQrCodeSheet
        focus: true // Escape closes it and Tab stays inside
        contentWidth: 360
        contentHeight: 480
        // A plain centred Popup: Kirigami.OverlaySheet sizes itself from its
        // own y and its y from its size (a binding loop on every load), and
        // the page inside carries its own title and Close/Done anyway.
        modal: true
        padding: 0
        anchors.centerIn: Overlay.overlay
        width: Math.min(implicitWidth, root.width - 32)
        height: Math.min(implicitHeight, root.height - 32)
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.4) }

        background: Rectangle {
            color: Theme.panel
            radius: Theme.shapeSheet
            border.width: 1
            border.color: Theme.line
        }

        PgpMyQrCode {
            anchors.fill: parent // the popup clamps to small windows
            onClosed: pgpMyQrCodeSheet.close()
        }
    }

    Popup {
        id: pgpScanContactKeySheet
        focus: true // Escape closes it and Tab stays inside
        contentWidth: 360
        contentHeight: pgpScanContactKeyLoader.item ? pgpScanContactKeyLoader.item.implicitHeight : 360
        // A plain centred Popup: Kirigami.OverlaySheet sizes itself from its
        // own y and its y from its size (a binding loop on every load), and
        // the page inside carries its own title and Close/Done anyway.
        modal: true
        padding: 0
        anchors.centerIn: Overlay.overlay
        width: Math.min(implicitWidth, root.width - 32)
        height: Math.min(implicitHeight, root.height - 32)
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.4) }

        background: Rectangle {
            color: Theme.panel
            radius: Theme.shapeSheet
            border.width: 1
            border.color: Theme.line
        }
        // Set right before open() by whichever entry point below opened
        // this sheet -- null means "create a brand-new contact from the
        // scan" (the contacts-list entry point); non-null means "write
        // into the persistently-embedded ContactDetail pane instead" (the
        // ContactDetail pane's own entry point, unlike the old MobileRoot there is
        // only ever one ContactDetail instance here, not one per push, so
        // this can point at it directly rather than capturing a per-push
        // reference).
        property var targetContactDetail: null

        // Loader, not a direct child -- PgpScanContactKey's Camera defaults
        // to active as soon as it's constructed (see its own "active:
        // PgpQr.scannedFingerprint === ''" binding), so instantiating it
        // directly here would turn the camera on at AppShell construction
        // time, before this sheet is ever opened. the old MobileRoot.qml avoids this
        // naturally since its equivalent lives inside a Component{} only
        // instantiated by pageStack.push(); a Popup has no push-style
        // API, so a Loader gated on this sheet's own opened()/closed()
        // signals is the equivalent here -- also releases the camera again
        // once the sheet closes, rather than leaving it running.
        onOpened: pgpScanContactKeyLoader.active = true
        onClosed: pgpScanContactKeyLoader.active = false

        Loader {
            id: pgpScanContactKeyLoader
            active: false
            anchors.fill: parent // the popup clamps to small windows
            sourceComponent: PgpScanContactKey {
                onClosed: pgpScanContactKeySheet.close()
                onKeyScanned: function (name, publicKey) {
                    if (pgpScanContactKeySheet.targetContactDetail) {
                        pgpScanContactKeySheet.targetContactDetail.applyScannedKey(name, publicKey)
                    } else {
                        // Fold in whatever contact-card details the scanned
                        // person shared (see PgpQr.scannedContactCardFields's
                        // doc comment) -- fn/pgpKey always come from this
                        // signal's own out-of-band-confirmed values, never
                        // from the card, so they're set last and win.
                        const fields = PgpQr.scannedContactCardFields()
                        fields.fn = name
                        fields.pgpKey = publicKey
                        ContactsApp.createContact(fields)
                    }
                    pgpScanContactKeySheet.close()
                }
            }
        }
    }

    // ---- compose: Loader, not a persistent embed -----------------------
    // Unlike EmailDetail/ContactDetail below (which rebind messageId/uid as
    // the selection changes and reload() in response -- see those files'
    // own doc comments confirming AppShell is expected to do this),
    // Compose.qml only seeds its fields once, in Component.onCompleted (it
    // has no onInitialToChanged-style rebind hook -- the old MobileRoot doesn't
    // need one either, since pageStack.push() always creates a fresh
    // instance). A persistently-embedded Compose would only ever read
    // initialTo/initialSubject/initialBody on this file's own startup, so a
    // second Reply/Forward while Compose is already the detail pane's
    // content would silently keep showing the FIRST draft's prefill. A
    // Loader with active bound to detailMode fixes this cheaply: toggling
    // active off then back on destroys and recreates the item (per Qt's
    // documented Loader sizing/lifecycle rules), which re-runs
    // Component.onCompleted every time, matching pageStack.push()'s own
    // fresh-instance semantics.
    Component {
        id: composePaneComponent
        Compose {
            restoredDraft: root.composeSeed.draft || ({})
            initialTo: root.composeSeed.to
            initialSubject: root.composeSeed.subject
            initialBody: root.composeSeed.body
            initialBodyIsHtml: true // EmailDetail's quotes; seedScript cleans them
            replyTo: root.composeSeed.replyTo || ({})
            initialNotice: root.composeSeed.notice || ""
            onSendSucceeded: root.closeDetail()
            onPopOutRequested: function (draft, replyTo) {
                root.popOutCompose(draft, replyTo)
            }
        }
    }

    // ---- pop-out windows -------------------------------------------------
    // Email/Compose can be detached into their own top-level Window here
    // (Desktop mode only -- both components gate their own pop-out
    // IconButton on General.isDesktopMode, so these functions are only ever
    // reachable from that mode already). Popping out CLOSES the embedded
    // copy in the 3-column view (root.closeDetail() / detailMode reset)
    // rather than leaving a duplicate behind -- a message is read-only so a
    // second view of it wouldn't cause data loss, but Compose is editable,
    // and this app has exactly one way to "seed" either component's initial
    // content (the same composeSeed/messageId+folder properties used
    // in-pane), so keeping both variants of the same pop-out behavior
    // consistent was simpler than special-casing one of them.
    //
    // createObject(null, ...) makes each window a genuine top-level window
    // with no QML parent (Window itself doesn't need one); onClosing calls
    // destroy() so closing the window also frees the dynamically-created
    // object instead of leaking it.
    Component {
        id: emailWindowComponent
        Window {
            id: emailWindow
            width: 640
            height: 720
            color: Theme.bg

            property string popMessageId: ""
            property string popFolder: ""

            // Window.title is drawn by the compositor, so LockOverlay cannot
            // cover it: the subject stayed legible in the titlebar, taskbar and
            // Alt-Tab switcher while the PIN screen was up. Same reasoning as
            // NotificationDispatcher::setContentHidden(), which redacts push
            // content for exactly this reason.
            title: AppLock.locked
                ? i18n("Email")
                : (poppedEmail.displaySubject || i18n("Email"))

            EmailDetail {
                id: poppedEmail
                anchors.fill: parent
                messageId: emailWindow.popMessageId
                folder: emailWindow.popFolder
                isPoppedOut: true
                beforeLeavingCompose: function (proceed) { root.leaveCompose(proceed) }
                onDraftRequested: function (draft) { root.openRestoredDraft(draft) }
                onComposeRequested: function (to, subject, body, replyTo, notice) { root.openCompose(to, subject, body, replyTo, notice) }
                onActionCompleted: emailWindow.close()
            }

            // Same background as the main window, so with some window
            // managers/themes the two are hard to tell apart when they
            // overlap -- an app-drawn outline stays visible regardless of
            // decoration theme. Declared last so it paints on top of
            // EmailDetail's content instead of underneath it.
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: 2
                border.color: Theme.line
            }

            // A popped-out window is a separate top-level Window, so the
            // main window's overlay does not cover it. Without this one, mail
            // stayed readable here while the app was locked.
            LockOverlay {}

            onClosing: emailWindow.destroy()
        }
    }

    Component {
        id: composeWindowComponent
        Window {
            id: composeWindow
            width: 640
            height: 720
            color: Theme.bg
            title: i18n("Compose")

            property var popRestoredDraft: ({})
            property string popInitialTo: ""
            property string popInitialSubject: ""
            property string popInitialBody: ""
            property bool popInitialBodyIsHtml: false
            property var popReplyTo: ({})
            // Set once closing may go ahead: after a send, or once the user
            // has answered "Discard this message?" (owner decision Q1).
            property bool closeConfirmed: false

            Compose {
                id: poppedCompose
                anchors.fill: parent
                restoredDraft: composeWindow.popRestoredDraft
                initialTo: composeWindow.popInitialTo
                initialSubject: composeWindow.popInitialSubject
                initialBody: composeWindow.popInitialBody
                initialBodyIsHtml: composeWindow.popInitialBodyIsHtml
                replyTo: composeWindow.popReplyTo
                isPoppedOut: true
                onSendSucceeded: {
                    composeWindow.closeConfirmed = true
                    composeWindow.close()
                }
                // No MailApp.sendWarning sink here: this Window closes on
                // sendSucceeded(), so any notice parented to it would be
                // destroyed in the same turn it appeared. The main window owns
                // that sink -- see the "send warnings" block near the bottom of
                // this file for the full reasoning and its one limitation.
            }

            // See the matching Rectangle in emailWindowComponent above --
            // same pop-out-blends-into-main-window issue, same fix.
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: 2
                border.color: Theme.line
            }

            // A popped-out window is a separate top-level Window, so the
            // main window's overlay does not cover it. Without this one, mail
            // stayed readable here while the app was locked.
            ConfirmDialog {
                id: popLeaveDialog
                anchors.fill: parent
                z: 900
                title: i18n("Discard this message?")
                message: i18n("It has not been sent. Save it to Drafts to finish it later.")
                acceptText: i18n("Save draft")
                alternateText: i18n("Discard")
                rejectText: i18n("Keep writing")
                onAccepted: poppedCompose.saveDraftThen(function () {
                    composeWindow.closeConfirmed = true
                    composeWindow.close()
                })
                onAlternated: {
                    composeWindow.closeConfirmed = true
                    composeWindow.close()
                }
            }

            LockOverlay {}

            onClosing: function (close) {
                // Accepted at once when it cannot hold anything, so an empty
                // window never holds up a quit.
                if (composeWindow.closeConfirmed || !poppedCompose.mayHaveContent()) {
                    composeWindow.destroy()
                    return
                }
                close.accepted = false
                poppedCompose.checkHasContent(function (hasContent) {
                    if (hasContent) {
                        popLeaveDialog.open()
                        return
                    }
                    composeWindow.closeConfirmed = true
                    composeWindow.close()
                })
            }
        }
    }

    Component {
        id: contactWindowComponent
        Window {
            id: contactWindow
            width: 640
            height: 720
            color: Theme.bg

            property string popUid: ""

            title: AppLock.locked
                ? i18n("Contact")
                : ((poppedContact.contact && poppedContact.contact.fn) ? poppedContact.contact.fn : i18n("Contact"))

            ContactDetail {
                id: poppedContact
                anchors.fill: parent
                uid: contactWindow.popUid
                isPoppedOut: true
                onClosed: contactWindow.close()
                onSaved: function (uid) { ContactsApp.load() }
                onScanPgpKeyRequested: {
                    pgpScanContactKeySheet.targetContactDetail = poppedContact
                    pgpScanContactKeySheet.open()
                }
            }

            // Same background as the main window -- see the matching
            // Rectangle in emailWindowComponent above for why this outline
            // is needed.
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: 2
                border.color: Theme.line
            }

            // A popped-out window is a separate top-level Window, so the
            // main window's overlay does not cover it. Without this one, mail
            // stayed readable here while the app was locked.
            LockOverlay {}

            onClosing: contactWindow.destroy()
        }
    }

    function popOutEmail(messageId, folder) {
        const win = emailWindowComponent.createObject(null, { popMessageId: messageId, popFolder: folder })
        win.show()
        // Same raise()+requestActivate() pairing as the tray restore
        // (TrayController.cpp) and notification tap-through (Connections
        // block above) -- without it, a freshly created top-level window
        // can end up behind root and never take focus, so show() alone
        // silently produces a window the user never sees.
        win.raise()
        win.requestActivate()
        root.closeDetail()
    }

    // draft is Compose.currentDraftForPopOut()'s snapshot: To/Subject/Body
    // seed as before, the rest (Cc, Bcc, attachments, Encrypt/Sign) rides in
    // as a token-less restoredDraft. The body is the editor's own HTML.
    function popOutCompose(draft, replyTo) {
        const win = composeWindowComponent.createObject(
            null, { popRestoredDraft: draft, popInitialTo: draft.to, popInitialSubject: draft.subject,
                    popInitialBody: draft.body, popInitialBodyIsHtml: true, popReplyTo: replyTo || ({}) })
        win.show()
        win.raise()
        win.requestActivate()
        root.detailMode = "empty"
    }

    function popOutContact(uid) {
        const win = contactWindowComponent.createObject(null, { popUid: uid })
        win.show()
        win.raise()
        win.requestActivate()
        root.closeDetail()
    }

    // ---- layout ------------------------------------------------------------
    RowLayout {
        anchors.fill: parent
        spacing: 0

        NavBar {
            objectName: "navRail"
            visible: root.layoutMode !== "compact"
            rail: true
            Layout.fillHeight: true
            Layout.preferredWidth: implicitWidth
            current: root.navCurrent
            onActivated: (target) => root.navigate(target)
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Item {
                id: content
                Layout.fillWidth: true
                Layout.fillHeight: true

                // ---- list pane: the mail list or the contact list --------
                ColumnLayout {
                    id: listPane
                    objectName: "listPane"
                    visible: root.destination !== "settings" && (root.split || !root.detailOpen)
                    x: 0
                    height: parent.height
                    width: root.split && root.detailOpen
                        ? Math.round(parent.width * Breakpoints.listRatio) : parent.width
                    spacing: 0

                    AppHeader {
                        Layout.fillWidth: true
                        place: root.place

                        IconButton {
                            objectName: "searchToggle"
                            visible: root.currentSection === "mail"
                            enabled: !AppLock.locked
                            icon: root.searchOpen ? "close" : "search"
                            tooltip: root.searchOpen ? i18n("Close search") : i18n("Search")
                            onClicked: {
                                root.searchOpen = !root.searchOpen
                                if (!root.searchOpen)
                                    MailApp.clearSearch()
                            }
                        }
                        IconButton {
                            visible: root.currentSection === "mail"
                            enabled: !MailApp.isBusy
                            icon: "refresh"
                            tooltip: i18n("Refresh")
                            onClicked: MailApp.refresh()
                        }
                        IconButton {
                            objectName: "foldersButton"
                            visible: root.currentSection === "mail"
                            enabled: !AppLock.locked
                            icon: "folder"
                            tooltip: i18n("Folders")
                            onClicked: folderDrawer.open()
                        }
                        IconButton {
                            id: contactsMoreButton
                            objectName: "contactsMore"
                            visible: root.currentSection === "contacts"
                            enabled: !AppLock.locked
                            icon: "more-vert"
                            tooltip: i18n("More")
                            onClicked: contactsList.openActions(contactsMoreButton)
                        }
                    }

                    ColumnLayout {
                        visible: root.currentSection === "mail"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        // Gmail's list: full width, flat rows. The rows keep
                        // their own 20px inset, the header's brand edge.
                        spacing: 8

                        MailSearchField {
                            id: mailSearchField
                            objectName: "mailSearchField"
                            Layout.fillWidth: true
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            visible: root.searchOpen
                            findShortcutEnabled: root.currentSection === "mail"
                            placeholderText: i18n("Search %1", root.folderDisplayName(root.currentFolder))
                            onDismissed: root.searchOpen = false

                            // Ctrl+F opens the field before focusing it.
                            Connections {
                                target: mailSearchField.findShortcut
                                function onActivated() {
                                    root.searchOpen = true
                                    Qt.callLater(() => mailSearchField.inputField.forceActiveFocus())
                                }
                            }
                        }

                        KeywordChips {
                            Layout.fillWidth: true
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            visible: !MailApp.searchActive // keywords filter the folder, not search results
                        }

                        BusyIndicator {
                            Layout.alignment: Qt.AlignHCenter
                            running: MailApp.isBusy
                            visible: MailApp.isBusy
                            implicitWidth: 28
                            implicitHeight: 28
                        }

                        // The last sync failure as a danger card in the row
                        // rhythm, not a thin line between chips and mail.
                        // (StatusBanner is for standing conditions with a
                        // remedy; a failed refresh clears on the next one.)
                        Rectangle {
                            objectName: "syncErrorCard"
                            Layout.fillWidth: true
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            visible: MailApp.lastError !== ""
                            implicitHeight: syncError.implicitHeight + 24
                            radius: Theme.shapePanel
                            color: Theme.dangerSoft
                            border.width: 1
                            border.color: Theme.dangerBorder

                            Text {
                                id: syncError
                                anchors.fill: parent
                                anchors.margins: 12
                                verticalAlignment: Text.AlignVCenter
                                textFormat: Text.PlainText
                                text: MailApp.lastError
                                color: Theme.danger
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fontSender
                                wrapMode: Text.WordWrap
                            }
                        }

                        EmptyState {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: emailListView.count === 0
                            text: !MailApp.searchActive ? i18n("No mail here yet. Pull down or tap Refresh.")
                                : MailApp.searchBusy ? i18n("Searching…") : i18n("No matches in this folder.")
                        }

                        ListView {
                            id: emailListView
                            objectName: "emailList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: count > 0
                            clip: true
                            spacing: 0
                            reuseItems: false
                            model: MailApp.searchActive ? MailApp.searchModel : MailApp.emailModel
                            ScrollBar.vertical: ThemedScrollBar {}
                            // Pull to refresh: drag the top of the list down and let go.
                            onDragEnded: {
                                if (verticalOvershoot < -Kirigami.Units.gridUnit * 4)
                                    MailApp.refresh()
                            }
                            onAtYEndChanged: if (atYEnd) MailApp.loadOlder()

                            delegate: MailCard {
                                selected: root.detailMode === "email" && root.selectedMessageId === model.messageId
                                showDetails: root.layoutMode !== "compact"
                                onClicked: root.selectEmail(model.messageId, model.folder)
                                onMoveRequested: (ids, folder) => moveDialog.open(ids, folder, MailApp.mailFolders())
                                onSnoozeRequested: (ids) => snoozeDialog.open(ids, MailApp.snoozePresets())
                            }
                        }
                    }

                    ContactsList {
                        id: contactsList
                        objectName: "contactsList"
                        visible: root.currentSection === "contacts"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.maximumWidth: 960
                        Layout.alignment: Qt.AlignHCenter
                        onContactSelected: function (uid) { root.selectContact(uid) }
                        onScanPgpKeyRequested: {
                            pgpScanContactKeySheet.targetContactDetail = null
                            pgpScanContactKeySheet.open()
                        }
                    }
                }

                // ---- detail pane: beside the list from 840, over it below --
                Rectangle {
                    id: detailPane
                    objectName: "detailPane"
                    visible: root.destination !== "settings" && root.detailOpen
                    x: root.split ? listPane.width : 0
                    width: parent.width - x
                    height: parent.height
                    color: Theme.panel
                    clip: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        AppHeader {
                            visible: root.detailMode === "compose" && !root.split
                            Layout.fillWidth: true
                            place: root.place
                        }
                        SubHeader {
                            visible: !(root.detailMode === "compose" && !root.split)
                            Layout.fillWidth: true
                            color: Theme.bg
                            showBack: !root.split
                            title: root.detailMode === "email" ? i18n("Email")
                                : root.detailMode === "contact" ? i18n("Contact")
                                : i18n("Compose")
                            onBackRequested: root.leaveCompose(root.closeDetail)

                            IconButton {
                                visible: root.split
                                icon: "close"
                                tooltip: i18n("Close")
                                onClicked: root.leaveCompose(root.closeDetail)
                            }
                        }

                        EmailDetail {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: root.detailMode === "email"
                            messageId: root.detailMode === "email" ? root.selectedMessageId : ""
                            folder: root.detailMode === "email" ? root.selectedEmailFolder : ""
                            onDraftRequested: function (draft) { root.openRestoredDraft(draft) }
                            onComposeRequested: function (to, subject, body, replyTo, notice) { root.openCompose(to, subject, body, replyTo, notice) }
                            onActionCompleted: root.closeDetail()
                            onPopOutRequested: root.popOutEmail(root.selectedMessageId, root.selectedEmailFolder)
                        }

                        ContactDetail {
                            id: contactDetailPane
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: root.detailMode === "contact"
                            uid: root.detailMode === "contact" ? root.selectedContactUid : ""
                            onClosed: root.closeDetail()
                            onSaved: function (uid) { ContactsApp.load() }
                            onScanPgpKeyRequested: {
                                pgpScanContactKeySheet.targetContactDetail = contactDetailPane
                                pgpScanContactKeySheet.open()
                            }
                            onPopOutRequested: function (uid) { root.popOutContact(uid) }
                        }

                        Loader {
                            id: composeLoader
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: active
                            active: root.detailMode === "compose"
                            sourceComponent: composePaneComponent
                        }
                    }
                }

                // ---- settings ------------------------------------------------
                ColumnLayout {
                    anchors.fill: parent
                    visible: root.destination === "settings"
                    spacing: 0

                    // A narrow window's open section has its own titled bar.
                    AppHeader {
                        visible: settingsPane.wide || !settingsPane.paneOpen
                        Layout.fillWidth: true
                        place: root.place
                    }
                    Settings {
                        id: settingsPane
                        objectName: "settingsPane"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        onClosed: root.navigate("inbox")
                        onMyPgpQrCodeRequested: pgpMyQrCodeSheet.open()
                    }
                }
            }

            NavBar {
                objectName: "navBar"
                visible: root.layoutMode === "compact" && !(root.detailOpen && root.detailMode !== "compose")
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                current: root.navCurrent
                onActivated: (target) => root.navigate(target)
            }
        }
    }

    // ---- folders ---------------------------------------------------------
    Drawer {
        id: folderDrawer
        objectName: "folderDrawer"
        edge: Qt.LeftEdge
        width: Math.min(320, root.width * 0.85)
        height: root.height
        interactive: !AppLock.locked

        background: Rectangle { color: Theme.bg }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 4

            SectionLabel {
                Layout.leftMargin: 8
                Layout.topMargin: 12
                Layout.bottomMargin: 4
                text: i18n("Folders")
            }

            ListView {
                id: folderList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: MailApp.mailFolders()

                Connections {
                    target: MailApp
                    function onFoldersChanged() { folderList.model = MailApp.mailFolders() }
                }
                Connections {
                    target: Pairing
                    function onPairingChanged() { folderList.model = MailApp.mailFolders() }
                }

                // A nav row, as the suite's sidebar: the current folder is
                // accent-soft with an accent bar.
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool isCurrent: root.currentFolder === modelData.wireName
                    width: folderList.width
                    height: 44
                    radius: 6
                    color: isCurrent ? Theme.accentSoft : "transparent"

                    Rectangle {
                        visible: parent.isCurrent
                        y: 8
                        width: 3
                        height: parent.height - 16
                        radius: 1.5
                        color: Theme.accent
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 16 + (modelData.depth || 0) * 16
                        anchors.rightMargin: 16
                        textFormat: Text.PlainText
                        text: Format.withUnread(modelData.displayName, MailApp.unreadCounts[modelData.wireName])
                        color: Theme.inkStrong
                        font.family: Theme.fontUi
                        font.pixelSize: modelData.depth > 0 ? Theme.fontPreview : Theme.fontSender
                        font.weight: parent.isCurrent ? Font.Medium : Font.Normal
                        elide: Text.ElideRight
                    }

                    TapHandler {
                        onTapped: {
                            root.selectFolder(modelData.wireName)
                            folderDrawer.close()
                        }
                        onLongPressed: root.openFolderActions(modelData)
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: root.openFolderActions(modelData)
                    }
                }
            }

            GhostButton {
                Layout.fillWidth: true
                text: i18n("Scheduled")
                enabled: !AppLock.locked
                onClicked: {
                    folderDrawer.close()
                    scheduledSheet.open()
                }
            }
            GhostButton {
                Layout.fillWidth: true
                text: i18n("New folder…")
                onClicked: {
                    folderDrawer.close()
                    folderDialog.openCreate(MailApp.archiveFolder, root.folderDisplayName(MailApp.archiveFolder))
                }
            }
            MutedHint {
                Layout.fillWidth: true
                text: i18n("Press and hold, or right-click, a folder for more actions.")
                wrapMode: Text.WordWrap
            }
        }
    }

    Menu {
        id: folderActionsMenu
        property var folder: ({})

        MenuItem {
            text: i18n("New subfolder…")
            onTriggered: folderDialog.openCreate(folderActionsMenu.folder.wireName, folderActionsMenu.folder.displayName)
        }
        MenuItem {
            text: i18n("Rename…")
            enabled: folderActionsMenu.folder.deletable === true
            onTriggered: folderDialog.openRename(folderActionsMenu.folder.wireName, folderActionsMenu.folder.displayName)
        }
        MenuItem {
            text: i18n("Delete folder…")
            enabled: folderActionsMenu.folder.deletable === true
            onTriggered: folderDialog.openDelete(folderActionsMenu.folder.wireName, folderActionsMenu.folder.displayName)
        }
    }
    // ---- send warnings ---------------------------------------------------
    // MailApp.sendWarning means the message WAS sent, with partial trouble
    // (the Sent copy failed, or a pickup link did not reach every recipient).
    // Shown as a notice, never as a failure, and never with a retry that would
    // duplicate it.
    //
    // The sink lives HERE rather than in Compose.qml because a warning only
    // ever accompanies a successful send, and this file answers Compose's
    // sendSucceeded() by closing the detail pane (destroying the composer) --
    // a notice parented to it would be torn down in the same turn it appeared.
    // MailApp is a singleton, so this receives the warning whichever composer
    // sent.
    //
    // Known limitation, stated rather than papered over: this Toast is a child
    // of THIS window, so it cannot render over the pop-out compose Window
    // (composeWindowComponent above), which is a separate top-level window. A
    // second Toast inside that Window would not help -- the pop-out closes
    // itself on sendSucceeded(), in the same turn the warning arrives, which
    // is the exact "doomed component" problem that moved this sink out of
    // Compose.qml in the first place. So for a pop-out send the notice appears
    // here, on the main window, which the user is returned to as the pop-out
    // disappears. It is missed only if the main window is minimized or fully
    // occluded for the Toast's 2.5s lifetime; fixing that properly needs a
    // persistent, window-independent notification surface this app does not
    // have yet.
    Connections {
        target: MailApp
        function onSendWarning(warning) {
            sendWarningToast.show(warning)
        }
    }

    Toast {
        id: sendWarningToast
        z: 900 // under the app-lock overlay (1000), over every pane
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter
    }

    // Persistent warnings for an expired pairing / undecryptable credentials
    // -- see components/StatusBanner.qml. Declared before the lock overlay so
    // it paints underneath it: a locked app must not leak account state.
    StatusBanner {
        onCertificateReviewRequested: certificateChangeDialog.open()
    }

    // Hosted here rather than inside StatusBanner: it fills the window, and
    // the banner is only as tall as its own text. Declared before the lock
    // overlay for the same reason the banner is -- it is an Item, not a
    // Popup, so z-order alone keeps it under the lock gate.
    CertificateChangeDialog {
        id: certificateChangeDialog
        onNewLinkRequested: root.openPairing()
    }

    // Deliberately a Toast rather than a StatusBanner row. StatusBanner is
    // danger-styled and cannot be dismissed, and is reserved for conditions
    // that stop the app working. A routine "a new version exists" painted in
    // the same red, unclearable strip would devalue the warnings that matter.
    //
    // UpdateToast (not plain Toast): defers the toast until unlock when the
    // startup update check lands while the PIN screen (z: 1000) is covering
    // this z: 940 toast -- see UpdateToast.qml for why a plain Toast here
    // meant a lock-enabled user was never told at all.
    UpdateToast {
        id: updateToast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        z: 940
    }

    // ---- app lock -----------------------------------------------------
    // One per Window, including every pop-out below -- see
    // components/LockOverlay.qml for why this cannot live only here.
    // Every QQC2 Popup and Kirigami.OverlaySheet renders inside QQuickOverlay,
    // which Qt stacks ABOVE the window's content item -- so LockOverlay's
    // z: 1000 cannot reach it, and a sheet left open when the app locked
    // stayed visible and clickable over the PIN screen. Because these popups
    // are modal, their dimmer also swallowed clicks aimed at the PIN field, so
    // they blocked unlocking as well as sitting on top of it.
    //
    // Popups owned by components close themselves on lock (MailCard's menu,
    // ContactsList's menu) or through a function here (Settings).
    Connections {
        target: AppLock
        function onLockedChanged() {
            if (!AppLock.locked)
                return;
            scheduledSheet.close();
            pgpMyQrCodeSheet.close();
            pgpScanContactKeySheet.close();
            folderDrawer.close();
            folderActionsMenu.close();
            pairingConfirmPopup.close();
            settingsPane.closePopups();
        }
    }

    // Sign-in approval. An Item under LockOverlay, never a Popup: QQuickOverlay
    // would stack it above the PIN screen.
    MfaApproval {
        anchors.fill: parent
        z: 950
        visible: Mfa.open
    }

    Connections {
        target: Mfa
        function onShowRequested() {
            root.raise()
            root.requestActivate()
        }
    }

    LockOverlay { id: unlockOverlay }

    // Answers a notification tap-through that cannot say which mailbox it
    // meant. Anchored over everything, like the other overlay dialogs.
    NotificationFolderDialog {
        id: notificationFolderDialog
        objectName: "notificationFolderDialog"
        anchors.fill: parent
        z: 900
        onChosen: function (messageId, folder) {
            root.openEmailInItsFolder(messageId, folder)
        }
    }

    // The list's "Move to…". The reader pane hosts its own (EmailDetail.qml),
    // so a pop-out window's picker opens in that window.
    MoveToFolderDialog {
        id: moveDialog
        anchors.fill: parent
        z: 900
        onChosen: function (messageIds, folder) {
            MailApp.moveEmails(messageIds, folder)
        }
    }

    SnoozeDialog {
        id: snoozeDialog
        anchors.fill: parent
        z: 900
        parseCustom: function (date, time) { return MailApp.snoozeCustomTime(date, time) }
        onChosen: function (messageIds, at) {
            MailApp.snoozeEmails(messageIds, at)
        }
    }

    // Owner decision Q3: a different account was paired while this device held
    // contacts. Nothing of them reaches the new account until this is
    // answered; "Not now" (or the lock) leaves them waiting, and a contact
    // sync asks again.
    readonly property bool carryAsk: ContactsApp.contactsToCarry > 0 && Pairing.isPaired && !AppLock.locked
    onCarryAskChanged: if (carryAsk) carryContactsDialog.open()
    Connections {
        target: ContactsApp
        function onCarryChanged() {
            if (root.carryAsk && !carryContactsDialog.isOpen)
                carryContactsDialog.open()
        }
    }
    ConfirmDialog {
        id: carryContactsDialog
        objectName: "carryContactsDialog"
        anchors.fill: parent
        z: 900
        title: i18np("Bring your contact into %2?", "Bring your %1 contacts into %2?",
                     ContactsApp.contactsToCarry, Pairing.pairedServerHost)
        message: i18n("They are on this device from the account it was paired with before. Bring them adds them to this account. Remove them deletes them from this device only.")
        acceptText: i18n("Bring them")
        alternateText: i18n("Remove them")
        rejectText: i18n("Not now")
        onAccepted: ContactsApp.answerCarry(true)
        onAlternated: ContactsApp.answerCarry(false)
    }

    ConfirmDialog {
        id: leaveComposeDialog
        objectName: "leaveComposeDialog"
        anchors.fill: parent
        z: 900
        title: i18n("Discard this message?")
        message: i18n("It has not been sent. Save it to Drafts to finish it later.")
        acceptText: i18n("Save draft")
        alternateText: i18n("Discard")
        rejectText: i18n("Keep writing")
        function take() {
            const leave = { proceed: root.pendingLeave, cancel: root.pendingLeaveCancel }
            root.pendingLeave = null
            root.pendingLeaveCancel = null
            return leave
        }
        onAccepted: {
            const leave = take()
            const composer = composeLoader.item
            if (!composer || !leave.proceed)
                return
            composer.saveDraftThen(function () {
                if (composeLoader.item !== composer)
                    return
                root.detailMode = "empty"
                sendWarningToast.show(i18n("Draft saved"))
                leave.proceed()
            })
        }
        onAlternated: {
            const leave = take()
            root.detailMode = "empty"
            if (leave.proceed) leave.proceed()
        }
        onRejected: {
            const leave = take()
            if (leave.cancel) leave.cancel()
        }
    }

    ConfirmDialog {
        id: appLockOfferDialog
        objectName: "appLockOfferDialog"
        anchors.fill: parent
        z: 900
        title: i18n("Lock KyPost?")
        message: i18n("Your mail now opens without a PIN. An app lock keeps it closed to anyone using this device while you're away. You can set it up later in Settings, Security.")
        acceptText: i18n("Set up app lock")
        rejectText: i18n("Not now")
        onAccepted: root.openSettingsPane(6) // Security
    }

    FolderDialog {
        id: folderDialog
        anchors.fill: parent
        z: 900
        busy: MailApp.isBusy
        onCreateRequested: function (parent, name) { MailApp.createFolder(parent, name) }
        onRenameRequested: function (folder, name) { MailApp.renameFolder(folder, name) }
        onDeleteRequested: function (folder) { MailApp.deleteFolder(folder) }

        Connections {
            target: MailApp
            function onFolderMutationCompleted(ok) { folderDialog.finished(ok, MailApp.lastError) }
        }
    }

}
