import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

// "Check library": albums with tags to fill in (click: Edit info for the album) and songs saved twice
// (click a copy: the track menu, to delete it or open its folder).
Dialog {
    id: dialog

    property var missing: []
    property var duplicates: []
    property bool checking: false

    signal editRequested(var fileUrls)

    function check() {
        checking = true
        LibraryController.checkLibrary()
    }

    title: qsTr("Check library")
    modal: true
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.6) }
    anchors.centerIn: Overlay.overlay
    width: Math.min(640, Overlay.overlay ? Overlay.overlay.width - 48 : 640)
    height: Math.min(640, Overlay.overlay ? Overlay.overlay.height - 48 : 640)
    standardButtons: Dialog.Close
    onOpened: check()

    Connections {
        target: LibraryController
        function onLibraryChecked(missing, duplicates) {
            dialog.missing = missing
            dialog.duplicates = duplicates
            dialog.checking = false
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true

            TabBar {
                id: tabs

                Layout.fillWidth: true
                TabButton { text: qsTr("Missing tags (%1)").arg(dialog.missing.length) }
                TabButton { text: qsTr("Duplicates (%1)").arg(dialog.duplicates.length) }
            }
            Button {
                flat: true
                enabled: !dialog.checking
                text: dialog.checking ? qsTr("Checking…") : qsTr("Check again")
                onClicked: dialog.check()
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ListView {
                id: missingList

                clip: true
                spacing: 2
                model: dialog.missing
                ScrollBar.vertical: SlimScrollBar {}

                delegate: ItemDelegate {
                    id: gapRow

                    required property var modelData

                    width: missingList.width - 8
                    padding: 6
                    onClicked: dialog.editRequested(modelData.urls)
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Edit info")

                    contentItem: RowLayout {
                        spacing: 10

                        CoverArt {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            source: gapRow.modelData.coverUrl
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label { Layout.fillWidth: true; text: gapRow.modelData.album; elide: Text.ElideRight }
                            Label {
                                Layout.fillWidth: true
                                text: gapRow.modelData.artist || qsTr("Unknown artist")
                                elide: Text.ElideRight
                                opacity: 0.6
                                font.pixelSize: 12
                            }
                        }
                        Label {
                            text: qsTr("No %1").arg(gapRow.modelData.missing)
                            color: Theme.accent
                            font.pixelSize: 12
                        }
                    }
                }
            }

            ListView {
                id: duplicateList

                clip: true
                spacing: 8
                model: dialog.duplicates
                ScrollBar.vertical: SlimScrollBar {}

                delegate: ColumnLayout {
                    id: group

                    required property var modelData

                    width: duplicateList.width - 8
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        text: group.modelData.artist ? group.modelData.artist + " – " + group.modelData.title : group.modelData.title
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Repeater {
                        model: group.modelData.files

                        ItemDelegate {
                            required property var modelData

                            Layout.fillWidth: true
                            padding: 6
                            text: modelData.folder
                            font.pixelSize: 12
                            onClicked: ApplicationWindow.window.openTrackMenu([modelData.url])
                        }
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: !dialog.checking && (tabs.currentIndex === 0 ? dialog.missing : dialog.duplicates).length === 0
            text: tabs.currentIndex === 0 ? qsTr("Every album has its cover, year, genre and track numbers.")
                                          : qsTr("No song is saved twice.")
            opacity: 0.6
        }
    }
}
