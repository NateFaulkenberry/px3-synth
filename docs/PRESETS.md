# P(X3) Preset System

## Overview

P(X3) presets are built on top of the existing plugin state representation (`PX3_STATE` ValueTree), so UI, DSP, automation, project state, and presets all use the same parameter/state source of truth.

State flow:

1. Current plugin state is captured from processor parameters and state metadata.
2. Preset files serialize that state plus preset metadata.
3. Loading a preset restores the same state path used by project/session restore.

## File Format

Extension:

- `.px3preset`

Serialization:

- JUCE XML ValueTree

Root node:

- `PX3_PRESET`

Top-level properties:

- `presetVersion` (currently `1`)
- `pluginVersion`
- `name`
- `category`
- `author`
- `description`
- `isFactory`

Children:

1. `PX3_STATE`
   - Parameter and engine state payload used by processor restore.
2. `ASSETS`
   - Zero or more `ASSET` entries.

Asset entry fields:

- `type` (`image` or `audio`)
- `parameterKey` (`imagePath` or `audioPath`)
- `originalPath`
- `fileName`
- `hash`
- `embedded` (bool)
- `data` (base64 payload when embedded)

## Versioning And Migration

- Every preset stores `presetVersion`.
- Loader validates version and routes through migration handling (`migratePresetTreeIfNeeded`).
- Newer unsupported versions are rejected safely.
- Missing optional fields are backfilled with defaults.

## Storage Layout (macOS)

Root:

- `~/Library/Application Support/P(X3)/`

Structure:

- `Presets/Factory/`
- `Presets/User/`
- `Assets/Images/`
- `Assets/Audio/`
- `Settings/`

Favorites metadata:

- `Settings/favorites.xml`

Factory and user presets are separated at the data-model level.

## Factory Presets

On first initialization, P(X3) ensures an initial factory set exists, including:

- `INIT`
- BASS / LEADS / PADS / PLUCKS / EXPERIMENTAL / IMAGE ENGINE / AUDIO ENGINE examples

Factory presets are read-only from the UI perspective.

## User Presets

User presets support:

- save
- save as
- overwrite confirmation
- delete (non-factory)
- favorite toggle
- import
- export

## Import / Export

- Export copies `.px3preset` to user-selected destination.
- Import validates and copies into user preset library (category-based).
- Name collisions on import are resolved by suffixing (`Name 2`, `Name 3`, ...).

## Asset Handling

Current implementation supports embedded asset data in `.px3preset` for image/audio path-backed sources:

1. On save:
   - If `imagePath` or `audioPath` points to an existing file, file data is embedded in preset.
   - Asset is also cached under Application Support assets folder.
2. On load:
   - Embedded data is materialized into the local assets cache.
   - `PX3_STATE` asset paths are rewritten to the materialized local file before state apply.

If a preset has no available asset payload/path, state loads safely and engine behavior depends on available sources.

## Safety And Compatibility

- Corrupt/invalid preset files are rejected with user-visible errors.
- Missing parameters are tolerated (current/default values remain).
- Unknown parameters are ignored safely.
- Loading does not run in the audio callback and uses existing async image/audio load mechanisms for engine resources.

## DAW Project State

The preset system does not replace DAW project/session state.

- DAW save/load still uses plugin `getStateInformation`/`setStateInformation`.
- Presets are an additional user-managed state exchange layer.
