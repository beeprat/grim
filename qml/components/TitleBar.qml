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
            const error = StatsController.exportTo(selectedFile, SettingsController.libraryFolder)
            bar.showNote(error !== "" ? error : qsTr("Statistics exported"))
        }
    }

    FileDialog {
        id: importDialog

        title: qsTr("Import statistics")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("grim. statistics (*.json)")]
        onAccepted: bar.showNote(StatsController.importFrom(selectedFile, SettingsController.libraryFolder))
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
        property string shape // minimize / maximize / restore / close, drawn where Segoe MDL2 is missing
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
        contentItem: Item {
            id: glyphs

            readonly property color ink: button.danger && button.hovered ? "white" : Material.foreground

            Text {
                anchors.fill: parent
                visible: Qt.platform.os === "windows"
                text: button.text
                font: button.font
                color: glyphs.ink
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            // Other systems lack the Windows icon font: the same 10 px glyphs drawn with 1 px lines.
            Item {
                anchors.centerIn: parent
                width: 10
                height: 10
                visible: Qt.platform.os !== "windows"

                Rectangle { // minimize
                    visible: button.shape === "minimize"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 10
                    height: 1
                    color: glyphs.ink
                }
                Rectangle { // maximize, and the front window of restore
                    visible: button.shape === "maximize" || button.shape === "restore"
                    x: 0
                    y: button.shape === "restore" ? 2 : 0
                    width: button.shape === "restore" ? 8 : 10
                    height: width
                    color: "transparent"
                    border.width: 1
                    border.color: glyphs.ink
                }
                Item { // the back window of restore: top and right edges only
                    visible: button.shape === "restore"
                    Rectangle { x: 2; y: 0; width: 8; height: 1; color: glyphs.ink }
                    Rectangle { x: 9; y: 0; width: 1; height: 8; color: glyphs.ink }
                }
                Repeater { // close: two diagonals
                    model: button.shape === "close" ? [45, -45] : []
                    Rectangle {
                        required property int modelData
                        anchors.centerIn: parent
                        width: 13
                        height: 1
                        rotation: modelData
                        antialiasing: true
                        color: glyphs.ink
                    }
                }
            }
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

    // A newer release on GitHub (checked at startup): click to download and install it (UpdateController.install).
    AbstractButton {
        id: updateNote

        anchors.centerIn: parent
        height: 24
        visible: UpdateController.latestVersion !== ""
        hoverEnabled: true
        leftPadding: 12
        rightPadding: 12
        onClicked: UpdateController.install()


        background: Rectangle {
            radius: height / 2
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, updateNote.hovered ? 0.35 : 0.2)
        }
        contentItem: Label {
            text: {
                switch (UpdateController.state) {
                case UpdateController.Downloading:
                    return qsTr("Downloading grim. %1… %2%").arg(UpdateController.latestVersion)
                                                          .arg(Math.round(UpdateController.progress * 100))
                case UpdateController.Installing: return qsTr("Installing…")
                case UpdateController.Failed: return qsTr("Update failed: click to open the download page")
                }
                return qsTr("grim. %1 is available: click to update").arg(UpdateController.latestVersion)
            }
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
        }
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

        // Starts or stops grim.'s slskd (it runs in the background otherwise).
        IconButton {
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            Layout.alignment: Qt.AlignVCenter
            padding: 0
            topInset: 0
            bottomInset: 0
            leftInset: 0
            rightInset: 0
            // A circle filling the button; Material's ripple sat off the icon here.
            background: Rectangle {
                radius: width / 2
                color: parent.pressed ? Qt.darker(Theme.subtle, 1.2) : parent.hovered ? Theme.subtle : "transparent"
            }
            visible: SlskdController.managed
            iconName: SlskdController.managedRunning ? "stop" : "play"
            iconSize: 14
            onClicked: SlskdController.managedRunning = !SlskdController.managedRunning
        }

        // slskd status, checked while the app starts. Click to check again.
        AbstractButton {
            id: status

            Layout.fillHeight: true
            Layout.rightMargin: 8
            hoverEnabled: true
            onClicked: SlskdController.recheckStatus()


            readonly property string statusText: {
                switch (SlskdController.status) {
                case SlskdController.Checking: return qsTr("Checking slskd…")
                case SlskdController.Offline: return SlskdController.managedStatus
                case SlskdController.AuthRequired: return qsTr("slskd did not accept grim.'s API key")
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
            shape: "minimize"
            onClicked: bar.window.showMinimized()
        }

        WindowButton {
            text: bar.maximized ? "" : "" // ChromeRestore / ChromeMaximize
            shape: bar.maximized ? "restore" : "maximize"
            onClicked: bar.maximized ? bar.window.showNormal() : bar.window.showMaximized()
        }

        WindowButton {
            text: "" // ChromeClose
            shape: "close"
            danger: true
            onClicked: bar.window.close()
        }
    }
}
