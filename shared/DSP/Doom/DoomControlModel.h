#pragma once

#include "DoomTypes.h"

namespace px3
{

// What the ENGINE consumes.
//
// Every field is a DSP quantity - a delay in seconds, a tap count, a harmony
// index, a clock ratio. None is a knob. The four macros mean different things
// in different modes, and this is where that translation happens, once, rather
// than inside six render functions each reaching for the same raw control.
struct DoomDerivedParameters
{
    // ---- the shared clock --------------------------------------------------
    //
    // The engine's rate as a fraction of the host's. Everything downstream is
    // measured in engine samples, so this one number is the loop's length AND
    // its pitch AND the wet channel's time AND its bandwidth.
    float clockRatio { 1.0f };

    // ---- micro-looper: BURST ----------------------------------------------
    float burstStepSeconds { 0.1f };   // pace of the sequence, and so slice size
    float burstSensitivity { 0.5f };   // how readily live playing scrambles it

    // ---- micro-looper: RADIO ----------------------------------------------
    //
    // A continuous scan across the stations, not five buttons: between two
    // centres both are audible and the static rises.
    float radioLength { 0.45f };
    int radioLowerStation { 0 };
    int radioUpperStation { 0 };
    float radioStationBlend { 0.0f };

    // ---- micro-looper: MASK ------------------------------------------------
    float maskCharacter { 0.45f };
    float maskThreshold { 0.5f };

    // ---- wet: SOUP ---------------------------------------------------------
    float soupT60Seconds { 1.0f };     // decay time the spectral magnitudes fall over
    float soupCharacter { 0.4f };      // how synthetic the resynthesis is

    // ---- wet: RELAY --------------------------------------------------------
    float relayDelaySeconds { 0.2f };
    int relayTaps { 4 };               // COUNTABLE repeats, 1..8
    bool relayInfinite { false };      // the top of the knob: pile up like a looper

    // ---- wet: FLIP ---------------------------------------------------------
    float flipLagSeconds { 0.2f };
    int flipHarmonyIndex { 0 };

    // ---- global ------------------------------------------------------------
    float crossDepth { 0.0f };
    DoomCrossSource crossSource { DoomCrossSource::input };
    float glueDrive { 0.0f };
    float eqTilt { 0.0f };
    float channelBalance { 0.5f };     // 0 looper, 1 wet
    float loopBlend { 0.0f };
    float loopFadeRetain { 1.0f };
    float stereoSpread { 0.5f };
    float mix { 0.35f };
};

namespace doom_control
{

// ---- CLOCK ----------------------------------------------------------------
//
// The harmonised ratio table, and the continuous sweep SMOOTH substitutes for
// it. Quantised is the default because landing on a musical ratio is the
// defining part of the control; SMOOTH is the secondary option.
int clockStepCount();
float mapClockToRatio(float clock, bool smooth);

// ---- the four macros, per mode --------------------------------------------
//
// Each of these answers "what does this knob mean right now". They are
// separate functions rather than one switch because their curves have nothing
// to do with each other and are tuned independently.

// BURST: LENGTH is the pace of the sequence, so a fast setting takes a short
// bite out of each slice. Inverted - more LENGTH is a shorter step.
float mapLengthToBurstStep(float length);
float mapLoopModifyToBurstSensitivity(float modify);

// RADIO: LOOP MODIFY is a scan across the station axis, which is why it is
// returned as a pair of stations and a blend rather than as an index.
void mapLoopModifyToStation(float modify, int stationCount,
                            int& lower, int& upper, float& blend);

// MASK: LENGTH is the disguise's character, LOOP MODIFY the threshold. Fully
// down is the untouched loop, which is a useful place to build one up.
float mapLengthToMaskCharacter(float length);
float mapLoopModifyToMaskThreshold(float modify);

// SOUP: TIME is a decay time. Squared, because reverberation time is
// perceived logarithmically and a linear knob spends its top half in lengths
// that are hard to tell apart.
float mapWetTimeToSoupT60(float time);
float mapWetModifyToSoupCharacter(float modify);

// RELAY: TIME is a delay, squared for the same reason. WET MODIFY chooses HOW
// MANY repeats there are rather than how loud they are - a count, so the knob
// has eight useful positions and one more that never stops.
float mapWetTimeToRelayDelay(float time);
int mapWetModifyToRelayTaps(float modify, int maxTaps);
bool mapWetModifyToRelayInfinite(float modify);

// FLIP: TIME is the lag between voices, WET MODIFY picks the harmony set.
float mapWetTimeToFlipLag(float time);
int mapWetModifyToFlipHarmony(float modify, int harmonyCount);

} // namespace doom_control

// The whole translation, once per block.
//
// Every mode's values are computed, not just the active one. They are a
// handful of multiplies between them, and computing all of them means the
// tests can assert what a mode WOULD do without switching to it.
DoomDerivedParameters deriveDoomParameters(const DoomUserParameters& user,
                                           int relayMaxTaps,
                                           int harmonyCount,
                                           int radioStationCount);

} // namespace px3
