#pragma once

namespace px3
{

// How an effect's MIX / AMOUNT combines what it makes with its input.
//
//   crossfade - in * dry(amount) + wet * wet(amount). A pedal's law, and every
//               standalone PX3 effect's: at full amount you hear the effect
//               alone.
//   additive  - in + wet * wet(amount). For an engine on an aux send, whose
//               return is (output - send): there a crossfade subtracts part of
//               the send from the mix while the dry bus carries on, so turning
//               an effect UP made the patch quieter (DELAY at 0.5: -3.4 dB) and
//               left the effect under a dry it could not touch. Additive
//               returns exactly wet * wet(amount) on top of an intact dry.
//
// The wet gain curve is each engine's own and is the same under both laws;
// additive only stops the dry being taken down. The Synth sets additive on the
// engines in its send chain; everything else keeps the default.
enum class FxMixLaw
{
    crossfade,
    additive
};

} // namespace px3
