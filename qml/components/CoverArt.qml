import QtQuick
import MusicPlayer

// Album art with a music-note placeholder while loading or when a file has none.
Rectangle {
    id: cover

    property string source

    radius: Math.min(Theme.radius * 0.6, width / 4)
    color: Theme.subtle
    clip: true

    Image {
        anchors.centerIn: parent
        width: parent.width * 0.4
        height: width
        source: "../icons/music_note.svg"
        sourceSize: Qt.size(width, height)
        opacity: 0.3
        visible: art.status !== Image.Ready
    }

    Image {
        id: art

        anchors.fill: parent
        source: cover.source
        // Decoded at 1.5x the pixels it covers on screen (logical size x interface zoom x display
        // scaling): the slight oversampling keeps covers as sharp as before, where exactly 1x looked
        // soft. Zooming in re-requests a sharper copy; the old fixed 2x of the logical size wasted
        // memory at normal zoom.
        readonly property real pixelScale: 1.5 * Theme.uiScale * Screen.devicePixelRatio

        sourceSize: Qt.size(Math.ceil(cover.width * pixelScale), Math.ceil(cover.height * pixelScale))
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        opacity: status === Image.Ready ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }
    }
}
