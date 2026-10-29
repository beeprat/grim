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
        Component.onCompleted: {
            try {
                const gains = JSON.parse(equalizerGains)
                if (Array.isArray(gains) && gains.length === 16)
                    effects.equalizerGains = gains
            } catch (e) {}
        }
    }
    onEqualizerGainsChanged: store.equalizerGains = JSON.stringify(equalizerGains)

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
