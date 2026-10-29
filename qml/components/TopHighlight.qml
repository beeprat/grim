import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Controls.Material
import MusicPlayer

// One of the "number one" tiles on the statistics page: the picture fills the tile, with the name
// and listening time over its darkened bottom. `entry` is StatsController.topTrack / topArtist /
// topAlbum (empty before anything was played).
Item {
    id: card

    required property string label // "Top artist" etc., small above the name
    property var entry: ({})
    property string detail // e.g. "12h 3m · 40 plays", formatted by the page
    // Fills the row's spare width (the artist's banner photo); otherwise a square.
    property bool stretch: false
    property real tileHeight: 200
    readonly property bool hasEntry: entry !== undefined && entry.title !== undefined

    signal clicked()

    // A stretching tile takes the row's spare width (the picture is cropped to fit); the others
    // stay square.
    Layout.fillWidth: stretch
    Layout.preferredWidth: stretch ? tileHeight * 1.8 : tileHeight
    Layout.preferredHeight: tileHeight
    Accessible.name: label

    Rectangle {
        id: shape

        anchors.fill: parent
        radius: Theme.radius
        color: Theme.panel
    }

    Rectangle {
        id: roundMask

        anchors.fill: parent
        radius: Theme.radius
        visible: false
        layer.enabled: true
    }

    CoverArt {
        anchors.fill: parent
        radius: 0
        source: card.hasEntry ? card.entry.image : ""
        // Rounded like the other cards.
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: roundMask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
        }
    }

    // Darkens the bottom so the white text reads on any picture.
    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        gradient: Gradient {
            GradientStop { position: 0.45; color: "#00000000" }
            GradientStop { position: 1.0; color: "#b0000000" }
        }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 18
        spacing: 0

        Label {
            visible: card.hasEntry
            text: card.label
            color: "white"
            opacity: 0.7
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }

        Label {
            Layout.fillWidth: true
            text: card.hasEntry ? card.entry.title : card.label
            elide: Text.ElideRight
            color: "white"
            font.pixelSize: 32
            font.weight: Font.DemiBold
            font.underline: mouse.containsMouse && card.hasEntry
            opacity: card.hasEntry ? 1 : 0.5
        }

        Label {
            Layout.fillWidth: true
            text: card.hasEntry ? card.detail : qsTr("Nothing played yet")
            elide: Text.ElideRight
            color: "white"
            opacity: 0.75
            font.pixelSize: 13
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
