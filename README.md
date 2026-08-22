# P(X3) Synth

First milestone JUCE synthesizer bootstrap for macOS + VS Code + CMake.

This project provides:

- Polyphonic subtractive synthesizer using JUCE Synthesiser voices
- Amp ADSR per voice, oscillator mix, low-pass filter, and master gain
- Custom 88-key piano UI (A0 to C8)
- Visual key illumination for active MIDI notes
- Top-left logo branding with note-driven motion
- Standalone, VST3, and AU targets

## Requirements

- macOS (Apple Silicon supported)
- Xcode Command Line Tools
- CMake 3.22+
- Ninja
- VS Code
- LLDB (from Xcode tools)

## Setup

Clone and build from terminal:

```bash
git clone <your-repo-url>
cd synth-plug
cmake -B build -G Ninja
cmake --build build
```

JUCE is fetched automatically through CMake FetchContent.

## Running The Standalone Synth

From terminal:

```bash
./scripts/run-standalone.sh
```

If you see `_LSOpenURLsWithCompletionHandler() failed with error -600`, an older app instance is still running. Stop it and relaunch:

```bash
./scripts/run-standalone.sh
```

Expected behavior:

- Window opens with title and 88-key keyboard
- External MIDI note-on produces sine wave
- Corresponding key lights up
- Polyphony works (multiple notes at once)

## VS Code Workflow

1. Open this folder in VS Code.
2. Run task: CMake: Configure (Ninja).
3. Run task: CMake: Build.
4. Press F5 and select Debug P(X3) Synth Standalone.
5. Place breakpoints in Source/SynthVoice.cpp to debug oscillator/envelope code.

Provided configuration:

- .vscode/tasks.json
  - CMake: Configure (Ninja)
  - CMake: Build
  - Run Standalone
- .vscode/launch.json
  - LLDB launch for standalone executable inside the app bundle

## MIDI On macOS

- Connect your MIDI controller before launching, or while the app is running.
- In standalone builds, JUCE uses the platform MIDI subsystem (CoreMIDI).
- If notes do not appear, verify device visibility in Audio MIDI Setup.

For this milestone, no custom MIDI device selector is exposed in the UI; rely on JUCE standalone defaults.

## Plugin Targets And Locations

After build, JUCE generates artifacts under:

- Standalone app: build/SynthProject_artefacts/Standalone/PX3 Synth.app
- VST3 bundle: build/SynthProject_artefacts/VST3/PX3 Synth.vst3
- AU component: build/SynthProject_artefacts/AU/PX3 Synth.component

Install locations may vary by host and local JUCE copy/install settings.

## Architecture Summary

- Source/PluginProcessor.*
  - Owns juce::Synthesiser and audio callback
  - Handles incoming MIDI and lock-free note-state tracking for UI
- Source/SynthVoice.*
  - One voice, MIDI-note frequency mapping, subtractive oscillator mix, low-pass filter, ADSR
- Source/SynthSound.*
  - Accepts all notes/channels
- Source/PluginEditor.*
  - Main editor and timer-driven UI refresh
- Source/PianoKeyboard.*
  - Custom 88-key renderer with active-key highlighting

## Next Milestones

1. Expose ADSR controls in UI and parameter state.
2. Add optional computer-keyboard note input.
3. Add pitch bend/mod wheel handling.
4. Add voice-stealing policy visualization/debug info.
5. Add unit/integration tests for MIDI note-state mapping and key geometry.
