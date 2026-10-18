import QtQuick
import MusicPlayer

// Content scrolling under it fades into the card instead of a hard cut.
// Bottom (default): anchor it to the bottom of the card, over the list.
// Top: it moves to the top edge of `flickable`; anchor its left/right like the bottom one.
Rectangle {
    id: fade

    // The list it covers; at that end (or when the list does not scroll) the fade goes away,
    // so the first/last row is shown in full.
    property Flickable flickable
    property bool atTop: false
    // What the content fades into: the card behind the list.
    property color baseColor: Theme.card

    opacity: !flickable ? 0 : atTop ? (flickable.atYBeginning ? 0 : 1) : (flickable.atYEnd ? 0 : 1)
    rotation: atTop ? 180 : 0 // the same gradient, upside down
    Behavior on opacity {
        NumberAnimation { duration: 150 }
    }

    // Eased rather than linear: a straight ramp still reads as an edge where it starts.
    gradient: Gradient {
        GradientStop { position: 0; color: Qt.rgba(baseColor.r, baseColor.g, baseColor.b, 0) }
        GradientStop { position: 0.35; color: Qt.rgba(baseColor.r, baseColor.g, baseColor.b, 0.25) }
        GradientStop { position: 0.65; color: Qt.rgba(baseColor.r, baseColor.g, baseColor.b, 0.65) }
        GradientStop { position: 0.85; color: Qt.rgba(baseColor.r, baseColor.g, baseColor.b, 0.92) }
        GradientStop { position: 1; color: baseColor }
    }

    // Top: follow the list vertically (it sits below a header whose height can change). The `>= -1e9`
    // reads make the binding re-run when the layout moves the list (a comma expression would
    // lose that dependency in compiled QML).
    Binding on y {
        when: fade.atTop && fade.flickable
        value: fade.flickable && fade.flickable.y >= -1e9 && fade.parent.height >= 0
               ? fade.flickable.mapToItem(fade.parent, 0, 0).y : 0
    }}
