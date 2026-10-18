import QtQuick
import MusicPlayer

// Panel behind menus and drop-down lists: a card-like box with a thin outline, smaller
// and flatter than Material's elevated sheet. A shade lighter than the cards it opens over.
Rectangle {
    implicitWidth: 160
    color: Qt.lighter(Theme.card, 1.25)
    radius: 10
    border.color: Theme.subtle
}
