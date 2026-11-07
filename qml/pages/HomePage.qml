import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import QtQuick.Controls.impl
import MusicPlayer

// Start page: greeting, library counts, album of the day, then rows of albums (or favorite
// songs) to jump back into. Clicking one opens it in the Library.
Card {
    id: page

    readonly property int rowLimit: 20

    property var recentlyAdded: []
    property var recentlyPlayed: []
    property var favorites: []
    property var mostPlayed: []
    property var topRated: []
    property var neverPlayed: []
    property int favoriteCount: 0

    function reload() {
        if (!visible)
            return
        recentlyAdded = LibraryController.albums.recentlyAdded(rowLimit)
        recentlyPlayed = StatsController.homeAlbums(true, rowLimit)
        favorites = LibraryController.favoriteTracks(rowLimit)
        mostPlayed = StatsController.homeAlbums(false, rowLimit)
        topRated = LibraryController.albums.topRated(rowLimit)
        // shortcut: counts favorites by fetching them all; add a count API if libraries get huge.
        favoriteCount = LibraryController.favoriteTracks(1000000).length
        // shortcut: fetches every album for Never played; add a count/at(index) API if libraries get huge.
        const all = LibraryController.albums.recentlyAdded(1000000)
        neverPlayed = StatsController.neverPlayed(all, rowLimit)
        StatsController.refresh()
    }

    function greeting() {
        const hour = new Date().getHours()
        return hour < 12 ? qsTr("Good morning!") : hour < 18 ? qsTr("Good afternoon!") : qsTr("Good evening!")
    }

    onVisibleChanged: reload()
    Component.onCompleted: reload()
    Connections {
        target: LibraryController.albums
        function onCountChanged() { page.reload() }
    }
    Connections {
        target: LibraryController
        function onMetaChanged() { page.reload() }
    }

    // Number + label chip under the greeting.
    component CountChip: Rectangle {
        id: chip

        property string iconName
        property string value
        property string label

        implicitWidth: chipRow.implicitWidth + 32
        implicitHeight: 64
        radius: Theme.radius
        color: Theme.hover

        RowLayout {
            id: chipRow
            anchors.centerIn: parent
            spacing: 12

            IconImage {
                source: Qt.resolvedUrl("../icons/" + chip.iconName + ".svg")
                sourceSize: Qt.size(28, 28)
                color: Theme.accent
            }
            ColumnLayout {
                spacing: 0
                Label { text: chip.value; font.pixelSize: 20; font.weight: Font.DemiBold }
                Label { text: chip.label; font.pixelSize: 12; opacity: 0.6 }
            }
        }
    }

    // One titled, sideways-scrolling row of tiles; hidden while empty.
    component HomeRow: ColumnLayout {
        id: row

        property string title
        property var entries: []
        property bool songs: false // entries are songs: open their album with the song lit up

        Layout.fillWidth: true
        visible: entries.length > 0
        spacing: 4

        // Divider above each row.
        Rectangle {
            Layout.fillWidth: true
            Layout.bottomMargin: 8
            height: 1
            color: Theme.subtle
        }

        Label {
            text: row.title
            font.pixelSize: 17
            font.weight: Font.DemiBold
        }

        ListView {
            id: list

            // As many whole tiles as fit at ~166 px, stretched to fill the row with no gap left over.
            readonly property int columns: Math.max(1, Math.floor((width + spacing) / (166 + spacing)))
            readonly property real tileWidth: (width - spacing * (columns - 1)) / columns

            Layout.fillWidth: true
            Layout.preferredHeight: contentItem.childrenRect.height // exactly the tiles, no slack under them
            orientation: ListView.Horizontal
            spacing: 2
            clip: true
            model: row.entries

            // The wheel over the left three quarters scrolls the row sideways (while it overflows); over the
            // right quarter it goes on to the page, which scrolls up and down.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                onWheel: (wheel) => {
                    if (wheel.x > width * 0.75 || list.contentWidth <= list.width) {
                        wheel.accepted = false
                        return
                    }
                    const delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x
                    // Keep adding to the animation's target while a scroll is still running.
                    const from = sideScroll.running ? sideScroll.to : list.contentX
                    sideScroll.stop()
                    sideScroll.to = Math.max(0, Math.min(list.contentWidth - list.width, from - delta * 2))
                    sideScroll.start()
                }
            }

            NumberAnimation {
                id: sideScroll
                target: list
                property: "contentX"
                duration: 180
                easing.type: Easing.OutCubic
            }

            EdgeFade {
                flickable: list
                horizontal: true
                topHeight: 48
                bottomHeight: 48
            }

            delegate: ItemDelegate {
                id: tile

                required property var modelData

                width: list.tileWidth
                // Hover box: 8 px around the cover and text, corners following the cover's.
                padding: 8
                background: Rectangle {
                    radius: Math.min(Theme.radius * 0.6, tile.width / 4) + tile.padding
                    color: Theme.hover
                    opacity: tile.hovered ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 120 } }
                }
                onClicked: ApplicationWindow.window.showAlbumOf(modelData.url, row.songs)

                // Right-click: the shared track menu, for the song or the whole album.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: {
                        const albums = LibraryController.albums
                        if (row.songs)
                            ApplicationWindow.window.openTrackMenu([tile.modelData.url])
                        else
                            ApplicationWindow.window.openTrackMenu(albums.urlsOf(albums.rowForTrack(tile.modelData.url)), true)
                    }
                }

                contentItem: ColumnLayout {
                    spacing: 0

                    CoverArt {
                        Layout.fillWidth: true
                        Layout.preferredHeight: width
                        Layout.bottomMargin: 6
                        source: tile.modelData.coverUrl

                        // Album not opened since it was added (opening it in the Library marks it seen).
                        Rectangle {
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 6
                            visible: !row.songs && LibraryController.unseenCount > 0
                                     && LibraryController.anyUnseen(LibraryController.albums.urlsOf(
                                            LibraryController.albums.rowForTrack(tile.modelData.url)))
                            width: newLabel.implicitWidth + 12
                            height: newLabel.implicitHeight + 4
                            radius: height / 2
                            color: Theme.accent

                            Label {
                                id: newLabel

                                anchors.centerIn: parent
                                text: qsTr("New")
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                color: Theme.window
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: tile.modelData.title
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: tile.modelData.artist || qsTr("Unknown artist")
                        elide: Text.ElideRight
                        opacity: 0.6
                        font.pixelSize: 12
                    }
                }
            }
        }
    }

    Flickable {
        id: flick

        anchors.fill: parent
        anchors.margins: 24
        contentHeight: rows.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: SlimScrollBar {}
        FastWheelScroll { flickable: flick }

        ColumnLayout {
            id: rows

            width: flick.width
            spacing: 12

            // Greeting and counts.
            RowLayout {
                Layout.fillWidth: true
                spacing: 24

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 20

                    ColumnLayout {
                        spacing: 4
                        Label { text: page.greeting(); font.pixelSize: 40; font.weight: Font.Bold }
                        Label { text: qsTr("grim %1").arg(Qt.application.version); font.pixelSize: 18; opacity: 0.85 }
                        Rectangle { Layout.topMargin: 10; width: 50; height: 3; radius: 1.5; color: Theme.accent }
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 12
                        CountChip { iconName: "music_note"; value: LibraryController.tracks.count.toLocaleString(Qt.locale(), "f", 0); label: qsTr("Tracks") }
                        CountChip { iconName: "library"; value: LibraryController.albums.count.toLocaleString(Qt.locale(), "f", 0); label: qsTr("Albums") }
                        CountChip { iconName: "heart"; value: page.favoriteCount.toLocaleString(Qt.locale(), "f", 0); label: qsTr("Favorites") }
                        CountChip {
                            iconName: "statistics"
                            value: qsTr("%1h").arg(Math.floor(StatsController.totalListenedMs / 3600000).toLocaleString(Qt.locale(), "f", 0))
                            label: qsTr("Time listened")
                        }
                    }
                }
            }

            HomeRow { title: qsTr("Recently played"); entries: page.recentlyPlayed }
            HomeRow { title: qsTr("Recently added"); entries: page.recentlyAdded }
            HomeRow { title: qsTr("Favorites"); entries: page.favorites; songs: true }
            HomeRow { title: qsTr("Most played"); entries: page.mostPlayed }
            HomeRow { title: qsTr("Top rated"); entries: page.topRated }
            HomeRow { title: qsTr("Never played"); entries: page.neverPlayed }
        }
    }

    // Rows fade at the top (once scrolled) and bottom, like the library.
    EdgeFade {
        flickable: flick
    }
}
