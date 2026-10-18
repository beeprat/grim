import QtQuick
import QtQuick.Controls.Material
import MusicPlayer

// Menu row for CompactMenu: 30 px, small text, a rounded highlight inset from the menu's edges.
MenuItem {
    id: item

    implicitHeight: 30
    font.pixelSize: 12
    leftPadding: 14
    rightPadding: 14
    leftInset: 4
    rightInset: 4
    background: Rectangle {
        radius: 6
        color: item.highlighted ? Theme.hover : "transparent"
    }
}
