# First Air

A physics-based reverb plugin for macOS (VST3/AU/CLAP). Rather than simulating a generic reverb algorithm and layering controls on top, First Air models a physical room — its geometry, wall materials, and atmospheric conditions — and lets the reverb emerge from the physics. The result is a reverb that responds to parameter changes the way a real space would, with some creative extensions that push beyond what's physically possible.

## Core Idea

Traditional reverbs give you decay time, damping, and diffusion as abstract knobs. First Air inverts this: you define a room (dimensions, materials, atmosphere) and the algorithm derives the reverb characteristics. Want a longer tail? Make the room bigger or the walls more reflective. Want it darker? Use carpet instead of marble. Want it alien? Fill the room with sulfur hexafluoride.

The DSP engine is a 16-line Feedback Delay Network (FDN) with Householder feedback matrix, driven by room geometry calculations. Six first-order image-source early reflections precede the FDN. The entire signal path is written in Faust and compiled to optimized C++.

---

## Parameter Groups

### Space

Defines the physical room.

- **Length / Width / Height** — Room dimensions in meters (1–100m each). These directly determine delay line lengths via mean free path calculations. A 3m cube sounds tight and fast; a 100m hall has a long, evolving tail.
- **Skew** — Tilts the room geometry so parallel walls become non-parallel (trapezoidal). Breaks modal symmetry, reducing metallic resonances. Subtle values (5–15) clean up the sound; extreme values create asymmetric decay patterns.
- **Curvature** — Bows the walls inward (concave/focusing) or outward (convex/scattering). Positive values compress delay times together for a tighter, more focused sound. Negative values spread them apart for a washy, diffuse character.

### Material

Each of the six room surfaces (Left Wall, Right Wall, Front Wall, Back Wall, Floor, Ceiling) has its own material selector from 11 options:

**Concrete** / **Glass** / **Wood** / **Plaster** / **Carpet** / **Metal** / **Brick** / **Tile** / **Fabric** / **Foam** / **Marble**

Materials determine frequency-dependent wall absorption using 3-band coefficients (low/mid/high). Foam absorbs almost everything (very dead room). Marble reflects almost everything (very long, bright tail). The RT60 decay time is computed from the Sabine equation using area-weighted absorption across all six surfaces.

A "Material Macro" dropdown provides quick presets (All Wood, Studio, Bathroom, etc.) that set all six walls at once.

### Energy

Controls the nonlinear and tonal character of the reverb feedback path.

- **Energy** (0–100%) — SPL-dependent nonlinear compression in the FDN feedback. At 0%, the reverb is perfectly linear. At moderate settings (15–30%), transients compress naturally and the tail gains warmth — like a real room under high SPL. At extreme settings, heavy harmonic saturation.
- **Feedback Mode** — Switches from normal (Householder matrix) to tonal feedback (adds pitch-tuned comb filtering to the FDN). Creates pitched, resonant tails.
- **Tone On / Tone** — High-shelf EQ in the feedback path. Negative = darker tail, positive = brighter.
- **Pitch On / Pitch** — When Feedback Mode is on, sets the fundamental pitch of the resonant feedback (20–2000 Hz).
- **MIDI** — Enables MIDI note input to control the feedback pitch. Monophonic, last-note-priority. Play a note and the reverb tail tunes to that pitch.
- **Glide** (0–2000ms) — Portamento between pitch changes, operating in semitone space for musically correct glide behavior.
- **Snap** — Quantizes pitch to the nearest semitone. When active, the current note name displays in the UI.
- **Freeze** (0–100%) — Progressive sustain. Crossfades the FDN feedback gain toward unity (infinite decay). At 100%, the tail sustains indefinitely with Quantec-style damping reduction (wall absorption fades proportionally, maintaining tonal balance).
- **Input Freeze** — Mutes new audio entering the reverb while the existing tail continues. Combined with Freeze, creates a frozen reverb texture isolated from the input.
- **Buffer Freeze** — Captures and loops the reverb output in a 2-second self-feeding delay buffer. Creates a sustained, evolving pad from whatever the reverb was producing at the moment of capture.
- **Shimmer On / Shimmer** — Pitch shifter in the FDN feedback path (-24 to +24 semitones). At +12st, each feedback iteration shifts up an octave — the classic shimmer reverb effect. At other intervals, creates different harmonic series in the tail.

### Position

Controls where the sound source and listener sit within the room (0–1 normalized for each axis). Affects early reflection timing, levels, and stereo panning through image-source calculations.

- **Source X/Y/Z** — Sound source position.
- **Listener X/Y/Z** — Listener position.

Moving source and listener apart increases the direct-to-reverb ratio and changes the early reflection pattern.

### Mix

Output stage controls.

- **Dry/Wet** — Mix ratio (0–100%).
- **Output Level** — Gain trim (-24 to +6 dB).
- **Predelay** — Delay before reverb onset (0–500ms). Separates the dry signal from the reverb tail.
- **Duck** (0–100%) — Envelope-following ducker. Reduces reverb level while input is loud, lets it swell up in gaps. Creates a "breathing" reverb that stays out of the way of the dry signal.
- **Snapback** (10–2000ms) — How quickly the duck releases after input drops. Short = tight ducking, long = slow swell.
- **Width** (0–200%) — Stereo width of the wet signal. 100% = natural stereo field. 0% = mono. 200% = exaggerated.
- **Filter** (20–20000 Hz) — Low-pass filter on the wet signal. Useful for taming bright reverb tails.
- **Filter Env** (-100 to +100) — Envelope follower on the filter cutoff. Positive = filter opens when input is loud. Negative = filter closes when input is loud.
- **Output Mode** — Three routing options:
  - **Mix**: Normal dry+wet output.
  - **Duck Reject**: Outputs only the portion of the wet signal being removed by the ducker. Useful for parallel processing or sidechain-style effects.
  - **Wet Post-Duck**: Outputs only the ducked wet signal without dry. Useful for send/return configurations.

### Atmosphere

Models the physical properties of the air inside the room.

- **Temperature** (-80 to +200°C) — Primary driver of the speed of sound. Hot air = faster sound = shorter delays = room sounds smaller. Cold air = slower sound = longer delays = room sounds bigger. At default (20°C), speed is ~343 m/s.
- **Gas** — Composition of the atmosphere. 13 options:
  - **Real gases**: Air, Helium, CO2, SF6, Methane, Hydrogen
  - **Planetary**: Mars (~95% CO2, thin), Venus (~96% CO2, thick), Saturn (~96% H2), Titan (~98% N2), Early Earth (CO2/N2/H2O)
  - **Exotic**: Photosphere (H/He plasma), Submarine (water-saturated air)

  Each gas has its own heat capacity ratio and molar mass, which determine speed of sound. Helium makes the room sound ~3x smaller. SF6 makes it ~2.5x bigger.
- **Humidity** (0–100%) — Affects high-frequency air absorption. Peak absorption around 15–20% RH. Also slightly increases speed of sound in air.
- **Pressure** (10–500 kPa) — Atmospheric pressure. Affects air density (which scales SPL nonlinearity) and absorption strength. High pressure = denser air = more compression. Low pressure = thinner air = less absorption, more linear.
- **Turbulence** (0–100%) — Replaces simple LFO modulation with multi-oscillator thermal turbulence. Three sine oscillators per delay line at incommensurate rates create organic, non-repeating modulation. 0% = very stable, slightly metallic. 25% (default) = natural room feel. 100% = lush, heavily modulated.

### Dynamics

Additional modulation driven by atmospheric conditions.

- **Wind / Convection / Gusts** — Slow macro-modulation of various parameters, creating evolving, breathing textures over time.

---

## Signal Flow

```
Input → Predelay → Input Gate (Input Freeze) → 4-stage Allpass Diffusion
      → 6 Early Reflections (image-source, per-wall absorption)
      → 16-line FDN:
          Each line: Delay → Wall Damping → Air Absorption → Tone EQ
                   → Shimmer → SPL Waveshaper → Freeze Gain → Saturation Ceiling
          → Householder Feedback Matrix → back to delays
      → Stereo Extraction (alternating L/R pan weighting)
      → 2-stage Output Diffusion → DC Blocker → Buffer Freeze
      → Stereo Width → Duck → Filter (with envelope) → Output Mode Select
      → Dry/Wet Mix → Output Level → Output
```

---

## Factory Presets

14 presets demonstrating the range of the plugin:

| Preset | Character |
|--------|-----------|
| Default Room | Medium wood room, neutral starting point |
| Cathedral | Large concrete/marble space, cool and damp |
| Small Studio | Tight, treated room with fabric and carpet |
| Metal Tank | Small cylindrical metal chamber with compression |
| Warm Hall | Medium concert hall, dark and warm |
| Drone Machine | Metal room with tonal feedback, MIDI-enabled |
| Glass Greenhouse | Bright, reflective glass with slight skew |
| Dead Room | Small foam-treated room, very short decay |
| Mars Cavern | Martian atmosphere (-63°C, 0.6 kPa CO2) |
| Venus Depths | Venusian surface (460°C, 9MPa, heavy compression) |
| Titan Rain | Titan's nitrogen atmosphere (-179°C, fabric/foam) |
| Solar Corona | Photosphere gas, shimmer + buffer freeze |
| Frozen Cathedral | Marble walls, 100% freeze + shimmer |
| Submarine Ping | Water-saturated air, metal hull, bright tone |

---

## Technical Details

- **FDN**: 16 delay lines with Householder feedback matrix (O(N) computation). Delay lengths derived from room geometry via mean free path, distributed across a coprime ratio series.
- **Damping**: Per-line high-shelf and low-shelf filters with gains derived from material absorption coefficient tables (3-band: 250Hz, 1kHz, 4kHz).
- **SPL Nonlinearity**: Asymmetric tanh waveshaper with bias (models real air compression asymmetry), pre/de-emphasis shelving, drive scaled by Energy parameter and per-line propagation distance, further scaled by atmospheric density.
- **Speed of Sound**: Computed from the ideal gas law: `c = sqrt(gamma * R * T / M)` where gamma and M are per-gas constants. Humidity correction for air.
- **Air Absorption**: Frequency-dependent high-shelf attenuation in FDN feedback, scaled by per-line delay distance, humidity, temperature, gas type, and pressure.
- **Thermal Modulation**: 3 sine oscillators per delay line at incommensurate frequencies (0.05–1.2 Hz range), weighted and scaled by Turbulence and Temperature. Replaces simple single-LFO modulation with organic, non-repeating delay time variations.
- **Formats**: VST3, AU, CLAP (macOS). Built with JUCE + clap-juce-extensions.
- **DSP Language**: Faust, compiled to vectorized C++ (`-vec -vs 32`).

---

## Creative Applications

**Realistic spaces**: Set room dimensions, pick appropriate materials, leave Atmosphere at defaults. Use Position to place source and listener. This is the straightforward reverb use case.

**Tonal reverb / drone**: Enable Feedback Mode + Pitch On. Set pitch to a musical note, Snap on. Send transient material in and the reverb tail resonates at that pitch. Enable MIDI for playable pitched reverb.

**Freeze textures**: Play audio, engage Progressive Freeze to sustain the tail, then Input Freeze to isolate it. The frozen tail maintains its tonal balance. Layer with Shimmer for evolving pads.

**Buffer loops**: Buffer Freeze captures and loops the current reverb output. Useful for creating sustained textures from momentary inputs. Combine with Duck to create rhythmic frozen-reverb patterns.

**Alien environments**: Choose a planetary gas preset. Mars creates a thin, cold, distant reverb. Venus creates a crushing, dense, heavily compressed space. Photosphere + Shimmer + Freeze creates otherworldly sustained textures.

**Parallel processing**: Use Output Mode "Duck Reject" to extract just the ducked portion of the reverb for parallel bus processing. "Wet Post-Duck" gives you the clean ducked wet signal for send/return setups.
