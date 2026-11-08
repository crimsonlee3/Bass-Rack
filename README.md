# Bass Rack

A macOS Audio Unit effect for Logic Pro: up to five bands, stock distortion,
per-band AU insert chains and stereo controls, plus a master finishing rack.
Designed first for 808s and bass; usable on other stereo sources.

This is an original implementation inspired by the general multiband workflow
of iZotope Trash. No iZotope code, artwork, presets or binary internals are used.

## Status

Early development / 0.2.0. Not a production release. See the pull request and
Actions checks for the actual build result. A successful compile does not verify
Logic compatibility or every third-party AU. No listening test in Logic has
been performed in this environment.

## Interface / 0.2.0

The interface uses a larger frequency display, colored response curves and
draggable level nodes, with a live input spectrum and input/output peak readouts.
A selected-band inspector combines a transfer-curve preview, stock distortion
controls and three AU slots. The master inspector provides EQ and compression.
This is an original visual design; no FabFilter or iZotope assets are used.

The spectrum reads an SPSC sample FIFO; audio processing never waits for the
display. The plot's colored curves show crossover magnitude plus band level,
before nonlinear distortion and third-party inserts; they are not an output EQ
measurement. Native previews use a deterministic 55/110/220 Hz bass test tone.

## Workflow

1. Insert **Audio Units → Crimson Audio → Bass Rack** on a stereo channel.
   Stereo and mono-to-stereo input configurations are supported, so a mono 808
   can use the per-band stereo rack without an extra routing bus.
2. Select **808 - clean sub** for a starting point. It keeps the lowest band
   clean and mono while adding parallel harmonics above 90 Hz.
3. Drag crossover handles. Double-click the map to add a crossover, up to five
   bands. The + button enables the next band; - removes the highest band.
4. Select a band: Clean, Soft, Tube, Clip, Fold, or Sine. Adjust drive,
   distortion mix, band level, width (0 = mono, 1 = original, 2 = wider), and pan.
5. **Scan AUs** opens the Audio Unit scanner. Use its Options menu to scan.
   In an insert slot, choose **Load / replace AU effect**. Click a populated
   slot to open its editor, replace it, or remove it.
6. Select **MASTER** for bass/treble shelving EQ, optional compressor, output
   trim and three additional AU insert slots.
7. **Save preset** saves controls, crossovers, plugin identities and plugin
   states to a .bassrack file. Logic project state saves the same rack.

Factory settings change stock controls only and keep your third-party inserts.
Loading a saved full preset replaces the insert graph. Plugins must be installed
and licensed on each machine where a saved rack is opened.

## Signal path

Input → phase-compensated LR4 crossover bank → each band:
2× oversampled stock distortion with parallel mix → up to three serial AU
effects → latency alignment → stereo width/pan → level/solo/mute → sum →
master bass and treble EQ → optional compressor → three master AU effects →
output trim.

Crossover slopes are 24 dB/octave. All-pass compensation on earlier bands keeps
the neutral summed magnitude flat. The frequency display combines phase-compensated band response curves with a
live input FFT spectrum. Drag numbered nodes vertically to adjust band levels;
drag crossover boundaries to change the frequency ranges. The plugin reports oversampling and insert latency
to Logic. Parallel band chains are aligned to the longest active chain.

## macOS build

Install Xcode command line tools and CMake. First configure downloads JUCE 8.0.6:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
    cmake --build build --config Release --parallel 3
    ctest --test-dir build -C Release --output-on-failure

Outputs:

- build/BassRack_artefacts/Release/AU/Bass Rack.component
- build/BassRack_artefacts/Release/Standalone/Bass Rack.app

GitHub Actions uploads **BassRack-macOS-Installers**, containing a .pkg installer
and a .dmg with the same installer and short installation notes. Both contain
the universal Apple Silicon + Intel AU. The pipeline installs the actual .pkg
on the disposable macOS runner, verifies the installed bundle/signature and
architectures, and runs Apple's auval on that installed component.

### Install

1. Download **BassRack-macOS-Installers** from the successful Actions run and
   extract the GitHub artifact ZIP.
2. Quit Logic. If you installed an earlier copy manually under
   ~/Library/Audio/Plug-Ins/Components/, remove that older Bass Rack.component
   to avoid duplicate installations.
3. Open the .pkg directly, or open the .dmg and double-click the .pkg inside it.
4. Follow macOS Installer. It installs into
   /Library/Audio/Plug-Ins/Components/ for all users and requires administrator
   authorization.
5. Reopen Logic. Find **Audio Units → Crimson Audio → Bass Rack**.

The AU is ad-hoc signed for development. The installer is unsigned and neither
is notarized; macOS may require explicit approval in **System Settings →
Privacy & Security**. A Developer ID signed/notarized distribution needs the
owner's Apple Developer certificates and notarization credentials.

To package a local build:

    bash scripts/package-macos.sh

The script produces .pkg, .dmg and SHA256SUMS.txt in build/installers/.
It uses Apple's pkgbuild and hdiutil. The package has no pre/post-install scripts
and disables bundle relocation so it always installs at the documented location.

To uninstall, quit Logic and remove only
/Library/Audio/Plug-Ins/Components/Bass Rack.component.
Saved presets and third-party plugins remain in place.

For a command-line AU validation:

    auval -v aufx BsRk Crsn

## Portable DSP tests

No JUCE or audio device required:

    cmake -S . -B build-dsp -DBASSRACK_DSP_ONLY=ON
    cmake --build build-dsp
    ctest --test-dir build-dsp --output-on-failure

Tests check neutral crossover magnitude for one through five bands at three
sample rates, distortion bounds, stereo identity/mono, and latency delay impulses.

## Current limits

- Three insert slots per band and three on the master. AU effects only; no VST,
  instruments, MIDI, external sidechain input or multichannel effects.
- Hosted plugins run in-process. Scanning or loading a faulty AU can crash the
  host. Scan with your Logic project saved.
- AU editor automation is saved as plugin state; hosted parameters are not yet
  exposed as Logic automation lanes.
- Editing crossover frequencies or band topology can click. Drive, mix, width,
  pan, band level and output trim are smoothed; crossover/topology crossfades,
  solo/mute fades and anti-click insert swaps remain future work.
- Inserts are before per-band stereo imaging; arbitrary module ordering is not
  implemented. Replacing/removing an insert closes all floating plugin/scanner
  windows; band bypass skips the AU chain and inserts an equivalent delay.
- A band-chain latency must stay below 262144 samples. Excessive latency mutes
  output with a status message rather than silently misaligning bands.
- Adding a crossover sorts frequency boundaries; chains remain assigned by band
  number, so check which frequencies each existing chain receives afterward.
- User plugins with different CPU architectures must support the architecture
  Logic is running. The plugin does not bridge Intel-only AUs into Apple Silicon.
- No automatic gain matching, brickwall limiter, convolution module, undo stack,
  preset browser, or production signing yet. Output starts at -6 dB to give room
  for distortion; it is still possible to clip, so use the output trim.
- JUCE is an external dependency under its own license. Choose an appropriate
  JUCE license before distributing a proprietary product. This repository does
  not assign a license to the original project source.

## Before calling it release-ready

Run auval; open/close UI repeatedly in Logic; validate Apple Silicon and Intel;
test third-party AU loading and missing-plugin recall; compare bypass/neutral
gain; verify long-latency inserts with impulses; test offline bounce, sample-rate
and buffer-size changes, project reopen and preset round-trips. Listen to actual
808s at matched levels. Add anti-click topology changes and real-time profiling.
