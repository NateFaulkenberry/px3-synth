# FX sweep

Every control of every effect is swept from minimum to maximum, and through every choice. The effect is on and everything else is off. Each setting is measured at the instrument's output. The same sweep runs through two hosts:

- **Standalone path:** `PX3Tests fxsweep` renders the processor directly. Its checks fail on:
  - NaN or Inf;
  - a sample past full scale;
  - a control that changes nothing (except the known-inert list in `FxSweep.h`);
  - a control that silences the synth;
  - a sweep that clicks.

  `PX3Tests sweepfindings`, part of the default run, pins the specific bugs the sweep found.
- **Plug-in path:** `PX3FxSweepHost`, in this directory, loads a built AU or VST3 through JUCE's hosting. It applies the same settings by parameter name and writes the same table.

`FxSweep.h` is the single definition of the sweep, shared by both hosts. It holds:

- the sections and controls;
- the material: a held chord, then plucks, at a 120 BPM playhead;
- the companions and contexts a control needs before it can act (a gate threshold needs the gate on, for example);
- the known-inert controls, and the controls allowed to silence or step;
- the measurements.

## Running it

Standalone path (about 3.5 minutes):

```sh
PX3_FXSWEEP_TSV=standalone.tsv PX3_FXSWEEP_VERBOSE=1 build/diag/PX3Tests_artefacts/RelWithDebInfo/PX3Tests fxsweep
```

Plug-in path. Build the host once, outside `build/`:

```sh
cmake -S tools/FxSweep -B /tmp/fxsweephost -DCMAKE_BUILD_TYPE=Release -DPX3_JUCE_DIR=$PWD/build/diag/_deps/juce-src
cmake --build /tmp/fxsweephost -j8
H=/tmp/fxsweephost/PX3FxSweepHost_artefacts/Release/PX3FxSweepHost
```

Then run it:

```sh
# <plugin> AU|VST3 <rate> <block|var> <all|main> <out.tsv> [SECTION ...]
$H "build/release/PX3Synth_artefacts/Release/VST3/PX3 Synth.vst3" VST3 48000 512 all vst3.tsv
$H "build/release/PX3Synth_artefacts/Release/VST3/PX3 Synth.vst3" VST3 48000 var all vst3-var.tsv
```

The `all` and `main` options choose the buses:

- `all` enables every bus, as JUCE's AU wrapper does in Logic;
- `main` keeps the main pair only.

The `var` option cycles the block size through 1, 37, 512, 1024 and 256 samples.

macOS loads an AU by its registered component, which is the installed one. A freshly built AU therefore can't be tested from its build folder until it is installed. Test the build through the VST3, and the installed release through the AU.

## Comparing the two paths

Each table has one row per section, control and value. It records:

- the difference from the effect switched off;
- the difference from the control at its default;
- the level relative to the effect off;
- the peak and whether the output was finite;
- the sweep's step crest against the still settings';
- the flags.

At 48 kHz / 512 the two paths should agree row for row. Effects that use the shared system random (MOOD, the Granular DELAY, LUCY's packets) differ run to run by a few tenths of a dB.

`compare.py` diffs two tables:

```sh
python3 tools/FxSweep/compare.py standalone.tsv vst3.tsv
```

It lists:

- rows that differ by more than 0.5 dB;
- flags present in only one table;
- controls that are dead in one table and live in the other.
