import QtQuick
import QtQuick.Controls.Material
import MusicPlayer

// ComboBox whose list drops down under it in compact rows, styled like the context menu.
ComboBox {
    id: combo

    font.pixelSize: 13

    delegate: ItemDelegate {
        id: option

        required property int index

        width: ListView.view.width
        implicitHeight: 30
        leftPadding: 12
        rightPadding: 12
        text: combo.textAt(index)
        font.pixelSize: 13
        highlighted: combo.highlightedIndex === index
        // Rounded highlight inset from the list's rounded edges.
        leftInset: 4
        rightInset: 4
        background: Rectangle {
            radius: 6
            color: option.highlighted ? Theme.hover : "transparent"
        }
        Material.foreground: combo.currentIndex === index ? Theme.accent : combo.Material.foreground
    }

    popup.y: height + 4
    popup.topPadding: 4
    popup.bottomPadding: 4
    popup.background: PopupBackground {}
}
