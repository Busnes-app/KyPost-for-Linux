import QtQuick
import com.kysecurity.mail 1.0

// A group header: sentence case at the caller's casing, never all caps or
// tracked (the suite shell's label rule). Medium weight in ink, a step under
// body size. Group headers only, not body copy -- no interactive behavior,
// so this is a plain styled Text.
//
// `text` is Text's own built-in property, reused as-is: QML forbids
// shadowing a property already defined on the base type.
Text {
    id: root

    // The text comes from callers, and some of them pass relay strings
    // (ContactDetail's field labels are vCard data). Pinned here so no
    // call site has to remember.
    textFormat: Text.PlainText
    color: Theme.ink
    font.family: Theme.fontUi
    font.pixelSize: Theme.fontPreview
    font.weight: Font.Medium
}
