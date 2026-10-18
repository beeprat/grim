import QtQuick
import QtQuick.Controls.Material
import MusicPlayer

// Right-click menu for a track, or for an album (several tracks): play, view album,
// rate (single track only), open containing folder, add to playlist.
// One instance lives in Main; pages open it with ApplicationWindow.window.openTrackMenu().
CompactMenu {
    // Not "menu": every MenuItem has its own `menu` property (its parent menu), which
    // shadows such an id inside the submenus.
    id: root

    property var urls: [] // file:// URLs; the first one is "the" track for album/folder/rating
    property bool isAlbum: false // opened on an album: offers deleting it
    readonly property url firstUrl: urls.length > 0 ? urls[0] : ""

    signal viewAlbumRequested(url fileUrl)
    signal newPlaylistRequested(var fileUrls)
    signal deleteAlbumRequested(var fileUrls)

    CompactMenuItem {
        text: root.urls.length > 1 ? qsTr("Play album") : qsTr("Play")
        onTriggered: PlayerController.playQueue(root.urls, 0)
    }

    CompactMenuItem {
        text: qsTr("View album")
        onTriggered: root.viewAlbumRequested(root.firstUrl)
    }

    CompactMenu {
        id: rateMenu

        implicitWidth: 170

        title: qsTr("Rate")
        enabled: root.urls.length === 1

        Repeater {
            model: 5

            delegate: CompactMenuItem {
                required property int index
                readonly property int stars: 5 - index

                text: "★".repeat(stars) + "☆".repeat(5 - stars)
                checkable: true
                checked: LibraryController.metaRevision >= 0 && LibraryController.rating(root.firstUrl) === stars
                onTriggered: LibraryController.setRating(root.firstUrl, stars)
            }
        }

        MenuSeparator {}

        CompactMenuItem {
            text: qsTr("Clear rating")
            onTriggered: LibraryController.setRating(root.firstUrl, 0)
        }
    }

    CompactMenuItem {
        text: qsTr("Open containing folder")
        onTriggered: LibraryController.showInFolder(root.firstUrl)
    }

    CompactMenu {
        id: playlistMenu

        implicitWidth: 170

        title: qsTr("Add to playlist")

        // Favorites and every playlist, rebuilt when the list changes.
        Instantiator {
            model: PlaylistController.playlists

            delegate: CompactMenuItem {
                required property var modelData

                text: modelData.name
                onTriggered: PlaylistController.addTracks(modelData.id, root.urls)
            }

            onObjectAdded: (index, item) => playlistMenu.insertItem(index, item)
            onObjectRemoved: (index, item) => playlistMenu.removeItem(item)
        }

        MenuSeparator {}

        CompactMenuItem {
            text: qsTr("New playlist…")
            onTriggered: root.newPlaylistRequested(root.urls)
        }
    }

    CompactMenuItem {
        visible: root.isAlbum
        height: visible ? implicitHeight : 0
        text: qsTr("Delete album…")
        Material.foreground: Material.color(Material.Red, Material.Shade300)
        onTriggered: root.deleteAlbumRequested(root.urls)
    }
}
