import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

Item {
    id: page

    // Main shows the new-playlist window.
    signal newPlaylistRequested()

    function formatTime(ms) {
        const totalSeconds = Math.floor(ms / 1000)
        const seconds = totalSeconds % 60
        return Math.floor(totalSeconds / 60) + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    readonly property bool isFavorites: PlaylistController.currentId === 0

    Settings {
        id: layoutSettings

        category: "playlists"
        property real listWidth: 240 // dragged by the playlists card's right edge
    }

    Dialog {
        id: renameDialog

        property var playlistId: 0

        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Rename playlist")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: PlaylistController.renamePlaylist(playlistId, renameField.text)

        TextField {
            id: renameField

            width: 280
            onAccepted: renameDialog.accept()
        }
    }

    Dialog {
        id: deleteDialog

        property var playlistId: 0
        property string name

        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Delete playlist")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: PlaylistController.deletePlaylist(playlistId)

        Label {
            text: qsTr("Delete \"%1\"? The songs stay in your library.").arg(deleteDialog.name)
        }
    }

    CompactMenu {
        id: playlistMenu

        property var playlistId: 0
        property string name

        CompactMenuItem {
            text: qsTr("Rename…")
            onTriggered: {
                renameDialog.playlistId = playlistMenu.playlistId
                renameField.text = playlistMenu.name
                renameDialog.open()
                renameField.selectAll()
                renameField.forceActiveFocus()
            }
        }

        CompactMenuItem {
            text: qsTr("Delete…")
            onTriggered: {
                deleteDialog.playlistId = playlistMenu.playlistId
                deleteDialog.name = playlistMenu.name
                deleteDialog.open()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 12

        // Playlists
        Card {
            id: playlistsCard

            // Dragged narrower than kMinWidth, the card snaps to a strip of icons (kCompactWidth).
            readonly property real kMinWidth: 140
            readonly property real kCompactWidth: 72
            readonly property bool compact: layoutSettings.listWidth < kMinWidth

            Layout.preferredWidth: compact ? kCompactWidth : Math.min(layoutSettings.listWidth, page.width * 0.5)
            Layout.fillHeight: true
            // A shade darker than the tracks card, so the open playlist's row (in the tracks card's
            // color, joined to it by `tabBridge`) reads as a tab of that card.
            color: Qt.darker(Theme.panel, 1.6)

            // Drag the right edge to resize; it straddles the gap to the tracks card.
            MouseArea {
                property real startX
                property real startWidth

                x: parent.width - width / 2
                width: 12
                height: parent.height
                z: 1
                cursorShape: Qt.SizeHorCursor
                onPressed: (mouse) => {
                    startX = mapToItem(page, mouse.x, 0).x
                    startWidth = playlistsCard.width
                }
                onPositionChanged: (mouse) => {
                    const width = startWidth + mapToItem(page, mouse.x, 0).x - startX
                    // Snap: under about halfway between the two widths, collapse to icons.
                    layoutSettings.listWidth = width < (playlistsCard.kMinWidth + playlistsCard.kCompactWidth) / 2
                            ? playlistsCard.kCompactWidth
                            : Math.max(playlistsCard.kMinWidth, Math.min(width, page.width * 0.5))
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: playlistsCard.compact ? 0 : 4

                    Label {
                        Layout.fillWidth: true
                        visible: !playlistsCard.compact
                        text: qsTr("Playlists")
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }

                    // An icon, not a "+" glyph: the glyph sat below the title's center line.
                    IconButton {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: playlistsCard.compact
                        iconName: "add"
                        tip: qsTr("New playlist")
                        onClicked: page.newPlaylistRequested()
                    }
                }

                ListView {
                    id: playlistList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: PlaylistController.playlists
                    currentIndex: PlaylistController.playlists.findIndex(entry => entry.id === PlaylistController.currentId)
                    highlightFollowsCurrentItem: false

                    delegate: ItemDelegate {
                        id: entry

                        required property var modelData
                        readonly property bool current: modelData.id === PlaylistController.currentId

                        width: ListView.view.width
                        height: 44
                        leftPadding: playlistsCard.compact ? 0 : 16
                        rightPadding: playlistsCard.compact ? 0 : 16
                        onClicked: PlaylistController.currentId = modelData.id

                        ToolTip.text: modelData.name
                        ToolTip.visible: playlistsCard.compact && hovered
                        ToolTip.delay: 400

                        background: Rectangle {
                            radius: Math.min(Theme.radius, 10)
                            // The open row runs on into `tabBridge`: no rounding on that side.
                            topRightRadius: entry.current ? 0 : radius
                            bottomRightRadius: entry.current ? 0 : radius
                            color: entry.current ? Theme.panel : entry.hovered ? Theme.hover : "transparent"
                        }

                        contentItem: RowLayout {
                            spacing: 10

                            Image {
                                // Compact: the icon alone, centered in the row.
                                Layout.fillWidth: playlistsCard.compact
                                fillMode: Image.Pad
                                source: entry.modelData.id === 0 ? "../icons/heart.svg" : "../icons/playlist.svg"
                                sourceSize: Qt.size(18, 18)
                                opacity: entry.current ? 1 : 0.6
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: !playlistsCard.compact
                                text: entry.modelData.name
                                elide: Text.ElideRight
                                color: entry.current ? Theme.accent : Material.foreground
                            }

                            Label {
                                visible: !playlistsCard.compact
                                text: entry.modelData.trackCount
                                opacity: 0.5
                                font.pixelSize: 12
                            }
                        }

                        // Favorites is built in: no rename or delete.
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            enabled: entry.modelData.id !== 0
                            onTapped: {
                                playlistMenu.playlistId = entry.modelData.id
                                playlistMenu.name = entry.modelData.name
                                playlistMenu.popup()
                            }
                        }
                    }
                }
            }
        }

        // Tracks of the selected playlist
        Card {
            id: tracksCard

            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            Layout.fillWidth: true
                            text: PlaylistController.currentName
                            font.pixelSize: 24
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Label {
                            text: trackList.count === 1 ? qsTr("1 track") : qsTr("%1 tracks").arg(trackList.count)
                            opacity: 0.5
                        }
                    }

                    RoundButton {
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 48
                        enabled: trackList.count > 0
                        display: AbstractButton.IconOnly
                        icon.source: "../icons/play.svg"
                        icon.width: 26
                        icon.height: 26
                        icon.color: Theme.window
                        Material.background: Theme.accent
                        Material.elevation: 0
                        opacity: enabled ? 1 : 0.4
                        ToolTip.text: qsTr("Play")
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        onClicked: PlayerController.playQueue(PlaylistController.currentUrls(), 0)
                    }
                }

                ListView {
                    id: trackList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: PlaylistController.currentTracks
                    ScrollBar.vertical: SlimScrollBar {}
                    FastWheelScroll { flickable: trackList }

                    delegate: ItemDelegate {
                        id: row

                        required property int index
                        required property var modelData

                        width: ListView.view.width - 12
                        height: 56
                        // Same as the library rows: no Material insets, so the content is centered in
                        // the hover highlight.
                        leftPadding: 14
                        rightPadding: 14
                        topPadding: 0
                        bottomPadding: 0
                        topInset: 0
                        bottomInset: 0
                        onClicked: PlayerController.playTrack(PlaylistController.currentUrls(), index)

                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: ApplicationWindow.window.openTrackMenu([row.modelData.url])
                        }

                        background: Rectangle {
                            radius: Math.min(Theme.radius, 10)
                            color: row.hovered ? Theme.hover : "transparent"
                        }

                        contentItem: RowLayout {
                            spacing: 12

                            CoverArt {
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 40
                                source: row.modelData.coverUrl
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 3
                                spacing: 0

                                Label {
                                    Layout.fillWidth: true
                                    text: row.modelData.title
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: row.modelData.artist || qsTr("Unknown artist")
                                    elide: Text.ElideRight
                                    opacity: 0.6
                                    font.pixelSize: 12
                                }
                            }

                            // A link to the album page, with this track flashing.
                            Label {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 2
                                text: row.modelData.album
                                elide: Text.ElideRight
                                font.underline: albumMouse.containsMouse
                                opacity: albumMouse.containsMouse ? 1 : 0.6

                                MouseArea {
                                    id: albumMouse

                                    // Only over the text, not the empty rest of the column.
                                    width: Math.min(parent.contentWidth, parent.width)
                                    height: parent.height
                                    enabled: row.modelData.album !== ""
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: ApplicationWindow.window.showAlbumOf(row.modelData.url, true)
                                }
                            }

                            StarRating { url: row.modelData.url }

                            HeartButton { url: row.modelData.url }

                            Label {
                                Layout.preferredWidth: 36
                                horizontalAlignment: Text.AlignRight
                                text: row.modelData.durationMs > 0 ? page.formatTime(row.modelData.durationMs) : ""
                                opacity: 0.6
                                font.pixelSize: 12
                            }

                            ToolButton {
                                text: "✕"
                                opacity: row.hovered ? 0.7 : 0
                                ToolTip.text: page.isFavorites ? qsTr("Remove from favorites") : qsTr("Remove from playlist")
                                ToolTip.visible: hovered
                                ToolTip.delay: 500
                                onClicked: PlaylistController.removeTrack(row.index)
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: trackList.count === 0
                        width: parent.width * 0.8
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: page.isFavorites
                              ? qsTr("No favorites yet. Click the heart on a track to add it here.")
                              : qsTr("This playlist is empty. Right-click a track → Add to playlist.")
                        opacity: 0.5
                    }
                }
            }
        }
    }

    // Joins the open playlist's row to the tracks card across the card padding and the gap,
    // like a tab. Hidden while that row is scrolled out of view. The `>= -1e9` reads keep the
    // bindings updating on scroll and layout changes (a comma expression would lose them).
    Rectangle {
        id: tabBridge

        readonly property Item row: playlistList.currentItem
        readonly property real rowY: row && playlistList.contentY >= -1e9 && playlistsCard.width >= 0
                                     ? row.mapToItem(page, 0, 0).y : 0
        readonly property real listTop: playlistsCard.height >= 0 ? playlistList.mapToItem(page, 0, 0).y : 0
        // Starts where the row ends and stops at the tracks card, overlapping neither: the panel color
        // is see-through over a background image, and an overlap would show as a darker strip.
        readonly property real listRight: playlistsCard.width >= 0 && playlistsCard.x >= -1e9
                                          ? playlistList.mapToItem(page, playlistList.width, 0).x : 0

        x: Math.round(listRight)
        width: Math.round(tracksCard.x) - x
        y: rowY
        height: row ? row.height : 0
        color: Theme.panel
        visible: row !== null && rowY >= listTop && rowY + height <= listTop + playlistList.height
    }
}
