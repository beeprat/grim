import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

// Volume with mute toggle; the mouse wheel anywhere on the card changes it by 5 %.
Card {
    id: card

    property real volumeBeforeMute: 1

    implicitHeight: 52

    WheelHandler {
        target: null
        acceptedModifiers: Qt.NoModifier // Ctrl + wheel stays the UI zoom
        onWheel: (event) => PlayerController.volume = Math.max(0, Math.min(1,
                     PlayerController.volume + 0.05 * event.angleDelta.y / 120))
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 16
        spacing: 4

        // Settings → Effects → "Show effects button". Lit while any effect is on.
        IconButton {
            id: effectsButton

            visible: Effects.showInNowPlaying
            iconName: "tune"
            tip: qsTr("Effects")
            active: Effects.anyOn
            onClicked: effectsPopup.opened ? effectsPopup.close() : effectsPopup.open()

            // Opens above the button and to its left, over the pages: the column is too
            // narrow for three sliders.
            Popup {
                id: effectsPopup

                x: effectsButton.width - width
                y: -height - 6
                width: 440
                // Not "outside the popup": pressing the button would close it, and the click that
                // follows would open it again. Outside the button, so the button toggles it.
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                padding: 14
                // Popups live in the window overlay, outside the Ctrl+wheel zoom: scale like the
                // rest. Its position is already mapped through the zoom, so grow from that corner.
                scale: Theme.uiScale
                transformOrigin: Popup.TopLeft
                // Material's open/close animations animate `scale`, which would undo the zoom.
                enter: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 }
                }
                exit: Transition {
                    NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 }
                }
                background: PopupBackground {}

                contentItem: EffectsControls {
                    switchWidth: 140
                }
            }
        }

        IconButton {
            iconName: PlayerController.volume > 0 ? "volume" : "volume_off"
            tip: PlayerController.volume > 0 ? qsTr("Mute") : qsTr("Unmute")
            onClicked: {
                if (PlayerController.volume > 0) {
                    card.volumeBeforeMute = PlayerController.volume
                    PlayerController.volume = 0
                } else {
                    PlayerController.volume = card.volumeBeforeMute
                }
            }
        }

        Slider {
            Layout.fillWidth: true
            from: 0
            to: 1
            value: PlayerController.volume
            onMoved: PlayerController.volume = value
        }

        Label {
            Layout.preferredWidth: 34
            horizontalAlignment: Text.AlignRight
            text: Math.round(PlayerController.volume * 100) + "%"
            opacity: 0.6
            font.pixelSize: 12
        }
    }
}
