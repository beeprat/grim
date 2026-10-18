import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

// The current track: cover, title, album, artist, and the favorite / lyrics buttons.
// Fills its panel; the cover takes the room the text leaves.
Card {
    id: panel

    // The album or artist name was clicked; Main opens it in the library.
    signal albumClicked()
    signal artistClicked()

    // Set by the classic (default) layout: the cover size, as the side column had it; the panel's
    // implicitHeight then fits it. -1: the cover takes the room the text leaves.
    property real fixedCoverSide: -1
    // What the title, album and artist sizes scale with. Main passes the classic cover size, so the
    // text stays the same when the panel is moved or resized (it only follows window size and zoom).
    property real textScale: fixedCoverSide >= 0 ? fixedCoverSide : Math.min(width - 32, height)

    implicitHeight: info.implicitHeight + 32

    ColumnLayout {
        id: info

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: 16
        spacing: 0

        CoverArt {
            id: cover

            // Square: as wide as the card, as tall as the room the text below leaves.
            readonly property real side: panel.fixedCoverSide >= 0 ? panel.fixedCoverSide
                                         : Math.max(0, Math.min(panel.width - 32,
                                                                panel.height - 32 - 10 - textBlock.implicitHeight))

            Layout.preferredWidth: side
            Layout.preferredHeight: side
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 10
            source: PlayerController.coverUrl
        }

        // Title, album and artist on the left; the buttons stacked on the right, so long
        // titles get the full width instead of sharing it with three buttons.
        RowLayout {
            id: textBlock

            Layout.fillWidth: true
            spacing: 4

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 0

                Label {
                    Layout.fillWidth: true
                    text: PlayerController.hasTrack ? PlayerController.title : qsTr("Nothing playing")
                    // Long titles get a second line before they are cut.
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.pixelSize: Math.max(14, Math.round(panel.textScale * 0.07))
                    font.weight: Font.DemiBold
                    opacity: PlayerController.hasTrack ? 1 : 0.5
                }

                Label {
                    id: albumLabel

                    Layout.fillWidth: true
                    visible: text !== ""
                    text: PlayerController.album
                    elide: Text.ElideRight
                    font.pixelSize: Math.max(12, Math.round(panel.textScale * 0.055))
                    font.underline: albumMouse.containsMouse
                    opacity: albumMouse.containsMouse ? 1 : 0.75

                    MouseArea {
                        id: albumMouse

                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panel.albumClicked()
                    }

                    ToolTip.text: qsTr("Show album in the library")
                    ToolTip.visible: albumMouse.containsMouse
                    ToolTip.delay: 600
                }

                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: PlayerController.artist
                    elide: Text.ElideRight
                    font.pixelSize: Math.max(11, Math.round(panel.textScale * 0.05))
                    font.underline: artistMouse.containsMouse
                    opacity: artistMouse.containsMouse ? 1 : 0.55

                    MouseArea {
                        id: artistMouse

                        // Only over the name, not the empty rest of the line.
                        width: Math.min(parent.contentWidth, parent.width)
                        height: parent.height
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panel.artistClicked()
                    }

                    ToolTip.text: qsTr("Show this artist's albums")
                    ToolTip.visible: artistMouse.containsMouse
                    ToolTip.delay: 600
                }
            }

            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                spacing: 0

                // Compact: full-size tool buttons would make the stack much taller than the text.
                HeartButton {
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 26
                    padding: 0
                    topInset: 0
                    bottomInset: 0
                    background: Item {} // just the heart: no hover highlight or ripple here
                    visible: PlayerController.hasTrack
                    url: PlayerController.url
                }

                IconButton {
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 26
                    padding: 0
                    topInset: 0
                    bottomInset: 0
                    background: Item {}
                    iconName: "lyrics"
                    iconSize: 18
                    tip: ApplicationWindow.window.lyricsVisible ? qsTr("Hide lyrics") : qsTr("Show lyrics")
                    active: ApplicationWindow.window.lyricsVisible
                    onClicked: ApplicationWindow.window.lyricsVisible = !ApplicationWindow.window.lyricsVisible
                }
            }
        }

    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        enabled: PlayerController.hasTrack
        onTapped: ApplicationWindow.window.openTrackMenu([PlayerController.url])
    }
}
