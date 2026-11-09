# Bass Rack 0.4.1

A macOS AU effect for Logic Pro with up to five independent frequency regions,
stock distortion, per-region AU effects, stereo controls, master EQ/compression,
Auto Gain and selectable oversampling. Development build.

## Install

Download **BassRack-macOS-Installers** from a successful Actions run. Extract the
GitHub artifact ZIP and run the .pkg, or open the .dmg and run its .pkg.
Quit Logic first. Installer puts the AU into
/Library/Audio/Plug-Ins/Components/ for all users and requires administrator
authorization. Reopen Logic: **Audio Units > Crimson Audio > Bass Rack**.

The development PKG is unsigned/not notarized; the AU is ad-hoc signed. macOS
may require approval in System Settings > Privacy & Security. If a prior manual
copy is in ~/Library/Audio/Plug-Ins/Components/, remove that duplicate.

## Independent regions

Double-click the graph to create a region at that frequency. Up to five regions
can be active, with gaps or overlaps. There is no linked crossover bank.

- Drag a numbered node horizontally for frequency and vertically for level.
- Scroll over a node to change Q. Drag the selected region's two edges to
  change its bandwidth while keeping its centre fixed.
- Use FREQUENCY and Q for numeric edits. Choose 12, 24, 48 or 96 dB/oct slope.
- DELETE REGION removes the selected region and its three AU inserts.
- Muting attenuates that selected frequency region. Solo auditions its processed
  region and temporarily pauses Auto Gain. Bypass contributes no region change.
- Use the + slots/buttons to create regions; MASTER selects the finishing rack.

Each region has Clean / Soft / Tube / Clip / Fold / Sine, drive, distortion
mix, level, stereo width (0% mono / 100% original / 200% wider) and pan.

The graph shows requested region selection/level responses, not the measured
output of nonlinear processing or third-party effects. Regions are finite
filters: extremely narrow low-frequency selections are limited by the 8193-tap
FIR resolution, and overlaps combine the changes from each region.

## Scanner hotfix / 0.4.1

The default scanner now reads macOS AU registration metadata only and reuses
cached entries. It never instantiates effect DSP during discovery, removing the
in-process validation path that could take Logic down when a third-party effect
crashed. Stereo compatibility is checked when an insert is actually loaded.
Actual insert hosting is still in-process; this hotfix is not full crash isolation
for faults during playback/loading. No in-process "validate all" option is offered.

## Effects-only AU discovery

SCAN AUs opens a foreground metadata-only discovery window with progress,
elapsed time, cache reuse and cancellation. Instruments (aumu), generators
(augn), MIDI-only processors (aumi), mixers and outputs are excluded. Discovery
enumerates only aufx/aumf registrations and does not instantiate effects.

Stereo input/output compatibility is checked when an effect is loaded into an
insert. Three serial AU slots are available per region and on the master.
Effects must be installed, licensed and compatible with Logic's architecture.
Hosting an inserted AU is still in-process; this scanner hotfix does not isolate
faults in an effect during loading or playback. No in-process deep scan is used.

## Stereo analysis / 0.4.0

Turn **STEREO VIEW** on in the toolbar. The region graph narrows to make room
for a stereo vectorscope, a signed correlation meter and frequency-resolved
side-energy bars. The graph overlays **MID (white)** and **SIDE (teal)** spectra.

The source selector chooses **Selected region** (follows the region being
edited) or **Master output** (the complete processed 808/mix). A selected
region is measured after its distortion, AU chain, width, pan, level and region
filter, before master EQ/compression, master AUs, Auto Gain and output trim.
Bypassed/muted/soloed regions are represented accordingly. Master output is
measured after all processing and output gain.

- A centred mono signal gives a vertical trace, +1 correlation and zero side.
- Stereo differences spread the trace horizontally. Negative correlation
  indicates opposite-polarity content. Silence or a missing channel shows "--".
- LOW is 20-120 Hz, MID is 120 Hz-2 kHz, HIGH is above 2 kHz up to Nyquist.
  Their bars show side energy as a percentage of mid-plus-side energy,
  not the position of the width knob. 0% is mono, 50% is equal mid/side energy,
  and 100% is pure anti-phase. Hard-panned mono can also produce 50%.
  Frequency ranges with insufficient signal show "--".
- The vectorscope uses a common auto-scale for both axes. Mid/side FFTs use
  energy-preserving sqrt(2) normalization, so mono mid amplitude is 3 dB above
  one original channel; this is a display convention and adds no audio gain.
- The stereo FFT uses a 4096-sample Hann window with 50% overlap. It is a
  measurement view, not an additional audio effect or stereoizer.

Turn STEREO VIEW off to hide it and stop its stereo capture/FFT/scope filtering.
The standard input spectrum and region editing remain available. Analysis
settings are saved with the project/preset. The display also stops capturing
when the editor closes. Switching sources discards queued samples from the old
source, avoiding mixed-source traces. Audio capture uses a fixed stereo SPSC
queue and never waits for the display.

Native previews include a deterministic stereo test signal with a mono 55 Hz
fundamental and increasing stereo differences in higher harmonics.

## Gain and oversampling

A new clean instance uses **0 dB output trim**. The neutral output is the
latency-aligned original signal; it does not inherit the previous -6 dB trim.

AUTO GAIN matches smoothed stereo mean-square level after processing to input,
before manual output trim. It is optional and bounded to approximately +/-12 dB.
The applied correction is shown in the status line. It is not a perceptual LUFS
meter, peak limiter, or guarantee against clipping. It pauses during solo and
holds during silence.

Oversampling is selectable as **Off/1x, 2x, 4x, 8x or 16x**. It affects stock
distortion; hosted AUs retain their own oversampling settings. All modes are
prepared in advance. Changing it changes reported latency and resets the
oversampling state; switch with playback stopped for clean transitions.

## Signal path and latency

The original signal stays intact. Each active region extracts a linear-phase
frequency selection, applies distortion/AU effects/stereo/level, subtracts its
matched unprocessed reference, filters that change back into the region and
adds it to the delayed original. This preserves a clean instance without
crossover attenuation and keeps generated harmonics local to the selection.

Two linear-phase region filters contribute **8192 samples** of fixed latency,
plus stock oversampling and the longest active AU chain. Logic receives that
latency for plugin delay compensation. At 48 kHz, the fixed part is about
171 ms. This is a mixing effect, not a low-latency live-monitoring effect.
Pre-ringing is possible with linear-phase filters.

Regions sum into master bass/treble EQ, optional compressor, three AU inserts,
Auto Gain and manual output trim. Region changes and wet references align to
the longest active insert chain. The finite delay budget is 262143 samples.

## Presets and upgrades

Save/Load preset stores region positions, Q/slope, stock controls, global
settings, AU identities and full AU state. Logic project state saves the same.
Factory settings change stock controls and preserve insert chains.

Older 0.1/0.2 crossover presets are converted to independent centre/Q selections
while preserving plugin slots and stock controls. This is approximate: the old
full-spectrum crossover sum is no longer the processing model. Check migrated
projects by ear. Their legacy default output trim is reset to 0 dB.

## Build and verification

Requires macOS, Xcode command-line tools and CMake. First configure fetches
JUCE 8.0.6, which has its own license.

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
    cmake --build build --parallel 3
    ctest --test-dir build --output-on-failure
    bash scripts/package-macos.sh

Portable DSP/filter/Auto Gain/scanner classification tests:

    cmake -S . -B build-dsp -DBASSRACK_DSP_ONLY=ON
    cmake --build build-dsp
    ctest --test-dir build-dsp --output-on-failure

The native preview tool additionally runs clean, latency-aligned unity/null
tests at all five oversampling settings, then tests in-region gain and
out-of-region preservation before rendering the actual editor with a bass test
signal. Build with -DBASSRACK_BUILD_PREVIEW=ON and target BassRackPreview.

Actions verifies actual PKG installation, DMG integrity, installed signature,
both binary slices, DSP tests and Apple auval. Successful checks still do not
replace listening/session tests in Logic or compatibility tests with your AUs.

## Current limits

No VST/instrument hosting, external sidechain, arbitrary module reordering,
hosted-parameter automation lanes, Intel-only bridging, LUFS matching,
brickwall limiting, production signing or notarization. Filter/region/insert
and oversampling edits can produce transients; development playback/session
testing is needed. Very high Q at sub frequencies has finite FIR resolution.
Separate Intel runtime and third-party compatibility tests remain necessary.

The UI and code are original. General FabFilter/iZotope workflow references
do not include copied binaries, source, artwork or presets.
