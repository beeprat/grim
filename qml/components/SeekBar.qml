import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

// Thin progress bar: elapsed, seek slider, length, across the whole card.
// Mouse wheel seeks ±5 s. A-B loop: Ctrl+click (drag to fine-tune, set on release) marks the start, the next
// Ctrl+click the end; right-click removes the loop.
Card {
    id: bar

    function formatTime(ms) {
        const totalSeconds = Math.floor(ms / 1000)
        const seconds = totalSeconds % 60
        return Math.floor(totalSeconds / 60) + ":" + (seconds < 10 ? "0" : "") + seconds
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
            Layout.preferredWidth: 40
            horizontalAlignment: Text.AlignRight
            text: bar.formatTime(seekSlider.value)
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
            readonly property real shownStart: loopDrag === "start" ? loopDragMs : PlayerController.loopStartMs
            readonly property real shownEnd: loopDrag === "end" ? loopDragMs
                                           : loopDrag === "start" ? -1 : PlayerController.loopEndMs

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
                        // A complete loop (or none) gets a new start; a lone start gets its end.
                        const settingEnd = PlayerController.loopStartMs >= 0 && PlayerController.loopEndMs < 0
                        seekSlider.loopDrag = settingEnd ? "end" : "start"
                        seekSlider.loopDragMs = seekSlider.msAt(mouse.x)
                    } else {
                        mouse.accepted = false // plain clicks and drags seek as before
                    }
                }
                onPositionChanged: (mouse) => {
                    if (seekSlider.loopDrag !== "")
                        seekSlider.loopDragMs = seekSlider.msAt(mouse.x)
                }
                onReleased: {
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
