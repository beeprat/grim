import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import MusicPlayer

// Slowed, reverb and pitch: a switch, an amount slider and the value for each. Off keeps the
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
            onMoved: effect.moved(value)
        }

        Label {
            Layout.preferredWidth: 52
            text: effect.valueText
            opacity: effectSwitch.checked ? 0.7 : 0.35
        }
    }

    EffectRow {
        label: qsTr("Slowed")
        checked: Effects.slowed
        from: 0.7
        to: 0.95
        stepSize: 0.01
        value: Effects.slowedSpeed
        valueText: Math.round(value * 100) + " %"
        onToggled: (on) => Effects.slowed = on
        onMoved: (v) => Effects.slowedSpeed = Math.round(v * 100) / 100
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

    EffectRow {
        label: qsTr("Pitch")
        checked: Effects.pitch
        from: -12
        to: 12
        stepSize: 0.5
        value: Effects.pitchSemitones
        valueText: (value > 0 ? "+" : "") + value.toFixed(1)
        onToggled: (on) => Effects.pitch = on
        onMoved: (v) => Effects.pitchSemitones = Math.round(v * 2) / 2
    }
}
