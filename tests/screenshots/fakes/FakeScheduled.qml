import QtQuick

QtObject {
    property bool busy: false
    property string message: ""
    property var items: [{id: "s1", subject: "Q4 roadmap: final review", to: "Jordan Ellis <jordan@northwind.example>",
                          when: "Thu Oct 22, 9:00 AM", status: "scheduled", reason: "", cancellable: true}]
    property var presets: [{label: "Tomorrow morning", when: "08:00"}, {label: "Monday morning", when: "08:00"}]
    function refresh() {}
    function cancel() {}
    function customTime() { return new Date() }
}
