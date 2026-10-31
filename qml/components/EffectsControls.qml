import QtCore
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

// Equalizer (16 band sliders, always on), then speed and reverb: a switch, an amount slider and
// its value each. Off keeps the
// amount for next time. Used on the Settings page and in the now-playing effects popup.
ColumnLayout {
    id: controls

    // Width of the switch + name column; narrower in the popup.
    property real switchWidth: 150

    spacing: 0

    component EffectRow: RowLayout {
        id: effect

        property alias label: effectSwitch.text
        property alias checked: effectSwitch.checked
        property alias from: effectSlider.from
        property alias to: effectSlider.to
        property alias stepSize: effectSlider.stepSize
        property alias value: effectSlider.value
        property string valueText
        // Values marked on the track; a drag within `snapRange` of one lands on it.
        property list<real> marks
        property real snapRange: 0
        signal toggled(bool checked)
        signal moved(real value)

        Layout.fillWidth: true
        spacing: 12

        Switch {
            id: effectSwitch

            Layout.leftMargin: -6 // align the switch track with the text above
            Layout.preferredWidth: controls.switchWidth
            onToggled: effect.toggled(checked)
        }

        Slider {
            id: effectSlider

            Layout.fillWidth: true
            Layout.maximumWidth: 260
            enabled: effectSwitch.checked
            // One wheel notch = one step (1 % speed, 5 % reverb).
            wheelEnabled: true
            onMoved: {
                let v = value
                // Snap only while dragging: the wheel must be able to step off a mark.
                for (const mark of effect.marks) {
                    if (pressed && Math.abs(v - mark) <= effect.snapRange)
                        v = mark
                }
                effect.moved(v)
            }

            Repeater {
                model: effect.marks

                Rectangle {
                    required property real modelData

                    z: -0.5 // over the track, under the handle
                    x: effectSlider.leftPadding + (modelData - effectSlider.from) / (effectSlider.to - effectSlider.from)
                       * effectSlider.availableWidth - width / 2
                    y: effectSlider.topPadding + effectSlider.availableHeight / 2 - height / 2
                    width: 2
                    height: 12
                    radius: 1
                    color: Material.foreground
                    opacity: effectSwitch.checked ? 0.5 : 0.2
                }
            }
        }

        Label {
            Layout.preferredWidth: 52
            text: effect.valueText
            opacity: effectSwitch.checked ? 0.7 : 0.35
        }
    }

    // Bare icon button for the equalizer: no hover highlight or ripple.
    component EqButton: IconButton {
        Layout.preferredWidth: 22
        Layout.preferredHeight: 22
        padding: 0
        topInset: 0
        bottomInset: 0
        leftInset: 0
        rightInset: 0
        background: Item {} // just the icon: no hover highlight or ripple
        iconSize: 16
    }

    // A preset's name: its file name without the folder and .json.
    function presetName(fileUrl) {
        const name = decodeURIComponent(fileUrl.toString().split("/").pop())
        return name.replace(/\.json$/i, "")
    }

    // Saved equalizers: pick one to apply it. Shows "Custom" when the bands match none, or the preset
    // last picked marked as edited; save, rename and delete act on that preset.
    RowLayout {
        id: presets

        readonly property int matching: Effects.equalizerPresets.findIndex(
                                             p => p.gains.every((g, i) => g === Effects.equalizerGains[i]))
        readonly property int picked: Effects.equalizerPresets.findIndex(p => p.name === Effects.currentPreset)
        readonly property bool edited: picked >= 0 && matching !== picked

        Layout.bottomMargin: 10
        visible: Effects.equalizerPresets.length > 0
        spacing: 6

        // Renaming: the name is edited in place of the list. Enter saves, Escape or clicking away cancels.
        TextField {
            id: renameField

            property string name

            Layout.preferredWidth: 200
            Layout.preferredHeight: 32
            visible: false
            topPadding: 0
            bottomPadding: 0
            leftPadding: 10
            topInset: 0
            bottomInset: 0
            font.pixelSize: 13
            selectByMouse: true
            background: Rectangle {
                radius: 6
                color: Theme.subtle
                border.width: 1
                border.color: Theme.accent
            }
            onAccepted: if (Effects.renamePreset(name, text)) visible = false
            onActiveFocusChanged: if (!activeFocus) visible = false
            Keys.onEscapePressed: visible = false
        }

        CompactComboBox {
            id: presetBox

            Layout.preferredWidth: 200
            Layout.preferredHeight: 32
            visible: !renameField.visible
            // The box fills its height and the list opens flush under it (Material leaves room above/below).
            topInset: 0
            bottomInset: 0
            popup.y: height - 4 // PopupBackground draws its edge inside: flush with the box
            textRole: "name"
            model: Effects.equalizerPresets
            currentIndex: presets.edited ? presets.picked : presets.matching
            displayText: presets.edited ? qsTr("%1 (edited)").arg(currentText)
                       : currentIndex >= 0 ? currentText : qsTr("Custom")
            onActivated: (index) => {
                Effects.equalizerGains = Effects.equalizerPresets[index].gains
                Effects.currentPreset = Effects.equalizerPresets[index].name
            }
            // Plain rounded box: no outline (the Material one turns accent-coloured on focus).
            background: Rectangle {
                radius: 6
                color: presetBox.hovered ? Theme.hover : Theme.subtle
            }
        }

        EqButton {
            visible: presets.edited
            iconName: "save"
            tip: qsTr("Save changes to this preset")
            onClicked: Effects.savePreset(Effects.currentPreset, Effects.equalizerGains)
        }
        EqButton {
            visible: presetBox.currentIndex >= 0
            iconName: "edit"
            tip: qsTr("Rename preset")
            onClicked: {
                renameField.name = Effects.equalizerPresets[presetBox.currentIndex].name
                renameField.text = renameField.name
                renameField.visible = true
                renameField.forceActiveFocus()
                renameField.selectAll()
            }
        }
        EqButton {
            visible: presetBox.currentIndex >= 0
            iconName: "delete"
            tip: qsTr("Delete preset")
            onClicked: Effects.deletePreset(Effects.equalizerPresets[presetBox.currentIndex].name)
        }
    }

    // Equalizer, always on: 16 bands, 31 Hz … 16 kHz (0.6 octave apart), ±12 dB. Double-click a
    // band to zero it; the button at the end zeroes them all.
    RowLayout {
        id: bands

        readonly property var labels: ["31", "47", "72", "109", "165", "250", "380", "576",
                                       "873", "1.3k", "2k", "3k", "4.6k", "7k", "10.6k", "16k"]

        Layout.fillWidth: true
        Layout.bottomMargin: 10
        spacing: 0

        Repeater {
            model: 16

            delegate: ColumnLayout {
                id: band

                required property int index

                Layout.fillWidth: true
                spacing: 0

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: {
                        const g = Effects.equalizerGains[band.index]
                        return g === 0 ? "0" : (g > 0 ? "+" : "") + g
                    }
                    opacity: 0.6
                    font.pixelSize: 9
                }

                Slider {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredHeight: 110
                    // At least narrow enough that 16 bands and the reset button fit the popup
                    // (wider pushed every row past its edge); grows to share the spare width.
                    Layout.preferredWidth: 20
                    Layout.fillWidth: true
                    orientation: Qt.Vertical
                    from: -12
                    to: 12
                    stepSize: 1
                    wheelEnabled: true
                    value: Effects.equalizerGains[band.index]
                    onMoved: {
                        const gains = Effects.equalizerGains.slice()
                        gains[band.index] = Math.round(value)
                        Effects.equalizerGains = gains
                    }

                    TapHandler {
                        onDoubleTapped: {
                            const gains = Effects.equalizerGains.slice()
                            gains[band.index] = 0
                            Effects.equalizerGains = gains
                        }
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: bands.labels[band.index]
                    opacity: 0.6
                    font.pixelSize: 9
                }
            }
        }

        // Reset, then import and export a preset: bare icons, so the bands get the width.
        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: 8


            EqButton {
                iconName: "restart"
                tip: qsTr("Reset equalizer")
                enabled: Effects.equalizerGains.some(g => g !== 0)
                onClicked: Effects.equalizerGains = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
            }
            EqButton {
                iconName: "upload"
                tip: qsTr("Import equalizer")
                onClicked: eqImportDialog.open()
            }
            EqButton {
                iconName: "download"
                tip: qsTr("Export equalizer")
                onClicked: eqExportDialog.open()
            }
        }
    }

    FileDialog {
        id: eqExportDialog

        title: qsTr("Export equalizer")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("grim. equalizer (*.json)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            if (PlayerController.exportEqualizer(selectedFile, Effects.equalizerGains))
                Effects.savePreset(controls.presetName(selectedFile), Effects.equalizerGains)
        }
    }

    FileDialog {
        id: eqImportDialog

        title: qsTr("Import equalizer")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("grim. equalizer (*.json)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            const gains = PlayerController.importEqualizer(selectedFile)
            if (gains.length === 16) {
                Effects.equalizerGains = gains
                Effects.savePreset(controls.presetName(selectedFile), gains)
            }
        }
    }

    // Divider between the equalizer and the speed / reverb sliders.
    Rectangle {
        Layout.fillWidth: true
        Layout.bottomMargin: 4
        implicitHeight: 1
        color: Theme.subtle
    }

    EffectRow {
        label: qsTr("Speed")
        checked: Effects.speed
        from: 0.5
        to: 1.5
        stepSize: 0.01
        value: Effects.speedAmount
        // The usual slowed / sped-up amounts.
        marks: [0.8, 1.2]
        snapRange: 0.015
        valueText: Math.round(value * 100) + " %"
        onToggled: (on) => Effects.speed = on
        onMoved: (v) => Effects.speedAmount = Math.round(v * 100) / 100
    }

    EffectRow {
        label: qsTr("Reverb")
        checked: Effects.reverb
        from: 0.05
        to: 1
        stepSize: 0.05
        value: Effects.reverbAmount
        valueText: Math.round(value * 100) + " %"
        onToggled: (on) => Effects.reverb = on
        onMoved: (v) => Effects.reverbAmount = Math.round(v * 20) / 20
    }
}
