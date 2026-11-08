import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import QtQuick.Controls.impl
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

    // Chart slice i: the accent's hue turned round the color wheel, so slices stay apart with any accent.
    function sliceColor(i) {
        return Qt.hsla((Theme.accent.hslHue + i * 0.13) % 1, Theme.accent.hslSaturation, Theme.accent.hslLightness, 1)
    }

    readonly property var days: StatsController.dailyListening
    readonly property real bestDayMs: days.reduce((best, d) => Math.max(best, d.ms), 0)
    readonly property real genreTotalMs: StatsController.genreShares.reduce((sum, s) => sum + s.ms, 0)

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

    // Scrolls once the window is too short for the charts plus readable top-10 lists.
    Flickable {
        id: flick

        anchors.fill: parent
        contentHeight: content.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: SlimScrollBar {}
        FastWheelScroll { flickable: flick }

    ColumnLayout {
        id: content

        width: flick.width
        height: Math.max(flick.height, implicitHeight)
        spacing: 12

        // Time range of everything below.
        RowLayout {
            spacing: 4
            Repeater {
                model: [{ days: 7, text: qsTr("7 days") }, { days: 30, text: qsTr("30 days") },
                        { days: 90, text: qsTr("90 days") }, { days: 0, text: qsTr("All time") }]
                delegate: Button {
                    required property var modelData
                    text: modelData.text
                    flat: StatsController.rangeDays !== modelData.days
                    highlighted: !flat
                    onClicked: StatsController.rangeDays = modelData.days
                }
            }
        }

        // The number one of each list as picture tiles (same click targets as the lists below).
        RowLayout {
            id: highlights

            // Album and track are squares of this size and the artist takes the rest of the width.
            // In a narrow window everything shrinks so the artist keeps at least 1.2 squares.
            readonly property real tileHeight: Math.max(100, Math.min(380, (parent.width - 2 * 12) / 3.2))

            Layout.fillWidth: true
            Layout.preferredHeight: tileHeight
            Layout.maximumHeight: tileHeight
            spacing: 12

            TopHighlight {
                label: qsTr("Top artist")
                tileHeight: highlights.tileHeight
                entry: StatsController.topArtist
                detail: page.highlightDetail(entry)
                stretch: true
                onClicked: ApplicationWindow.window.showArtist(entry.title)
            }

            TopHighlight {
                label: qsTr("Top album")
                tileHeight: highlights.tileHeight
                entry: StatsController.topAlbum
                detail: page.highlightDetail(entry)
                onClicked: ApplicationWindow.window.showAlbumOf(entry.url, false)
            }

            TopHighlight {
                label: qsTr("Top track")
                tileHeight: highlights.tileHeight
                entry: StatsController.topTrack
                detail: page.highlightDetail(entry)
                onClicked: ApplicationWindow.window.showAlbumOf(entry.url, true)
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

            StatTile {
                label: qsTr("Daily average")
                value: page.formatDuration(page.days.length > 0 ? StatsController.totalListenedMs / page.days.length : 0)
            }

            StatTile {
                label: qsTr("Most in one day")
                value: page.formatDuration(page.bestDayMs)
            }
        }

        GridLayout {
            id: charts

            // Graph, pie, medals: 3 across, 2 + medals below, or stacked, by width; each row 170 px.
            readonly property bool wide: width >= 1000
            readonly property int rowCount: wide ? 1 : width >= 620 ? 2 : 3
            Layout.fillWidth: true
            Layout.preferredHeight: rowCount * 170 + (rowCount - 1) * rowSpacing
            columns: wide ? 3 : rowCount === 2 ? 2 : 1
            rowSpacing: 12
            columnSpacing: 12

            // Listening time per day; hovering a bar shows its day in the header.
            Card {
                Layout.fillWidth: true
                Layout.preferredWidth: 1 // with the pie's 1: the row splits evenly
                Layout.minimumWidth: 200
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 8

                    // Title, or the hovered day; metrics for the range on the right.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        Label {
                            readonly property var day: graph.hoverIndex >= 0 && graph.hoverIndex < page.days.length ? page.days[graph.hoverIndex] : null
                            Layout.fillWidth: true
                            text: day ? qsTr("%1: %2").arg(day.date.toLocaleDateString(Qt.locale(), "ddd d MMM yyyy")).arg(page.formatDuration(day.ms))
                                      : qsTr("Listening per day")
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Label {
                            text: qsTr("%1 of %2 days active").arg(page.days.filter(d => d.ms > 0).length).arg(page.days.length)
                            opacity: 0.6
                            font.pixelSize: 12
                        }
                        RowLayout {
                            spacing: 4
                            Rectangle { width: 12; height: 2; color: Theme.accent }
                            Label { text: qsTr("Average"); opacity: 0.6; font.pixelSize: 12 }
                        }
                    }

                    Canvas {
                        id: graph

                        property int hoverIndex: -1
                        readonly property real axisWidth: 34 // left room for the hour labels
                        readonly property real plotWidth: Math.max(1, width - axisWidth)

                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        // Short duration for an axis label: "45m", "2h", "1h30".
                        function axisLabel(minutes) {
                            if (minutes < 60)
                                return qsTr("%1m").arg(minutes)
                            return minutes % 60 === 0 ? qsTr("%1h").arg(minutes / 60)
                                                      : qsTr("%1h%2").arg(Math.floor(minutes / 60)).arg(minutes % 60)
                        }

                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            const n = page.days.length
                            if (n === 0 || page.bestDayMs <= 0)
                                return
                            const top = 8, plotH = height - top - 8 // room for half a label above the top and below the 0 line
                            // Gridline step: the smallest round step giving at most 4 lines.
                            const bestMin = page.bestDayMs / 60000
                            const steps = [5, 10, 15, 30, 60, 120, 180, 240, 360, 480, 720]
                            const step = steps.find(s => bestMin / s <= 4) ?? 720
                            const maxMin = Math.ceil(bestMin / step) * step
                            const y = (min) => top + plotH - min / maxMin * plotH

                            ctx.font = "11px sans-serif"
                            ctx.textAlign = "right"
                            ctx.textBaseline = "middle"
                            ctx.lineWidth = 1
                            for (let m = 0; m <= maxMin; m += step) {
                                const ly = Math.round(y(m)) + 0.5
                                ctx.strokeStyle = todayLabel.color
                                ctx.globalAlpha = m === 0 ? 0.3 : 0.1
                                ctx.beginPath()
                                ctx.moveTo(axisWidth, ly)
                                ctx.lineTo(width, ly)
                                ctx.stroke()
                                ctx.fillStyle = todayLabel.color
                                ctx.globalAlpha = 0.5
                                ctx.fillText(axisLabel(m), axisWidth - 6, ly)
                            }

                            const slot = plotWidth / n
                            const gap = slot > 6 ? Math.min(4, slot * 0.25) : 0
                            const r = Math.min(3, (slot - gap) / 2)
                            // Hovered day: a faint column behind its bar.
                            if (hoverIndex >= 0 && hoverIndex < n) {
                                ctx.fillStyle = todayLabel.color
                                ctx.globalAlpha = 0.06
                                ctx.fillRect(axisWidth + hoverIndex * slot, top, slot, plotH)
                            }
                            ctx.fillStyle = Theme.accent
                            for (let i = 0; i < n; ++i) {
                                const ms = page.days[i].ms
                                if (ms <= 0)
                                    continue
                                const h = Math.max(2, ms / 60000 / maxMin * plotH)
                                const x = axisWidth + i * slot + gap / 2, w = slot - gap, by = top + plotH - h
                                ctx.globalAlpha = hoverIndex < 0 || i === hoverIndex ? 0.85 : 0.45
                                // Rounded top corners only.
                                const rr = Math.min(r, h)
                                ctx.beginPath()
                                ctx.moveTo(x, top + plotH)
                                ctx.lineTo(x, by + rr)
                                ctx.arcTo(x, by, x + rr, by, rr)
                                ctx.lineTo(x + w - rr, by)
                                ctx.arcTo(x + w, by, x + w, by + rr, rr)
                                ctx.lineTo(x + w, top + plotH)
                                ctx.closePath()
                                ctx.fill()
                            }

                            // Dashed average line over the whole range.
                            const avgMin = StatsController.totalListenedMs / n / 60000
                            if (avgMin > 0) {
                                const ay = Math.round(y(avgMin)) + 0.5
                                ctx.strokeStyle = Theme.accent
                                ctx.globalAlpha = 1
                                ctx.lineWidth = 1.5
                                ctx.setLineDash([4, 3])
                                ctx.beginPath()
                                ctx.moveTo(axisWidth, ay)
                                ctx.lineTo(width, ay)
                                ctx.stroke()
                                ctx.setLineDash([])
                            }
                        }
                        onHoverIndexChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        Connections {
                            target: StatsController
                            function onChartsChanged() { graph.requestPaint() }
                        }
                        Connections {
                            target: Theme
                            function onAccentChanged() { graph.requestPaint() }
                        }

                        MouseArea {
                            anchors.fill: parent
                            anchors.leftMargin: graph.axisWidth
                            hoverEnabled: true
                            onPositionChanged: (mouse) => graph.hoverIndex = page.days.length > 0
                                ? Math.max(0, Math.min(page.days.length - 1, Math.floor(mouse.x / (width / page.days.length)))) : -1
                            onExited: graph.hoverIndex = -1
                        }
                    }

                    // Date ticks: first day, middle, today.
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: graph.axisWidth
                        Label {
                            text: page.days.length > 0 ? page.days[0].date.toLocaleDateString(Qt.locale(), "d MMM") : ""
                            opacity: 0.6
                            font.pixelSize: 11
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: page.days.length > 2 ? page.days[Math.floor(page.days.length / 2)].date.toLocaleDateString(Qt.locale(), "d MMM") : ""
                            opacity: 0.6
                            font.pixelSize: 11
                        }
                        Item { Layout.fillWidth: true }
                        Label { id: todayLabel; text: qsTr("Today"); opacity: 0.6; font.pixelSize: 11 } // its color also inks the graph's grid
                    }
                }
            }

            // Share of listening time per genre.
            Card {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.minimumWidth: 260
                Layout.fillHeight: true

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16

                    Canvas {
                        id: pie

                        property int hovered: -1 // slice under the mouse, drawn darker

                        // Index of the slice at (x, y), -1 outside the circle.
                        function sliceAt(x, y) {
                            const dx = x - width / 2, dy = y - height / 2
                            const r = Math.min(width, height) / 2
                            if (dx * dx + dy * dy > r * r || page.genreTotalMs <= 0)
                                return -1
                            // Fraction of the circle clockwise from 12 o'clock, where the first slice starts.
                            const at = ((Math.atan2(dy, dx) + Math.PI / 2) / (2 * Math.PI) + 1) % 1 * page.genreTotalMs
                            const shares = StatsController.genreShares
                            let sum = 0
                            for (let i = 0; i < shares.length; ++i) {
                                sum += shares[i].ms
                                if (at < sum)
                                    return i
                            }
                            return -1
                        }

                        onHoveredChanged: requestPaint()
                        Layout.fillHeight: true
                        Layout.preferredWidth: height

                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            const shares = StatsController.genreShares
                            if (page.genreTotalMs <= 0)
                                return
                            const r = Math.min(width, height) / 2
                            let angle = -Math.PI / 2
                            for (let i = 0; i < shares.length; ++i) {
                                const sweep = shares[i].ms / page.genreTotalMs * 2 * Math.PI
                                ctx.beginPath()
                                ctx.moveTo(width / 2, height / 2)
                                ctx.arc(width / 2, height / 2, r, angle, angle + sweep)
                                ctx.closePath()
                                ctx.fillStyle = page.sliceColor(i)
                                ctx.globalAlpha = i === hovered ? 0.55 : 1
                                ctx.fill()
                                angle += sweep
                            }
                        }
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()

                        // Clicking a slice opens its genre, like its legend row.
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: pie.hovered >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onPositionChanged: (mouse) => pie.hovered = pie.sliceAt(mouse.x, mouse.y)
                            onExited: pie.hovered = -1
                            onClicked: (mouse) => {
                                const i = pie.sliceAt(mouse.x, mouse.y)
                                if (i >= 0 && StatsController.genreShares[i].key !== "")
                                    ApplicationWindow.window.showGenre(StatsController.genreShares[i].key)
                            }
                        }
                        Connections {
                            target: StatsController
                            function onChartsChanged() { pie.requestPaint() }
                        }
                        Connections {
                            target: Theme
                            function onAccentChanged() { pie.requestPaint() }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 0

                        Label {
                            visible: StatsController.genreShares.length === 0
                            text: qsTr("No listening yet")
                            opacity: 0.6
                        }

                        Repeater {
                            model: StatsController.genreShares
                            delegate: RowLayout {
                                id: legendRow
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                spacing: 8
                                opacity: legendMouse.containsMouse ? 0.7 : 1
                                onOpacityChanged: pie.hovered = legendMouse.containsMouse ? index : (pie.hovered === index ? -1 : pie.hovered)

                                // Opens the genre's albums in the Library ("Unknown" and "Other" have none to open).
                                MouseArea {
                                    id: legendMouse
                                    anchors.fill: parent
                                    enabled: legendRow.modelData.key !== ""
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: ApplicationWindow.window.showGenre(legendRow.modelData.key)
                                }
                                Rectangle { width: 10; height: 10; radius: 5; color: page.sliceColor(index) }
                                Label { Layout.fillWidth: true; text: modelData.genre; elide: Text.ElideRight; font.pixelSize: 12 }
                                Label {
                                    font.pixelSize: 12
                                    text: qsTr("%1 %").arg(Math.round(modelData.ms * 100 / Math.max(1, page.genreTotalMs)))
                                    opacity: 0.6
                                }
                            }
                        }
                    }
                }
            }

            // Milestones: one medal per set (the highest won, else the first to win); clicking it lists the set.
            Card {
                id: milestones

                Layout.preferredWidth: 260
                Layout.minimumWidth: 220
                Layout.fillWidth: !charts.wide
                Layout.columnSpan: charts.columns === 2 ? 2 : 1
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 8

                        Repeater {
                            model: ApplicationWindow.window.medalSets
                            delegate: ItemDelegate {
                                id: medal

                                required property var modelData
                                readonly property int won: ApplicationWindow.window.medalsWon(modelData)
                                readonly property int shown: Math.max(0, won - 1) // tier on display

                                Layout.fillWidth: true
                                Layout.preferredWidth: 1 // equal shares, whatever the label widths
                                Layout.fillHeight: true
                                padding: 4
                                background: Rectangle {
                                    radius: Theme.radius
                                    color: Theme.hover
                                    opacity: medal.down ? 0.5 : medal.hovered ? 1 : 0
                                    Behavior on opacity { NumberAnimation { duration: 120 } }
                                }
                                onClicked: {
                                    medalPopup.set = modelData
                                    medalPopup.open()
                                }

                                contentItem: ColumnLayout {
                                    spacing: 2
                                    IconImage {
                                        Layout.alignment: Qt.AlignHCenter
                                        source: Qt.resolvedUrl("../icons/medal.svg")
                                        sourceSize: Qt.size(40, 40)
                                        color: medal.won > 0 ? Theme.medalColors[Math.min(medal.shown, Theme.medalColors.length - 1)] : Theme.subtle
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignHCenter
                                        text: ApplicationWindow.window.medalLabel(medal.modelData, medal.shown)
                                        font.weight: Font.DemiBold
                                        opacity: medal.won > 0 ? 1 : 0.4
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignHCenter
                                        text: medal.modelData.name
                                        font.pixelSize: 11
                                        opacity: 0.6
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignHCenter
                                        text: qsTr("%1/%2").arg(medal.won).arg(medal.modelData.tiers.length)
                                        font.pixelSize: 11
                                        opacity: 0.6
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 420 // room for all 10 rows
            spacing: 12

            // Clicking jumps to the library: a track's album (with the track flashing),
            // an artist's albums, or an album.
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

            RankedList {
                title: qsTr("Top tracks")
                listModel: StatsController.topTracks
                onEntryClicked: (fileUrl) => ApplicationWindow.window.showAlbumOf(fileUrl, true)
            }
        }
    }
    }

    // Every medal of one set, won or not, with what unlocks it.
    Popup {
        id: medalPopup

        property var set: null
        readonly property int won: set ? ApplicationWindow.window.medalsWon(set) : 0

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 340
        padding: 16
        modal: true
        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.6)
        }
        background: PopupBackground {}

        contentItem: ColumnLayout {
            spacing: 8

            Label {
                text: medalPopup.set ? qsTr("%1 medals").arg(medalPopup.set.name) : ""
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }

            Repeater {
                model: medalPopup.set ? medalPopup.set.tiers.length : 0
                delegate: RowLayout {
                    required property int index
                    readonly property bool unlocked: index < medalPopup.won
                    Layout.fillWidth: true
                    spacing: 12

                    IconImage {
                        source: Qt.resolvedUrl("../icons/medal.svg")
                        sourceSize: Qt.size(28, 28)
                        color: parent.unlocked ? Theme.medalColors[Math.min(index, Theme.medalColors.length - 1)] : Theme.subtle
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            text: ApplicationWindow.window.medalLabel(medalPopup.set, index)
                            font.weight: Font.DemiBold
                            opacity: parent.parent.unlocked ? 1 : 0.5
                        }
                        Label {
                            Layout.fillWidth: true
                            text: ApplicationWindow.window.medalGoal(medalPopup.set, index)
                            font.pixelSize: 12
                            opacity: 0.6
                            elide: Text.ElideRight
                        }
                    }
                    Label {
                        text: parent.unlocked ? qsTr("Unlocked") : qsTr("Locked")
                        font.pixelSize: 12
                        color: parent.unlocked ? Theme.medalColors[Math.min(index, Theme.medalColors.length - 1)] : Theme.muted
                    }
                }
            }
        }
    }

    // Rows fade at the top (once scrolled) and bottom, like the library.
    EdgeFade {
        flickable: flick
    }
}
