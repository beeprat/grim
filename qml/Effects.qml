pragma Singleton

import QtCore
import QtQuick
import MusicPlayer

// Effect settings (the effects popup by the volume), remembered between runs and handed to the
// player as heard. They apply only while Settings → Effects → "Show effects button" is on, the only
// place they can be turned off.
QtObject {
    id: effects

    // Playback speed, slower or faster; the pitch follows, like a record.
    property bool speed: false
    property real speedAmount: 0.85
    property bool reverb: false
    property real reverbAmount: 0.4
    // Graphic equalizer, always on: 16 gains in dB (PlayerController.equalizer has the bands).
    property list<real> equalizerGains: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    // Saved equalizers (exported or imported ones), picked from the list above the bands: [{name, gains}].
    property var equalizerPresets: []
    // Adds a preset, replacing one of the same name.
    function savePreset(name, gains) {
        const presets = equalizerPresets.filter(p => p.name !== name)
        presets.push({name: name, gains: Array.from(gains)})
        presets.sort((a, b) => a.name.localeCompare(b.name))
        equalizerPresets = presets
        currentPreset = name
    }
    // The preset last picked or saved (by name): the list shows it as edited once the bands change, and
    // its changes can be saved over it. Empty: none.
    property string currentPreset: ""
    function deletePreset(name) {
        equalizerPresets = equalizerPresets.filter(p => p.name !== name)
        if (currentPreset === name)
            currentPreset = ""
    }
    // False if the new name is empty or taken.
    function renamePreset(name, newName) {
        newName = newName.trim()
        if (newName === "" || (newName !== name && equalizerPresets.some(p => p.name === newName)))
            return false
        const preset = equalizerPresets.find(p => p.name === name)
        if (!preset)
            return false
        const presets = equalizerPresets.filter(p => p.name !== name)
        presets.push({name: newName, gains: preset.gains})
        presets.sort((a, b) => a.name.localeCompare(b.name))
        equalizerPresets = presets
        if (currentPreset === name)
            currentPreset = newName
        return true
    }
    // Show the effects button next to the volume.
    property bool showInNowPlaying: false

    readonly property bool anyOn: speed || reverb || equalizerGains.some(g => g !== 0)

    property Settings store: Settings {
        category: "effects"
        // Stored under the old "Slowed" names, so the saved values carry over.
        property alias slowed: effects.speed
        property alias slowedSpeed: effects.speedAmount
        property alias reverb: effects.reverb
        property alias reverbAmount: effects.reverbAmount
        property alias showInNowPlaying: effects.showInNowPlaying
        // As text: Settings stores a list of numbers differently per platform.
        property string equalizerGains: JSON.stringify(effects.equalizerGains)
        property string equalizerPresets: "[]"
        Component.onCompleted: {
            try {
                const presets = JSON.parse(equalizerPresets)
                if (Array.isArray(presets))
                    effects.equalizerPresets = presets.filter(p => p && typeof p.name === "string"
                                                                   && Array.isArray(p.gains) && p.gains.length === 16)
            } catch (e) {}
            try {
                const gains = JSON.parse(equalizerGains)
                if (Array.isArray(gains) && gains.length === 16)
                    effects.equalizerGains = gains
            } catch (e) {}
        }
    }
    onEqualizerGainsChanged: store.equalizerGains = JSON.stringify(equalizerGains)
    onEqualizerPresetsChanged: store.equalizerPresets = JSON.stringify(equalizerPresets)

    property Binding speedBinding: Binding {
        target: PlayerController
        property: "speed"
        value: effects.showInNowPlaying && effects.speed ? effects.speedAmount : 1
    }
    property Binding equalizerBinding: Binding {
        target: PlayerController
        property: "equalizer"
        value: effects.showInNowPlaying ? effects.equalizerGains
                                                              : [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    }
    property Binding reverbBinding: Binding {
        target: PlayerController
        property: "reverb"
        value: effects.showInNowPlaying && effects.reverb ? effects.reverbAmount : 0
    }
}
