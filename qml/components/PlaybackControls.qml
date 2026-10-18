import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

// Shuffle, previous, play/pause, next, repeat (off → all → one).
Card {
    implicitHeight: 64

    RowLayout {
        anchors.centerIn: parent
        spacing: 2

        IconButton {
            iconName: "shuffle"
            tip: qsTr("Shuffle")
            active: PlayerController.shuffle
            onClicked: PlayerController.shuffle = !PlayerController.shuffle
        }

        IconButton {
            iconName: "previous"
            tip: qsTr("Previous")
            iconSize: 24
            enabled: PlayerController.hasTrack
            onClicked: PlayerController.previous()
        }

        RoundButton {
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            enabled: PlayerController.hasTrack
            display: AbstractButton.IconOnly
            icon.source: PlayerController.playing ? "../icons/pause.svg" : "../icons/play.svg"
            icon.width: 26
            icon.height: 26
            icon.color: Theme.window
            Material.background: Theme.accent
            Material.elevation: 0
            onClicked: PlayerController.togglePlay()
        }

        IconButton {
            iconName: "next"
            tip: qsTr("Next")
            iconSize: 24
            enabled: PlayerController.hasTrack
            onClicked: PlayerController.next()
        }

        IconButton {
            iconName: PlayerController.repeat === PlayerController.RepeatOne ? "repeat_one" : "repeat"
            tip: PlayerController.repeat === PlayerController.RepeatOff ? qsTr("Repeat: off")
                 : PlayerController.repeat === PlayerController.RepeatAll ? qsTr("Repeat: all") : qsTr("Repeat: one")
            active: PlayerController.repeat !== PlayerController.RepeatOff
            onClicked: PlayerController.repeat = (PlayerController.repeat + 1) % 3
        }
    }
}
