import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

// Separate window for making a playlist: a name, and every library track to tick, with search.
ApplicationWindow {
    id: window

    // Ticked tracks in the order they were ticked: url string -> url.
    property var selected: ({})
    property int selectedCount: 0

    signal created(var playlistId)

    // Opens the window empty, or with some tracks already ticked (from "Add to playlist → New playlist…").
    function openWith(fileUrls) {
        const preselected = {}
        for (const url of fileUrls || [])
            preselected[url.toString()] = url
        selected = preselected
        selectedCount = Object.keys(preselected).length
        nameField.text = ""
        searchField.text = ""
        LibraryController.pickerTracks.setFilter("")
        show()
        raise()
        requestActivate()
        nameField.forceActiveFocus()
    }

    function toggle(url) {
        const key = url.toString()
        const next = Object.assign({}, selected)
        if (next[key] !== undefined)
            delete next[key]
        else
            next[key] = url
        selected = next
        selectedCount = Object.keys(next).length
    }

    width: 560
    height: 680
    minimumWidth: 420
    minimumHeight: 420
    title: qsTr("New playlist")
    modality: Qt.ApplicationModal
    color: Theme.window
    Material.theme: Material.Dark
    Material.accent: Theme.accent
    Material.background: Theme.card

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        TextField {
            id: nameField

            Layout.fillWidth: true
            placeholderText: qsTr("Playlist name")
            onAccepted: createButton.clicked()
        }

        TextField {
            id: searchField

            Layout.fillWidth: true
            placeholderText: qsTr("Search title, artist, album")
            onTextEdited: LibraryController.pickerTracks.setFilter(text)
        }

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list

                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: LibraryController.pickerTracks
                ScrollBar.vertical: SlimScrollBar {}
                FastWheelScroll { flickable: list }

                delegate: ItemDelegate {
                    id: row

                    required property string title
                    required property string artist
                    required property string album
                    required property url url
                    required property string coverUrl
                    readonly property bool ticked: window.selected[url.toString()] !== undefined

                    width: ListView.view.width - 12
                    height: 52
                    onClicked: window.toggle(url)

                    background: Rectangle {
                        radius: Math.min(Theme.radius, 10)
                        color: row.ticked ? Theme.selected : row.hovered ? Theme.hover : "transparent"
                    }

                    contentItem: RowLayout {
                        spacing: 10

                        CheckBox {
                            checked: row.ticked
                            onToggled: window.toggle(row.url)
                        }

                        CoverArt {
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 36
                            source: row.coverUrl
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Label {
                                Layout.fillWidth: true
                                text: row.title
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                text: [row.artist, row.album].filter(part => part !== "").join("  ·  ")
                                elide: Text.ElideRight
                                opacity: 0.6
                                font.pixelSize: 12
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: qsTr("No tracks match")
                    opacity: 0.5
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: window.selectedCount === 1 ? qsTr("1 track selected")
                                                 : qsTr("%1 tracks selected").arg(window.selectedCount)
                opacity: 0.7
            }

            Button {
                text: qsTr("Cancel")
                flat: true
                onClicked: window.close()
            }

            AccentButton {
                id: createButton

                text: qsTr("Create playlist")
                enabled: nameField.text.trim() !== ""
                onClicked: {
                    if (!enabled)
                        return
                    const id = PlaylistController.createPlaylist(nameField.text, Object.values(window.selected))
                    if (id > 0) {
                        window.created(id)
                        window.close()
                    }
                }
            }
        }
    }
}
