import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

// Star rating of the playing track, under the now-playing card.
Card {
    implicitHeight: 52

    StarRating {
        anchors.centerIn: parent
        // The ★ glyph sits low in its line box; nudge it up to look centered.
        anchors.verticalCenterOffset: -Math.round(size * 0.12)
        visible: PlayerController.hasTrack
        url: PlayerController.url
        size: 24
        spacing: 6
    }

    Label {
        anchors.centerIn: parent
        visible: !PlayerController.hasTrack
        text: qsTr("Rating")
        opacity: 0.35
        font.pixelSize: 12
    }
}
