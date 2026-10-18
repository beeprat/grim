import QtCore
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

// Replaces the native Windows title bar (the window is frameless): drag to move,
// double-click to maximize, window buttons, and the slskd status dot.
Rectangle {
    id: bar

    required property ApplicationWindow window

    // Result of a statistics export/import, shown next to the buttons for a few seconds.
    function showNote(text) {
        transferNote.text = text
        noteTimer.restart()
    }

    // Statistics export / import (the buttons show on the Statistics page).
    FileDialog {
        id: exportDialog

        title: qsTr("Export statistics")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("grim. statistics (*.json)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            const error = StatsController.exportTo(selectedFile)
            bar.showNote(error !== "" ? error : qsTr("Statistics exported"))
        }
    }

    FileDialog {
        id: importDialog

        title: qsTr("Import statistics")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("grim. statistics (*.json)")]
        onAccepted: bar.showNote(StatsController.importFrom(selectedFile))
    }

    Timer {
        id: noteTimer

        interval: 5000
        onTriggered: transferNote.text = ""
    }

    readonly property bool maximized: window.visibility === Window.Maximized

    implicitHeight: 36
    color: "transparent"

    // Window buttons use Windows' own icon font (Win10 and Win11).
    component WindowButton: AbstractButton {
        id: button

        property bool danger: false // close button: red on hover
        readonly property color hoverColor: danger ? "#c42b1c" : Theme.subtle

        Layout.preferredWidth: 46
        Layout.fillHeight: true
        font.family: "Segoe MDL2 Assets"
        font.pixelSize: 10

        background: Rectangle {
            color: button.pressed ? Qt.darker(button.hoverColor, 1.2)
                 : button.hovered ? button.hoverColor
                 : "transparent"
        }
        contentItem: Text {
            text: button.text
            font: button.font
            color: button.danger && button.hovered ? "white" : Material.foreground
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    DragHandler {
        target: null
        grabPermissions: PointerHandler.CanTakeOverFromAnything
        onActiveChanged: if (active) bar.window.startSystemMove()
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onDoubleTapped: bar.maximized ? bar.window.showNormal() : bar.window.showMaximized()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        spacing: 0

        Label {
            text: bar.window.title
            font.pixelSize: 12
            font.weight: Font.DemiBold
            opacity: 0.7
        }

        Item { Layout.fillWidth: true }

        Label {
            id: transferNote

            Layout.rightMargin: 4
            visible: text !== "" && bar.window.page === 2
            opacity: 0.6
            font.pixelSize: 11
        }

        // Export / import of the listening history, on the Statistics page only.
        component TitleTextButton: AbstractButton {
            id: textButton

            Layout.fillHeight: true
            hoverEnabled: true
            visible: bar.window.page === 2
            leftPadding: 8
            rightPadding: 8
            contentItem: Label {
                text: textButton.text
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
                opacity: textButton.hovered ? 1 : 0.7
            }
            background: Item {}
        }

        TitleTextButton {
            text: qsTr("Export")
            onClicked: exportDialog.open()
        }

        TitleTextButton {
            text: qsTr("Import")
            onClicked: importDialog.open()
        }

        // Thin line between the statistics buttons and the slskd status.
        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 14
            Layout.leftMargin: 4
            Layout.rightMargin: 8
            visible: bar.window.page === 2
            color: Qt.rgba(1, 1, 1, 0.2)
        }

        // slskd status, checked while the app starts. Click to check again.
        AbstractButton {
            id: status

            Layout.fillHeight: true
            Layout.rightMargin: 8
            hoverEnabled: true
            onClicked: SlskdController.recheckStatus()

            ToolTip.visible: hovered
            ToolTip.delay: 300
            ToolTip.text: statusText + (SlskdController.status === SlskdController.Checking ? "" : "
" + qsTr("Click to check again"))

            readonly property string statusText: {
                switch (SlskdController.status) {
                case SlskdController.Checking: return qsTr("Checking slskd…")
                case SlskdController.Offline: return qsTr("slskd is not running at %1").arg(SlskdController.serverUrl)
                case SlskdController.AuthRequired: return qsTr("slskd needs an API key (Soulseek › Settings), or start it with --no-auth")
                case SlskdController.NotLoggedIn: return qsTr("slskd is running but not logged in to Soulseek")
                case SlskdController.Online: return qsTr("slskd is connected to Soulseek")
                }
                return ""
            }

            background: Item {}
            contentItem: RowLayout {
                spacing: 6

                Rectangle {
                    Layout.leftMargin: 8
                    Layout.preferredWidth: 8
                    Layout.preferredHeight: 8
                    radius: 4
                    color: {
                        switch (SlskdController.status) {
                        case SlskdController.Online: return Material.color(Material.Green, Material.Shade400)
                        case SlskdController.Offline: return Material.color(Material.Red, Material.Shade400)
                        case SlskdController.Checking: return Material.color(Material.Grey)
                        default: return Material.color(Material.Amber, Material.Shade400)
                        }
                    }

                    Behavior on color {
                        ColorAnimation { duration: 200 }
                    }
                }

                Label {
                    Layout.rightMargin: 8
                    text: "slskd"
                    font.pixelSize: 12
                    opacity: 0.6
                }
            }
        }

        WindowButton {
            text: "" // ChromeMinimize
            onClicked: bar.window.showMinimized()
        }

        WindowButton {
            text: bar.maximized ? "" : "" // ChromeRestore / ChromeMaximize
            onClicked: bar.maximized ? bar.window.showNormal() : bar.window.showMaximized()
        }

        WindowButton {
            text: "" // ChromeClose
            danger: true
            onClicked: bar.window.close()
        }
    }
}
