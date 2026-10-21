.pragma library
// Window-size classes, as Android's layout-w600dp / list-detail panes:
// a bottom bar below 600, a nav rail from 600, list and detail side by side
// from 840 (list takes 0.4 of the content width).
var railMin = 600
var splitMin = 840
var listRatio = 0.4

function mode(width) {
    return width >= splitMin ? "expanded" : width >= railMin ? "medium" : "compact"
}
