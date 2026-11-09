# Bass Rack 0.5.1

## AU loading stability update

A replacement is prepared outside the live audio graph lock. The existing insert
and its editor remain available until the new effect is ready; a reported load
failure leaves them intact. Removing or replacing an insert closes only its own
editor, before releasing and destroying the old AU. Canceled callbacks and
callbacks from an obsolete audio configuration cannot install an effect.

Native lifecycle regression checks exercise replacement, failure rollback,
editor destruction order, cancellation and configuration changes with test effects.
They do not establish compatibility with Acustica Purple4AA. AU hosting still
runs in-process: a native fault inside an effect can take down Logic.

If an AU crashes while loading, inspect
`~/Library/Application Support/BassRack/last-host-load.txt`.
It records the most recent load stage, effect name/identifier/version and time,
without audio or preset contents. In projects with multiple rack instances it
describes the most recent operation across those instances.


A Logic-compatible AU rack with independent frequency regions, stock distortion,
AU insert hosting, stereo generation, Mid/Side processing, Auto Gain and Sub Protect.

## Install / channel configurations

Download BassRack-macOS-Installers from the successful Actions run. Extract the
artifact ZIP, quit Logic and run the PKG (directly or inside the DMG). Reopen Logic:
Audio Units > Crimson Audio > Bass Rack. Installer writes the system Components
folder. This development installer is unsigned and not notarized.

The AU supports 1-in/1-out (Mono), 1-in/2-out (Mono->Stereo), and 2-in/2-out
(Stereo). Logic offers the applicable choices for the channel strip. On a mono
808 track, choose **Mono->Stereo** to hear newly generated spread. A true mono
output cannot produce stereo; its width/stereoizer controls are disabled.

## Stereoize / Depth

The old width control multiplied existing side content, which did nothing to
a mono source. **STEREOIZE** now generates a quadrature side signal from the
processed Mid when Width exceeds 100%. DEPTH controls its amount. Width at
100% remains unchanged, 0% collapses to mono, and values above 100% can create
spread even from identical L/R input. Disable STEREOIZE for traditional M/S width.

The stereoizer uses a band-limited quadrature FIR path aligned within the existing
region-filter delay. There is no pitch modulation, random wobble or added Haas
delay. At centred pan, generated side is added with opposite signs to L/R, so
it cancels in their mono sum. Master processing, Auto Gain and output trim can
intentionally change that sum afterward. This is an original phase stereoizer,
not an emulation of an Ozone algorithm or measured analog hardware.

STEREO VIEW shows actual selected-region or final-output spread, correlation and
frequency-resolved side energy. Its source selector and on/off state are saved.
Turning it off or closing the editor stops additional analysis capture.

## Independent Low / High editing and Mid/Side

Double-click the graph to create a region, up to five. Drag each boundary
independently: moving the high edge does not move the low edge, and vice versa.
LOW EDGE and HIGH EDGE accept numeric Hz or kHz entries (e.g. 200 and 2k).
Dragging a numbered node moves the whole region and adjusts level. Scrolling a
node or editing Q changes bandwidth symmetrically. Edges map internally to
centre/Q; finite Q and FIR resolution limit extremely narrow selections.

The region selector **Stereo / Mid only / Side only** routes the chosen
component into stock distortion and AU effects. Mid-only processing preserves
the original Side; Side-only preserves the original Mid. Stereoize works in
Stereo mode. Pan is available in Stereo mode; Width also scales Side-only mode.
Solo auditions the selected component of the processed region.

## Natural / Contained harmonics

Natural harmonics selects the frequencies entering distortion and lets newly
generated harmonics extend beyond that selection. This avoids forcing all
distortion products through a second narrow band-pass. Contained harmonics
retains the previous strict post-filtered behavior for localized processing.
At extreme drive, any chosen region can still dominate the tonal balance.

## Sub Protect

SUB PROTECT restores the original low-frequency **Mid/centre** signal after
distortion, insert chains, master EQ/compression and Auto Gain, before final output
trim. The cutoff is in MASTER (default 80 Hz). It preserves bass weight while
retaining generated stereo Side content, so width and a clean fundamental can coexist.

This is a phase-aligned parallel restoration: delayed processed stereo plus
low-pass(original Mid) minus low-pass(processed Mid). The original centre is
restored through the pass band and blends out above the cutoff. Final output
trim still applies to everything. Protected low Mid bypasses master tonal/gain
changes; disable protection if those changes are intentional.

Auto Gain measures the processed path before restoration. Restoring clean bass
prioritizes bass weight over an exact final overall RMS match. A finite FIR
transition exists around/above the cutoff.

Region filtering plus Sub Protect alignment contributes **10240 samples**
fixed latency (~213 ms at 48 kHz), plus oversampling/AU latency. This is reported
to Logic for compensation and is intended for mixing, not live monitoring.

## Distortion palette / anti-aliasing

Clean, Soft, Tube, Clip, Fold and Sine remain at their original indices.
New original models are Valve A, Valve B, Diode, Tape, Transformer, Console,
FET Drive, Soft Diode, Hard Diode and Opto Drive. Drive now reaches 60 dB.

These are **circuit-inspired approximations**, not calibrated Neve, SSL, Trident,
1176, LA-2A, TS-808, RAT or other branded hardware emulations:
- Valve models use cascaded biased stages, finite bandwidth and supply-envelope memory.
- Diode models solve a normalized antiparallel Shockley diode/load equation.
- Tape/Transformer include nonlinear magnetic-feedback memory and frequency shaping.
- Console/FET/Opto models use distinct gain-stage curves, bandwidth and dynamic memory.
- Soft/Hard Diode contrast emphasized feedback-style drive and hard rail/diode clipping.
The static character preview omits time-dependent memory.

**ADAA** enables first-order antiderivative antialiasing of nonlinear kernels,
including an implicit diode antiderivative. It reduces aliasing; it cannot
promise alias-free output for all inputs, dynamic states or discontinuities.
Use it alongside selectable 1x/2x/4x/8x/16x oversampling for cleaner results.
For deliberate raw aliasing, disable ADAA and select Off/1x. Higher modes cost CPU.

## Fast scanner and plugin compatibility

Normal discovery reads macOS effect registration metadata and cached entries.
It does not instantiate effects. Instruments/generators/MIDI-only units are
excluded before discovery. Stereo compatibility validates on loading an insert.
A failed replacement preserves the existing insert. Actual AU loading/playback
is still in-process: a faulty loaded effect can still crash its host. No unsafe
in-process deep-validation scan is offered.

## Presets / testing / limits

Projects and rack presets save the controls, AU identities and plugin state.
Older projects retain Contained harmonics and traditional width until Stereoize
is enabled. Older crossover projects migrate approximately; check migrated
sessions by ear. New instances default to Natural harmonics, Stereoize enabled
with neutral 100% Width, ADAA on and Sub Protect on.

Build with CMake/JUCE 8.0.6 on macOS. Portable tests use -DBASSRACK_DSP_ONLY=ON.
Native checks/previews use -DBASSRACK_BUILD_PREVIEW=ON, target BassRackPreview.
The workflow builds universal AU/Standalone, creates PKG/DMG, installs the PKG,
verifies both binary slices/signature and runs Apple auval.

Native tests cover clean gain/OS modes, all three layouts, mono->stereo side
creation and fold-down, Mid/Side isolation, one-sided edges, natural vs contained
harmonics, heavy-drive sub restoration and Auto Gain/manual trim. Portable
tests cover ADAA primitives, a measured alias-reduction case and model stability.

Still requires listening and session testing in Logic with actual third-party
AUs. Filter/region/model/OS changes can cause transients or pre-ringing. FIR
resolution limits very narrow sub selections. No VST/instrument hosting, AU
bridging, hosted-parameter automation or production signing/notarization.
