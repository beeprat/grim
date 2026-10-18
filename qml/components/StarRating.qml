import QtQuick
import QtQuick.Controls.Material
import MusicPlayer

// 1–5 stars for one track. Click a star to rate; click the current rating again to clear.
Row {
    id: stars

    required property url url
    property int size: 14

    // Reads metaRevision so the binding re-runs when any rating changes. A ternary, not a comma
    // expression: the compiled binding drops the dependency of an unused comma operand.
    readonly property int rating: LibraryController.metaRevision >= 0 ? LibraryController.rating(url) : 0
    property int hovered: 0 // star under the mouse, 0 = none

    spacing: 1
    visible: url.toString() !== ""

    Repeater {
        model: 5

        delegate: Text {
            required property int index
            readonly property int value: index + 1
            readonly property bool lit: value <= (stars.hovered > 0 ? stars.hovered : stars.rating)

            text: lit ? "★" : "☆"
            font.pixelSize: stars.size
            color: lit ? Theme.accent : Material.foreground
            opacity: lit ? 1 : 0.3

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onEntered: stars.hovered = parent.value
                onExited: stars.hovered = 0
                onClicked: LibraryController.setRating(stars.url, stars.rating === parent.value ? 0 : parent.value)
            }
        }
    }
}
