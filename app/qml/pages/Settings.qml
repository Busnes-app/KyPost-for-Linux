import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.kysecurity.mail 1.0
import "../components"
import "../utils/layout.js" as Breakpoints
// Qualified import of this same directory, aliased to PairingPage -- the
// bare name "Pairing" is ambiguous in this file: the automatic IMPLICIT
// same-directory import (every .qml file in pages/ is implicitly visible by
// filename to every other file in pages/, no import statement needed) would
// resolve "Pairing" to Pairing.qml, but the "com.kysecurity.mail 1.0"
// import above explicitly registers a QML singleton ALSO named "Pairing"
// (the PairingController instance, see main.cpp) -- and per QML's import
// precedence rules, an explicit import always wins over the implicit
// per-directory one. The bare name resolved to the singleton at runtime
// ("Pairing: Element is not creatable" -- a singleton can't be instantiated
// with curly-brace syntax), confirmed during this task's manual
// verification. A qualified EXPLICIT import of "." (this same directory,
// re-imported under an alias) outranks the implicit import the same way
// MobileRoot.qml's own explicit `import "pages"` does from outside this
// directory, sidestepping the collision entirely.
import "." as PagesDir

// Settings as Android draws it: a list of section cards and, per section,
// eyebrow labels over rounded panel cards. Below 840px the list fills the
// page and a section opens over it with a back arrow; from 840 the list sits
// on the left and the open section on the right. The shell's AppHeader is
// the page title, so this component draws none of its own.
Item {
    id: root

    // The shell connects this; ConnectionStatus emits it when a history row
    // opens a message.
    signal closed()
    // PGP QR key exchange: the host decides how to show it.
    signal myPgpQrCodeRequested()

    implicitWidth: 480
    implicitHeight: 560

    property int currentPane: 5 // 0 Connection, 1 Appearance, 2 Keywords, 3 Contacts, 4 Notifications, 5 General, 6 Security
    readonly property bool wide: width >= Breakpoints.splitMin
    // Narrow only: whether a section covers the list.
    property bool paneOpen: false

    function showPane(pane) {
        root.currentPane = pane
        root.paneOpen = true
    }

    function openPairing() {
        root.showPane(0)
        pairingPopup.open()
    }

    // Indexed by pane number.
    readonly property var sections: [
        { "name": i18n("Connection"), "icon": "link", "hint": i18n("Pairing and sync status") },
        { "name": i18n("Appearance"), "icon": "palette", "hint": i18n("Theme") },
        { "name": i18n("Keywords"), "icon": "label", "hint": i18n("Order and visibility of keyword tabs") },
        { "name": i18n("Contacts"), "icon": "person", "hint": i18n("Sync and your PGP QR code") },
        { "name": i18n("Notifications"), "icon": "notifications", "hint": i18n("How new mail reaches this device") },
        { "name": i18n("General"), "icon": "settings", "hint": i18n("Interface, tray and about") },
        { "name": i18n("Security"), "icon": "lock", "hint": i18n("App lock, erasing and OpenPGP") }
    ]

    ListModel {
        id: keywordSettingsModel
    }

    // Which erase-after value the PIN prompt is about to apply. Held here
    // rather than passed through securityPrompt.begin() because that helper
    // takes only a mode and a message, and widening it for one caller would
    // touch every other prompt in this file. AppLockManager clamps and
    // re-checks whatever arrives, so this is a convenience, not a control.
    property int eraseThresholdChoice: 0

    // Same pattern as eraseThresholdChoice above: the value the PIN prompt is
    // about to apply. AppLockManager clamps whatever arrives.
    property int graceSecondsChoice: 0

    function refreshKeywordSettings() {
        const settings = MailApp.allKeywordSettings()
        keywordSettingsModel.clear()
        for (let i = 0; i < settings.length; ++i)
            keywordSettingsModel.append(settings[i])
    }

    function persistKeywordOrder() {
        const keywords = []
        for (let i = 0; i < keywordSettingsModel.count; ++i)
            keywords.push(keywordSettingsModel.get(i).keyword)
        MailApp.setKeywordOrder(keywords)
    }

    Component.onCompleted: refreshKeywordSettings()

    // Called by the host each time Settings is shown, so copies taken
    // earlier are not shown again. A fresh visit starts at the section list.
    function refreshSnapshots() {
        refreshKeywordSettings()
        connectionStatus.reloadHistory()
        root.paneOpen = false
    }

    // The keyword list is a copy of the INBOX cache, which an account
    // replacement purges. Pairing happens from inside this component, so
    // without this the previous account's keywords stayed listed.
    Connections {
        target: Pairing
        function onPairingChanged() { root.refreshKeywordSettings() }
    }

    onCurrentPaneChanged: {
        if (currentPane === 6)
            securityFlickable.contentY = 0
    }

    // Consume otherwise unhandled input so a press here cannot also reach
    // whatever sits behind this component.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: function (wheel) { wheel.accepted = true }
    }

    // ---- building blocks ---------------------------------------------------
    // A scrolling section body.
    component PaneBody: Flickable {
        default property alias rows: paneColumn.data
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: paneColumn.implicitHeight + 40
        ScrollBar.vertical: ThemedScrollBar {}

        ColumnLayout {
            id: paneColumn
            x: 16
            y: 8
            width: Math.min(parent.width - 32, 800)
            spacing: 8
        }
    }

    // Android's panelBackground() card holding a group of rows.
    component Panel: Card {
        default property alias rows: panelColumn.data
        Layout.fillWidth: true
        Layout.bottomMargin: 12
        implicitHeight: panelColumn.implicitHeight + 32

        ColumnLayout {
            id: panelColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16
            spacing: 12
        }
    }

    // Android's accent switch row ("Require unlock to open").
    component ToggleRow: Rectangle {
        id: toggle
        property string text: ""
        property bool checked: false
        signal toggled()

        Layout.fillWidth: true
        implicitHeight: Math.max(52, toggleLabel.implicitHeight + 24)
        radius: Theme.shapeButton
        color: Theme.accent
        opacity: enabled ? 1.0 : 0.5

        Accessible.role: Accessible.CheckBox
        Accessible.name: toggle.text
        Accessible.checked: toggle.checked
        Accessible.onPressAction: if (toggle.enabled && !AppLock.locked) toggle.toggled()

        Text {
            id: toggleLabel
            anchors.left: parent.left
            anchors.right: track.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 16
            anchors.rightMargin: 12
            textFormat: Text.PlainText
            text: toggle.text
            color: Theme.readableOnAccent
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontSender
            wrapMode: Text.WordWrap
        }
        Rectangle {
            id: track
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            width: 40
            height: 22
            radius: height / 2
            color: Qt.rgba(Theme.readableOnAccent.r, Theme.readableOnAccent.g, Theme.readableOnAccent.b,
                           toggle.checked ? 0.45 : 0.2)

            Rectangle {
                x: toggle.checked ? parent.width - width - 3 : 3
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                radius: 8
                color: Theme.readableOnAccent
                opacity: toggle.checked ? 1.0 : 0.7
                Behavior on x { NumberAnimation { duration: 120 } }
            }
        }
        TapHandler {
            enabled: toggle.enabled
            onTapped: toggle.toggled()
        }
    }

    // Explanation text under a row, as Android's 14sp body copy.
    component Explain: MutedHint {
        Layout.fillWidth: true
        font.pixelSize: Theme.fontPreview
        wrapMode: Text.WordWrap
    }

    // A label over a monospace value (server, device id, transport).
    component ValueRow: ColumnLayout {
        property string label: ""
        property string value: ""
        Layout.fillWidth: true
        spacing: 2
        MutedHint { text: parent.label }
        Text {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: parent.value
            color: Theme.inkStrong
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontSender
            wrapMode: Text.WrapAnywhere
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- section list ------------------------------------------------
        Flickable {
            id: sectionList
            objectName: "settingsSections"
            visible: root.wide || !root.paneOpen
            Layout.fillHeight: true
            Layout.fillWidth: !root.wide
            Layout.preferredWidth: root.wide ? 320 : -1
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: sectionColumn.implicitHeight + 40
            ScrollBar.vertical: ThemedScrollBar {}

            ColumnLayout {
                id: sectionColumn
                x: 16
                y: 8
                width: parent.width - 32
                spacing: 12

                Repeater {
                    model: root.sections
                    delegate: Card {
                        id: sectionCard
                        required property var modelData
                        required property int index
                        readonly property bool current: root.wide && root.currentPane === index
                        Layout.fillWidth: true
                        implicitHeight: sectionRow.implicitHeight + 28
                        color: current ? Theme.selection : Theme.panel

                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.name
                        Accessible.onPressAction: if (!AppLock.locked) root.showPane(index)

                        RowLayout {
                            id: sectionRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 16
                            anchors.rightMargin: 12
                            spacing: 16

                            MaterialIcon {
                                name: sectionCard.modelData.icon
                                tint: Theme.accent
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    textFormat: Text.PlainText
                                    text: sectionCard.modelData.name
                                    color: Theme.inkStrong
                                    font.family: Theme.fontUi
                                    font.pixelSize: Math.round(16 * Theme.fontScale)
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }
                                MutedHint {
                                    Layout.fillWidth: true
                                    text: sectionCard.modelData.hint
                                    elide: Text.ElideRight
                                }
                            }
                            MaterialIcon {
                                name: "chevron-right"
                                tint: Theme.ink
                                visible: !root.wide
                            }
                        }

                        TapHandler { onTapped: root.showPane(sectionCard.index) }
                    }
                }
            }
        }

        Rectangle {
            visible: root.wide
            Layout.fillHeight: true
            implicitWidth: 1
            color: Theme.line
        }

        // ---- open section ------------------------------------------------
        ColumnLayout {
            visible: root.wide || root.paneOpen
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            spacing: 0

            SubHeader {
                Layout.fillWidth: true
                title: root.sections[root.currentPane].name
                showBack: !root.wide
                onBackRequested: root.paneOpen = false
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.fillHeight: true
                currentIndex: root.currentPane

            // ---- 1. Connection ----------------------------------------
            PaneBody {
                SectionLabel { text: i18n("This device") }

                Panel {
                    StatusBadge {
                        active: Pairing.isPaired
                        text: Pairing.isPaired ? i18n("Paired") : i18n("Not paired")
                    }

                    ValueRow {
                        visible: Pairing.isPaired
                        label: i18n("Server")
                        value: Pairing.pairedServerHost
                    }
                    ValueRow {
                        visible: Pairing.isPaired
                        label: i18n("Device")
                        value: Pairing.deviceId
                    }

                    // No "Test connection" / desktop-session pairing here:
                    // this client family only does sub/hash native pairing.
                    PrimaryButton {
                        text: i18n("Pair this device…")
                        visible: !Pairing.isPaired
                        onClicked: pairingPopup.open()
                    }
                    DangerButton {
                        text: i18n("Remove pairing")
                        visible: Pairing.isPaired
                        onClicked: Pairing.removePairing()
                    }
                }

                SectionLabel {
                    visible: Pairing.isPaired
                    text: i18n("Status")
                }

                Panel {
                    visible: Pairing.isPaired

                    ConnectionStatus {
                        id: connectionStatus
                        Layout.fillWidth: true
                        distributorActive: Pairing.distributorActive
                        pushServerHost: Pairing.pushServerHost
                        pushEncrypted: Pairing.pushEncrypted
                        lastSyncAt: MailApp.lastSyncAt
                        lastError: MailApp.lastError
                        // Re-read when the pane is shown, the sheet opens or the
                        // pairing changes; see ConnectionStatus.
                        active: root.currentPane === 0
                        onHistoryWanted: connectionStatus.history = MailApp.notificationHistory()
                        onOpenRequested: (messageId) => {
                            root.closed()
                            MailApp.openEmailRequested(messageId)
                        }
                    }
                }
            }

            // ---- 2. Appearance ------------------------------------------
            // Name-only list + checkmark, not per-theme color swatches:
            // ThemeController only exposes the ACTIVE theme's palette, so
            // there is no QML-reachable way to preview another theme.
            PaneBody {
                SectionLabel { text: i18n("Theme") }

                Panel {
                    Repeater {
                        model: Theme.themeNames
                        delegate: Rectangle {
                            id: themeDelegate
                            required property string modelData
                            Layout.fillWidth: true
                            implicitHeight: themeRow.implicitHeight + 20
                            radius: Theme.shapeButton
                            color: modelData === Theme.themeName ? Theme.selection
                                : (themeHover.hovered ? Theme.bg : "transparent")

                            Behavior on color {
                                ColorAnimation { duration: 120 }
                            }

                            Accessible.role: Accessible.RadioButton
                            Accessible.name: modelData
                            Accessible.checked: modelData === Theme.themeName
                            Accessible.onPressAction: if (!AppLock.locked) Theme.setTheme(modelData)

                            RowLayout {
                                id: themeRow
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 8

                                Text {
                                    Layout.fillWidth: true
                                    // A theme's proper name (core/theme/AppTheme.cpp), not
                                    // translated: it is also the identifier
                                    // Theme.setTheme() stores and compares.
                                    textFormat: Text.PlainText
                                    text: themeDelegate.modelData
                                    color: Theme.inkStrong
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fontSender
                                }
                                MaterialIcon {
                                    visible: themeDelegate.modelData === Theme.themeName
                                    name: "check"
                                    tint: Theme.accent
                                    Layout.preferredWidth: 20
                                    Layout.preferredHeight: 20
                                    width: 20
                                    height: 20
                                }
                            }

                            HoverHandler {
                                id: themeHover
                            }

                            TapHandler {
                                onTapped: Theme.setTheme(themeDelegate.modelData)
                            }
                        }
                    }
                }
            }

            // ---- 3. Keywords ---------------------------------------------
            // A ListView, not a Pane: drag-to-reorder scrolls it from its
            // edges.
            ColumnLayout {
                spacing: 8

                SectionLabel {
                    Layout.leftMargin: 16
                    Layout.topMargin: 8
                    text: i18n("Keyword tabs")
                }

                Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.maximumWidth: 800
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.bottomMargin: 16

                EmptyState {
                    anchors.fill: parent
                    anchors.margins: 8
                    visible: keywordListView.count === 0
                    text: i18n("No keywords yet.")
                }

                ListView {
                    id: keywordListView
                    property bool reorderActive: false
                    property var dragSource: null
                    property real dragY: -1
                    property int dropIndex: -1
                    anchors.fill: parent
                    anchors.margins: 8
                    visible: count > 0
                    clip: true
                    spacing: 4
                    model: keywordSettingsModel
                    ScrollBar.vertical: ThemedScrollBar {}

                    Timer {
                        interval: 30
                        repeat: true
                        running: keywordListView.reorderActive
                        onTriggered: {
                            if (keywordListView.dragSource) {
                                keywordListView.dragY = keywordListView.mapFromItem(
                                    keywordListView.dragSource,
                                    keywordListView.dragSource.width / 2,
                                    keywordListView.dragSource.height / 2).y
                            }
                            const edge = 48
                            const maximum = Math.max(0, keywordListView.contentHeight - keywordListView.height)
                            if (keywordListView.dragY < edge)
                                keywordListView.contentY = Math.max(0, keywordListView.contentY - 12)
                            else if (keywordListView.dragY > keywordListView.height - edge)
                                keywordListView.contentY = Math.min(maximum, keywordListView.contentY + 12)
                        }
                    }

                    delegate: Rectangle {
                        id: keywordDelegate
                        property int visualIndex: index
                        width: keywordListView.width
                        height: keywordRowContent.implicitHeight + 16
                        radius: Theme.shapeButton
                        color: keywordDrag.active || keywordListView.dropIndex === index || keywordHover.hovered
                               ? Theme.bg : "transparent"
                        border.width: keywordListView.dropIndex === index && !keywordDrag.active ? 2 : 0
                        border.color: Theme.accent
                        Drag.active: keywordDrag.active
                        Drag.source: keywordDelegate
                        Drag.hotSpot.x: width / 2
                        Drag.hotSpot.y: height / 2
                        z: keywordDrag.active ? 1 : 0
                        transform: Translate {
                            y: keywordDrag.active ? keywordDrag.activeTranslation.y : 0
                        }

                        Behavior on color {
                            ColorAnimation { duration: 120 }
                        }

                        RowLayout {
                            id: keywordRowContent
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 10
                            spacing: 8

                            Text {
                                text: "☰"
                                color: Theme.ink
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fontSubject
                                Accessible.name: i18n("Drag to reorder")
                            }

                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: keyword
                                color: Theme.inkStrong
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fontSender
                            }
                            PillTab {
                                text: model.visible ? i18n("Visible") : i18n("Hidden")
                                selected: model.visible
                                onClicked: {
                                    MailApp.setKeywordVisible(keyword, !model.visible)
                                    root.refreshKeywordSettings()
                                }
                            }
                        }

                        HoverHandler {
                            id: keywordHover
                        }

                        DragHandler {
                            id: keywordDrag
                            target: null
                            cursorShape: active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            onActiveChanged: {
                                keywordListView.reorderActive = active
                                keywordListView.dragSource = active ? keywordDelegate : null
                                if (!active) {
                                    keywordListView.dropIndex = -1
                                    root.persistKeywordOrder()
                                }
                            }
                        }

                        DropArea {
                            id: keywordDrop
                            anchors.fill: parent
                            onEntered: function (drag) {
                                const from = drag.source ? drag.source.visualIndex : -1
                                keywordListView.dropIndex = index
                                if (from >= 0 && from !== index) {
                                    keywordSettingsModel.move(from, index, 1)
                                }
                            }
                        }
                    }
                }
                }
            }

            // ---- 4. Contacts ------------------------------------------------
            // No "Sync to system contacts" toggle: Linux has no system
            // address book this app talks to, and a toggle that does nothing
            // is worse than none. Real sync status and action instead.
            PaneBody {
                SectionLabel { text: i18n("Address book") }

                Panel {
                    Explain {
                        text: ContactsApp.lastError !== "" ? ContactsApp.lastError
                            : (ContactsApp.statusMessage !== "" ? ContactsApp.statusMessage : i18n("No sync yet."))
                        color: ContactsApp.lastError !== "" ? Theme.dangerActionText : Theme.ink
                    }
                    PrimaryButton {
                        text: i18n("Sync now")
                        enabled: !ContactsApp.isBusy
                        onClicked: ContactsApp.sync()
                    }
                }

                SectionLabel { text: i18n("Key exchange") }

                Panel {
                    Explain {
                        text: i18n("Show your PGP key as a QR code for a contact to scan in person.")
                    }
                    GhostButton {
                        text: i18n("My PGP QR code")
                        onClicked: root.myPgpQrCodeRequested()
                    }
                }
            }

            // ---- 5. Notifications -------------------------------------------
            // Read-only: live re-registration is not wired (see
            // PairingController.h's known-gap comment), so an editable
            // push-server field would look functional while doing nothing.
            PaneBody {
                SectionLabel { text: i18n("Delivery") }

                Panel {
                    Explain {
                        visible: Pairing.deliveryMode === "" && Pairing.transport === ""
                        text: i18n("Not yet registered")
                    }
                    ValueRow {
                        visible: Pairing.deliveryMode !== "" || Pairing.transport !== ""
                        label: i18n("Delivery mode")
                        value: Pairing.deliveryMode
                    }
                    ValueRow {
                        visible: Pairing.deliveryMode !== "" || Pairing.transport !== ""
                        label: i18n("Transport")
                        value: Pairing.transport
                    }

                    // Where notifications come from: a system UnifiedPush
                    // distributor if one is installed, otherwise polling the
                    // relay directly (core/domain/TransportStateMachine.h).
                    Explain {
                        // Unpaired: any pairing made now registers keys.
                        text: !Pairing.isPaired || Pairing.pushEncrypted
                            ? i18n("KyPost receives notifications through your system's UnifiedPush "
                                   + "distributor. Your KyPost server encrypts them to this device, so the "
                                   + "push server that distributor uses sees only that one arrived. With "
                                   + "no distributor installed, KyPost instead checks the relay directly "
                                   + "every 90 seconds.")
                            : i18n("KyPost receives notifications through your system's UnifiedPush "
                                   + "distributor. This pairing predates push encryption, so sender names "
                                   + "and subject lines pass through that distributor's push server. Pair "
                                   + "this device again to encrypt them. With no distributor installed, "
                                   + "KyPost instead checks the relay directly every 90 seconds.")
                    }
                }
            }

            // ---- 6. General ---------------------------------------------
            PaneBody {
                SectionLabel { text: i18n("Interface mode") }

                Panel {
                    Flow {
                        Layout.fillWidth: true
                        Layout.preferredHeight: childrenRect.height
                        spacing: 8

                        PillTab {
                            text: i18n("Auto")
                            selected: General.preferredMode === "auto"
                            onClicked: General.setPreferredMode("auto")
                        }
                        PillTab {
                            text: i18n("Desktop")
                            selected: General.preferredMode === "desktop"
                            onClicked: General.setPreferredMode("desktop")
                        }
                        PillTab {
                            text: i18n("Mobile")
                            selected: General.preferredMode === "mobile"
                            onClicked: General.setPreferredMode("mobile")
                        }
                    }
                }

                // Desktop-only: gated on the mode THIS process resolved to at
                // startup, not the pending preference above.
                SectionLabel {
                    visible: General.isDesktopMode
                    text: i18n("System tray")
                }

                Panel {
                    visible: General.isDesktopMode

                    ToggleRow {
                        text: i18n("Enable tray icon")
                        checked: General.trayIconEnabled
                        onToggled: General.setTrayIconEnabled(!General.trayIconEnabled)
                    }
                    ToggleRow {
                        enabled: General.trayIconEnabled
                        text: i18n("Minimize to tray on close")
                        checked: General.minimizeToTrayOnClose
                        onToggled: General.setMinimizeToTrayOnClose(!General.minimizeToTrayOnClose)
                    }
                }

                SectionLabel { text: i18n("About") }

                Panel {
                    GhostButton {
                        text: i18n("Support KyPost")
                        onClicked: Qt.openUrlExternally("https://buymeacoffee.com/yoshiofthewire")
                    }

                    UpdateNotice {
                        Layout.fillWidth: true
                    }
                }
            }

            // ---- 7. Security --------------------------------------------
            // Every toggle here needs the current PIN, including turning
            // things OFF: a lock that can be removed by whoever already has
            // the window open protects nothing.
            PaneBody {
                id: securityFlickable

                SectionLabel { text: i18n("App lock") }

                Panel {
                    ToggleRow {
                        text: i18n("Require unlock to open")
                        checked: AppLock.lockEnabled
                        onToggled: {
                            if (AppLock.lockEnabled)
                                securityPrompt.begin("disableLock", i18n("Enter your PIN to turn off the lock"))
                            else
                                securityPrompt.begin("setPin", i18n("Choose a PIN"))
                        }
                    }

                    Explain {
                        text: i18n("A PIN is required every time KyPost starts, and whenever the window is hidden or minimised. After 10 failed attempts all local data is erased.")
                    }

                    // Says plainly what the PIN does not do.
                    //
                    // Cached mail and contacts live in an unencrypted SQLite
                    // file, so anyone who can read this account's files can
                    // read them without ever meeting this PIN. The lock
                    // guards the running app; "Keep nothing on this device"
                    // below is the setting that guards the disk. Leaving
                    // that unsaid let a PIN prompt imply at-rest protection
                    // it has never provided.
                    Explain {
                        text: i18n("The PIN protects this app while it is running. It does not encrypt the cached mail and contacts stored on this computer — for that, turn on Keep nothing on this device below.")
                    }

                    GhostButton {
                        Layout.fillWidth: true
                        visible: AppLock.lockEnabled
                        text: i18n("Change PIN…")
                        onClicked: securityPrompt.begin("changePin", i18n("Enter your current PIN, then a new one"))
                    }
                }

                SectionLabel { text: i18n("Hostile location protection") }

                Panel {
                    ToggleRow {
                        // Same dependency as the credential gate: only
                        // meaningful alongside a PIN.
                        enabled: AppLock.lockEnabled
                        text: i18n("Keep nothing on this device")
                        checked: AppLock.hostileLocationEnabled
                        onToggled: securityPrompt.begin(
                            AppLock.hostileLocationEnabled ? "hlpOff" : "hlpOn",
                            AppLock.hostileLocationEnabled
                                ? i18n("Enter your PIN to turn this off. KyPost will restart.")
                                : i18n("Enter your PIN. KyPost will erase its local data and restart."))
                    }

                    Explain {
                        text: i18n("Mail, contacts and folders are held in memory only and are gone when KyPost closes. Turning this on erases what is already stored and restarts the app. Attachments open in a temporary location instead of being saved to Downloads.")
                    }
                }

                SectionLabel { text: i18n("Lock when KyPost is not in front") }

                Panel {
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        enabled: AppLock.lockEnabled
                        opacity: enabled ? 1.0 : 0.5
                        Text {
                            Layout.fillWidth: true
                            text: i18n("Lock after")
                            color: Theme.inkStrong
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fontSender
                        }
                        Flow {
                            Layout.fillWidth: true
                            Layout.preferredHeight: childrenRect.height
                            spacing: 8

                            Repeater {
                                model: [0, 15, 60, 300]
                                delegate: PillTab {
                                    required property int modelData
                                    text: modelData === 0
                                        ? i18n("At once")
                                        : (modelData < 60
                                            ? i18np("%1 second", "%1 seconds", modelData)
                                            : i18np("%1 minute", "%1 minutes", Math.round(modelData / 60)))
                                    selected: AppLock.backgroundGraceSeconds === modelData
                                    onClicked: {
                                        if (AppLock.backgroundGraceSeconds === modelData)
                                            return
                                        graceSecondsChoice = modelData
                                        securityPrompt.begin(
                                            "backgroundGrace",
                                            modelData === 0
                                                ? i18n("Enter your PIN to lock KyPost as soon as it leaves the front.")
                                                : i18n("Enter your PIN. KyPost will stay unlocked for this long after "
                                                       + "it leaves the front."))
                                    }
                                }
                            }
                        }
                    }

                    Explain {
                        text: AppLock.backgroundGraceSeconds === 0
                            ? i18n("KyPost locks the moment its window is minimised, hidden or sent to the background.")
                            : i18n("KyPost stays unlocked for this long after its window goes away, so switching to another app briefly does not mean typing your PIN again. Anyone who reaches this computer within that time can read your mail. Lock now, from the menu, always locks straight away.")
                    }
                }

                SectionLabel { text: i18n("Erase after failed attempts") }

                Panel {
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        // Nothing to count failures against without a PIN.
                        enabled: AppLock.lockEnabled
                        opacity: enabled ? 1.0 : 0.5
                        Text {
                            Layout.fillWidth: true
                            text: i18n("Erase this device after")
                            color: Theme.inkStrong
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fontSender
                        }
                        Flow {
                            Layout.fillWidth: true
                            Layout.preferredHeight: childrenRect.height
                            spacing: 8

                            Repeater {
                                model: [0, 5, 10, 20]
                                delegate: PillTab {
                                    required property int modelData
                                    text: modelData === 0
                                        ? i18n("Never")
                                        : i18np("%1 attempt", "%1 attempts", modelData)
                                    selected: AppLock.wipeAfterAttempts === modelData
                                    onClicked: {
                                        if (AppLock.wipeAfterAttempts === modelData)
                                            return
                                        eraseThresholdChoice = modelData
                                        securityPrompt.begin(
                                            "eraseAfter",
                                            modelData === 0
                                                ? i18n("Enter your PIN to stop KyPost erasing this device after "
                                                       + "repeated failed attempts.")
                                                : i18np("Enter your PIN to erase this device after %1 failed attempt.",
                                                        "Enter your PIN to erase this device after %1 failed attempts.",
                                                        modelData))
                                    }
                                }
                            }
                        }
                    }

                    // Says what "Never" does and does not switch off. It is
                    // the erase that stops, not the rate limiting -- and
                    // being explicit here is the difference between an
                    // informed choice and a surprise.
                    Explain {
                        text: AppLock.wipeAfterAttempts === 0
                            ? i18n("KyPost will not erase itself, however many attempts fail. Repeated failures are still slowed down and eventually refused until you restart the app. Anyone who takes this computer can keep guessing.")
                            : i18n("Cached mail, contacts and this device's pairing are erased after this many consecutive failed attempts. Your mail on the server is not affected.")
                    }
                }

                SectionLabel { text: i18n("OpenPGP private key") }

                Panel {
                    Explain {
                        text: i18n("Enroll this computer from webmail. KyPost creates a temporary device key, webmail seals your OpenPGP private key to it, and the imported key remains under GnuPG's control.")
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        visible: PgpEnrollment.verificationCode.length > 0
                        implicitHeight: enrollmentCode.implicitHeight + 28
                        radius: Theme.shapeField
                        color: Theme.bg
                        border.width: Theme.fieldStroke
                        border.color: Theme.accent

                        Text {
                            id: enrollmentCode
                            anchors.centerIn: parent
                            text: PgpEnrollment.verificationCode
                            textFormat: Text.PlainText
                            color: Theme.inkStrong
                            font.family: Theme.fontMono
                            font.pixelSize: Math.round(20 * Theme.fontScale)
                            font.letterSpacing: 2
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        Layout.preferredHeight: childrenRect.height
                        spacing: 12

                        PrimaryButton {
                            text: PgpEnrollment.state === 3 ? i18n("Check again")
                                  : PgpEnrollment.state === 5 ? i18n("Enroll again")
                                  : i18n("Enroll this device")
                            enabled: Pairing.isPaired && !PgpEnrollment.busy
                            onClicked: PgpEnrollment.state === 3
                                ? PgpEnrollment.checkAgain() : PgpEnrollment.start()
                        }
                        GhostButton {
                            visible: PgpEnrollment.busy || PgpEnrollment.state === 3
                            text: i18n("Cancel")
                            onClicked: PgpEnrollment.cancel()
                        }
                    }

                    Explain {
                        visible: PgpEnrollment.status.length > 0
                        text: PgpEnrollment.status
                    }
                }

                SectionLabel { text: i18n("Push and MFA") }

                Panel {
                    ToggleRow {
                        // Meaningless without a PIN to gate on, so the row
                        // is inert until the lock is enabled.
                        enabled: AppLock.lockEnabled
                        text: i18n("Require unlock to receive push and MFA")
                        checked: AppLock.credentialPinGateEnabled
                        onToggled: securityPrompt.begin(
                            AppLock.credentialPinGateEnabled ? "gateOff" : "gateOn",
                            i18n("Enter your PIN"))
                    }

                    Explain {
                        text: i18n("When on, the pairing credential is encrypted with your PIN. Mail, push and MFA stop working until you unlock the app — that is the point, and it is a real tradeoff.")
                    }

                    // Permanently visible while it is true, not a one-time
                    // dialog: a warning the user has dismissed is a warning
                    // they no longer have. Pairings with push keys make it false.
                    Rectangle {
                        visible: Pairing.isPaired && !Pairing.pushEncrypted
                        Layout.fillWidth: true
                        implicitHeight: pushWarningText.implicitHeight + 24
                        radius: Theme.shapeButton
                        color: Theme.dangerActionFill
                        border.width: 1
                        border.color: Theme.dangerActionBorder

                        Text {
                            id: pushWarningText
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 12
                            text: i18n("This pairing predates push encryption: notifications send the sender and subject through a relay server (UnifiedPush/ntfy) in the clear, even with the settings above enabled. Pair this device again to encrypt them, or ask your server admin to switch it to Pull mode. Sign-in approvals reach this device only after it is paired again.")
                            color: Theme.dangerActionText
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fontDate
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
            }
        }
    }

    // ---- Security PIN prompt ---------------------------------------------
    // One prompt for all five flows (set / change / disable / gate on / gate
    // off). They differ only in which fields are shown and which AppLock
    // call runs, so a single popup with a mode is far less to get wrong than
    // five near-identical dialogs.
    Popup {
        id: securityPrompt

        property string mode: ""
        property string headingText: ""
        property string errorText: ""
        // Only the change-PIN flow needs both fields; every other flow
        // needs exactly one.
        readonly property bool needsCurrent: mode === "changePin" || mode === "disableLock"
                                              || mode === "gateOn" || mode === "gateOff"
                                              || mode === "hlpOn" || mode === "hlpOff"
        readonly property bool needsNew: mode === "setPin" || mode === "changePin"

        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        width: Math.min(360, root.width - 32)
        padding: 20

        background: Rectangle {
            color: Theme.panel
            radius: Theme.shapeSheet
            border.width: 1
            border.color: Theme.line
        }

        function begin(newMode, heading) {
            securityPrompt.mode = newMode
            securityPrompt.headingText = heading
            securityPrompt.errorText = ""
            currentPinField.text = ""
            newPinField.text = ""
            confirmPinField.text = ""
            securityPrompt.open()
        }

        // Live PinPolicy verdict for whatever is currently typed, from the
        // same C++ rule AppLock.setPin() enforces -- so the dialog explains
        // a rejection before the user commits instead of just refusing.
        readonly property string newPinProblem:
            !securityPrompt.needsNew || newPinField.text.length === 0
                ? ""
                : AppLock.pinRejectionReason(newPinField.text)
        readonly property bool confirmMismatch:
            securityPrompt.needsNew && confirmPinField.text.length > 0
            && confirmPinField.text !== newPinField.text

        onOpened: {
            if (securityPrompt.needsCurrent)
                currentPinField.inputField.forceActiveFocus()
            else
                newPinField.inputField.forceActiveFocus()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: securityPrompt.headingText
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontSender
                font.bold: true
                wrapMode: Text.WordWrap
            }

            ThemedTextField {
                id: currentPinField
                Layout.fillWidth: true
                visible: securityPrompt.needsCurrent
                placeholderText: i18n("Current PIN")
                Component.onCompleted: {
                    inputField.echoMode = TextInput.Password
                    inputField.inputMethodHints = Qt.ImhDigitsOnly
                }
            }

            ThemedTextField {
                id: newPinField
                Layout.fillWidth: true
                visible: securityPrompt.needsNew
                placeholderText: i18np("New PIN (at least %1 digit)",
                                       "New PIN (at least %1 digits)", AppLock.minimumPinLength)
                Component.onCompleted: {
                    inputField.echoMode = TextInput.Password
                    inputField.inputMethodHints = Qt.ImhDigitsOnly
                }
            }

            // Second entry, because the PIN is unrecoverable by design: get
            // it wrong once and the only way back in is ten failed attempts
            // and a full local wipe. A single masked field with no
            // confirmation was one typo away from that.
            ThemedTextField {
                id: confirmPinField
                Layout.fillWidth: true
                visible: securityPrompt.needsNew
                placeholderText: i18n("Confirm new PIN")
                Component.onCompleted: {
                    inputField.echoMode = TextInput.Password
                    inputField.inputMethodHints = Qt.ImhDigitsOnly
                }
            }

            MutedHint {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: securityPrompt.newPinProblem !== "" || securityPrompt.confirmMismatch
                color: Theme.dangerActionText
                text: securityPrompt.confirmMismatch
                          ? i18n("The two PINs do not match.")
                          : securityPrompt.newPinProblem
            }

            Text {
                Layout.fillWidth: true
                visible: securityPrompt.errorText !== ""
                textFormat: Text.PlainText
                text: securityPrompt.errorText
                color: Theme.dangerActionText
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontDate
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                GhostButton {
                    text: i18n("Cancel")
                    onClicked: securityPrompt.close()
                }
                PrimaryButton {
                    text: i18n("Confirm")
                    enabled: (!securityPrompt.needsCurrent || currentPinField.text.length > 0)
                             && (!securityPrompt.needsNew
                                 || (securityPrompt.newPinProblem === ""
                                     && newPinField.text.length > 0
                                     && confirmPinField.text === newPinField.text))
                    onClicked: securityPrompt.submit()
                }
            }
        }

        function submit() {
            const current = currentPinField.text
            const fresh = newPinField.text
            let ok = false

            if (securityPrompt.mode === "setPin" || securityPrompt.mode === "changePin")
                ok = AppLock.setPin(current, fresh)
            else if (securityPrompt.mode === "disableLock")
                ok = AppLock.disableLock(current)
            else if (securityPrompt.mode === "gateOn")
                ok = AppLock.setCredentialPinGateEnabled(true, current)
            else if (securityPrompt.mode === "gateOff")
                ok = AppLock.setCredentialPinGateEnabled(false, current)
            else if (securityPrompt.mode === "hlpOn")
                ok = AppLock.setHostileLocationEnabled(true, current)
            else if (securityPrompt.mode === "hlpOff")
                ok = AppLock.setHostileLocationEnabled(false, current)
            else if (securityPrompt.mode === "eraseAfter")
                ok = AppLock.setWipeAfterAttempts(eraseThresholdChoice, current)
            else if (securityPrompt.mode === "backgroundGrace")
                ok = AppLock.setBackgroundGraceSeconds(graceSecondsChoice, current)

            if (ok) {
                securityPrompt.close()
                return
            }
            // Stay open so the user can retry without re-navigating.
            //
            // These calls now also refuse when the device secret could not
            // be re-wrapped under the new PIN (or unwrapped from the old
            // one), which is a different situation from a mistyped PIN and
            // has a different fix -- so the wording covers both rather than
            // implying the user got the PIN wrong. AppLockManager
            // deliberately fails closed there: leaving the lock on is
            // recoverable, stranding the pairing behind a destroyed key is
            // not.
            securityPrompt.errorText = AppLock.credentialPinGateEnabled
                ? i18n("Incorrect PIN, or the stored credentials could not be re-encrypted. If the "
                       + "PIN is right, check that your system keyring is unlocked and try again.")
                : i18n("Incorrect PIN, or the change could not be saved.")
            currentPinField.text = ""
        }
    }

    // ---- "Pair this device…" overlay -------------------------------------
    // Settings.qml manages its own nested popup rather than asking the host
    // to navigate anywhere -- keeps this file host-agnostic (same reasoning
    // as the signal-based navigation on EmailDetail/ContactsList/etc.)
    // instead of assuming a pageStack that DesktopRoot doesn't have.
    Popup {
        id: pairingPopup
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        width: Math.min(380, root.width - 32)
        height: Math.min(560, root.height - 32)
        padding: 0

        background: Rectangle {
            color: Theme.panel
            radius: Theme.shapeSheet
            border.width: 1
            border.color: Theme.line
        }

        contentItem: PagesDir.Pairing {
            anchors.fill: parent
            onClosed: pairingPopup.close()
        }
    }
}
