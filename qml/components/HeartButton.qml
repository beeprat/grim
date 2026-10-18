import QtQuick
import MusicPlayer

// Favorite toggle for one track; favorites show up under Playlists → Favorites.
IconButton {
    id: heart

    required property url url

    // Reads metaRevision so the binding re-runs when any favorite changes (see StarRating).
    readonly property bool favorite: LibraryController.metaRevision >= 0 && LibraryController.isFavorite(url)

    visible: url.toString() !== ""
    iconName: favorite ? "heart" : "heart_outline"
    iconSize: 18
    active: favorite
    tip: favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites")
    onClicked: LibraryController.toggleFavorite(url)
}
