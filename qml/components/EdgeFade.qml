import QtQuick
import QtQuick.Effects

// Rows near a list's top (once scrolled) and bottom fade out to transparent instead of a hard cut, so
// whatever is behind the list (its card, a background image) shows through and nothing is painted over
// it. One per list: it masks `flickable` through a layer. Each fade goes away at that end of the list.
// Declare it next to the list; it stays invisible itself (it is only the mask).
Item {
    id: fade

    property Flickable flickable
    property real topHeight: 28
    property real bottomHeight: 100

    // 1 = no fade at that edge (the list is at that end, so the first/last row shows in full).
    property real topAlpha: flickable && !flickable.atYBeginning ? 0 : 1
    property real bottomAlpha: flickable && !flickable.atYEnd ? 0 : 1
    Behavior on topAlpha { NumberAnimation { duration: 150 } }
    Behavior on bottomAlpha { NumberAnimation { duration: 150 } }

    width: flickable ? flickable.width : 0
    height: flickable ? flickable.height : 0
    visible: false
    layer.enabled: true // the mask is read from this layer

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(1, 1, 1, fade.topAlpha) }
            GradientStop { position: Math.min(0.5, fade.topHeight / Math.max(1, fade.height)); color: "white" }
            GradientStop { position: Math.max(0.5, 1 - fade.bottomHeight / Math.max(1, fade.height)); color: "white" }
            GradientStop { position: 1; color: Qt.rgba(1, 1, 1, fade.bottomAlpha) }
        }
    }

    Component {
        id: maskEffect

        MultiEffect {
            maskEnabled: true
            maskSource: fade
            // A soft mask: the gradient's alpha becomes the list's opacity.
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
        }
    }

    // The library switches lists (grid, tracks, genres, album): move the mask with it.
    property Flickable masked: null
    function applyMask() {
        if (masked) {
            masked.layer.enabled = false
            masked.layer.effect = null
        }
        masked = flickable
        if (masked) {
            masked.layer.effect = maskEffect
            masked.layer.enabled = true
        }
    }
    onFlickableChanged: applyMask()
    Component.onCompleted: applyMask()
}
