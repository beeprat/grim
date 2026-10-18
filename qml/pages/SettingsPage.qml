import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

Item {
    id: page

    // The cache grows while browsing; show its current size on each visit.
    onVisibleChanged: if (visible) SettingsController.refreshCacheSize()
    Component.onCompleted: SettingsController.refreshCacheSize()

    // Every section is its own card, like the rest of the app's panels.
    component SettingsPanel: Card {
        default property alias content: panelColumn.data

        Layout.fillWidth: true
        implicitHeight: panelColumn.implicitHeight + 40

        ColumnLayout {
            id: panelColumn

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 20
            spacing: 8
        }
    }

    component SectionTitle: Label {
        Layout.bottomMargin: 4
        font.pixelSize: 16
        font.weight: Font.DemiBold
    }

    component Hint: Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        opacity: 0.6
        font.pixelSize: 12
    }

    FolderDialog {
        id: folderDialog

        title: qsTr("Choose the music folder")
        currentFolder: "file:///" + SettingsController.libraryFolder
        onAccepted: SettingsController.setLibraryFolder(selectedFolder)
    }

    Item {
        anchors.fill: parent

        Flickable {
            id: settingsFlick

            anchors.fill: parent
            contentHeight: column.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: column

                width: parent.width
                spacing: 12

                SettingsPanel {
                    Label {
                        text: qsTr("Settings")
                        font.pixelSize: 24
                        font.weight: Font.DemiBold
                    }
                }

                SettingsPanel {
                    SectionTitle { text: qsTr("Appearance") }

                    Label { text: qsTr("Accent"); opacity: 0.8 }

                    RowLayout {
                        spacing: 10

                        Repeater {
                            model: Theme.accents

                            delegate: AbstractButton {
                                id: swatch

                                required property int index
                                required property var modelData

                                implicitWidth: 30
                                implicitHeight: 30
                                hoverEnabled: true
                                onClicked: Theme.accentIndex = index
                                ToolTip.text: modelData.name
                                ToolTip.visible: hovered
                                ToolTip.delay: 300

                                background: Rectangle {
                                    radius: width / 2
                                    color: swatch.modelData.color
                                    border.width: Theme.accentIndex === swatch.index ? 3 : 0
                                    border.color: Theme.window
                                    // Ring around the chosen one.
                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: -3
                                        radius: width / 2
                                        color: "transparent"
                                        border.width: Theme.accentIndex === swatch.index ? 2 : 0
                                        border.color: swatch.modelData.color
                                    }
                                }
                            }
                        }
                    }

                    Label { Layout.topMargin: 4; text: qsTr("Background"); opacity: 0.8 }

                    RowLayout {
                        spacing: 8

                        Repeater {
                            model: Theme.backgrounds

                            delegate: Button {
                                required property int index
                                required property var modelData

                                text: (checked ? "✓  " : "") + modelData.name
                                checkable: true
                                checked: Theme.backgroundIndex === index
                                onClicked: Theme.backgroundIndex = index
                                Material.background: modelData.card
                                Material.foreground: checked ? Theme.accent : Material.foreground
                            }
                        }
                    }

                    RowLayout {
                        Layout.topMargin: 4
                        spacing: 12

                        Label { text: qsTr("Corner roundness"); opacity: 0.8; Layout.preferredWidth: 140 }

                        Slider {
                            Layout.preferredWidth: 220
                            from: 0
                            to: 24
                            stepSize: 2
                            snapMode: Slider.SnapAlways
                            value: Theme.radius
                            onMoved: Theme.radius = value
                        }

                        Label { text: Theme.radius + " px"; opacity: 0.6 }
                    }

                    RowLayout {
                        spacing: 12

                        Label { text: qsTr("Interface size"); opacity: 0.8; Layout.preferredWidth: 140 }

                        Slider {
                            Layout.preferredWidth: 220
                            from: 0.6
                            to: 2.0
                            stepSize: 0.1
                            snapMode: Slider.SnapAlways
                            value: Theme.uiScale
                            onMoved: Theme.uiScale = Math.round(value * 100) / 100
                        }

                        Label { text: Math.round(Theme.uiScale * 100) + "%"; opacity: 0.6 }
                    }

                    Hint {
                        text: qsTr("Ctrl + mouse wheel also changes the interface size.")
                    }
                }

                SettingsPanel {
                    SectionTitle { text: qsTr("Language") }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        ComboBox {
                            id: languageBox

                            Layout.preferredWidth: 220
                            // "English" is the built-in texts; the rest are translation files.
                            model: [qsTr("English")].concat(SettingsController.languages)
                            currentIndex: SettingsController.language === ""
                                          ? 0 : SettingsController.languages.indexOf(SettingsController.language) + 1
                            onActivated: (index) => SettingsController.language = index === 0 ? "" : SettingsController.languages[index - 1]
                        }

                        Button {
                            text: qsTr("Reload")
                            flat: true
                            ToolTip.text: qsTr("Pick up translation files you added or edited")
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            onClicked: SettingsController.reloadLanguages()
                        }

                        Button {
                            text: qsTr("Open folder")
                            flat: true
                            onClicked: SettingsController.openTranslationsFolder()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: newLanguageField

                            Layout.preferredWidth: 220
                            placeholderText: qsTr("Language name, e.g. Bosanski")
                            onAccepted: createLanguageButton.clicked()
                        }

                        Button {
                            id: createLanguageButton

                            text: qsTr("Create translation file")
                            enabled: newLanguageField.text.trim() !== ""
                            onClicked: {
                                if (SettingsController.createTranslation(newLanguageField.text) !== "")
                                    newLanguageField.text = ""
                            }
                        }
                    }

                    Hint {
                        text: qsTr("Creates a text file with every text in the app, one per line: \"Settings\" = \"Settings\";  Replace the right side with your translation, save, press Reload and pick the language above. Using an existing name updates that file with new texts and keeps your translations.")
                    }
                }

                SettingsPanel {
                    SectionTitle { text: qsTr("Playback") }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Label { text: qsTr("Crossfade") }

                        Slider {
                            id: crossfadeSlider

                            Layout.fillWidth: true
                            Layout.maximumWidth: 320
                            from: 0
                            to: 12
                            // Tenths of a second; no snapping, which would draw 120 tick marks.
                            stepSize: 0.1
                            value: SettingsController.crossfadeSeconds
                            onMoved: SettingsController.crossfadeSeconds = Math.round(value * 10) / 10
                        }

                        Label {
                            Layout.preferredWidth: 44
                            text: crossfadeSlider.value < 0.05 ? qsTr("Off") : qsTr("%1 s").arg(crossfadeSlider.value.toFixed(1))
                            opacity: 0.7
                        }
                    }

                    Hint {
                        text: qsTr("The end of a song fades into the next one. Skipping a song by hand stays instant.")
                    }
                }

                SettingsPanel {
                    SectionTitle { text: qsTr("Effects") }

                    EffectsControls {
                        Layout.fillWidth: true
                    }

                    Switch {
                        Layout.leftMargin: -6 // align the switch track with the text above
                        text: qsTr("Show effects button")
                        checked: Effects.showInNowPlaying
                        onToggled: Effects.showInNowPlaying = checked
                    }

                    Hint {
                        text: qsTr("Slowed plays songs slower and lower, like a record at a slower speed. Pitch raises or lowers the key in semitones without changing the tempo. Effects apply to everything that plays. With \"Show effects button\" on, a button next to the volume opens them there too.")
                    }
                }

                SettingsPanel {
                    SectionTitle { text: qsTr("Lyrics") }

                    Switch {
                        Layout.leftMargin: -6 // align the switch track with the text above
                        text: qsTr("Look up lyrics online")
                        checked: SettingsController.onlineLyrics
                        onToggled: SettingsController.onlineLyrics = checked
                    }

                    Hint {
                        text: qsTr("For songs without lyrics in the file, the artist, title, album and length are sent to lrclib.net. Found lyrics are kept on this PC.")
                    }
                }

                SettingsPanel {
                    SectionTitle { text: qsTr("Library") }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            Layout.fillWidth: true
                            readOnly: true
                            text: SettingsController.libraryFolder.replace(/\//g, "\\") // Windows-style for display
                        }

                        Button {
                            text: qsTr("Change…")
                            onClicked: folderDialog.open()
                        }
                    }

                    Hint {
                        text: qsTr("Every music file in this folder and its subfolders shows up in the library. Finished Soulseek downloads are moved here, converted first to the format picked on the Soulseek page (M4A 320 kbps by default); a converted original goes to the Recycle Bin.")
                    }
                }

                SettingsPanel {
                    SectionTitle { text: qsTr("Cache") }

                    RowLayout {
                        spacing: 12

                        Label {
                            text: qsTr("Album covers, artist photos and lyrics: %1 MB")
                                  .arg((SettingsController.cacheBytes / (1024 * 1024)).toFixed(1))
                        }

                        Button {
                            text: qsTr("Clear cache")
                            enabled: SettingsController.cacheBytes > 0
                            onClicked: SettingsController.clearCache()
                        }
                    }

                    Hint {
                        text: qsTr("Small copies of album covers, downloaded artist photos and lyrics found online, kept so the app loads them fast. Clearing frees the space; they are made or downloaded again when needed.")
                    }
                }

                SettingsPanel {
                    SectionTitle { text: qsTr("Window") }

                    Switch {
                        Layout.leftMargin: -6 // align the switch track with the text above
                        text: qsTr("Minimize to system tray")
                        checked: SettingsController.minimizeToTray
                        onToggled: SettingsController.minimizeToTray = checked
                    }

                    Hint {
                        text: qsTr("Minimizing hides the window; click the tray icon to bring it back.")
                    }
                }

                SettingsPanel {
                    SectionTitle { text: qsTr("Last.fm") }

                    // The API key and secret live in lastfm.txt in the data folder (see LastFmController).
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: LastFmController.userName !== ""
                                  ? qsTr("Scrobbling as %1").arg(LastFmController.userName)
                                  : LastFmController.awaitingApproval
                                    ? qsTr("Allow access in the browser page that opened, then press Finish.")
                                    : qsTr("Not connected")
                            opacity: LastFmController.userName !== "" ? 1 : 0.7
                        }

                        Button {
                            visible: LastFmController.userName === "" && !LastFmController.awaitingApproval
                            text: qsTr("Connect account")
                            onClicked: LastFmController.connectAccount()
                        }

                        Button {
                            visible: LastFmController.awaitingApproval
                            text: qsTr("Finish")
                            onClicked: LastFmController.finishConnect()
                        }

                        Button {
                            visible: LastFmController.userName !== "" || LastFmController.awaitingApproval
                            flat: true
                            text: LastFmController.userName !== "" ? qsTr("Disconnect") : qsTr("Cancel")
                            onClicked: LastFmController.disconnectAccount()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: LastFmController.errorText !== ""
                        wrapMode: Text.Wrap
                        text: LastFmController.errorText
                        color: Material.color(Material.Red, Material.Shade300)
                        font.pixelSize: 12
                    }

                    Hint {
                        text: LastFmController.pendingCount > 0
                              ? qsTr("Songs are scrobbled after 15 seconds of listening. %1 waiting to be sent.").arg(LastFmController.pendingCount)
                              : qsTr("Songs are scrobbled after 15 seconds of listening.")
                    }
                }

                SettingsPanel {
                    SectionTitle { text: qsTr("Discord") }

                    Switch {
                        Layout.leftMargin: -6 // align the switch track with the text above
                        text: qsTr("Show what I'm listening to on Discord")
                        checked: DiscordController.enabled
                        onToggled: DiscordController.enabled = checked
                    }

                    // The IDs live in discord.txt (DiscordController.keysFile); a problem, like a missing ID, shows here.
                    Label {
                        Layout.fillWidth: true
                        visible: DiscordController.errorText !== ""
                        wrapMode: Text.Wrap
                        text: DiscordController.errorText
                        color: Material.color(Material.Red, Material.Shade300)
                        font.pixelSize: 12
                    }
                }

                SettingsPanel {
                    SectionTitle { text: qsTr("slskd") }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: urlField

                            Layout.preferredWidth: 240
                            placeholderText: "http://localhost:5030"
                            text: SlskdController.serverUrl
                        }

                        TextField {
                            id: keyField

                            Layout.fillWidth: true
                            placeholderText: qsTr("API key (optional)")
                            text: SlskdController.apiKey
                            echoMode: TextInput.Password
                        }

                        Button {
                            text: qsTr("Save")
                            enabled: urlField.text !== SlskdController.serverUrl || keyField.text !== SlskdController.apiKey
                            onClicked: SlskdController.saveSettings(urlField.text, keyField.text)
                        }
                    }

                    Hint {
                        text: qsTr("No API key is needed when slskd runs with --no-auth. Then also start it with --http-ip-address 127.0.0.1 so only this PC can reach it.")
                    }
                }

                SettingsPanel {
                    SectionTitle { text: qsTr("Experimental") }

                    Switch {
                        Layout.leftMargin: -6 // align the switch track with the text above
                        text: qsTr("Arrange panels")
                        checked: Theme.arrangePanels
                        onToggled: Theme.arrangePanels = checked
                    }

                    Hint {
                        text: qsTr("Shows a button in the sidebar that lets you move and resize the Library's panels. A layout you made stays when this is off; the button's reset brings back the default.")
                    }
                }

                // Signature: deliberately not qsTr(), so translation files never pick it up.
                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: -8 // right under the last card (the column spaces cards 12 px apart)
                    text: "grim. - GRIMSIXSTAR, šarić. - 2026. - 1.0"
                    opacity: 0.35
                    font.pixelSize: 11
                }
            }
        }

        // Fades at the top once scrolled and at the bottom, as in the library.
        EdgeFade {
            anchors.left: parent.left
            anchors.right: parent.right
            atTop: true
            height: 28
            baseColor: Theme.window // panels sit on the window background here
            flickable: settingsFlick
        }

        EdgeFade {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 100
            baseColor: Theme.window
            flickable: settingsFlick
        }
    }
}
