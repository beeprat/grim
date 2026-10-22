import QtQuick
import QtQuick.Controls.Material
import MusicPlayer

// Lyrics of the playing track. Synced lyrics follow playback (current line about a third down);
// unsynced ones are shown as plain text. Click a synced line to jump there.
Card {
    ListView {
        id: lyrics

        anchors.fill: parent
        anchors.margins: 16
        clip: true
        spacing: 6
        model: PlayerController.lyrics
        currentIndex: PlayerController.lyricsSynced ? PlayerController.lyricIndex : -1
        highlightRangeMode: PlayerController.lyricsSynced ? ListView.ApplyRange : ListView.NoHighlightRange
        preferredHighlightBegin: height / 3
        preferredHighlightEnd: height / 3 + 40
        highlightMoveDuration: 400
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: SlimScrollBar {}

        delegate: Label {
            id: line

            required property int index
            required property var modelData
            readonly property bool current: index === lyrics.currentIndex

            width: ListView.view.width - 8
            // An empty synced line is an instrumental gap.
            text: modelData.text !== "" ? modelData.text : (modelData.time >= 0 ? "♪" : " ")
            wrapMode: Text.Wrap
            font.pixelSize: 14
            font.weight: current ? Font.DemiBold : Font.Normal
            color: current ? Theme.accent : Material.foreground
            opacity: !PlayerController.lyricsSynced ? 0.75
                     : current ? 1
                     : index < lyrics.currentIndex ? 0.3 : 0.5

            Behavior on opacity {
                NumberAnimation { duration: 200 }
            }

            MouseArea {
                anchors.fill: parent
                enabled: line.modelData.time >= 0
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: PlayerController.seek(line.modelData.time)
            }
        }

        Label {
            anchors.centerIn: parent
            visible: lyrics.count === 0
            text: !PlayerController.hasTrack ? qsTr("Lyrics")
                  : PlayerController.lyricsLoading ? qsTr("Searching lyrics online…")
                  : qsTr("No lyrics found")
            opacity: 0.35
            font.pixelSize: 12
        }
    }

    EdgeFade {
        flickable: lyrics
        // The card is short; a quarter of it keeps the fade from eating the lyrics.
        bottomHeight: Math.min(100, lyrics.height / 4)
    }
}
