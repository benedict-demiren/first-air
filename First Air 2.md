# First Air — v2 Master Spec

## Physics-Based Reverb Plugin: Evolution from v1.0

### Context

First Air v1.0 is a working, stable plugin: room geometry (L/W/H/Skew/Curvature), 11 wall materials per surface, 13 gases (including planetary presets), SPL nonlinearity, 3 freeze modes (progressive, input, buffer), shimmer, tonal feedback with MIDI, duck, source/listener position, dynamics, and 14 presets. It is written in Faust, compiled to C++ via faust2juce, targeting VST3/AU/CLAP on macOS.

v2 is structured as six sequential passes, each self-contained enough to be a single Claude Code session. Every pass leaves the plugin in a better state regardless of how far you get. The ordering is: fix what's broken, expand what works, add new capabilities.

### Important: What v1 Does Well

The gas/atmosphere system, freeze modes, room geometry, and the general "sounds genuinely new" character (Jay's assessment) are strengths. The creative/tonal territory — strange sounds from extreme settings — is where the plugin excels. Do not break these qualities while fixing what doesn't work.

---

## Pass 1 — Fix & Refine

**Priority: HIGH. This is the most important pass.**

The SPL nonlinearity (Energy parameter) is the founding concept of this plugin — the Albini spatial compression idea — and it doesn't work well in v1. This pass reworks it from the ground up plus addresses any other roughness.

### 1.1 SPL Waveshaper Rework

**Problem:** The current asymmetric tanh waveshaper with pre/de-emphasis doesn't produce the intended effect: the sensation of a room getting louder, of air compressing the sound spatially. Instead it reads as distortion applied to a reverb tail.

**Root cause (hypothesis):** The waveshaper operates identically on every FDN line regardless of propagation state. In a real room, SPL compression is distance-dependent, frequency-dependent, and cumulative across reflections. The current implementation misses all three.

**Proposed rework:**

1. **Distance-scaled drive.** Each of the 16 FDN delay lines represents a different path length. The waveshaper drive on each line should scale with that line's delay length — longer paths = more accumulated nonlinearity. Short early reflections should be nearly linear; long reverberant paths should show the most compression. Implementation: `drive_per_line = base_drive * (delay_length_ms / max_delay_length_ms)`.

2. **Frequency-dependent nonlinearity.** Real air compression affects different frequencies differently. Split the signal before the waveshaper into three bands (low/mid/high via Linkwitz-Riley crossover at ~200 Hz and ~4 kHz). Apply different drive amounts per band: low band gets the most drive (bass compresses first in real rooms), high band gets the least. Recombine after waveshaping. This replaces the pre/de-emphasis shelving which was a crude approximation.

3. **Envelope-coupled drive.** Add an envelope follower on the FDN input. The waveshaper drive should be partially modulated by input energy — louder input = more SPL compression in the feedback paths. Use a slow envelope (attack ~50ms, release ~200ms) so it tracks energy, not transients. Scale: `effective_drive = base_drive * (1 + env_amount * envelope)`, where `env_amount` is controlled by the Energy parameter's upper range (say 60–100%).

4. **Softer waveshaper curve.** Replace the asymmetric tanh with a polynomial soft clipper: `f(x) = x - (x^3)/3` for `|x| <= 1`, hard-limited beyond that. This has a gentler onset than tanh and more even-order harmonics (2nd harmonic warmth rather than odd-harmonic harshness). The asymmetric bias from v1 can be retained but reduced — at most 5% DC offset, smoothed.

5. **Saturation ceiling.** The existing saturation ceiling limiter in the FDN should remain as a safety net, but if the waveshaper rework is successful, it should rarely engage. Add a subtle soft-knee compressor (ratio 2:1, threshold at -6 dBFS in the FDN internal signal) before the hard ceiling to catch peaks more gracefully.

**Quality target:** At Energy 20–40%, the reverb tail should sound "pressurised" — denser, warmer, with gentle dynamic range reduction. Transients entering the reverb should compress into the tail rather than poking through it. At Energy 0%, behaviour must be identical to a clean linear reverb. A/B testing: play a snare through the reverb at Energy 0% and Energy 30% — the 30% version should sound like "the same room, but louder", not like "the same room with distortion added."

### 1.2 General Refinements

- **DC blocker audit.** Verify the DC blocker is catching any offset introduced by the waveshaper rework. The polynomial clipper with asymmetric bias will generate DC — the blocker needs to be downstream of the waveshaper in every FDN line, not just at the output.
- **Saturation at extreme gas densities.** Venus preset (9 MPa, very high density) scales SPL nonlinearity via atmospheric density. Verify this doesn't cause runaway feedback or harsh clipping. The density multiplier may need soft-limiting: `density_scale = tanh(density / reference_density)` rather than linear scaling.
- **Freeze tonal balance.** Verify that progressive freeze at 100% still maintains tonal balance after the waveshaper rework. The Quantec-style proportional damping reduction should interact correctly with the new multiband drive structure.
- **Turbulence smoothing.** Check that the 3-oscillator-per-line thermal turbulence doesn't create audible zipper noise on delay time changes. If so, add 1-pole smoothing on the modulation depth (not the oscillators themselves — the oscillators should remain incommensurate).

---

## Pass 2 — Shimmer & Buffer Expansion

Expands the two features that are already working well but feel constrained.

### 2.1 Shimmer

- **Shimmer Feedback (0–100%):** Currently shimmer applies once per FDN iteration. Add a dedicated feedback path for the shimmer pitch shifter: output of the shifter feeds back to its own input before re-entering the FDN. At low feedback, the shimmer contributes one layer of shifted harmonics per iteration (current behaviour). At high feedback, the shifter self-oscillates through its own chain, generating cascading harmonic series that accelerate away from the source pitch.
- **Shimmer Curve (linear / log / exp):** Controls how the shimmer's amplitude scales across iterations. Linear = equal contribution from each generation. Log = early generations dominate. Exp = later generations dominate. Default: log (most natural).
- **Shimmer Detune (-50 to +50 cents):** Slight detuning of the pitch shifter. At 0, the shimmer is mathematically exact. Small detuning values (5–15 cents) create gentle beating and organic drift. Large values create metallic, detuned harmonic clouds.

### 2.2 Buffer Freeze

- **Buffer Length:** Currently fixed at 2 seconds. Make variable: 0.1s to 8s, with tempo-sync options.
- **Buffer Source:** Selector: Pre-FDN, Post-FDN (current), Post-Duck.
- **Buffer Feedback (0–100%):** How much of the buffer output feeds back to its input.
- **Buffer Crossfade (1–500ms):** Length of the crossfade loop point. Default: 50ms.

---

## Pass 3 — Freeze Routing

### 3.1 Freeze Routing Matrix

Replace independent freeze toggles with a routing selector offering six named modes:

| Mode | Progressive | Input Freeze | Buffer Freeze | Character |
|------|-------------|-------------|---------------|-----------|
| Sustain | On | Off | Off | Classic infinite sustain |
| Isolate | On | On | Off | Freeze the tail, stop new input |
| Capture | Off | Off | On | Buffer loops current reverb output |
| Crystallise | On | On | On | Triple-locked sustain (see Pass 6) |
| Layer | Off | On | On | Input muted, buffer captures decaying tail |
| Through | Off | Off | Off | Normal reverb operation |

### 3.2 Freeze Transition Behaviour

Crossfade over 50ms between modes. Exception: engaging Buffer Freeze is instant. Disengaging crossfades. Isolate→Through transition: crossfade feedback gain back to normal over 200ms.

### 3.3 Signal Flow Update

```
Input → Predelay → [Input Gate] → 4-stage Allpass Diffusion
      → 6 Early Reflections (image-source, per-wall absorption)
      → 16-line FDN:
          Each line: Delay → Wall Damping → Air Absorption → Tone EQ
                   → Shimmer (with dedicated feedback) → Multiband SPL Waveshaper
                   → Freeze Gain (progressive) → Saturation Ceiling
          → Householder Feedback Matrix → back to delays
      → Stereo Extraction → 2-stage Output Diffusion → DC Blocker
      → [Buffer Freeze: capture/playback with crossfade, variable length]
      → Stereo Width → Duck → Filter (with envelope) → Output Mode Select
      → Dry/Wet Mix → Output Level → Output
```

---

## Pass 4 — Filter Expansion

### 4.1 SVF Implementation

Replace the single low-pass with a state-variable filter offering four modes: LP, BP, HP, Notch.

### 4.2 Parameters

- **Filter Mode** (LP / BP / HP / Notch)
- **Cutoff** (20–20000 Hz, logarithmic)
- **Resonance / Q** (0–95%)
- **Filter Env** (-100 to +100, retained from v1)

### 4.3 Filter Placement

Remains in the output path (post-FDN, post-buffer, pre-duck).

---

## Pass 5 — Palette Expansion

### 5.1 Modal Lock

Quantises tonal feedback pitch to scale degrees. Modes: Chromatic, Major, Minor, Dorian, Mixolydian, Pentatonic Major, Pentatonic Minor, Whole Tone, Diminished.

### 5.2 New Gases (7 + 2 planetary)

| Gas | gamma | M (g/mol) | c at 20C (m/s) | Character |
|-----|-------|-----------|----------------|-----------|
| Neon | 1.667 | 20.18 | 454 | Bright, fast |
| Argon | 1.667 | 39.95 | 323 | Subtle darkening |
| Xenon | 1.667 | 131.29 | 178 | Very slow, deep |
| N2O | 1.303 | 44.01 | 263 | Sweet tonal character |
| NH3 | 1.310 | 17.03 | 436 | Fast, bright |
| Cl2 | 1.355 | 70.91 | 213 | Dark and oppressive |
| Steam | 1.330 | 18.02 | 472 | Very fast, unusual |

Planetary: Jupiter (H2/He, -110C), Io (SO2, near-vacuum).

### 5.3 New Materials (7)

Water, Earth/Soil, Ice, Rubber, Stone, Vegetation, Sand.

### 5.4 Preset Morphing

Smooth interpolation between any two presets over 0.1s to 30s. Morph knob (0–100%) + Auto-Morph toggle.

### 5.5 New Presets (13+)

Ice Cave, Desert Canyon, Jungle Clearing, Swimming Pool, Jupiter Storm, Io Whisper, Rubber Room, Earth Burial, Xenon Cathedral, Steam Room, Modal Drone C, Morph: Cave to Cathedral, Crystallise Pad.

---

## Pass 6 — Architecture Expansion

### 6.1 Crystallise Freeze

Frequency-dependent freeze rates: Low band freezes fastest (x1.5), Mid normal (x1.0), High slowest (x0.6). "Crystallise Complete" flag triggers automatic buffer capture.

### 6.2 Frequency-Dependent Scattering

Low band: modulation depth x0.3, Mid: x1.0, High: x2.0. Applied after multiband split inside FDN lines.

### 6.3 Outdoor Propagation Mode

Indoor/Outdoor toggle. Outdoor: no ceiling reflections, walls become absorptive boundaries, reduced FDN feedback (0.3–0.5 openness factor), air absorption dominant, reduced diffusion.

---

## Pass Dependencies

| Pass | Depends On | Key Deliverable |
|------|-----------|-----------------|
| 1 | None | SPL rework: multiband, distance-scaled, envelope-coupled |
| 2 | Pass 1 | Shimmer self-feedback, variable buffer |
| 3 | Pass 2 | Freeze routing matrix with 6 modes |
| 4 | None | SVF with 4 modes + resonance |
| 5 | None | New gases, materials, modal lock, morphing, presets |
| 6 | Pass 1, 3 | Crystallise DSP, freq-dependent scattering, outdoor mode |

---

## Version History

- **v1.0** — Room geometry, 11 materials, 13 gases, SPL nonlinearity, 3 freeze modes, shimmer, tonal feedback with MIDI, duck, position, dynamics, 14 presets.
- **v2.0** — This spec.
