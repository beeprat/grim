import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

// One of the "number one" cards on the statistics page: picture, label, name, listening time.
// `entry` is StatsController.topTrack / topArtist / topAlbum (empty before anything was played).
Card {
    id: card

    required property string label
    property var entry: ({})
    property string detail // e.g. "12h 3m · 40 plays", formatted by the page
    readonly property bool hasEntry: entry !== undefined && entry.title !== undefined

    signal clicked()

    Layout.fillWidth: true
    Layout.preferredWidth: 1 // three equal cards
    implicitHeight: 128

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 14

        CoverArt {
            Layout.preferredWidth: 96
            Layout.preferredHeight: 96
            source: card.hasEntry ? card.entry.image : ""
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                text: card.label
                opacity: 0.6
                font.pixelSize: 12
            }

            Label {
                Layout.fillWidth: true
                text: card.hasEntry ? card.entry.title : qsTr("Nothing played yet")
                elide: Text.ElideRight
                font.pixelSize: 18
                font.weight: Font.DemiBold
                font.underline: mouse.containsMouse && card.hasEntry
                opacity: card.hasEntry ? 1 : 0.5
            }

            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: card.hasEntry ? card.entry.subtitle : ""
                elide: Text.ElideRight
                opacity: 0.75
                font.pixelSize: 13
            }

            Label {
                Layout.fillWidth: true
                visible: card.hasEntry
                text: card.detail
                elide: Text.ElideRight
                opacity: 0.5
                font.pixelSize: 12
            }
        }
    }

    MouseArea {
        id: mouse

        anchors.fill: parent
        enabled: card.hasEntry
        hoverEnabled: true
        cursorShape: card.hasEntry ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: card.clicked()
    }
}
