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
        { name: "Red", color: "#e07a7a" },
        { name: "Purple", color: "#b392e3" },
        { name: "Orange", color: "#e3a06e" },
        { name: "Teal", color: "#6cc5bf" },
        { name: "Pink", color: "#e38bb6" },
        { name: "Yellow", color: "#e0cd78" }
    ]
    readonly property var backgrounds: [
        { name: "Black", window: "#000000", card: "#111111" },
        { name: "Charcoal", window: "#0b0b0b", card: "#191919" },
        { name: "Graphite", window: "#151515", card: "#222222" },
        { name: "Midnight", window: "#05070d", card: "#111723" },
        { name: "Forest", window: "#050906", card: "#111b15" },
        { name: "Plum", window: "#09060b", card: "#1c1422" },
        { name: "Mocha", window: "#0b0806", card: "#1e1814" },
        { name: "Ocean", window: "#04090b", card: "#0f1c21" }
    ]
    // Color themes: a background and an accent that go together (indexes into the lists above).
    readonly property var themes: [
        { name: "Default", background: 0, accent: 5 },
        { name: "Midnight", background: 3, accent: 3 },
        { name: "Forest", background: 4, accent: 4 },
        { name: "Plum", background: 5, accent: 6 },
        { name: "Mocha", background: 6, accent: 7 },
        { name: "Ocean", background: 7, accent: 8 }
    ]

    function applyTheme(index) {
        backgroundIndex = themes[index].background
        accentIndex = themes[index].accent
    }

    // Stored choices.
    property int accentIndex: 5 // Red
    property int backgroundIndex: 0
    property int radius: 14
    property real uiScale: 1.0 // also Ctrl + mouse wheel
    // Settings → Experimental: show the arrange-panels button in the sidebar.
    property bool arrangePanels: false
    // Settings → Appearance: a picture shown blurred behind the panels (a copy in the data folder,
    // SettingsController.importBackground); empty = plain background color.
    property string backgroundImage: ""

    // Derived colors used by every page.
    readonly property color accent: accents[Math.min(accentIndex, accents.length - 1)].color
    readonly property color window: backgrounds[Math.min(backgroundIndex, backgrounds.length - 1)].window
    readonly property color card: backgrounds[Math.min(backgroundIndex, backgrounds.length - 1)].card
    // Panel fill: the card color, see-through over a background image.
    readonly property color panel: backgroundImage === "" ? card : Qt.rgba(card.r, card.g, card.b, 0.72)
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
        property alias backgroundImage: theme.backgroundImage
    }
}
