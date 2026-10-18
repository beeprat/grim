import QtQuick
import QtQuick.Controls.Material
import MusicPlayer

// Filled button in the accent color with dark text. Material's highlighted
// Button always draws white text, unreadable on the light grey accents.
AbstractButton {
    id: button

    implicitWidth: label.implicitWidth + 36
    implicitHeight: 38
    hoverEnabled: true
    opacity: enabled ? 1 : 0.4

    background: Rectangle {
        radius: height / 2
        color: button.pressed ? Qt.darker(Theme.accent, 1.25)
             : button.hovered ? Qt.lighter(Theme.accent, 1.1)
             : Theme.accent
    }

    contentItem: Label {
        id: label

        text: button.text
        color: Theme.window
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
