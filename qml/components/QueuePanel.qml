import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

// The play queue: the current track on top, then "Up next". Click a row to play it, drag it by its
// number to reorder (an accent line marks where it lands), ✕ (on hover) takes it out, Clear empties it.
// Sized to its rows, up to `maxHeight`.
Popup {
    id: panel

    property real maxHeight: 640

    padding: 14
    background: PopupBackground {}
    height: Math.min(maxHeight, implicitHeight)

    // Read only while open, and only the first rows: a whole-library queue is thousands of songs.
    readonly property int shownLimit: 200
    readonly property int queuedCount: PlayerController.upNext.length
    readonly property var entries: visible ? LibraryController.tracksFor(PlayerController.upNext.slice(0, shownLimit)) : []
    readonly property real totalMs: visible ? LibraryController.durationOf(PlayerController.upNext) : 0
    // Row the dragged row would land on (-1 = no drag).
    property int dropIndex: -1

    function formatTime(ms) {
        const totalSeconds = Math.floor(ms / 1000)
        const seconds = totalSeconds % 60
        return Math.floor(totalSeconds / 60) + ":" + (seconds < 10 ? "0" : "") + seconds
    }
    function formatTotal(ms) {
        const totalMinutes = Math.round(ms / 60000)
        const hours = Math.floor(totalMinutes / 60)
        return hours > 0 ? qsTr("%1h %2m").arg(hours).arg(totalMinutes % 60) : qsTr("%1 min").arg(totalMinutes)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // Now playing.
        Label {
            visible: PlayerController.hasTrack
            text: qsTr("Now playing")
            font.pixelSize: 12
            font.weight: Font.DemiBold
            color: Theme.accent
        }
        RowLayout {
            visible: PlayerController.hasTrack
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            spacing: 10

            CoverArt {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                source: PlayerController.coverUrl
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: PlayerController.title
                    elide: Text.ElideRight
                    font.weight: Font.DemiBold
                    color: Theme.accent
                }
                Label {
                    Layout.fillWidth: true
                    text: PlayerController.artist
                    elide: Text.ElideRight
                    opacity: 0.6
                    font.pixelSize: 12
                }
            }
            Label {
                text: panel.formatTime(PlayerController.durationMs)
                visible: PlayerController.durationMs > 0
                opacity: 0.5
                font.pixelSize: 12
            }
        }

        Rectangle {
            visible: PlayerController.hasTrack
            Layout.fillWidth: true
            Layout.topMargin: 6
            Layout.bottomMargin: 2
            implicitHeight: 1
            color: Theme.subtle
        }

        // Up next: count, total time, Clear.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Up next")
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: Theme.accent
            }
            Label {
                Layout.fillWidth: true
                visible: panel.queuedCount > 0
                text: (panel.queuedCount === 1 ? qsTr("1 track") : qsTr("%1 tracks").arg(panel.queuedCount))
                      + (panel.totalMs > 0 ? "  ·  " + panel.formatTotal(panel.totalMs) : "")
                opacity: 0.5
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            Item { Layout.fillWidth: panel.queuedCount === 0 }
            Button {
                visible: panel.queuedCount > 0
                flat: true
                text: qsTr("Clear")
                topInset: 0
                bottomInset: 0
                implicitHeight: 28
                onClicked: PlayerController.clearUpNext()
            }
        }

        Label {
            visible: panel.entries.length === 0
            text: qsTr("Nothing queued. Right-click a song or album: Play next.")
            opacity: 0.6
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.topMargin: 4
            Layout.bottomMargin: 4
        }

        ListView {
            id: list

            visible: panel.entries.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: contentHeight
            clip: true
            model: panel.entries
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: SlimScrollBar {}

            footer: Label {
                visible: panel.queuedCount > panel.entries.length
                height: visible ? 32 : 0
                width: list.width - 8
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("and %1 more").arg(panel.queuedCount - panel.entries.length)
                opacity: 0.5
                font.pixelSize: 12
            }

            // While a row is dragged near the top or bottom edge, the list scrolls that way.
            property int autoScroll: 0
            Timer {
                running: list.autoScroll !== 0
                interval: 16
                repeat: true
                onTriggered: {
                    const maxY = Math.max(0, list.contentHeight - list.height) + list.originY
                    list.contentY = Math.max(list.originY, Math.min(maxY, list.contentY + list.autoScroll * 8))
                }
            }

            delegate: Item {
                id: row

                required property int index
                required property var modelData

                width: list.width - 8
                height: 52

                // Where the dragged row lands: above this one (moving up) or below it (moving down).
                Rectangle {
                    visible: panel.dropIndex === row.index && !drag.active
                    y: panel.dropIndex < panel.draggedIndex ? 0 : parent.height - height
                    width: parent.width
                    height: 2
                    radius: 1
                    color: Theme.accent
                    z: 3
                }

                ItemDelegate {
                    id: content

                    width: row.width
                    height: row.height
                    padding: 6
                    // Lifted out of the list while dragged, so it follows the mouse over the other rows.
                    z: drag.active ? 2 : 0
                    opacity: drag.active ? 0.85 : 1
                    onClicked: PlayerController.playUpNext(row.index)
                    // Rounded, like the library's track rows.
                    background: Rectangle {
                        radius: Math.min(Theme.radius, 10)
                        color: content.down || drag.active ? Theme.selected : content.hovered ? Theme.hover : "transparent"
                    }

                    function targetIndex() {
                        const y = row.y + content.y + content.height / 2
                        return list.indexAt(10, Math.max(0, Math.min(list.contentHeight - 1, y)))
                    }

                    onYChanged: {
                        if (!drag.active)
                            return
                        panel.dropIndex = targetIndex()
                        // Row position inside the visible part of the list.
                        const viewY = row.y + content.y - list.contentY
                        list.autoScroll = viewY < 0 ? -1 : viewY + content.height > list.height ? 1 : 0
                    }

                    contentItem: RowLayout {
                        spacing: 10

                        // Number; a grip on hover. Dragging it reorders.
                        Item {
                            Layout.preferredWidth: 20
                            Layout.fillHeight: true

                            Label {
                                anchors.centerIn: parent
                                text: content.hovered || drag.active ? "⋮⋮" : row.index + 1
                                opacity: 0.5
                                font.pixelSize: 12
                            }
                            DragHandler {
                                id: drag

                                target: content
                                xAxis.enabled: false
                                cursorShape: Qt.ClosedHandCursor
                                onActiveChanged: {
                                    if (active) {
                                        panel.draggedIndex = row.index
                                        return
                                    }
                                    list.autoScroll = 0
                                    const to = content.targetIndex()
                                    content.y = 0
                                    panel.dropIndex = -1
                                    panel.draggedIndex = -1
                                    if (to >= 0 && to !== row.index)
                                        PlayerController.moveUpNext(row.index, to)
                                }
                            }
                            HoverHandler { cursorShape: Qt.OpenHandCursor }
                        }
                        CoverArt {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            source: row.modelData.coverUrl
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label { Layout.fillWidth: true; text: row.modelData.title; elide: Text.ElideRight }
                            Label {
                                Layout.fillWidth: true
                                text: row.modelData.artist
                                elide: Text.ElideRight
                                opacity: 0.6
                                font.pixelSize: 12
                            }
                        }
                        // Duration; the remove button takes its place on hover.
                        Item {
                            Layout.preferredWidth: 40
                            Layout.fillHeight: true

                            Label {
                                anchors.centerIn: parent
                                visible: !content.hovered && row.modelData.durationMs > 0
                                text: panel.formatTime(row.modelData.durationMs)
                                opacity: 0.5
                                font.pixelSize: 12
                            }
                            IconButton {
                                anchors.centerIn: parent
                                visible: content.hovered
                                width: 30
                                height: 30
                                padding: 0
                                topInset: 0
                                bottomInset: 0
                                background: Item {}
                                iconName: "close"
                                iconSize: 16
                                tip: qsTr("Remove from queue")
                                onClicked: PlayerController.removeUpNext(row.index)
                            }
                        }
                    }
                }
            }
        }

        // Rows fade out at the list's scrolled edges. In the column (a second Popup child zeroes its
        // implicit height); invisible, so the layout skips it.
        EdgeFade {
            flickable: list
            topHeight: 20
            bottomHeight: 48
        }
    }

    property int draggedIndex: -1
}
