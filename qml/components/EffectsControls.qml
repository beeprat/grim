import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

// Speed and reverb: a switch, an amount slider and the value for each. Off keeps the
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
