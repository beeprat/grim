import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

// Thin progress bar: elapsed, seek slider, length, across the whole card.
// Mouse wheel seeks ±5 s.
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
            opacity: 0.6
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
        }

        Label {
            Layout.preferredWidth: 40
            text: bar.formatTime(PlayerController.durationMs)
            opacity: 0.6
            font.pixelSize: 12
        }
    }
}
