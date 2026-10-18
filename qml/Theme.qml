pragma Singleton

import QtCore
import QtQuick

// App-wide colors and shape, customizable on the Settings page and remembered
// between runs. Black background and red accent by default.
QtObject {
    id: theme

    // Choices offered on the Settings page.
    readonly property var accents: [
        { name: "Silver", color: "#c4c4c4" },
        { name: "White", color: "#f2f2f2" },
        { name: "Graphite", color: "#8c8c8c" },
        { name: "Blue", color: "#7aa7e0" },
        { name: "Green", color: "#7cc49a" },
        { name: "Red", color: "#e07a7a" }
    ]
    readonly property var backgrounds: [
        { name: "Black", window: "#000000", card: "#111111" },
        { name: "Charcoal", window: "#0b0b0b", card: "#191919" },
        { name: "Graphite", window: "#151515", card: "#222222" }
    ]

    // Stored choices.
    property int accentIndex: 5 // Red
    property int backgroundIndex: 0
    property int radius: 14
    property real uiScale: 1.0 // also Ctrl + mouse wheel
    // Settings → Experimental: show the arrange-panels button in the sidebar.
    property bool arrangePanels: false

    // Derived colors used by every page.
    readonly property color accent: accents[Math.min(accentIndex, accents.length - 1)].color
    readonly property color window: backgrounds[Math.min(backgroundIndex, backgrounds.length - 1)].window
    readonly property color card: backgrounds[Math.min(backgroundIndex, backgrounds.length - 1)].card
    readonly property color hover: Qt.rgba(1, 1, 1, 0.06)
    readonly property color selected: Qt.rgba(1, 1, 1, 0.12)
    readonly property color subtle: Qt.rgba(1, 1, 1, 0.08) // badges, placeholders, outlines

    property Settings store: Settings {
        category: "theme"
        property alias accentIndex: theme.accentIndex
        property alias backgroundIndex: theme.backgroundIndex
        property alias radius: theme.radius
        property alias uiScale: theme.uiScale
        property alias arrangePanels: theme.arrangePanels
    }
}
