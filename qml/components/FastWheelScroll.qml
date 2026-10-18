import QtQuick

// Faster, smooth mouse-wheel scrolling for a list or grid. Declare it inside the
// Flickable it scrolls. The default wheel step felt slow on long libraries.
WheelHandler {
    id: handler

    required property Flickable flickable
    // Pixels per wheel unit; one notch is 120 units, so 2 = 240 px per notch.
    property real speed: 2

    target: null
    acceptedModifiers: Qt.NoModifier // Ctrl + wheel stays the UI zoom

    onWheel: (event) => {
        const f = flickable
        const top = f.originY - f.topMargin
        const bottom = Math.max(top, f.originY + f.contentHeight + f.bottomMargin - f.height)
        // Keep adding to the animation's target while a scroll is still running.
        const from = scroll.running ? scroll.to : f.contentY
        scroll.stop()
        scroll.to = Math.max(top, Math.min(bottom, from - event.angleDelta.y * speed))
        scroll.start()
    }

    property NumberAnimation scroll: NumberAnimation {
        target: handler.flickable
        property: "contentY"
        duration: 180
        easing.type: Easing.OutCubic
    }
}
