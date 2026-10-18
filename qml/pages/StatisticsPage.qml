import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

Item {
    id: page

    function formatDuration(ms) {
        const totalMinutes = Math.floor(ms / 60000)
        const hours = Math.floor(totalMinutes / 60)
        const minutes = totalMinutes % 60
        return hours > 0 ? qsTr("%1h %2m").arg(hours).arg(minutes) : qsTr("%1m").arg(minutes)
    }

    // "3h 12m · 40 plays" for a highlight card.
    function highlightDetail(entry) {
        if (!entry || entry.title === undefined)
            return ""
        const plays = entry.playCount === 1 ? qsTr("1 play") : qsTr("%1 plays").arg(entry.playCount)
        return formatDuration(entry.listenedMs) + "  ·  " + plays
    }

    // Re-read on every visit, and every few seconds while open: the player saves
    // the running track's listening time as it plays.
    onVisibleChanged: if (visible) StatsController.refresh()
    Component.onCompleted: StatsController.refresh()

    Timer {
        interval: 5000
        repeat: true
        running: page.visible
        onTriggered: StatsController.refresh()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // The number one of each list; same click targets as the lists below.
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            TopHighlight {
                label: qsTr("Top track")
                entry: StatsController.topTrack
                detail: page.highlightDetail(entry)
                onClicked: ApplicationWindow.window.showAlbumOf(entry.url, true)
            }

            TopHighlight {
                label: qsTr("Top artist")
                entry: StatsController.topArtist
                detail: page.highlightDetail(entry)
                onClicked: ApplicationWindow.window.showArtist(entry.title)
            }

            TopHighlight {
                label: qsTr("Top album")
                entry: StatsController.topAlbum
                detail: page.highlightDetail(entry)
                onClicked: ApplicationWindow.window.showAlbumOf(entry.url, false)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            StatTile {
                label: qsTr("Listening time")
                value: page.formatDuration(StatsController.totalListenedMs)
            }

            StatTile {
                label: qsTr("Plays (30 s or more)")
                value: Number(StatsController.totalPlays).toLocaleString(Qt.locale(), "f", 0)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // Clicking jumps to the library: a track's album (with the track flashing),
            // an artist's albums, or an album.
            RankedList {
                title: qsTr("Top tracks")
                listModel: StatsController.topTracks
                onEntryClicked: (fileUrl) => ApplicationWindow.window.showAlbumOf(fileUrl, true)
            }

            RankedList {
                title: qsTr("Top artists")
                listModel: StatsController.topArtists
                onEntryClicked: (fileUrl, name) => ApplicationWindow.window.showArtist(name)
            }

            RankedList {
                title: qsTr("Top albums")
                listModel: StatsController.topAlbums
                onEntryClicked: (fileUrl) => ApplicationWindow.window.showAlbumOf(fileUrl, false)
            }
        }
    }
}
