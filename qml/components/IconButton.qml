import QtQuick
import QtQuick.Controls.Material
import MusicPlayer

// Round icon-only button; `active` tints it with the accent (shuffle/repeat on).
ToolButton {
    id: button

    required property string iconName // file in qml/icons without .svg
    property string tip
    property bool active: false
    property int iconSize: 20

    display: AbstractButton.IconOnly
    icon.source: "../icons/" + iconName + ".svg"
    icon.width: iconSize
    icon.height: iconSize
    icon.color: active ? Theme.accent : Material.foreground
    opacity: enabled ? 1 : 0.35

    ToolTip.text: tip
    ToolTip.visible: hovered && tip !== ""
    ToolTip.delay: 600
}
