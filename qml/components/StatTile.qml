import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

Card {
    id: tile

    required property string label
    required property string value

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitHeight: Math.max(110, column.implicitHeight + 32) // grows with the interface size

    Column {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 20
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        Label {
            width: parent.width
            text: tile.label
            opacity: 0.6
            font.pixelSize: 13
            elide: Text.ElideRight
        }

        Label {
            width: parent.width
            text: tile.value
            font.pixelSize: 32
            font.weight: Font.DemiBold
            fontSizeMode: Text.HorizontalFit // a narrow tile shrinks the number rather than spilling it
            minimumPixelSize: 12
        }
    }
}
