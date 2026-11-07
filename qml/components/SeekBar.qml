import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

// Thin progress bar: elapsed, seek slider, length, across the whole card.
// Mouse wheel seeks ±5 s. A-B loop: Ctrl+click (drag to fine-tune, set on release) marks the start, the next
// Ctrl+click the end; hold Shift while dragging to move the marker 10x slower; right-click removes the loop.
// While dragging, playback follows the marker (the start plays from it, the end plays its last second) and
// m:ss.mmm rides above it. Ctrl+drag on an existing marker moves that marker.
Card {
    id: bar

    function formatTime(ms) {
        const totalSeconds = Math.floor(ms / 1000)
        const seconds = totalSeconds % 60
        return Math.floor(totalSeconds / 60) + ":" + (seconds < 10 ? "0" : "") + seconds
    }
    // m:ss.t, for placing a loop marker.
    function formatPrecise(ms) {
        return formatTime(ms) + "." + Math.floor(ms % 1000 / 100)
    }
    // m:ss.mmm, shown over a marker while it is dragged.
    function formatMs(ms) {
        return formatTime(ms) + "." + String(Math.floor(ms % 1000)).padStart(3, "0")
    }

    // Same height as the playback controls next to it, so the bottom row lines up.
    implicitHeight: 64

    WheelHandler {
        target: null
        acceptedModifiers: Qt.NoModifier // Ctrl + wheel stays the UI zoom
        enabled: seekSlider.enabled
        onWheel: (event) => PlayerController.seek(PlayerController.positionMs + 5000 * event.angleDelta.y / 120)
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 12

        Label {
            Layout.preferredWidth: 52
            horizontalAlignment: Text.AlignRight
            text: seekSlider.loopDrag !== "" ? bar.formatPrecise(seekSlider.loopDragMs)
                  : PlayerController.loopEndMs >= 0 ? bar.formatPrecise(seekSlider.value) : bar.formatTime(seekSlider.value)
            // Accent while an A-B loop is set.
            color: PlayerController.loopEndMs >= 0 ? Theme.accent : Material.foreground
            opacity: PlayerController.loopEndMs >= 0 ? 1 : 0.6
            font.pixelSize: 12
        }

        Slider {
            id: seekSlider

            Layout.fillWidth: true
            enabled: PlayerController.hasTrack && PlayerController.durationMs > 0
            from: 0
            to: Math.max(1, PlayerController.durationMs)
            // Seek once on release, not on every drag step.
            onPressedChanged: if (!pressed) PlayerController.seek(value)

            Binding on value {
                when: !seekSlider.pressed
                value: PlayerController.positionMs
            }

            // Track position <-> x inside the slider (the groove spans availableWidth after leftPadding).
            function xOf(ms) { return leftPadding + availableWidth * ms / to }
            function msAt(x) { return Math.round(Math.max(0, Math.min(1, (x - leftPadding) / availableWidth)) * to) }

            // The loop being drawn: which end the Ctrl+drag sets, and where it is now (set on release).
            property string loopDrag: ""
            property real loopDragMs: 0
            property real lastX: 0 // pointer x at the previous move, for the Shift fine drag
            readonly property real shownStart: loopDrag === "start" ? loopDragMs : PlayerController.loopStartMs
            readonly property real shownEnd: loopDrag === "end" ? loopDragMs : PlayerController.loopEndMs

            // Loop band over the groove, with a marker at each end.
            Rectangle {
                visible: seekSlider.shownStart >= 0 && seekSlider.shownEnd >= 0
                x: seekSlider.xOf(Math.min(seekSlider.shownStart, seekSlider.shownEnd))
                width: Math.abs(seekSlider.xOf(seekSlider.shownEnd) - seekSlider.xOf(seekSlider.shownStart))
                anchors.verticalCenter: parent.verticalCenter
                height: 16
                radius: 4
                color: Theme.accent
                opacity: 0.35
            }
            // Exact time above the marker being dragged.
            Label {
                visible: seekSlider.loopDrag !== ""
                text: bar.formatMs(seekSlider.loopDragMs)
                x: Math.max(0, Math.min(seekSlider.width - width, seekSlider.xOf(seekSlider.loopDragMs) - width / 2))
                y: -height + 6
                font.pixelSize: 11
                color: Theme.accent
            }

            // Playback follows the marker while dragging; seeks are spaced out, not one per mouse move.
            Timer {
                id: auditionTimer

                interval: 120
                onTriggered: PlayerController.seek(seekSlider.loopDrag === "end"
                                                   ? Math.max(0, seekSlider.loopDragMs - 1000) : seekSlider.loopDragMs)
            }
            function audition() {
                if (!auditionTimer.running)
                    auditionTimer.start()
            }

            Repeater {
                model: [seekSlider.shownStart, seekSlider.shownEnd]

                Rectangle {
                    required property real modelData

                    visible: modelData >= 0 && seekSlider.enabled
                    x: seekSlider.xOf(modelData) - width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: 2
                    height: 18
                    radius: 1
                    color: Theme.accent
                }
            }

            MouseArea {
                anchors.fill: parent
                enabled: seekSlider.enabled
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onPressed: (mouse) => {
                    if (mouse.button === Qt.RightButton) {
                        PlayerController.clearLoop()
                    } else if (mouse.modifiers & Qt.ControlModifier) {
                        // On a marker of a complete loop: move it. Otherwise a complete loop (or none) gets a
                        // new start; a lone start gets its end.
                        const near = (ms) => ms >= 0 && Math.abs(mouse.x - seekSlider.xOf(ms)) <= 6
                        const complete = PlayerController.loopEndMs >= 0
                        if (complete && near(PlayerController.loopEndMs)) {
                            seekSlider.loopDrag = "end"
                            seekSlider.loopDragMs = PlayerController.loopEndMs
                        } else if (complete && near(PlayerController.loopStartMs)) {
                            seekSlider.loopDrag = "start"
                            seekSlider.loopDragMs = PlayerController.loopStartMs
                        } else {
                            const settingEnd = PlayerController.loopStartMs >= 0 && !complete
                            seekSlider.loopDrag = settingEnd ? "end" : "start"
                            seekSlider.loopDragMs = seekSlider.msAt(mouse.x)
                        }
                        seekSlider.lastX = mouse.x
                        seekSlider.audition()
                    } else {
                        mouse.accepted = false // plain clicks and drags seek as before
                    }
                }
                onPositionChanged: (mouse) => {
                    if (seekSlider.loopDrag === "")
                        return
                    if (mouse.modifiers & Qt.ShiftModifier) {
                        const step = (mouse.x - seekSlider.lastX) / seekSlider.availableWidth * seekSlider.to / 10
                        seekSlider.loopDragMs = Math.round(Math.max(0, Math.min(seekSlider.to, seekSlider.loopDragMs + step)))
                    } else {
                        seekSlider.loopDragMs = seekSlider.msAt(mouse.x)
                    }
                    seekSlider.lastX = mouse.x
                    seekSlider.audition()
                }
                onReleased: {
                    auditionTimer.stop()
                    if (seekSlider.loopDrag === "start")
                        PlayerController.setLoopStart(seekSlider.loopDragMs)
                    else if (seekSlider.loopDrag === "end")
                        PlayerController.setLoopEnd(seekSlider.loopDragMs)
                    seekSlider.loopDrag = ""
                }
                onCanceled: seekSlider.loopDrag = ""
            }
        }

        Label {
            Layout.preferredWidth: 40
            text: bar.formatTime(PlayerController.durationMs)
            opacity: 0.6
            font.pixelSize: 12
        }
    }
}
