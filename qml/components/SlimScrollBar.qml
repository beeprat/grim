import QtQuick
import QtQuick.Controls.Material
import MusicPlayer

// Thin scroll bar that only shows while scrolling or hovered, so it does not cover
// album covers at the right edge of the grid.
ScrollBar {
    id: bar

    implicitWidth: 8
    padding: 2
    minimumSize: 0.05
    background: null

    contentItem: Rectangle {
        implicitWidth: 4
        radius: 2
        color: Qt.rgba(1, 1, 1, bar.pressed ? 0.5 : bar.hovered ? 0.38 : 0.25)
        opacity: bar.active || bar.hovered || bar.pressed ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: 300 }
        }
    }
}
