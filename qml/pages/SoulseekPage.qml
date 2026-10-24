import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

Item {
    id: page

    readonly property bool online: SlskdController.status === SlskdController.Online

    function formatSize(bytes) {
        if (bytes >= 1073741824)
            return (bytes / 1073741824).toFixed(1) + " GB"
        return bytes >= 1048576 ? (bytes / 1048576).toFixed(1) + " MB" : Math.round(bytes / 1024) + " KB"
    }

    function formatLength(seconds) {
        const s = seconds % 60
        return Math.floor(seconds / 60) + ":" + (s < 10 ? "0" : "") + s
    }

    function slotText(hasFreeSlot, queueLength) {
        return hasFreeSlot ? qsTr("free slot") : qsTr("queue %1").arg(queueLength)
    }

    onVisibleChanged: if (visible) SlskdController.refreshDownloads()
    Component.onCompleted: SlskdController.refreshDownloads()

    // Results view (Tracks / Albums / Artists), remembered between runs.
    Settings {
        id: viewSettings

        category: "soulseek"
        property int view: 1 // 0 tracks, 1 albums, 2 artists
        onViewChanged: if (view > 0) SlskdController.groups.mode = view === 1 ? SearchGroupsModel.Albums
                                                                               : SearchGroupsModel.Artists
        Component.onCompleted: viewChanged()
    }

    component Badge: Rectangle {
        property alias text: badgeLabel.text

        implicitWidth: badgeLabel.implicitWidth + 14
        implicitHeight: 22
        radius: 6
        color: Theme.subtle
        visible: text !== ""

        Label {
            id: badgeLabel

            anchors.centerIn: parent
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
    }

    // Filter row boxes (format, quality, save as): one size, shrinking together in narrow windows.
    component FilterBox: CompactComboBox {
        Layout.fillWidth: true
        Layout.minimumWidth: 110
        Layout.maximumWidth: 170
        Layout.preferredHeight: 40
    }

    component Segment: Button {
        required property int view

        flat: true
        highlighted: viewSettings.view === view
        Material.foreground: highlighted ? Theme.accent : Material.foreground
        topInset: 0
        bottomInset: 0
        implicitHeight: 32
        onClicked: viewSettings.view = view
    }

    // Album or artist with its contents folded out below on click.
    component GroupRow: ItemDelegate {
        id: group

        required property string title
        required property string subtitle
        required property string formats
        required property string quality
        required property int fileCount
        required property real size
        required property bool hasFreeSlot
        required property int queueLength
        required property int albumIndex
        required property var items
        property bool expanded: false
        default property alias detail: detailColumn.data

        width: ListView.view.width - 12
        height: header.implicitHeight + (expanded ? detailColumn.implicitHeight + 8 : 0) + 16
        onClicked: expanded = !expanded

        background: Rectangle {
            radius: Math.min(Theme.radius, 10)
            color: group.expanded ? Theme.hover : group.hovered ? Theme.hover : "transparent"
        }

        contentItem: ColumnLayout {
            spacing: 8

            RowLayout {
                id: header

                Layout.fillWidth: true
                spacing: 10

                Label {
                    text: group.expanded ? "▾" : "▸"
                    opacity: 0.5
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        Layout.fillWidth: true
                        text: group.title
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: [group.subtitle, group.fileCount + " " + qsTr("files"), page.formatSize(group.size)]
                              .concat(group.albumIndex >= 0 ? [page.slotText(group.hasFreeSlot, group.queueLength)] : [])
                              .join("  ·  ")
                        elide: Text.ElideRight
                        opacity: 0.6
                        font.pixelSize: 12
                    }
                }

                Badge { text: group.formats }

                Label {
                    Layout.preferredWidth: 100
                    visible: group.quality !== ""
                    text: group.quality
                    font.pixelSize: 12
                    opacity: 0.8
                }

                Button {
                    visible: group.albumIndex >= 0
                    text: qsTr("Download album")
                    flat: true
                    onClicked: SlskdController.downloadAlbum(group.albumIndex)
                }
            }

            ColumnLayout {
                id: detailColumn

                Layout.fillWidth: true
                Layout.leftMargin: 24
                visible: group.expanded
                spacing: 2
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Card {
            Layout.fillWidth: true
            implicitHeight: searchColumn.implicitHeight + 32

            ColumnLayout {
                id: searchColumn

                anchors.fill: parent
                anchors.margins: 16
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: queryField

                        Layout.fillWidth: true
                        placeholderText: qsTr("Search Soulseek")
                        enabled: page.online
                        onAccepted: SlskdController.search(text)
                    }

                    RoundButton {
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 48
                        enabled: page.online && queryField.text.trim() !== ""
                        display: AbstractButton.IconOnly
                        icon.source: "../icons/search.svg"
                        icon.width: 22
                        icon.height: 22
                        icon.color: Theme.window
                        Material.background: Theme.accent
                        Material.elevation: 0
                        opacity: enabled ? 1 : 0.4
                        ToolTip.text: qsTr("Search")
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        onClicked: SlskdController.search(queryField.text)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Segment { view: 1; text: qsTr("Albums") }
                    Segment { view: 2; text: qsTr("Artists") }
                    Segment { view: 0; text: qsTr("Tracks") }

                    ToolSeparator {}

                    FilterBox {
                        id: formatBox

                        model: [qsTr("Any format"), "flac", "mp3", "m4a", "ogg", "opus", "wav"]
                        onActivated: {
                            // Quality options differ per format; start each format from "any".
                            const results = SlskdController.results
                            results.minBitRate = 0
                            results.bitDepth = 0
                            results.sampleRate = 0
                            qualityBox.currentIndex = 0
                            results.extensionFilter = currentIndex === 0 ? "" : currentText
                        }
                    }

                    // FLAC is lossless: choose CD quality or hi-res instead of a bitrate.
                    FilterBox {
                        id: qualityBox

                        readonly property bool lossless: formatBox.currentText === "flac"

                        textRole: "label"
                        model: lossless
                               ? [
                                   { label: qsTr("Any quality"), bitRate: 0, bitDepth: 0, sampleRate: 0 },
                                   { label: "16-bit · 44.1 kHz", bitRate: 0, bitDepth: 16, sampleRate: 44100 },
                                   { label: "24-bit · 96 kHz", bitRate: 0, bitDepth: 24, sampleRate: 96000 }
                               ]
                               : [
                                   { label: qsTr("Any bitrate"), bitRate: 0, bitDepth: 0, sampleRate: 0 },
                                   { label: "≥ 192 kbps", bitRate: 192, bitDepth: 0, sampleRate: 0 },
                                   { label: "≥ 256 kbps", bitRate: 256, bitDepth: 0, sampleRate: 0 },
                                   { label: "≥ 320 kbps", bitRate: 320, bitDepth: 0, sampleRate: 0 }
                               ]
                        onActivated: (index) => {
                            const option = model[index]
                            const results = SlskdController.results
                            results.minBitRate = option.bitRate
                            results.bitDepth = option.bitDepth
                            results.sampleRate = option.sampleRate
                        }
                    }

                    ToolSeparator {}

                    // What finished downloads become before they move into the library.
                    FilterBox {
                        textRole: "label"
                        valueRole: "value"
                        displayText: qsTr("Save as %1").arg(currentText)
                        model: [
                            { label: qsTr("original"), value: "original" },
                            { label: "M4A 320", value: "m4a" },
                            { label: "MP3 320", value: "mp3" },
                            { label: "FLAC", value: "flac" }
                        ]
                        // Reads `count` so it re-runs once the model is in: indexOfValue() is -1 before that.
                        currentIndex: count > 0 ? Math.max(0, indexOfValue(SlskdController.downloadFormat)) : -1
                        onActivated: SlskdController.downloadFormat = currentValue
                        ToolTip.text: qsTr("Format finished downloads are converted to (needs ffmpeg). \"Save as original\" moves them into the library as downloaded.")
                        ToolTip.visible: hovered && !popup.visible
                        ToolTip.delay: 500
                    }

                    Item { Layout.fillWidth: true }

                    BusyIndicator {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        running: SlskdController.searching
                        visible: running
                    }

                    Label {
                        opacity: 0.6
                        text: SlskdController.searching
                              ? qsTr("%1 users responded").arg(SlskdController.responseCount)
                              : viewSettings.view === 0
                                ? qsTr("%1 of %2 files").arg(tracksList.count).arg(SlskdController.results.totalCount)
                                : viewSettings.view === 1 ? qsTr("%1 albums").arg(groupsList.count)
                                                          : qsTr("%1 artists").arg(groupsList.count)
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: SlskdController.errorText
                    color: Material.color(Material.Red, Material.Shade300)
                    wrapMode: Text.Wrap
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Albums or artists.
                ListView {
                    id: groupsList

                    anchors.fill: parent
                    anchors.margins: 8
                    visible: viewSettings.view !== 0
                    clip: true
                    spacing: 2
                    model: visible ? SlskdController.groups : null
                    ScrollBar.vertical: SlimScrollBar {}

                    delegate: GroupRow {
                        id: groupRow

                        // Albums: the tracks, each downloadable on its own.
                        Repeater {
                            model: viewSettings.view === 1 && groupRow.expanded ? groupRow.items : []

                            delegate: RowLayout {
                                required property int index
                                required property var modelData

                                Layout.fillWidth: true
                                spacing: 10

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.fileName
                                    elide: Text.ElideMiddle
                                }

                                Label {
                                    text: [modelData.quality,
                                           modelData.lengthSec > 0 ? page.formatLength(modelData.lengthSec) : "",
                                           page.formatSize(modelData.size)].filter(p => p !== "").join("  ·  ")
                                    opacity: 0.6
                                    font.pixelSize: 12
                                }

                                Button {
                                    text: qsTr("Download")
                                    flat: true
                                    onClicked: SlskdController.downloadAlbumFile(groupRow.albumIndex, index)
                                }
                            }
                        }

                        // Artists: their albums from every user.
                        Repeater {
                            model: viewSettings.view === 2 && groupRow.expanded ? groupRow.items : []

                            delegate: RowLayout {
                                required property var modelData

                                Layout.fillWidth: true
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.title
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: [modelData.username, modelData.fileCount + " " + qsTr("files"),
                                               page.formatSize(modelData.size),
                                               page.slotText(modelData.hasFreeSlot, modelData.queueLength)].join("  ·  ")
                                        elide: Text.ElideRight
                                        opacity: 0.6
                                        font.pixelSize: 12
                                    }
                                }

                                Badge { text: modelData.formats }

                                Label {
                                    Layout.preferredWidth: 100
                                    text: modelData.quality
                                    font.pixelSize: 12
                                    opacity: 0.8
                                }

                                Button {
                                    text: qsTr("Download album")
                                    flat: true
                                    onClicked: SlskdController.downloadAlbum(modelData.albumIndex)
                                }
                            }
                        }
                    }
                }

                // Individual files.
                ListView {
                    id: tracksList

                    anchors.fill: parent
                    anchors.margins: 8
                    visible: viewSettings.view === 0
                    clip: true
                    spacing: 2
                    model: visible ? SlskdController.results : null
                    ScrollBar.vertical: SlimScrollBar {}

                    delegate: ItemDelegate {
                        id: result

                        required property int index
                        required property string fileName
                        required property string folder
                        required property string username
                        required property string extension
                        required property string quality
                        required property int lengthSec
                        required property real size
                        required property bool hasFreeSlot
                        required property int queueLength

                        width: ListView.view.width - 12
                        height: 56

                        background: Rectangle {
                            radius: Math.min(Theme.radius, 10)
                            color: result.hovered ? Theme.hover : "transparent"
                        }

                        contentItem: RowLayout {
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Label {
                                    Layout.fillWidth: true
                                    text: result.fileName
                                    elide: Text.ElideMiddle
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: [result.username,
                                           result.lengthSec > 0 ? page.formatLength(result.lengthSec) : "",
                                           page.formatSize(result.size),
                                           page.slotText(result.hasFreeSlot, result.queueLength)]
                                          .filter(part => part !== "").join("  ·  ")
                                    elide: Text.ElideRight
                                    opacity: 0.6
                                    font.pixelSize: 12
                                }
                            }

                            Badge { text: result.extension.toUpperCase() }

                            Label {
                                Layout.preferredWidth: 100
                                text: result.quality
                                font.pixelSize: 12
                                opacity: 0.8
                            }

                            Button {
                                text: qsTr("Download")
                                flat: true
                                onClicked: SlskdController.download(result.index)
                            }
                        }

                        ToolTip.text: result.folder
                        ToolTip.visible: hovered && result.folder !== ""
                        ToolTip.delay: 800
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: (viewSettings.view === 0 ? tracksList.count : groupsList.count) === 0
                             && !SlskdController.searching
                    text: page.online ? qsTr("Search for an artist, album or track")
                                      : qsTr("Start slskd to search Soulseek (status in the title bar)")
                    opacity: 0.5
                }
            }

            Card {
                // Narrower in small (e.g. snapped half-screen) windows.
                Layout.preferredWidth: Math.min(280, page.width * 0.38)
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Downloads")
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }

                        // Finished = succeeded, failed or cancelled; downloads still running stay.
                        Button {
                            text: qsTr("Clear downloads")
                            flat: true
                            ToolTip.text: qsTr("Removes finished downloads from the list. Downloads still running keep going.")
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            enabled: downloadsList.count > 0 && page.online
                            onClicked: SlskdController.clearFinishedDownloads()
                        }
                    }

                    ListView {
                        id: downloadsList

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 10
                        model: SlskdController.downloads

                        delegate: ColumnLayout {
                            id: transfer

                            required property int index
                            required property string fileName
                            required property string username
                            required property string state
                            required property real progress
                            required property bool finished
                            required property string note

                            width: ListView.view.width
                            spacing: 2

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    Layout.fillWidth: true
                                    text: transfer.fileName
                                    elide: Text.ElideMiddle
                                }

                                ToolButton {
                                    visible: !transfer.finished
                                    text: "✕"
                                    opacity: 0.7
                                    implicitHeight: 28
                                    ToolTip.text: qsTr("Cancel download")
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 500
                                    onClicked: SlskdController.cancelDownload(transfer.index)
                                }
                            }

                            ProgressBar {
                                Layout.fillWidth: true
                                value: transfer.progress
                                visible: !transfer.finished
                            }

                            Label {
                                Layout.fillWidth: true
                                text: transfer.username + "  ·  " + transfer.state
                                elide: Text.ElideRight
                                opacity: 0.6
                                font.pixelSize: 12
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: text !== ""
                                text: transfer.note
                                wrapMode: Text.Wrap
                                color: Theme.accent
                                font.pixelSize: 12
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: downloadsList.count === 0
                            text: qsTr("No downloads")
                            opacity: 0.5
                        }
                    }
                }
            }
        }
    }
}
