import QtQuick
import QtQuick.Controls
import com.kysecurity.mail 1.0

// Icon-only button for toolbars and action rows, Android style: a 24px
// bundled Material icon (qrc:/qml/icons, see MaterialIcon) in a round hit
// area. `variant` picks the ghost, primary or danger colours, or "outline":
// the smaller stroked circle of Android's compose format buttons.
Rectangle {
    id: root

    property string icon: ""
    property string tooltip: ""
    property string variant: "ghost" // "ghost" | "primary" | "danger" | "outline"
    signal clicked()

    readonly property int buttonSize: variant === "outline" ? 40 : 44
    readonly property int iconSize: variant === "outline" ? 20 : 24

    implicitWidth: buttonSize
    implicitHeight: buttonSize
    radius: Math.min(width, height) / 2

    readonly property color restColor: variant === "primary" ? Theme.accent
        : (variant === "danger" ? Theme.dangerActionFill : "transparent")
    readonly property color hoverColor: variant === "primary" ? Theme.accent
        : (variant === "danger" ? Theme.dangerActionFill : Theme.panel)
    readonly property color iconColor: variant === "primary" ? Theme.readableOnAccent
        : (variant === "danger" ? Theme.dangerActionText : Theme.inkStrong)

    color: (hoverHandler.hovered && !tapHandler.pressed) ? hoverColor : restColor
    // Android's toolbar icons are borderless; danger and outline keep a stroke.
    border.width: variant === "danger" || variant === "outline" ? 1 : 0
    border.color: variant === "danger" ? Theme.dangerActionBorder : Theme.line
    opacity: !root.enabled ? 0.5 : (tapHandler.pressed ? 0.85 : (hoverHandler.hovered ? 0.92 : 1.0))

    Behavior on color {
        ColorAnimation { duration: 120 }
    }
    Behavior on opacity {
        NumberAnimation { duration: 120 }
    }

    MaterialIcon {
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        name: root.icon
        tint: root.iconColor
    }

    HoverHandler {
        id: hoverHandler
        enabled: root.enabled
    }

    activeFocusOnTab: root.enabled
    Keys.onReturnPressed: if (root.enabled && !AppLock.locked) root.clicked()
    Keys.onSpacePressed: if (root.enabled && !AppLock.locked) root.clicked()
    FocusRing { cornerRadius: root.radius }

    TapHandler {
        id: tapHandler
        enabled: root.enabled
        onTapped: root.clicked()
    }

    // Icon-only, so the tooltip is the only name a screen reader can speak.
    Accessible.role: Accessible.Button
    Accessible.name: root.tooltip
    // The PIN overlay stops pointer and key input from reaching a button
    // behind it, but an accessibility action is delivered to the item
    // directly. Without the lock check, a reader left open under the lock
    // could still be archived or deleted that way. MailController refuses
    // the mailbox write too; this keeps every other action shut as well.
    Accessible.onPressAction: if (root.enabled && !AppLock.locked) root.clicked()

    ToolTip.visible: root.tooltip !== "" && hoverHandler.hovered
    ToolTip.text: root.tooltip
    ToolTip.delay: 500
}
