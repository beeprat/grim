import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import QtQuick.Controls.impl // IconImage: a tinted SVG icon
import MusicPlayer

Item {
    id: page

    // Album shown on the album page; -1 = none (browsing).
    property int openAlbum: -1

    // Leaving the album page: Escape, or the mouse's back button anywhere on this page.
    Shortcut {
        sequence: "Escape"
        enabled: page.visible && page.openAlbum >= 0
        onActivated: page.openAlbum = -1
    }
    TapHandler {
        acceptedButtons: Qt.BackButton
        enabled: page.openAlbum >= 0
        onTapped: page.openAlbum = -1
    }
    property string openAlbumTitle
    property string openAlbumArtist
    property string openAlbumCover
    property int openAlbumYear: 0
    property var openAlbumTracks: []
    // Average stars of the album's rated tracks, as the grid shows in rating sort (0 = none rated).
    // Reads metaRevision so it updates when a track on the page is rated.
    readonly property real openAlbumRating: {
        if (LibraryController.metaRevision < 0)
            return 0
        let sum = 0
        let rated = 0
        for (const track of openAlbumTracks) {
            const stars = LibraryController.rating(track.url)
            if (stars > 0) {
                sum += stars
                ++rated
            }
        }
        return rated > 0 ? sum / rated : 0
    }
    property url openAlbumFirstUrl // finds the album again after a re-sort
    property int flashTrack: -1 // album page row lit up for a moment after a jump

    Timer {
        id: flashTimer

        interval: 1600
        onTriggered: page.flashTrack = -1
    }

    // The album page has only just become visible and is not laid out yet; scrolling right
    // away does nothing, so scroll to the flashing row a moment later.
    Timer {
        id: scrollToFlashTimer

        interval: 50
        onTriggered: if (page.flashTrack >= 0) albumTracks.positionViewAtIndex(page.flashTrack, ListView.Center)
    }

    function sameFile(a, b) {
        const normalize = (u) => decodeURIComponent(u.toString()).toLowerCase()
        return a.toString() !== "" && normalize(a) === normalize(b)
    }

    // Shuffle everything currently shown: the visible albums' tracks, or the track list.
    function shufflePlay() {
        const urls = viewSettings.albums ? LibraryController.albums.allUrls() : LibraryController.tracks.urls()
        if (urls.length === 0)
            return
        PlayerController.shuffle = true
        PlayerController.playQueue(urls, Math.floor(Math.random() * urls.length))
    }

    function formatTime(ms) {
        const totalSeconds = Math.floor(ms / 1000)
        const seconds = totalSeconds % 60
        return Math.floor(totalSeconds / 60) + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    function showAlbum(row) {
        const info = LibraryController.albums.albumInfo(row)
        if (info.title === undefined)
            return
        openAlbum = row
        openAlbumTitle = info.title
        openAlbumArtist = info.artist
        openAlbumCover = info.coverUrl
        openAlbumYear = info.year
        openAlbumTracks = LibraryController.albums.tracksOf(row)
        openAlbumFirstUrl = openAlbumTracks.length > 0 ? openAlbumTracks[0].url : ""
    }

    // Albums sorted by genre start as a list of genres; picking one shows its albums.
    readonly property bool genreList: viewSettings.albums && viewSettings.albumSort === AlbumsModel.ByGenre
                                      && LibraryController.genreFilter === ""
    function genreName(name) {
        return name !== "" ? name : qsTr("Unknown genre")
    }

    // Album/track subtitle: the artist, plus what the view is sorted by.
    function albumSubtitle(artist, year, genre, rating, sort) {
        const parts = [artist || qsTr("Unknown artist")]
        switch (sort === undefined ? viewSettings.albumSort : sort) {
        case AlbumsModel.ByYear: if (year > 0) parts.push(year); break
        case AlbumsModel.ByGenre: if (genre !== "") parts.push(genre); break
        case AlbumsModel.ByRating: if (rating > 0) parts.push("★ " + rating.toFixed(1)); break
        }
        return parts.join("  ·  ")
    }

    // Opens the album containing this file (from the now-playing card). The search is
    // cleared if it hides that album.
    function openAlbumOf(fileUrl, highlightTrack) {
        let row = LibraryController.albums.rowForTrack(fileUrl)
        if (row < 0 && (LibraryController.filter !== "" || LibraryController.artistFilter !== "")) {
            LibraryController.filter = ""
            LibraryController.artistFilter = ""
            row = LibraryController.albums.rowForTrack(fileUrl)
        }
        if (row < 0)
            return
        showAlbum(row)
        if (highlightTrack) {
            // URLs reach QML sometimes percent-encoded ("%20") and sometimes not: compare decoded,
            // and case-insensitively like Windows paths.
            const normalize = (u) => decodeURIComponent(u.toString()).toLowerCase()
            const target = normalize(fileUrl)
            const index = openAlbumTracks.findIndex(track => normalize(track.url) === target)
            if (index >= 0) {
                flashTrack = index
                flashTimer.restart()
                scrollToFlashTimer.restart()
            }
        }
    }

    // Albums grid with only this artist's albums (Statistics → Top artists).
    function showArtist(name) {
        openAlbum = -1
        viewSettings.albums = true
        LibraryController.filter = ""
        LibraryController.artistFilter = name
    }

    function playAlbum(trackIndex) {
        PlayerController.playQueue(LibraryController.albums.urlsOf(openAlbum), trackIndex)
    }

    // Remembered between runs.
    Settings {
        id: viewSettings

        category: "library"
        property bool albums: true
        property int albumSort: AlbumsModel.ByArtist
        property int trackSort: AlbumsModel.ByArtist
        onAlbumSortChanged: {
            LibraryController.albums.sortOrder = albumSort
            if (albumSort !== AlbumsModel.ByGenre)
                LibraryController.genreFilter = "" // the genre list only exists in genre sort
        }
        onTrackSortChanged: LibraryController.tracks.sortOrder = trackSort
        Component.onCompleted: {
            LibraryController.albums.sortOrder = albumSort
            LibraryController.tracks.sortOrder = trackSort
        }
    }

    // Albums are rebuilt on rescans, imports and re-sorts: keep the open album page by finding its
    // album again; close it only if the album is gone.
    Connections {
        target: LibraryController.albums
        function onModelReset() {
            if (page.openAlbum < 0)
                return
            const row = LibraryController.albums.rowForTrack(page.openAlbumFirstUrl)
            if (row >= 0)
                page.showAlbum(row)
            else
                page.openAlbum = -1
        }
    }

    component Segment: Button {
        required property bool selected

        flat: true
        highlighted: selected
        Material.foreground: selected ? Theme.accent : Material.foreground
        font.pixelSize: 13
        topInset: 0
        bottomInset: 0
        implicitHeight: 32
    }

    // One row of the list views.
    component LibraryRow: ItemDelegate {
        id: row

        property string cover
        property string primary
        property string secondary
        property string tertiary
        property string trailing
        // Set for track rows: shows stars and the heart.
        property url trackUrl
        // Album page: the track number, in its own column left of the title.
        property string number
        // Briefly lit up when the row is the target of a jump (Statistics → Top tracks).
        property bool flashing: false
        readonly property bool playingNow: page.sameFile(trackUrl, PlayerController.url)

        signal contextMenu()
        // The album name (tertiary) was clicked.
        signal tertiaryClicked()

        height: cover !== "" ? 56 : 48
        // Equal side gaps between the highlight's edges and the first/last element.
        leftPadding: 14
        rightPadding: 14
        // No vertical padding or insets: the content is centered in exactly the area the
        // hover highlight covers (Material's defaults pushed it off-center).
        topPadding: 0
        bottomPadding: 0
        topInset: 0
        bottomInset: 0

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: row.contextMenu()
        }
        background: Rectangle {
            radius: Math.min(Theme.radius, 10)
            color: row.flashing ? Theme.selected : row.hovered ? Theme.hover : "transparent"

            Behavior on color {
                ColorAnimation { duration: 250 }
            }
        }
        contentItem: RowLayout {
            spacing: 12

            CoverArt {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                visible: row.cover !== ""
                source: row.cover
            }

            // Track number; on the playing track, its play/pause state instead (pause while playing).
            Item {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                visible: row.number !== ""

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !row.playingNow
                    text: row.number
                    opacity: 0.5
                }

                IconImage {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.playingNow
                    source: PlayerController.playing ? "../icons/pause.svg" : "../icons/play.svg"
                    sourceSize: Qt.size(18, 18)
                    color: Material.color(Material.Green, Material.Shade400) // same green as the title
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 3
                spacing: 0

                Label {
                    Layout.fillWidth: true
                    text: row.primary
                    elide: Text.ElideRight
                    // The track that is playing now shows in green.
                    color: row.playingNow ? Material.color(Material.Green, Material.Shade400) : Material.foreground
                    font.weight: row.playingNow ? Font.DemiBold : Font.Normal
                }

                Label {
                    Layout.fillWidth: true
                    visible: text !== "" // genre rows have one line, centered like the rest
                    text: row.secondary
                    elide: Text.ElideRight
                    opacity: 0.6
                    font.pixelSize: 12
                }
            }

            // A link, like the album on the now-playing card.
            Label {
                Layout.fillWidth: true
                Layout.preferredWidth: 2
                text: row.tertiary
                elide: Text.ElideRight
                font.underline: albumMouse.containsMouse
                opacity: albumMouse.containsMouse ? 1 : 0.6

                MouseArea {
                    id: albumMouse

                    // Only over the text, not the empty rest of the column.
                    width: Math.min(parent.contentWidth, parent.width)
                    height: parent.height
                    enabled: row.tertiary !== ""
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: row.tertiaryClicked()
                }
            }

            StarRating {
                visible: row.trackUrl.toString() !== ""
                url: row.trackUrl
            }

            HeartButton {
                visible: row.trackUrl.toString() !== ""
                url: row.trackUrl
            }

            Label {
                Layout.preferredWidth: 44
                horizontalAlignment: Text.AlignRight
                text: row.trailing
                opacity: 0.6
                font.pixelSize: 12
            }
        }
    }

    // One cell of the grid views.
    component LibraryTile: ItemDelegate {
        id: tile

        property string cover
        property string primary
        property string secondary

        signal contextMenu()

        padding: 8

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: tile.contextMenu()
        }
        background: Rectangle {
            radius: Theme.radius
            color: tile.hovered ? Theme.hover : "transparent"
        }
        contentItem: ColumnLayout {
            spacing: 0

            CoverArt {
                Layout.fillWidth: true
                Layout.preferredHeight: width
                Layout.bottomMargin: 8
                source: tile.cover
            }

            Label {
                Layout.fillWidth: true
                text: tile.primary
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                text: tile.secondary
                elide: Text.ElideRight
                opacity: 0.6
                font.pixelSize: 12
            }

        }
    }

    Card {
        anchors.fill: parent
        // The album page brings its own two cards (info, tracks); this one steps back.
        color: page.openAlbum >= 0 ? "transparent" : Theme.panel

        // Browsing: albums as a grid, or tracks as a list.
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12
            visible: page.openAlbum < 0

            // Title, filter chips and the controls in one row; in a narrow panel the controls move
            // to a second row under the title instead of running past the card's edge.
            GridLayout {
                readonly property bool narrow: page.width < 720

                Layout.fillWidth: true
                Layout.minimumWidth: 0
                columns: narrow ? 1 : 5
                columnSpacing: 8
                rowSpacing: 8

                // Title with the album/track count under it, left edges lined up.
                ColumnLayout {
                    // Same spot in the album grid and the track list, so switching views doesn't move it.
                    Layout.leftMargin: 8
                    spacing: 0

                    RowLayout {
                        spacing: 2

                        Label {
                            text: qsTr("Library")
                            font.pixelSize: 24
                            font.weight: Font.DemiBold
                        }

                        // The folder is read at startup and when it changes; this picks up files
                        // added by hand since. Soulseek downloads are added as they finish.
                        IconButton {
                            id: refreshButton

                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 36
                            Layout.alignment: Qt.AlignVCenter
                            // Nudged down to sit on the title's text rather than its line box.
                            Layout.topMargin: 4
                            padding: 0
                            topInset: 0
                            bottomInset: 0
                            leftInset: 0
                            rightInset: 0
                            background: Item {} // just the icon: no hover highlight or ripple
                            iconName: "refresh"
                            iconSize: 22
                            tip: qsTr("Refresh library")
                            enabled: !LibraryController.scanning
                            onClicked: LibraryController.rescan()
                        }
                    }

                    Label {
                        opacity: 0.5
                        font.pixelSize: 12
                        text: {
                            if (LibraryController.scanning)
                                return qsTr("Scanning…")
                            if (page.genreList) {
                                const g = LibraryController.albums.genres.length
                                return g === 1 ? qsTr("1 genre") : qsTr("%1 genres").arg(g)
                            }
                            const n = viewSettings.albums ? LibraryController.albums.count : LibraryController.tracks.count
                            return viewSettings.albums ? (n === 1 ? qsTr("1 album") : qsTr("%1 albums").arg(n))
                                                       : (n === 1 ? qsTr("1 track") : qsTr("%1 tracks").arg(n))
                        }
                    }
                }

                Button {
                    visible: LibraryController.artistFilter !== ""
                    flat: true
                    text: qsTr("Albums by %1").arg(LibraryController.artistFilter) + "   ✕"
                    Material.foreground: Theme.accent
                    ToolTip.text: qsTr("Show all albums")
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    onClicked: LibraryController.artistFilter = ""
                }

                // The genre picked in the genre list; ✕ goes back to the list.
                Button {
                    visible: LibraryController.genreFilter !== ""
                    flat: true
                    text: page.genreName(LibraryController.albums.genres.find(g => g.key === LibraryController.genreFilter)?.name ?? "")
                          + "   ✕"
                    Material.foreground: Theme.accent
                    ToolTip.text: qsTr("All genres")
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    onClicked: LibraryController.genreFilter = ""
                }

                Item { Layout.fillWidth: true; Layout.maximumWidth: 1e6; visible: !parent.narrow }

                RowLayout {
                    Layout.fillWidth: parent.narrow
                    Layout.minimumWidth: 0
                    spacing: 8

                    CompactComboBox {
                        Layout.fillWidth: true // shrinks with the search box in narrow panels
                        Layout.minimumWidth: 90
                        Layout.maximumWidth: 130
                        Layout.preferredWidth: 130
                        Layout.preferredHeight: 40
                        textRole: "label"
                        valueRole: "value"
                        model: [
                            { label: qsTr("Artist"), value: AlbumsModel.ByArtist },
                            { label: qsTr("Title A–Z"), value: AlbumsModel.ByTitle },
                            { label: qsTr("Year"), value: AlbumsModel.ByYear },
                            { label: qsTr("Rating"), value: AlbumsModel.ByRating },
                            { label: qsTr("Genre"), value: AlbumsModel.ByGenre }
                        ]
                        // Albums and tracks each keep their own order.
                        currentIndex: Math.max(0, indexOfValue(viewSettings.albums ? viewSettings.albumSort : viewSettings.trackSort))
                        onActivated: {
                            if (viewSettings.albums)
                                viewSettings.albumSort = currentValue
                            else
                                viewSettings.trackSort = currentValue
                        }
                        ToolTip.text: viewSettings.albums ? qsTr("Sort albums by") : qsTr("Sort tracks by")
                        ToolTip.visible: hovered && !popup.visible
                        ToolTip.delay: 600
                    }

                    TextField {
                        id: searchField

                        Layout.fillWidth: true // shrinks first when the window is narrow
                        Layout.minimumWidth: 60
                        Layout.maximumWidth: 300
                        Layout.preferredWidth: 300 // the whole placeholder fits
                        Layout.preferredHeight: 40
                        rightPadding: clearSearch.visible ? clearSearch.width + 4 : 12
                        placeholderText: qsTr("Search title, artist, album")
                        text: LibraryController.filter
                        onTextEdited: LibraryController.filter = text
                        Keys.onEscapePressed: LibraryController.filter = ""
                        selectByMouse: true

                        IconButton {
                            id: clearSearch

                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 32
                            height: 32
                            padding: 0
                            topInset: 0
                            bottomInset: 0
                            leftInset: 0
                            rightInset: 0
                            // A circle filling the button; Material's ripple sat off the icon here.
                            background: Rectangle {
                                radius: width / 2
                                color: clearSearch.pressed ? Qt.darker(Theme.subtle, 1.2)
                                     : clearSearch.hovered ? Theme.subtle : "transparent"
                            }
                            iconName: "close"
                            iconSize: 16
                            visible: searchField.text !== ""
                            onClicked: LibraryController.filter = ""
                        }
                    }

                    IconButton {
                        iconName: "shuffle"
                        tip: qsTr("Shuffle play")
                        onClicked: page.shufflePlay()
                    }

                    Segment {
                        text: qsTr("Albums")
                        selected: viewSettings.albums
                        onClicked: viewSettings.albums = true
                    }

                    Segment {
                        text: qsTr("Tracks")
                        selected: !viewSettings.albums
                        onClicked: viewSettings.albums = false
                    }
                }
            }

            // Albums, grid
            GridView {
                id: albumGrid

                // Tiles keep one size (kTile, padding included) however wide the panel is; the spare
                // width becomes even gaps between them instead of bigger covers.
                readonly property real kTile: 200
                readonly property int columns: Math.max(1, Math.floor(width / kTile))
                readonly property real tileSize: Math.min(kTile, Math.floor(width / columns))
                // Space either side of a tile within its cell.
                readonly property real sideGap: Math.max(0, (cellWidth - tileSize) / 2)

                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: viewSettings.albums && !page.genreList
                clip: true
                cellWidth: Math.floor(width / columns)
                // Tile height is its content; the rest of the cell is the gap between rows.
                cellHeight: tileSize + 52
                // Keep a few rows built off screen so covers are ready before they scroll in.
                cacheBuffer: cellHeight * 3
                // Recycle tiles that scroll out instead of building new ones (fast scrolling stuttered).
                reuseItems: true
                // By view mode, not `visible`: that turns false on the album page and would reset the
                // model, losing the scroll position.
                model: viewSettings.albums ? LibraryController.albums : null
                ScrollBar.vertical: SlimScrollBar {}
                FastWheelScroll { flickable: albumGrid }

                delegate: LibraryTile {
                    required property int index
                    required property string title
                    required property string artist
                    required property string coverUrl
                    required property int year
                    required property string genre
                    required property real rating

                    width: GridView.view.cellWidth
                    height: implicitHeight
                    leftInset: albumGrid.sideGap
                    rightInset: albumGrid.sideGap
                    leftPadding: 8 + albumGrid.sideGap
                    rightPadding: 8 + albumGrid.sideGap
                    cover: coverUrl
                    primary: title
                    secondary: page.albumSubtitle(artist, year, genre, rating)
                    onClicked: page.showAlbum(index)
                    onContextMenu: ApplicationWindow.window.openTrackMenu(LibraryController.albums.urlsOf(index), true)
                }
            }

            // Genres, list (albums sorted by genre): clicking one shows its albums.
            ListView {
                id: genreList

                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: page.genreList
                clip: true
                model: page.genreList ? LibraryController.albums.genres : null
                ScrollBar.vertical: SlimScrollBar {}
                FastWheelScroll { flickable: genreList }

                delegate: LibraryRow {
                    required property var modelData

                    width: ListView.view.width - 12
                    primary: page.genreName(modelData.name)
                    trailing: modelData.count === 1 ? qsTr("1 album") : qsTr("%1 albums").arg(modelData.count)
                    onClicked: LibraryController.genreFilter = modelData.key
                }
            }

            // Tracks, list: clicking plays the whole library from that track.
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                id: trackList

                visible: !viewSettings.albums
                clip: true
                reuseItems: true
                spacing: 2
                cacheBuffer: 56 * 10
                model: !viewSettings.albums ? LibraryController.tracks : null
                ScrollBar.vertical: SlimScrollBar {}
                FastWheelScroll { flickable: trackList }

                delegate: LibraryRow {
                    required property int index
                    required property string title
                    required property string artist
                    required property string album
                    required property real durationMs
                    required property string coverUrl
                    required property url url
                    required property int year
                    required property string genre

                    width: ListView.view.width - 12
                    cover: coverUrl
                    primary: title
                    // Rating is already shown as stars on the row.
                    secondary: page.albumSubtitle(artist, year, genre, 0, viewSettings.trackSort)
                    tertiary: album
                    onTertiaryClicked: page.openAlbumOf(url, true)
                    trailing: durationMs > 0 ? page.formatTime(durationMs) : ""
                    trackUrl: url
                    onClicked: PlayerController.playTrack(LibraryController.tracks.urls(), index)
                    onContextMenu: ApplicationWindow.window.openTrackMenu([url])
                }
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 24
                visible: (viewSettings.albums ? LibraryController.albums.count : LibraryController.tracks.count) === 0 && !LibraryController.scanning
                text: LibraryController.filter !== ""
                      ? qsTr("Nothing matches \"%1\"").arg(LibraryController.filter)
                      : qsTr("No music in %1 yet. Downloads from Soulseek land here after conversion.")
                        .arg(SettingsController.libraryFolder)
                opacity: 0.5
            }
        }

        // Album page: a card with cover, title and play button, and a card with the tracks.
        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            visible: page.openAlbum >= 0

            Card {
                Layout.fillWidth: true
                implicitHeight: albumHeader.implicitHeight + 32

                RowLayout {
                    id: albumHeader

                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16

                    CoverArt {
                        Layout.preferredWidth: 160
                        Layout.preferredHeight: 160
                        source: page.openAlbumCover
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            Layout.fillWidth: true
                            text: page.openAlbumTitle
                            font.pixelSize: 26
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        // Like the now-playing card: the name opens all of this artist's albums.
                        Label {
                            Layout.fillWidth: true
                            text: page.openAlbumArtist || qsTr("Unknown artist")
                            opacity: albumArtistMouse.containsMouse ? 1 : 0.7
                            font.pixelSize: 15
                            font.underline: albumArtistMouse.containsMouse
                            elide: Text.ElideRight

                            MouseArea {
                                id: albumArtistMouse

                                // Only over the name, not the empty rest of the line.
                                width: Math.min(parent.contentWidth, parent.width)
                                height: parent.height
                                enabled: page.openAlbumArtist !== ""
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page.showArtist(page.openAlbumArtist)
                            }

                            ToolTip.text: qsTr("Show this artist's albums")
                            ToolTip.visible: albumArtistMouse.containsMouse
                            ToolTip.delay: 600
                        }

                        Label {
                            text: (page.openAlbumTracks.length === 1 ? qsTr("1 track")
                                                                     : qsTr("%1 tracks").arg(page.openAlbumTracks.length))
                                  + (page.openAlbumYear > 0 ? "  ·  " + page.openAlbumYear : "")
                              + (page.openAlbumRating > 0 ? "  ·  ★ " + page.openAlbumRating.toFixed(1) : "")
                            opacity: 0.5
                        }

                        RowLayout {
                            Layout.topMargin: 8
                            spacing: 8

                            AccentButton {
                                implicitWidth: 56
                                text: "▶"
                                onClicked: {
                                    PlayerController.shuffle = false
                                    page.playAlbum(0)
                                }
                            }

                            IconButton {
                                iconName: "shuffle"
                                onClicked: {
                                    const urls = LibraryController.albums.urlsOf(page.openAlbum)
                                    PlayerController.shuffle = true
                                    PlayerController.playQueue(urls, Math.floor(Math.random() * urls.length))
                                }
                            }
                        }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    id: albumTracks

                    anchors.fill: parent
                    anchors.margins: 16
                    clip: true
                    spacing: 2
                    model: page.openAlbumTracks
                    ScrollBar.vertical: SlimScrollBar {}
                    FastWheelScroll { flickable: albumTracks }

                    delegate: LibraryRow {
                        required property int index
                        required property var modelData

                        width: ListView.view.width - 12
                        number: index + 1
                        primary: modelData.title
                        secondary: modelData.artist
                        flashing: index === page.flashTrack
                        trailing: modelData.durationMs > 0 ? page.formatTime(modelData.durationMs) : ""
                        trackUrl: modelData.url
                        onClicked: PlayerController.playTrack(LibraryController.albums.urlsOf(page.openAlbum), index)
                        onContextMenu: ApplicationWindow.window.openTrackMenu([modelData.url])
                    }
                }
            }
        }

        // Lists and the grid fade out at the bottom instead of a hard cut, and slightly at the top once scrolled.
        EdgeFade {
            flickable: page.openAlbum >= 0 ? albumTracks : page.genreList ? genreList : viewSettings.albums ? albumGrid : trackList
            bottomHeight: 140
        }
    }
}
