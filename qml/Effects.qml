pragma Singleton

import QtCore
import QtQuick
import MusicPlayer

// Effect settings (Settings → Effects, and the effects popup by the volume), remembered between
// runs and handed to the player as heard. One instance, so both places stay in step.
QtObject {
    id: effects

    property bool slowed: false
    property real slowedSpeed: 0.85
    property bool reverb: false
    property real reverbAmount: 0.4
    property bool pitch: false
    property real pitchSemitones: -2
    // Show the effects button next to the volume.
    property bool showInNowPlaying: false

    readonly property bool anyOn: slowed || reverb || pitch

    property Settings store: Settings {
        category: "effects"
        property alias slowed: effects.slowed
        property alias slowedSpeed: effects.slowedSpeed
        property alias reverb: effects.reverb
        property alias reverbAmount: effects.reverbAmount
        property alias pitch: effects.pitch
        property alias pitchSemitones: effects.pitchSemitones
        property alias showInNowPlaying: effects.showInNowPlaying
    }

    property Binding speedBinding: Binding {
        target: PlayerController
        property: "speed"
        value: effects.slowed ? effects.slowedSpeed : 1
    }
    property Binding reverbBinding: Binding {
        target: PlayerController
        property: "reverb"
        value: effects.reverb ? effects.reverbAmount : 0
    }
    property Binding pitchBinding: Binding {
        target: PlayerController
        property: "pitchSemitones"
        value: effects.pitch ? effects.pitchSemitones : 0
    }
}
