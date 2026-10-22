import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

// Card with a numbered list ranked by listening time; each row's bar shows its
// time relative to the top entry.
Card {
    id: card

    required property string title
    required property var listModel

    // A row was clicked: its url (track, or one track of the album; empty for artists) and title.
    signal entryClicked(url fileUrl, string name)

    function formatDuration(ms) {
        const totalMinutes = Math.floor(ms / 60000)
        if (totalMinutes < 1)
            return qsTr("%1 s").arg(Math.floor(ms / 1000))
        const hours = Math.floor(totalMinutes / 60)
        return hours > 0 ? qsTr("%1h %2m").arg(hours).arg(totalMinutes % 60) : qsTr("%1 min").arg(totalMinutes)
    }

    Layout.fillWidth: true
    Layout.fillHeight: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        Label {
            text: card.title
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }

        ListView {
            id: list

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: card.listModel

            delegate: Item {
                id: row

                required property int index
                required property string title
                required property string subtitle
                required property int playCount
                required property real listenedMs
                required property real share
                required property url url

                width: ListView.view.width
                height: 46

                MouseArea {
                    id: rowMouse

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: card.entryClicked(row.url, row.title)
                }

                Rectangle {
                    anchors.fill: parent
                    radius: Math.min(Theme.radius, 8)
                    color: rowMouse.containsMouse ? Theme.hover : "transparent"
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * row.share
                    radius: Math.min(Theme.radius, 8)
                    color: Theme.accent
                    opacity: 0.14
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 10

                    Label {
                        Layout.preferredWidth: 18
                        text: row.index + 1
                        opacity: 0.5
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
                            visible: text !== ""
                            text: row.subtitle
                            elide: Text.ElideRight
                            opacity: 0.6
                            font.pixelSize: 12
                        }
                    }

                    ColumnLayout {
                        spacing: 0

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: card.formatDuration(row.listenedMs)
                            font.pixelSize: 13
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: row.playCount === 1 ? qsTr("1 play") : qsTr("%1 plays").arg(row.playCount)
                            opacity: 0.5
                            font.pixelSize: 11
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                text: qsTr("Nothing played yet")
                opacity: 0.5
            }
        }
    }

    // Rows fade out at the top once scrolled and at the bottom, as in the library.
    EdgeFade {
        flickable: list
        bottomHeight: Math.min(100, list.height / 4)
    }
}
