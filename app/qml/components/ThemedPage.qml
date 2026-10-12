import QtQuick
import org.kde.kirigami as Kirigami
import com.kysecurity.mail 1.0

// A Kirigami.Page in the app theme. Kirigami paints pages and their title
// bar from the platform palette, which put this app's light-on-dark text on
// light grey. A Window-level override does not reach items, and the title bar
// is built outside the page with inherit: false, so both are painted here.
Kirigami.Page {
    id: page

    function paint(theme, background) {
        theme.inherit = false
        theme.backgroundColor = Qt.binding(() => background())
        theme.alternateBackgroundColor = Qt.binding(() => Theme.panel)
        theme.textColor = Qt.binding(() => Theme.inkStrong)
        theme.disabledTextColor = Qt.binding(() => Theme.ink)
        theme.highlightColor = Qt.binding(() => Theme.accent)
        theme.highlightedTextColor = Qt.binding(() => Theme.readableOnAccent)
        theme.hoverColor = Qt.binding(() => Theme.accent)
        theme.focusColor = Qt.binding(() => Theme.accent)
        theme.linkColor = Qt.binding(() => Theme.accent)
    }

    background: Rectangle { color: Theme.bg }
    Component.onCompleted: paint(Kirigami.Theme, () => Theme.bg)
    onGlobalToolBarItemChanged: {
        if (globalToolBarItem)
            paint(globalToolBarItem.Kirigami.Theme, () => Theme.panel)
    }
}
