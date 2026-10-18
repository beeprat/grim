import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

Card {
    id: tile

    required property string label
    required property string value

    Layout.fillWidth: true
    implicitHeight: 110

    Column {
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        Label {
            text: tile.label
            opacity: 0.6
            font.pixelSize: 13
        }

        Label {
            text: tile.value
            font.pixelSize: 32
            font.weight: Font.DemiBold
        }
    }
}
