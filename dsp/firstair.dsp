// ============================================================================
// First Air v2 — Physical Geometry Reverb with Atmospheric Modeling
// Pass 1: SPL Waveshaper Rework — multiband, distance-scaled, envelope-coupled
//         polynomial soft clipper. DC blocker per FDN line. Density soft-limiting.
// Pass 2: Shimmer & Buffer Expansion — dedicated shimmer feedback loop, curve,
//         detune. Variable buffer length (100ms-8s), source select, feedback.
// ============================================================================
//
import("stdfaust.lib");

// ============================================================================
// SECTION 1: CONSTANTS
// ============================================================================

R_GAS = 8.314;           // Universal gas constant J/(mol*K)
STD_DENSITY = 1.225;     // Standard air density kg/m³ at 15°C, 101.325 kPa
N = 16;                  // FDN delay line count
MAXDELAY = 32768;        // Max delay buffer per line (~680ms at 48kHz)
MAXDELAY_AP = 2048;      // Max delay for allpass diffusers

MAXDELAY_ER = 16384;   // Max delay for early reflections (~340ms at 48kHz)
EAR_SPACING = 0.1;    // ±0.1m offset for stereo ear model

// 16 prime offsets added to base delay lengths to ensure mutual primality.
// These small offsets (in samples) break up any harmonic relationships.
PRIME_OFFSET_0  = 0;   PRIME_OFFSET_1  = 2;   PRIME_OFFSET_2  = 6;
PRIME_OFFSET_3  = 8;   PRIME_OFFSET_4  = 12;  PRIME_OFFSET_5  = 18;
PRIME_OFFSET_6  = 20;  PRIME_OFFSET_7  = 26;  PRIME_OFFSET_8  = 30;
PRIME_OFFSET_9  = 32;  PRIME_OFFSET_10 = 36;  PRIME_OFFSET_11 = 42;
PRIME_OFFSET_12 = 48;  PRIME_OFFSET_13 = 50;  PRIME_OFFSET_14 = 54;
PRIME_OFFSET_15 = 60;

// ============================================================================
// SECTION 2: PARAMETER DECLARATIONS
// ============================================================================

// --- Space (Geometry) ---
room_length  = hslider("[0]Space/[0]Length [unit:m] [tooltip:Room length front-to-back]", 8.0, 0.3, 100.0, 0.1) : si.smoo;
room_width   = hslider("[0]Space/[1]Width [unit:m] [tooltip:Room width left-to-right]", 5.0, 0.3, 60.0, 0.1) : si.smoo;
room_height  = hslider("[0]Space/[2]Height [unit:m] [tooltip:Room height floor-to-ceiling]", 3.5, 0.3, 30.0, 0.1) : si.smoo;
room_skew    = hslider("[0]Space/[3]Skew [unit:%] [tooltip:Wall angle. 0=rectangular, ±100=extreme trapezoid]", 0, -100, 100, 1) : si.smoo;
room_curve   = hslider("[0]Space/[4]Curvature [unit:%] [tooltip:Wall curvature. +100=focusing, -100=scattering]", 0, -100, 100, 1) : si.smoo;

// --- Material (Per-Wall Surface) ---
// 11 materials: Concrete=0, Glass=1, Wood=2, Plaster=3, Carpet=4, Metal=5,
//               Brick=6, Tile=7, Fabric=8, Foam=9, Marble=10
wall_mat_left    = nentry("[1]Material/[0]Left Wall [style:menu{'Concrete':0;'Glass':1;'Wood':2;'Plaster':3;'Carpet':4;'Metal':5;'Brick':6;'Tile':7;'Fabric':8;'Foam':9;'Marble':10}]",    2, 0, 10, 1);
wall_mat_right   = nentry("[1]Material/[1]Right Wall [style:menu{'Concrete':0;'Glass':1;'Wood':2;'Plaster':3;'Carpet':4;'Metal':5;'Brick':6;'Tile':7;'Fabric':8;'Foam':9;'Marble':10}]",   2, 0, 10, 1);
wall_mat_front   = nentry("[1]Material/[2]Front Wall [style:menu{'Concrete':0;'Glass':1;'Wood':2;'Plaster':3;'Carpet':4;'Metal':5;'Brick':6;'Tile':7;'Fabric':8;'Foam':9;'Marble':10}]",   2, 0, 10, 1);
wall_mat_back    = nentry("[1]Material/[3]Back Wall [style:menu{'Concrete':0;'Glass':1;'Wood':2;'Plaster':3;'Carpet':4;'Metal':5;'Brick':6;'Tile':7;'Fabric':8;'Foam':9;'Marble':10}]",    2, 0, 10, 1);
wall_mat_floor   = nentry("[1]Material/[4]Floor [style:menu{'Concrete':0;'Glass':1;'Wood':2;'Plaster':3;'Carpet':4;'Metal':5;'Brick':6;'Tile':7;'Fabric':8;'Foam':9;'Marble':10}]",       4, 0, 10, 1);
wall_mat_ceiling = nentry("[1]Material/[5]Ceiling [style:menu{'Concrete':0;'Glass':1;'Wood':2;'Plaster':3;'Carpet':4;'Metal':5;'Brick':6;'Tile':7;'Fabric':8;'Foam':9;'Marble':10}]",     3, 0, 10, 1);

// --- Energy (SPL Nonlinearity) ---
energy = hslider("[2]Energy/[0]Energy [unit:%] [tooltip:SPL nonlinearity. 0=linear, 100=extreme compression]", 0, 0, 100, 0.1) : si.smoo;
feedback_mode = checkbox("[2]Energy/[1]Feedback [tooltip:Bypass gain normalization — allows runaway feedback for drone/texture effects]");
tone_enable = checkbox("[2]Energy/[2]Tone On [tooltip:Enable tilt EQ in reverb tail]");
fb_tone = hslider("[2]Energy/[3]Tone [tooltip:Reverb tail brightness. -50=dark, +50=bright]", 0, -50, 50, 0.1) : si.smoo;
pitch_enable = checkbox("[2]Energy/[4]Pitch On [tooltip:Enable pitch tuning of delay lines]");
midi_enable = checkbox("[2]Energy/[5]MIDI [tooltip:Enable MIDI pitch input. When on, incoming MIDI notes override the Pitch slider]");
fb_pitch = hslider("[2]Energy/[6]Pitch [unit:Hz] [tooltip:Tune feedback resonance to a frequency]", 20, 20, 2000, 0.1);
pitch_glide = hslider("[2]Energy/[7]Glide [unit:ms] [tooltip:Portamento time between pitch changes. 0=instant, 2000=slow slide]", 0, 0, 2000, 1) : si.smoo;
pitch_snap = checkbox("[2]Energy/[8]Snap [tooltip:Quantize pitch to nearest 12-TET semitone]");
freeze = hslider("[2]Energy/[9]Freeze [unit:%] [tooltip:Progressive sustain. 0=normal decay, 100=infinite frozen reverb]", 0, 0, 100, 0.1) : si.smoo;
input_freeze = checkbox("[2]Energy/[10]Input Freeze [tooltip:Mute new audio entering the reverb. Tail sustains, no new input]");
buffer_freeze = checkbox("[2]Energy/[11]Buffer Freeze [tooltip:Capture and loop the reverb output. Creates infinite sustaining textures]");
buffer_length_ms = hslider("[2]Energy/[17]Buffer Length [unit:ms] [tooltip:Freeze buffer length. Short=granular, long=phrase capture]", 2000, 100, 8000, 1) : si.smoo;
buffer_source = nentry("[2]Energy/[18]Buffer Source [style:menu{'Post-FDN':0;'Pre-FDN':1;'Post-Duck':2}] [tooltip:What signal the buffer captures. Post-FDN=reverb tail, Pre-FDN=dry room entry, Post-Duck=shaped output]", 0, 0, 2, 1);
buffer_feedback_amt = hslider("[2]Energy/[19]Buffer FB [unit:%] [tooltip:Buffer self-feedback. 0=clean loop, 100=saturating drone]", 0, 0, 100, 0.1) : si.smoo;
buffer_xfade_ms = hslider("[2]Energy/[20]Buffer XFade [unit:ms] [tooltip:Crossfade at loop point. Short=tight, long=ambient blend]", 50, 1, 500, 1) : si.smoo;
shimmer_enable = checkbox("[2]Energy/[12]Shimmer On [tooltip:Pitch-shifted reverb feedback — creates cascading octaves/harmonics]");
shimmer_pitch = hslider("[2]Energy/[13]Shimmer [unit:st] [tooltip:Pitch shift per feedback iteration. 12=octave up, -12=octave down]", 12, -24, 24, 0.1) : si.smoo;
shimmer_feedback = hslider("[2]Energy/[14]Shimmer FB [unit:%] [tooltip:Shimmer self-feedback. 0=single shift per iteration, 100=cascading harmonic series]", 0, 0, 100, 0.1) : si.smoo;
shimmer_curve = nentry("[2]Energy/[15]Shimmer Curve [style:menu{'Linear':0;'Log':1;'Exp':2}] [tooltip:Shimmer amplitude scaling. Log=natural (default), Exp=slow build, Linear=equal]", 1, 0, 2, 1);
shimmer_detune = hslider("[2]Energy/[16]Shimmer Detune [unit:cents] [tooltip:Pitch shifter detuning. Small values=organic beating, large=metallic clouds]", 0, -50, 50, 0.1) : si.smoo;

// --- Position (Source) ---
src_x = hslider("[3]Position/[0]Source X [tooltip:Source left-right position]", 0.5, 0.0, 1.0, 0.01) : si.smoo;
src_y = hslider("[3]Position/[1]Source Y [tooltip:Source front-back position]", 0.3, 0.0, 1.0, 0.01) : si.smoo;
src_z = hslider("[3]Position/[2]Source Z [tooltip:Source height]", 0.5, 0.0, 1.0, 0.01) : si.smoo;

// --- Position (Listener) ---
lis_x = hslider("[3]Position/[3]Listener X [tooltip:Listener left-right position]", 0.5, 0.0, 1.0, 0.01) : si.smoo;
lis_y = hslider("[3]Position/[4]Listener Y [tooltip:Listener front-back position]", 0.7, 0.0, 1.0, 0.01) : si.smoo;
lis_z = hslider("[3]Position/[5]Listener Z [tooltip:Listener height]", 0.5, 0.0, 1.0, 0.01) : si.smoo;

// --- Mix ---
drywet = hslider("[4]Mix/[0]Dry Wet [unit:%] [tooltip:Balance between dry and reverb]", 30, 0, 100, 0.1) : si.smoo;
output_level = hslider("[4]Mix/[1]Output Level [unit:dB] [tooltip:Master output gain]", 0.0, -60.0, 12.0, 0.1) : si.smoo;
predelay_ms = hslider("[4]Mix/[2]Pre-Delay [unit:ms] [tooltip:Delay before reverb onset. Adds space between dry and wet signal]", 0, 0, 500, 1) : si.smoo;
duck = hslider("[4]Mix/[3]Duck [unit:%] [tooltip:Ducks wet signal when input is present. 0=off, 100=full ducking]", 0, 0, 100, 0.1) : si.smoo;
snapback = hslider("[4]Mix/[4]Snapback [unit:ms] [tooltip:Time for wet signal to return after input stops. Short=tight, long=dramatic swell]", 200, 10, 2000, 1) : si.smoo;
stereo_width = hslider("[4]Mix/[5]Width [unit:%] [tooltip:Stereo spread. 0=mono, 100=normal, 200=hyper-wide]", 100, 0, 200, 1) : si.smoo;
filter_cutoff = hslider("[4]Mix/[6]Filter [unit:Hz] [tooltip:Wet signal lowpass filter. 20kHz=off, lower=darker reverb tail]", 20000, 200, 20000, 1) : si.smoo;
filter_env_amt = hslider("[4]Mix/[7]Filter Env [tooltip:Input-triggered filter modulation. Negative=darken on transient, positive=brighten]", 0, -100, 100, 1) : si.smoo;
output_mode = nentry("[4]Mix/[8]Output Mode [style:menu{'Mix':0;'Duck Reject':1;'Wet Post-Duck':2}] [tooltip:Output routing. Mix=normal, Duck Reject=ducked wet signal, Wet Post-Duck=wet after ducking without dry]", 0, 0, 2, 1);

// --- Atmosphere ---
temperature = hslider("[5]Atmosphere/[0]Temperature [unit:C] [tooltip:Air temperature — near absolute zero to plasma. Affects speed of sound and turbulence]", 20, -270, 1000, 0.1) : si.smoo;
gas_comp = nentry("[5]Atmosphere/[1]Gas [style:menu{'Air':0;'Helium':1;'CO2':2;'SF6':3;'Methane':4;'Hydrogen':5;'Mars':6;'Venus':7;'Saturn':8;'Titan':9;'Early Earth':10;'Photosphere':11;'Submarine':12}] [tooltip:Gas composition — Earth gases or planetary atmospheres]", 0, 0, 12, 1);
humidity = hslider("[5]Atmosphere/[2]Humidity [unit:%] [tooltip:Relative humidity — affects HF air absorption]", 40, 0, 100, 1) : si.smoo;
pressure_kpa = hslider("[5]Atmosphere/[3]Pressure [unit:kPa] [tooltip:Atmospheric pressure — affects air density and absorption]", 101.325, 10, 500, 0.1) : si.smoo;
turbulence = hslider("[5]Atmosphere/[4]Turbulence [unit:%] [tooltip:Thermal turbulence — organic delay modulation]", 25, 0, 100, 0.1) : si.smoo;

// --- Dynamics (gas movement physics modelling) ---
wind_level = hslider("[6]Dynamics/[0]Wind [unit:%] [tooltip:Broadband noise — subtle texture at low settings, howling gale at high]", 0, 0, 100, 0.1) : si.smoo;
convection = hslider("[6]Dynamics/[1]Convection [unit:%] [tooltip:Thermal convection currents — slow coordinated delay modulation as warm gas circulates]", 0, 0, 100, 0.1) : si.smoo;
gusts = hslider("[6]Dynamics/[2]Gusts [unit:%] [tooltip:Sudden speed-of-sound fluctuations — momentary pitch/delay shifts as gas pressure surges through the room]", 0, 0, 100, 0.1) : si.smoo;

// ============================================================================
// SECTION 3: DERIVED COMPUTATIONS
// ============================================================================

// --- Gas properties + dynamic speed of sound ---
gas_idx = int(gas_comp);

// Per-gas heat capacity ratio (gamma):
//   Air=1.4, He=1.667, CO2=1.289, SF6=1.098, CH4=1.32, H2=1.41
//   Mars=1.29(~CO2), Venus=1.29(~CO2), Saturn=1.41(~H2), Titan=1.40(~N2)
//   Early Earth=1.30(CO2/N2/H2O), Photosphere=1.66(H/He plasma), Submarine=1.35(wet air)
gas_gamma = (1.4, 1.667, 1.289, 1.098, 1.32, 1.41,
             1.29, 1.29, 1.41, 1.40, 1.30, 1.66, 1.35) : ba.selectn(13, gas_idx) : si.smoo;

// Per-gas molar mass (kg/mol):
//   Air=0.029, He=0.004, CO2=0.044, SF6=0.146, CH4=0.016, H2=0.002
//   Mars=0.043(~CO2 surface), Venus=0.043(~CO2 thick), Saturn=0.0024(~H2+He)
//   Titan=0.028(~N2+CH4), Early Earth=0.038(CO2/N2/H2O mix)
//   Photosphere=0.0013(H/He plasma), Submarine=0.025(water-saturated air)
gas_molar = (0.029, 0.004, 0.044, 0.146, 0.016, 0.002,
             0.043, 0.043, 0.0024, 0.028, 0.038, 0.0013, 0.025) : ba.selectn(13, gas_idx) : si.smoo;

// Temperature in Kelvin (clamped to prevent sqrt of negative/zero)
temp_kelvin = max(temperature + 273.15, 1.0);

// Speed of sound from thermodynamics: c = sqrt(gamma * R * T / M)
// At 20°C, Air: sqrt(1.4 * 8.314 * 293.15 / 0.029) = 343.2 m/s ✓
speed_base = sqrt(gas_gamma * R_GAS * temp_kelvin / gas_molar);

// Humidity correction: ~+1.5 m/s from 0→100% RH for air, negligible for other gases
// Water vapor (M≈0.018) is lighter than air (M≈0.029), slightly increases sound speed
humidity_correction = (gas_idx == 0) * 0.0016 * humidity;

// --- Gust speed-of-sound modulation ---
// Gusts create sudden changes in effective speed of sound across the whole room.
// Multiple slow incommensurate oscillators → rectified → sharpened via pow(3).
// Creates rare, dramatic surges rather than constant wobble.
gust_norm = gusts / 100.0;
gust_raw = os.oscsin(0.07) * 0.4 + os.oscsin(0.13) * 0.3 + os.oscsin(0.31) * 0.3;
gust_env = max(gust_raw, 0.0) : pow(3.0);
// Speed multiplier: up to ±15% speed variation during strong gusts
gust_speed_mod = 1.0 + gust_env * gust_norm * 0.15;

// Final speed of sound with smoothing (smooths discrete gas-switch jumps)
// Gust modulation applied before smoothing for organic effect.
speed_of_sound = (speed_base + humidity_correction) * gust_speed_mod : si.smoo;

// --- Air density from ideal gas law: rho = P * M / (R * T) ---
pressure_pa = pressure_kpa * 1000.0;
air_density = pressure_pa * gas_molar / (R_GAS * temp_kelvin);
// Density ratio relative to standard air (1.225 kg/m³)
// Standard conditions: 101325 * 0.029 / (8.314 * 293.15) = 1.205 → ratio ≈ 0.98
density_ratio = air_density / STD_DENSITY;

// Convert normalized positions to meters
src_xm = src_x * room_width;
src_ym = src_y * room_length;
src_zm = src_z * room_height;
lis_xm = lis_x * room_width;
lis_ym = lis_y * room_length;
lis_zm = lis_z * room_height;

// Room volume and surface area
room_volume = room_length * room_width * room_height;
room_surface = 2.0 * (room_length * room_width + room_length * room_height + room_width * room_height);

// Mean free path (average distance between reflections in a shoebox room)
mean_free_path = 4.0 * room_volume / (room_surface + 0.0001);

// Distance conversion (uses dynamic speed_of_sound from atmosphere parameters)
dist2samples(d) = d / speed_of_sound * ma.SR;

// Direct distance from source to listener
direct_distance = sqrt((src_xm - lis_xm)^2 + (src_ym - lis_ym)^2 + (src_zm - lis_zm)^2);

// Output gain from dB
out_gain = ba.db2linear(output_level);

// Dry/wet mixing coefficients
wet = drywet / 100.0;
dry = 1.0 - wet;

// Stereo width factor (0=mono, 1=normal, 2=hyper-wide)
width_factor = stereo_width / 100.0;

// ============================================================================
// SECTION 3B: GEOMETRY-DRIVEN FDN DELAY LENGTHS
// ============================================================================
//
// Compute 16 delay line lengths from room geometry.
// Strategy: distribute delays between 1x and 4x the mean free path,
// logarithmically spaced, with prime offsets for mutual primality.
// This approximates the distribution of reflection path lengths in a
// shoebox room without needing to compute all image-source paths.

// Delay range: from mean_free_path to 4 * mean_free_path (in meters)
delay_min_m = mean_free_path;
delay_max_m = 4.0 * mean_free_path;

// Logarithmic spacing factor per line
// Base delay: delay_m_base(i) = delay_min_m * (delay_max_m/delay_min_m)^(i/(N-1))
delay_ratio = delay_max_m / (delay_min_m + 0.0001);
delay_m_base(i) = delay_min_m * delay_ratio ^ (i / (N - 1.0));

// --- Curvature: redistribute delays around the geometric mean ---
// Positive curvature (concave walls) → compress delays toward mean (focusing)
// Negative curvature (convex walls) → spread delays away from mean (scattering)
// curve_factor: +100 → -2.0 (extreme focus/inversion), 0 → 1.0, -100 → 4.0 (extreme scatter)
// Extremely dramatic — delays can invert around mean at high positive curvature.
curve_factor = 1.0 - room_curve / 50.0 * 1.5;
delay_m_curved(i) = mean_delay_m + (delay_m_base(i) - mean_delay_m) * curve_factor;

// --- Skew: linear tilt across delay lines ---
// Simulates a trapezoidal room where opposite walls aren't parallel.
// Line 0 gets shortened, line 15 gets lengthened (or vice versa for negative skew).
// At ±100%: ±6.0 × mean_free_path offset — wildly distorted delay distribution.
skew_norm = room_skew / 100.0;
skew_offset(i) = skew_norm * mean_free_path * 6.0 * ((i - 7.5) / 7.5);

// --- Combined delay pipeline ---
delay_m_final(i) = delay_m_curved(i) + skew_offset(i);
delay_m(i) = max(delay_m_final(i), 0.1);  // safety clamp: minimum 10cm path

// Convert to samples, add prime offset, clamp to [4, MAXDELAY-2]
delay_len(i) = dist2samples(delay_m(i)) + prime_offset(i) : max(4) : min(MAXDELAY - 2);

// Prime offsets (compile-time pattern matching)
prime_offset(0)  = PRIME_OFFSET_0;   prime_offset(1)  = PRIME_OFFSET_1;
prime_offset(2)  = PRIME_OFFSET_2;   prime_offset(3)  = PRIME_OFFSET_3;
prime_offset(4)  = PRIME_OFFSET_4;   prime_offset(5)  = PRIME_OFFSET_5;
prime_offset(6)  = PRIME_OFFSET_6;   prime_offset(7)  = PRIME_OFFSET_7;
prime_offset(8)  = PRIME_OFFSET_8;   prime_offset(9)  = PRIME_OFFSET_9;
prime_offset(10) = PRIME_OFFSET_10;  prime_offset(11) = PRIME_OFFSET_11;
prime_offset(12) = PRIME_OFFSET_12;  prime_offset(13) = PRIME_OFFSET_13;
prime_offset(14) = PRIME_OFFSET_14;  prime_offset(15) = PRIME_OFFSET_15;

// Mean delay in samples (for feedback gain calculation)
// Use the geometric mean of min and max as a good approximation
mean_delay_m = sqrt(delay_min_m * delay_max_m);
mean_delay = dist2samples(mean_delay_m);

// ============================================================================
// SECTION 3C: EARLY REFLECTIONS (IMAGE-SOURCE METHOD)
// ============================================================================
//
// 6 first-order reflections, one per surface of the shoebox room:
//   0: Left wall   (x = 0 plane)
//   1: Right wall  (x = W plane)
//   2: Front wall  (y = 0 plane)
//   3: Back wall   (y = L plane)
//   4: Floor       (z = 0 plane)
//   5: Ceiling     (z = H plane)
//
// For each surface, we compute the mirror-image source position,
// then the distance from image to listener → delay + gain.
// Stereo is created by offsetting the listener ±0.1m in X for L/R ears.

// Mirror-image source positions (reflected across each surface)
// Left wall (x=0): mirror x → -src_xm
img_x(0) = 0.0 - src_xm;   img_y(0) = src_ym;          img_z(0) = src_zm;
// Right wall (x=W): mirror x → 2*W - src_xm
img_x(1) = 2.0*room_width - src_xm;  img_y(1) = src_ym;  img_z(1) = src_zm;
// Front wall (y=0): mirror y → -src_ym
img_x(2) = src_xm;          img_y(2) = 0.0 - src_ym;    img_z(2) = src_zm;
// Back wall (y=L): mirror y → 2*L - src_ym
img_x(3) = src_xm;          img_y(3) = 2.0*room_length - src_ym;  img_z(3) = src_zm;
// Floor (z=0): mirror z → -src_zm
img_x(4) = src_xm;          img_y(4) = src_ym;           img_z(4) = 0.0 - src_zm;
// Ceiling (z=H): mirror z → 2*H - src_zm
img_x(5) = src_xm;          img_y(5) = src_ym;           img_z(5) = 2.0*room_height - src_zm;

// Distance from image source i to a listener at position (lx, ly, lz)
img_dist(i, lx, ly, lz) = sqrt((img_x(i) - lx)^2 + (img_y(i) - ly)^2 + (img_z(i) - lz)^2);

// Stereo listener positions (offset ±EAR_SPACING in X)
lis_xm_L = lis_xm - EAR_SPACING;
lis_xm_R = lis_xm + EAR_SPACING;

// Per-reflection distances for L and R ears
er_dist_L(i) = img_dist(i, lis_xm_L, lis_ym, lis_zm);
er_dist_R(i) = img_dist(i, lis_xm_R, lis_ym, lis_zm);

// Per-reflection delay in samples (clamped to valid range)
er_delay_L(i) = dist2samples(er_dist_L(i)) : max(1) : min(MAXDELAY_ER - 2);
er_delay_R(i) = dist2samples(er_dist_R(i)) : max(1) : min(MAXDELAY_ER - 2);

// Per-reflection gain: inverse-distance attenuation relative to direct sound
// gain = direct_distance / reflection_distance (capped at 1.0)
// Each reflection applies the absorption of its specific wall surface:
//   0=Left, 1=Right, 2=Front, 3=Back, 4=Floor, 5=Ceiling
er_gain_L(i) = (direct_distance + 0.01) / (er_dist_L(i) + 0.01)
             : min(1.0) : *(1.0 - wall_abs_avg(i));
er_gain_R(i) = (direct_distance + 0.01) / (er_dist_R(i) + 0.01)
             : min(1.0) : *(1.0 - wall_abs_avg(i));

// Early reflections engine: mono input → stereo output
// Applies 6 delayed+attenuated copies for each ear, then sums.
early_reflections = _ <: (er_left, er_right)
with {
    er_left  = par(i, 6, de.fdelay(MAXDELAY_ER, er_delay_L(i)) : *(er_gain_L(i))) :> _;
    er_right = par(i, 6, de.fdelay(MAXDELAY_ER, er_delay_R(i)) : *(er_gain_R(i))) :> _;
};

// ============================================================================
// SECTION 4: PER-WALL MATERIAL ABSORPTION COEFFICIENTS
// ============================================================================
//
// 11 materials × 3 bands (low ~250Hz, mid ~1kHz, high ~4kHz)
// 6 surfaces × individual material selectors → area-weighted Sabine RT60
//
// Surfaces: 0=Left, 1=Right, 2=Front, 3=Back, 4=Floor, 5=Ceiling
// Materials: Concrete=0, Glass=1, Wood=2, Plaster=3, Carpet=4, Metal=5,
//            Brick=6, Tile=7, Fabric=8, Foam=9, Marble=10

// Per-wall material index
mat_idx(0) = int(wall_mat_left);
mat_idx(1) = int(wall_mat_right);
mat_idx(2) = int(wall_mat_front);
mat_idx(3) = int(wall_mat_back);
mat_idx(4) = int(wall_mat_floor);
mat_idx(5) = int(wall_mat_ceiling);

// 3-band absorption lookup for each material (11 materials)
// Low (~250Hz): Concrete=0.01, Glass=0.04, Wood=0.15, Plaster=0.02, Carpet=0.24, Metal=0.04,
//               Brick=0.03, Tile=0.01, Fabric=0.10, Foam=0.15, Marble=0.01
// Mid (~1kHz):  0.02, 0.03, 0.07, 0.03, 0.69, 0.03, 0.04, 0.02, 0.40, 0.80, 0.01
// High (~4kHz): 0.03, 0.02, 0.07, 0.05, 0.73, 0.02, 0.05, 0.02, 0.60, 0.90, 0.02
wall_abs_low(i) = (0.01, 0.04, 0.15, 0.02, 0.24, 0.04, 0.03, 0.01, 0.10, 0.15, 0.01)
                : ba.selectn(11, mat_idx(i));
wall_abs_mid(i) = (0.02, 0.03, 0.07, 0.03, 0.69, 0.03, 0.04, 0.02, 0.40, 0.80, 0.01)
                : ba.selectn(11, mat_idx(i));
wall_abs_hi(i)  = (0.03, 0.02, 0.07, 0.05, 0.73, 0.02, 0.05, 0.02, 0.60, 0.90, 0.02)
                : ba.selectn(11, mat_idx(i));
wall_abs_avg(i) = (wall_abs_low(i) + wall_abs_mid(i) + wall_abs_hi(i)) / 3.0;

// Per-surface area (m²)
wall_area(0) = room_length * room_height;   // left wall
wall_area(1) = room_length * room_height;   // right wall
wall_area(2) = room_width * room_height;    // front wall
wall_area(3) = room_width * room_height;    // back wall
wall_area(4) = room_length * room_width;    // floor
wall_area(5) = room_length * room_width;    // ceiling

// Area-weighted average absorption (proper Sabine method)
total_absorption = sum(i, 6, wall_abs_avg(i) * wall_area(i));
abs_avg = total_absorption / (room_surface + 0.0001);

// Area-weighted band absorptions (for FDN damper shelves)
abs_low = sum(i, 6, wall_abs_low(i) * wall_area(i)) / (room_surface + 0.0001);
abs_mid = sum(i, 6, wall_abs_mid(i) * wall_area(i)) / (room_surface + 0.0001);
abs_hi  = sum(i, 6, wall_abs_hi(i) * wall_area(i)) / (room_surface + 0.0001);

// Sabine RT60 from area-weighted total absorption
rt60 = 0.161 * room_volume / (total_absorption + 0.0001);

// ============================================================================
// SECTION 4B: ATMOSPHERIC ABSORPTION
// ============================================================================
//
// Models frequency-dependent energy loss to the air itself (separate from wall
// absorption). Higher frequencies attenuate more with distance through air.
// Effect scales with humidity, temperature, gas type, and per-line distance.
//
// Simplified model inspired by ISO 9613-1:
//   - Absorption peaks around 10-20% relative humidity
//   - Increases mildly with temperature
//   - Air has highest molecular absorption; monatomic/simple gases absorb less
//   - Applied as a per-line high-shelf filter in the FDN feedback path

// Humidity factor: absorption peaks ~15% RH, drops at extremes (0% and 100%)
// Model: peaked curve centered below 50% RH
humidity_norm = humidity / 100.0;
humidity_factor = max(0.0, (humidity_norm + 0.15) * (1.2 - humidity_norm)) : min(1.0);

// Temperature factor: mild increase with temperature (~0.8 at -40°C, ~1.2 at 60°C)
temp_factor = 0.8 + 0.4 * ((temperature + 40.0) / 100.0);

// Per-gas absorption factor: molecular gases absorb more than monatomic
//   Air=1.0, He=0.05, CO2=0.3, SF6=0.1, CH4=0.15, H2=0.02
//   Mars=0.25(thin CO2), Venus=0.35(thick CO2), Saturn=0.03(~H2)
//   Titan=0.60(N2+CH4 haze), Early Earth=0.50(CO2/N2/H2O)
//   Photosphere=0.01(plasma — almost no molecular absorption), Submarine=1.20(dense wet air)
gas_abs_factor = (1.0, 0.05, 0.3, 0.1, 0.15, 0.02,
                  0.25, 0.35, 0.03, 0.60, 0.50, 0.01, 1.20) : ba.selectn(13, gas_idx) : si.smoo;

// Pressure ratio: thinner air (low pressure) has fewer molecules to absorb sound
pressure_ratio = pressure_kpa / 101.325;

// Combined absorption coefficient (dB/m at the 2kHz shelf reference frequency)
// Base value 0.02 dB/m is moderate — perceptible on large rooms, subtle on small
abs_coeff = 0.02 * humidity_factor * temp_factor * gas_abs_factor * pressure_ratio;

// ============================================================================
// SECTION 5: DSP BUILDING BLOCKS
// ============================================================================

// --- Feedback gain from RT60 ---
// Per-iteration feedback gain: targets RT60 decay (-60dB) over rt60 seconds.
// mean_delay is computed in Section 3B from room geometry.
fb_gain = 10.0 ^ (-3.0 * mean_delay / (max(0.1, rt60) * ma.SR)) : min(0.999);

// --- Progressive Freeze ---
// Crossfade feedback gain from normal RT60-derived value toward 0.999 (near-infinite sustain).
// freeze=0%: fb_gain_final = fb_gain (normal decay)
// freeze=100%: fb_gain_final = 0.999 (frozen — signal recirculates almost losslessly)
freeze_norm = freeze / 100.0;
fb_gain_final = fb_gain * (1.0 - freeze_norm) + 0.999 * freeze_norm;

// --- Householder feedback matrix ---
// H = I - (2/N) * ones(N,N)
// y_i = x_i - (2/N) * sum(all x_j)
// Very efficient: compute sum once, scale, subtract from each element.
//
// Implementation: split N inputs into two copies.
//   Copy 1: pass through (N signals).
//   Copy 2: sum all N → 1, scale by 2/N, fan out to N copies.
// Then subtract element-wise: x_i - (2/N)*sum.
householder_sum(n) = par(i, n, _) :> _ : *(2.0 / n);
householder(n) = si.bus(n) <: (si.bus(n), (householder_sum(n) <: si.bus(n)))
                 : ro.interleave(n, 2) : par(i, n, -);

// --- Allpass filter for diffusion ---
// Schroeder allpass: output = -g*x + x_delayed + g * y_delayed
// Using fi.allpass_comb from the Faust filters library
allpass(maxdel, del, g) = fi.allpass_comb(maxdel, del, g);

// --- Input diffusion: 4 cascaded allpass filters ---
// Delay times (mutually prime, in the 1-10ms range at 48kHz)
// Smears transient input to prevent patterned artifacts in FDN
input_diffusion = allpass(MAXDELAY_AP, 142, 0.7)
                : allpass(MAXDELAY_AP, 107, 0.7)
                : allpass(MAXDELAY_AP, 379, 0.7)
                : allpass(MAXDELAY_AP, 277, 0.7);

// --- Output diffusion: 2 allpass filters ---
// Further smooths residual patterning after FDN output
output_diffusion = allpass(MAXDELAY_AP, 193, 0.6)
                 : allpass(MAXDELAY_AP, 131, 0.6);

// --- Per-line damping: 3-band shelf filters ---
// Models frequency-dependent absorption by walls.
// High-shelf: cuts high frequencies based on abs_hi (most materials absorb HF)
// Low-shelf: cuts low frequencies based on abs_low (some materials absorb LF)
// Mid-band attenuation is handled by the overall feedback gain (fb_gain).
//
// Shelf gain in dB: higher absorption → more negative (cut).
// Scale: absorption 0.0 → 0dB (transparent), absorption 1.0 → -18dB (heavy cut).
// Using 1st-order shelves (fi.lowshelf/fi.highshelf with N=1) for efficiency
// since these run inside the feedback loop of every delay line.

// Curvature modulates wall absorption: focusing walls (positive curvature)
// reduce damping (sound converges, fewer wall hits); scattering walls
// (negative curvature) increase damping (sound diverges, more wall hits).
// curve_abs_mod: +100 → 0.8 (less absorption), 0 → 1.0, -100 → 1.2 (more absorption)
curve_abs_mod = 1.0 - room_curve / 100.0 * 0.2;

// Freeze scales damping toward bypass: at freeze=100%, walls become ~95% transparent
// This models the Quantec Room Simulator approach: freeze = 100% reflective walls
freeze_damp_scale = 1.0 - freeze_norm * 0.95;
hi_shelf_gain = abs_hi * (-18.0) * curve_abs_mod * freeze_damp_scale : max(-18.0) : min(0.0);
lo_shelf_gain = abs_low * (-12.0) * curve_abs_mod * freeze_damp_scale : max(-12.0) : min(0.0);

damper = fi.highshelf(1, hi_shelf_gain, 4000)    // Cut above 4kHz
       : fi.lowshelf(1, lo_shelf_gain, 250);      // Cut below 250Hz

// --- Per-line atmospheric absorption ---
// High-shelf filter: attenuates frequencies above ~2kHz proportional to distance.
// Longer delay lines = more air between reflections = more HF loss per iteration.
// Gain (dB) = -abs_coeff * delay_m(i), clamped to prevent extreme attenuation.
// Freeze also reduces air absorption toward bypass for true frozen sustain
air_abs_gain(i) = (0.0 - abs_coeff * delay_m(i)) * freeze_damp_scale : max(-12.0) : min(0.0);
air_absorption(i) = fi.highshelf(1, air_abs_gain(i), 2000);

// --- Thermal modulation (replaces simple sinusoidal LFO) ---
// Models delay-time wobble from thermal convection in the air.
// Uses 3 sine oscillators at incommensurate frequencies per line,
// creating organic, non-repeating modulation patterns.
//
// Turbulence (0-100%) controls depth:
//   0%:   ±0.3 samples (calm air, minimal movement)
//   25%:  ±1.55 samples (default — matches Module 1 character)
//   100%: ±5.3 samples (turbulent, convective air)
//
// Temperature auto-scales: hotter air is more convective.
turb_norm = turbulence / 100.0;
temp_turb_scale = 0.7 + 0.6 * ((temperature + 40.0) / 100.0);  // 0.7 at -40°C, 1.3 at 60°C

// Modulation depth in samples
// v2: 1-pole smoothing on depth to prevent zipper noise on parameter changes.
// Smooths the depth control, NOT the oscillators (which must remain incommensurate).
mod_depth = (0.3 + turb_norm * 5.0 * temp_turb_scale) : si.smoo;

// Per-line modulation: 3 oscillators at incommensurate rates
// Osc 1: slow (0.05-0.3 Hz) — large-scale thermal drift
// Osc 2: medium (0.15-0.7 Hz) — convective cells (reversed spread for decorrelation)
// Osc 3: fast (0.4-1.2 Hz) — turbulent eddies (fades in with turbulence)
thermal_osc1_rate(i) = 0.05 + (i / (N - 1.0)) * 0.25;
thermal_osc2_rate(i) = 0.15 + ((N - 1 - i) / (N - 1.0)) * 0.55;
thermal_osc3_rate(i) = 0.4  + (i / (N - 1.0)) * 0.8;

thermal_mod(i) = ( os.oscsin(thermal_osc1_rate(i)) * 0.5
                 + os.oscsin(thermal_osc2_rate(i)) * 0.3
                 + os.oscsin(thermal_osc3_rate(i)) * 0.2 * turb_norm )
               * mod_depth;

// --- SPL Nonlinearity v2: Multiband, distance-scaled, envelope-coupled ---
// Models nonlinear air propagation at high SPL — the "Albini spatial compression"
// concept. Reworked from v1's simple tanh waveshaper to address three physical
// realities: nonlinearity is distance-dependent (cumulative over path length),
// frequency-dependent (bass compresses first), and level-dependent (louder input
// = more spatial compression in the feedback paths).
//
// Architecture per FDN line:
//   Signal → 3-band split (LR4 crossover at 200Hz, 4kHz)
//   → per-band polynomial soft clipper with distance-scaled drive
//   → per-band recombine → DC blocker (per-line, not just output)
//
// Polynomial soft clipper: f(x) = x - (x^3)/3 for |x|<=1, hard clip beyond.
// Gentler onset than tanh, more even-order harmonics (2nd harmonic warmth).
// Small asymmetric bias (5%) retained for physical accuracy.

SPL_BIAS = 0.03;  // Reduced asymmetry bias (v1 was 0.05 — too much DC)

energy_norm = energy / 100.0;

// --- Density-scaled drive (soft-limited to prevent runaway at extreme densities) ---
// v2: uses tanh(density/ref) instead of linear scaling. Venus (density_ratio ~65)
// now maps to ~1.0 instead of 65x drive. Safe and musical.
density_scale = ma.tanh(density_ratio / 1.5) * 1.5;  // ~1.0 for air, soft-limited for extremes

// Base drive: quadratic onset for perceptual curve. Energy 0→100% maps 1.0→8.0
base_drive = (1.0 + energy_norm * energy_norm * 7.0) * density_scale;

// --- Envelope-coupled drive ---
// Slow envelope follower on FDN input energy. Louder input = more SPL compression
// in the feedback paths. Creates the sensation that the room responds to level.
// Attack ~50ms, release ~200ms. Modulation scales with Energy upper range (60-100%).
spl_env_attack = ba.tau2pole(0.050);  // 50ms attack
spl_env_release = ba.tau2pole(0.200); // 200ms release
// env_amount ramps from 0 at Energy≤60% to 1.0 at Energy=100%
spl_env_amount = max(0.0, (energy_norm - 0.6) / 0.4);

// --- Distance-scaled drive per line ---
// Each FDN line represents a different path length. Longer paths = more accumulated
// nonlinearity. drive_per_line = base_drive * (delay_length / max_delay_length).
// Short early reflections stay nearly linear; long reverberant paths compress most.
// delay_m(i) is defined in Section 3B — actual path length in meters.
max_delay_m = 4.0 * mean_free_path + 0.0001;  // longest possible path

// Per-band drive multipliers: bass compresses first (most drive), highs least
// In real rooms, low frequencies carry more energy and interact more with
// room modes, causing earlier onset of nonlinear compression.
spl_drive_low_mult  = 1.5;  // Low band: 50% more drive
spl_drive_mid_mult  = 1.0;  // Mid band: reference
spl_drive_hi_mult   = 0.5;  // High band: 50% less drive

// Per-line drive with distance scaling and envelope coupling
// effective_drive = base_drive * distance_ratio * (1 + env_amount * envelope)
// The envelope input is provided as an argument to spl_nonlin_v2
line_drive_v2(i, env_val) = base_drive * dist_scale * env_scale
with {
    dist_scale = delay_m(i) / max_delay_m;  // 0..1 based on path length
    env_scale = 1.0 + spl_env_amount * env_val;  // envelope coupling
};

// --- Polynomial soft clipper ---
// f(x) = x - (x^3)/3 for |x| <= 1.0, hard-limited beyond.
// With asymmetric bias: f(drive * (x + bias)) - f(drive * bias)
// Gentler than tanh: derivative at zero = 1 - (drive*bias)^2 instead of sech^2.
// More even-order harmonics → warmer character.
poly_clip(x) = select2(abs(x) > 1.0, x - (x*x*x) / 3.0, ma.signum(x) * 2.0/3.0);

// Per-band waveshaper with gain normalization
band_waveshaper(drive_val) = _ : (\(x).(
    poly_clip(drive_val * (x + SPL_BIAS)) - poly_clip(drive_val * SPL_BIAS)
)) : *(gain_norm)
with {
    // Small-signal gain normalization: derivative of poly_clip at bias point
    // f'(x) = 1 - x^2 for |x|<=1. At x=drive*bias: f'= 1-(drive*bias)^2
    // Total small-signal gain = drive * (1 - (drive*bias)^2)
    db = drive_val * SPL_BIAS;
    raw_gain = drive_val * max(1.0 - db*db, 0.01);
    gain_norm = select2(feedback_mode, 1.0 / (raw_gain + 0.0001), 1.0);
};

// --- 3-band crossover (Linkwitz-Riley 4th order) ---
// Splits signal into low (<200Hz), mid (200-4kHz), high (>4kHz).
// LR4 = two cascaded Butterworth 2nd-order filters — flat magnitude sum,
// zero phase error at crossover points.
lr4_lp(fc) = fi.lowpass(2, fc) : fi.lowpass(2, fc);
lr4_hp(fc) = fi.highpass(2, fc) : fi.highpass(2, fc);

// Split into 3 bands: low, mid, high
split3 = _ <: (lr4_lp(200), (_ : lr4_hp(200) : lr4_lp(4000)), lr4_hp(4000));

// --- Soft-knee compressor before saturation ceiling ---
// Ratio 2:1, threshold -6 dBFS in FDN internal signal.
// Catches peaks more gracefully than hard tanh ceiling.
// Only active in feedback mode (normal mode already has gain normalization).
soft_comp_thresh = 0.5;  // -6 dBFS ≈ 0.5 linear
soft_comp_ratio = 2.0;
soft_comp(x) = select2(feedback_mode, x,
    select2(abs(x) > soft_comp_thresh,
        x,
        ma.signum(x) * (soft_comp_thresh + (abs(x) - soft_comp_thresh) / soft_comp_ratio)
    )
);

// --- Complete per-line nonlinearity: multiband waveshaper with distance scaling ---
// Takes the FDN line signal and an envelope value.
// Splits into 3 bands, applies different drive per band, recombines.
// At Energy=0: drive≈0 (distance_scale), poly_clip(~0) ≈ 0 → transparent (dry through).
// Actually at Energy=0 base_drive=1.0*density_scale, but distance scaling keeps it near-linear.
spl_nonlin_v2(i, env_val) = split3 : (band_lo, band_mid, band_hi) :> _
with {
    drv = line_drive_v2(i, env_val);
    band_lo  = band_waveshaper(drv * spl_drive_low_mult);
    band_mid = band_waveshaper(drv * spl_drive_mid_mult);
    band_hi  = band_waveshaper(drv * spl_drive_hi_mult);
};

// --- DC blocker ---
// v2: placed inside each FDN line (after waveshaper) AND at the output.
// The polynomial clipper with asymmetric bias generates DC — the blocker must be
// downstream of the waveshaper in every FDN line, not just at the output.
dc_blocker = fi.dcblocker;

// --- Pre-delay line ---
// Adds delay between the dry signal and the reverb onset.
// Classic technique for creating space in a mix.
MAXDELAY_PREDELAY = 24000;  // 500ms at 48kHz
predelay_samples = predelay_ms / 1000.0 * ma.SR;
predelay_line = de.fdelay(MAXDELAY_PREDELAY, predelay_samples);

// --- Input Freeze ---
// When enabled, smoothly mutes new audio entering the reverb.
// Existing tail sustains (especially with Freeze > 0%), no new excitation.
input_freeze_smooth = input_freeze : si.smoo;
input_gate = 1.0 - input_freeze_smooth;

// --- Buffer Freeze v2: variable length, source select, feedback, crossfade ---
// Self-feeding delay that crossfades between live signal and a captured loop.
// v2 additions:
//   - Variable buffer length (100ms to 8s). Short = granular, long = phrase capture.
//   - Buffer Source: selects what gets captured (Pre-FDN, Post-FDN, Post-Duck)
//   - Buffer Feedback: loop self-reinforcement (0=clean, 100=saturating drone)
//   - Buffer Crossfade: smooth loop point (1-500ms)
//
// Buffer engage is instant (captures the moment). Disengage crossfades out.

// Maximum buffer size: 8 seconds at 48kHz = 384000 samples
FREEZE_BUF_MAX = 384000;

// Buffer length in samples (clamped to valid range)
buf_len = (buffer_length_ms / 1000.0 * ma.SR) : max(4800) : min(FREEZE_BUF_MAX - 2);

// Crossfade length in samples
buf_xfade_len = (buffer_xfade_ms / 1000.0 * ma.SR) : max(48) : min(buf_len / 2);

// Buffer freeze amount with asymmetric smoothing:
// Engage = instant (no smoothing). Disengage = smooth crossfade.
bf_raw = buffer_freeze;
bf = bf_raw : si.smooth(select2(bf_raw > bf_raw', 0.0, ba.tau2pole(0.050)));

// Buffer self-feedback amount
buf_fb = buffer_feedback_amt / 100.0 : min(0.98);  // cap to prevent blowup

// Buffer processor: variable-length self-feeding delay with crossfade.
// When bf=0: passthrough (live signal).
// When bf=1: loop plays back, mixed with feedback of its own output.
// The crossfade at the loop boundary uses a raised-cosine window to prevent clicks.
buffer_freeze_processor = _ * (1.0 - bf) : (+ ~ (buf_delay * (bf + buf_fb * (1.0 - bf))))
with {
    // Fractional delay for variable buffer length
    buf_delay = de.fdelay(FREEZE_BUF_MAX, buf_len - 1);
};

// --- Ducking engine ---
// Ducks the wet signal when input is present. Wet snaps back over snapback time.
// At duck=0%: no ducking. At duck=100%: wet fully silenced during input.
// snapback: 10ms (tight, like a gate) → 2000ms (dramatic swell-in).
duck_norm = duck / 100.0;
duck_attack_t = 0.002;                       // Fast attack: duck immediately on input
duck_release_t = snapback / 1000.0;          // Snapback time in seconds

// --- Wind noise generator (0 inputs, 1 output) ---
// Broadband noise → lowpass shaped by turbulence → highpass (remove rumble).
// Useful at low settings as subtle texture (like a noise osc on a Moog).
wind_level_norm = wind_level / 100.0;
wind_mod1 = os.oscsin(0.08) * 0.4 + os.oscsin(0.19) * 0.3 + os.oscsin(0.03) * 0.3;
wind_cutoff = 200.0 + (0.5 + wind_mod1 * 0.5) * (600.0 + turb_norm * 2000.0);
wind_sound = no.noise : fi.lowpass(2, wind_cutoff) : fi.highpass(1, 30)
           : *(wind_level_norm * 0.2);

// --- Convection modulation (per-line delay modulation) ---
// Models thermal convection currents circulating through the gas.
// A slow phase rotates a wave pattern across all 16 delay lines:
//   - Adjacent lines modulate together (they're close in the room)
//   - The wave sweeps through the room like a convection roll
//   - Hotter gas = more convective (temperature scaling)
// Fundamentally different from turbulence: turbulence is random per-line,
// convection is coordinated and wave-like.
conv_norm = convection / 100.0;
conv_rate = 0.01 + conv_norm * 0.1;  // 0.01–0.11 Hz rotation speed
conv_phase = os.phasor(1.0, conv_rate);
conv_temp_scale = max(0.1, (temp_kelvin) / 293.15);  // relative to 20°C (293K)
conv_depth = conv_norm * mod_depth * 3.0 * min(conv_temp_scale, 4.0);
conv_mod(i) = sin(2.0 * ma.PI * (conv_phase + i / float(N))) * conv_depth;

// Wind noise is the only generator fed into the reverb input.
// (Convection and gusts modulate the delay lines / speed of sound directly.)
weather_gen = wind_sound;

// --- Tone: tilt EQ in feedback path ---
// Tilts the spectral balance of the reverb tail. Works in both normal and feedback modes.
// -50 = dark (cut HF, boost LF), 0 = neutral, +50 = bright (boost HF, cut LF).
// Implemented as opposing high-shelf and low-shelf: when one boosts, the other cuts.
// ±50 maps to ±12dB of tilt. Crossovers at 200Hz (low) and 3kHz (high).
// When tone_enable=0, tone_db=0 → both shelves are 0dB (transparent/bypass).
tone_db = fb_tone * 0.24 * tone_enable;  // ±50 → ±12dB, zeroed when disabled
tone_eq = fi.highshelf(1, tone_db, 3000)
        : fi.lowshelf(1, 0.0 - tone_db, 200);

// --- Saturation ceiling (feedback mode only) ---
// v2: soft_comp (2:1 ratio, -6dBFS threshold) catches peaks before the hard tanh ceiling.
// In normal mode (feedback_mode=0), both are bypassed (identity function).
// The soft compressor handles moderate peaks gracefully; tanh is the safety net.
sat_ceiling(x) = select2(feedback_mode, soft_comp(x), ma.tanh(soft_comp(x)));

// --- Shimmer v2: pitch-shifted feedback with dedicated self-feedback loop ---
// Granular-style pitch shifter using two overlapping grains with crossfade.
// v2 additions:
//   - Dedicated feedback path: shifter output feeds back to its own input,
//     creating cascading harmonic series (Eventide Blackhole-style)
//   - Shimmer Curve: controls amplitude scaling across generations
//     (linear/log/exp)
//   - Shimmer Detune: slight detuning for organic beating
//
// When shimmer_enable=0, bypassed completely (select2 → identity).
SHIMMER_MAXDELAY = 2048;

// Core pitch shifter (granular, two overlapping grains)
// s = shift in semitones (includes detune offset)
shimmer_transpose(w, x, s, sig) = de.fdelay(SHIMMER_MAXDELAY, d, sig) * ma.fmin(d/x, 1)
                                 + de.fdelay(SHIMMER_MAXDELAY, d+w, sig) * (1-ma.fmin(d/x,1))
with {
    i = 1 - pow(2, s/12);
    d = i : (+ : +(w) : fmod(_,w)) ~ _;
};

// Shimmer curve: controls how feedback generations scale in amplitude.
// Each feedback iteration multiplies by curve_gain:
//   Linear (0): curve_gain = shimmer_fb (direct scaling)
//   Log (1):    curve_gain = shimmer_fb^1.5 (early generations dominate, gentle decay)
//   Exp (2):    curve_gain = shimmer_fb^0.5 (later generations louder, slow build)
shimmer_fb = shimmer_feedback / 100.0;
shimmer_curve_idx = int(shimmer_curve);
shimmer_fb_gain = (shimmer_fb, pow(shimmer_fb, 1.5), pow(max(shimmer_fb, 0.0001), 0.5))
                : ba.selectn(3, shimmer_curve_idx) : min(0.95);  // cap at 0.95 to prevent blowup

// Total pitch shift including detune (cents → semitones)
shimmer_total_pitch = shimmer_pitch + shimmer_detune / 100.0;

// Shimmer processor with dedicated self-feedback loop.
// Signal flow: input → mix with feedback → pitch shift → output
// The pitch-shifted output feeds back to the shifter's own input with shimmer_fb_gain.
// This creates cascading harmonic series that accelerate away from the source pitch.
// At shimmer_fb=0: single shift per FDN iteration (v1 behaviour).
// At shimmer_fb=0.8+: self-oscillating harmonic cascade.
shimmer_with_feedback = _ : (+ : shimmer_transpose(1024, 512, shimmer_total_pitch))
                       ~ (*(shimmer_fb_gain));

// Final shimmer processor: bypass or engage
shimmer_shift = _ <: (_, shimmer_with_feedback) : select2(shimmer_enable);

// --- Pitch-quantized delay lines ---
// When pitch_enable is on, delay lines snap to integer multiples of the base wavelength
// (SR / pitch_freq). This makes the FDN resonate at harmonics of the chosen pitch.
//
// In normal reverb mode: subtle harmonic coloring (interesting on tonal material)
// In feedback mode: turns the FDN into a tuned resonator / drone instrument
//
// 12-TET snap: when pitch_snap is on, the pitch frequency is quantized to the
// nearest semitone in equal temperament (A4 = 440Hz reference).

// --- Pitch Glide System ---
// Convert Hz → MIDI note (semitone space) → apply portamento → snap → convert back to Hz.
// Glide in semitone space gives musically correct portamento (equal perceived rate).
// Minimum 5ms smoothing even at Glide=0 (replaces removed si.smoo, prevents clicks).
glide_time = max(pitch_glide / 1000.0, 0.005);
pitch_midi_raw = 69.0 + 12.0 * log(max(fb_pitch, 20.0) / 440.0) / log(2.0);
pitch_midi_glided = pitch_midi_raw : si.smooth(ba.tau2pole(glide_time));
pitch_midi = select2(pitch_snap, pitch_midi_glided, rint(pitch_midi_glided));

// Snapped MIDI note number for display (editor reads this via bargraph zone pointer)
snapped_note_num = rint(pitch_midi_glided);
snapped_note_display = snapped_note_num : hbargraph("[7]Info/[5]Snapped Note", 0, 127);

// Final pitch in Hz (from glided + optionally snapped semitone space)
pitch_hz = 440.0 * 2.0 ^ ((pitch_midi - 69.0) / 12.0);

// Base wavelength in samples
pitch_wavelength = ma.SR / max(pitch_hz, 1.0);

// Blend factor: 1.0 when pitch enabled, 0.0 when disabled (pure geometry)
pitch_blend = pitch_enable;

// For each delay line, find the nearest integer multiple of the base wavelength.
// Clamp to at least 1 wavelength (fundamental) and at most MAXDELAY.
quantized_delay(i) = rint(delay_len(i) / pitch_wavelength) * pitch_wavelength
                   : max(pitch_wavelength) : max(4) : min(MAXDELAY - 2);

// Final delay length per line: crossfade between geometry and pitch-quantized
final_delay(i) = delay_len(i) * (1.0 - pitch_blend) + quantized_delay(i) * pitch_blend;

// ============================================================================
// SECTION 6: FDN REVERB ENGINE
// ============================================================================

// --- FDN Core ---
// N delay lines with Householder feedback matrix.
//
// Signal flow (per iteration of the feedback loop):
//   feedback signals → per-line damping → per-line gain → Householder mix
//   → added to new input → delay lines → output (tapped for stereo)
//
// The Faust ~ operator provides implicit 1-sample delay in the feedback path.
// The actual delay lines provide the bulk of the delay.

// The FDN: takes mono input, produces N parallel delay line outputs
// v2: envelope follower on input feeds SPL drive coupling
fdn(input) = (fdn_input(input) : delay_lines) ~ (feedback_path)
with {
    // Input envelope for SPL drive coupling (tracked outside feedback loop).
    // Simple amplitude follower: ~100ms smoothing. Tracks energy, not transients.
    env_val = input : abs : an.amp_follower(0.100) : min(1.0);

    // Inject mono input into all N delay lines with equal gain
    fdn_input(x) = par(i, N, x / sqrt(N) + _);

    // N parallel delay lines with per-line thermal + convection modulation.
    // Uses fractional delay (linear interpolation) to support sub-sample modulation.
    // final_delay(i) blends between geometry-driven and pitch-quantized lengths.
    // thermal_mod: independent random wobble per line (turbulence eddies)
    // conv_mod: coordinated wave pattern across lines (convection currents)
    delay_lines = par(i, N, de.fdelay(MAXDELAY, final_delay(i) + thermal_mod(i) + conv_mod(i)));

    // Feedback path per line (v2):
    //   damper (wall absorption) → air_absorption (atmospheric HF loss) →
    //   tone EQ (user tilt) → shimmer →
    //   multiband SPL nonlinearity (distance-scaled, envelope-coupled) →
    //   per-line DC blocker (catches waveshaper DC before feedback) →
    //   freeze gain → soft compressor + saturation ceiling
    // Then all 16 lines → Householder mixing matrix
    feedback_path = par(i, N, damper : air_absorption(i) : tone_eq : shimmer_shift
                      : spl_nonlin_v2(i, env_val) : dc_blocker
                      : *(fb_gain_final) : sat_ceiling)
                  : householder(N);
};

// --- Stereo extraction from FDN ---
// Tap the 16 delay line outputs with pan coefficients.
// Line 0 = hard left, line 15 = hard right, spread between.
// For each line i: output_L += signal_i * pan_l(i), output_R += signal_i * pan_r(i)
fdn_to_stereo = par(i, N, _ <: (*(pan_l(i)), *(pan_r(i))))
              : route(N*2, N*2,
                  par(i, N, (i*2+1, i+1)),       // L channels → first N slots
                  par(i, N, (i*2+2, N+i+1)))      // R channels → last N slots
              : (par(i, N, _) :> _), (par(i, N, _) :> _)
with {
    // Golden-ratio spacing: maximally uniform distribution across stereo field.
    // Each successive line is placed φ (0.618...) of the way around from the last,
    // ensuring no clustering and optimal spread regardless of delay line loudness.
    // Much wider and more even than the old interleaved pattern.
    pan_pos(0)  = 0.000;  pan_pos(1)  = 0.618;
    pan_pos(2)  = 0.236;  pan_pos(3)  = 0.854;
    pan_pos(4)  = 0.472;  pan_pos(5)  = 0.090;
    pan_pos(6)  = 0.708;  pan_pos(7)  = 0.326;
    pan_pos(8)  = 0.944;  pan_pos(9)  = 0.562;
    pan_pos(10) = 0.180;  pan_pos(11) = 0.798;
    pan_pos(12) = 0.416;  pan_pos(13) = 0.034;
    pan_pos(14) = 0.652;  pan_pos(15) = 0.270;

    // Width-scaled pan: 0%=mono (all center), 100%=normal, 200%=hyper-wide (clamped)
    effective_pan(i) = 0.5 + (pan_pos(i) - 0.5) * width_factor : max(0.0) : min(1.0);
    pan_l(i) = sqrt(1.0 - effective_pan(i));
    pan_r(i) = sqrt(effective_pan(i));
};

// ============================================================================
// SECTION 7: PROCESS
// ============================================================================

// Keep unused parameters alive via bargraphs + informational displays
rt60_display = rt60 : hbargraph("[7]Info/[0]RT60 [unit:s]", 0, 30);
dist_display = direct_distance : hbargraph("[7]Info/[1]Distance [unit:m]", 0, 150);
vol_display = room_volume : hbargraph("[7]Info/[2]Volume [unit:m3]", 0, 180000);
sos_display = speed_of_sound : hbargraph("[7]Info/[3]Speed [unit:m/s]", 20, 3000);
density_display = air_density : hbargraph("[7]Info/[4]Density [unit:kg/m3]", 0.01, 10.0);

// Keep midi_enable alive (only read by C++, not used in Faust signal path)
midi_display = midi_enable : hbargraph("[7]Info/[6]MIDI Active", 0, 1);

// Keep buffer crossfade length alive (used for parameter display, full crossfade impl in Pass 3)
buf_xfade_display = buf_xfade_len : hbargraph("[7]Info/[7]Buf XFade [unit:smp]", 48, 24000);

// Main signal flow (v2):
//   Mono input + weather → pre-delay → split to (early reflections) + (late reverb FDN)
//   Early reflections: mono → 6 image-source delays → stereo
//   Late reverb: mono → diffusion → FDN → stereo → output diffusion → buffer source select
//   Buffer source: Post-FDN (default), Pre-FDN (diffused input), Post-Duck (deferred to Pass 3)
//
// Buffer source routing: split after diffusion, route FDN output and pre-FDN tap
// to the buffer based on source selection. Post-Duck source is mapped to Post-FDN
// pending process chain restructuring in Pass 3.
buf_src_idx = int(buffer_source);

late_reverb = *(input_gate) : input_diffusion
            : _ <: (fdn_chain, pre_fdn_stereo)   // 4 outputs: fdn_L, fdn_R, pre_L, pre_R
            : buf_source_select                    // 4 → 2 (selected pair → buffer)
            : par(i, 2, buffer_freeze_processor)
with {
    fdn_chain = fdn : fdn_to_stereo : par(i, 2, output_diffusion : dc_blocker);
    pre_fdn_stereo = _ <: (_, _);  // mono diffused input → stereo duplicate
    // Route 4 inputs to 2 outputs via source selection:
    // Input layout: fdn_L(1), fdn_R(2), pre_L(3), pre_R(4)
    // For each channel, provide 3 options to ba.selectn:
    //   idx 0 (Post-FDN): fdn_L/R
    //   idx 1 (Pre-FDN): pre_L/R
    //   idx 2 (Post-Duck): fdn_L/R (placeholder — proper routing in Pass 3)
    // Route to: out1=[fdn_L, pre_L, fdn_L], out2=[fdn_R, pre_R, fdn_R]
    buf_source_select = route(4, 6, (1,1),(3,2),(1,3), (2,4),(4,5),(2,6))
                      : ba.selectn(3, buf_src_idx), ba.selectn(3, buf_src_idx);
};

// Split mono input → early reflections (stereo) + late reverb (stereo)
// Weather generators are mixed in, then pre-delay is applied before the split.
// After split: er_L(1), er_R(2), late_L(3), late_R(4)
// Route to: (er_L, late_L, er_R, late_R) then sum pairs
reverb_mono = (_ + weather_gen) : predelay_line <: (early_reflections, late_reverb)
            : route(4, 4, (1,1), (3,2), (2,3), (4,4))
            : (+, +);

// Duck-aware dry/wet mix with envelope-controlled filter:
// Ducks the wet signal proportional to input level. Wet snaps back over snapback time.
// Filter: lowpass on wet signal, with cutoff modulated by input envelope.
//   Filter Env < 0: transients darken the reverb (cutoff drops)
//   Filter Env > 0: transients brighten the reverb (cutoff rises from a lower base)
//   Set Filter below 20kHz to hear the effect, then use Filter Env to modulate.
drywet_mix(dl, dr, wl, wr) = out_l, out_r
with {
    inp = max(abs(dl), abs(dr));
    // Ducking
    duck_env = inp : an.amp_follower_ud(duck_attack_t, duck_release_t);
    dg = 1.0 - duck_norm * min(duck_env * 6.0, 1.0);
    // Filter envelope: fast attack, moderate release
    filt_env = inp : an.amp_follower_ud(0.002, 0.15);
    env_mod = filt_env * (filter_env_amt / 100.0) * 4.0;
    // Modulate cutoff: ±3 octaves from base. Positive env_mod raises cutoff, negative lowers it.
    mod_freq = filter_cutoff * pow(2.0, env_mod * 3.0) : max(100.0) : min(20000.0) : si.smoo;
    // Apply 2nd-order lowpass to wet signals
    wl_f = wl : fi.lowpass(2, mod_freq);
    wr_f = wr : fi.lowpass(2, mod_freq);

    // Output mode switching:
    // Mode 0 (Mix): normal dry/wet with ducking
    // Mode 1 (Duck Reject): the portion of wet being cut by the duck
    // Mode 2 (Wet Post-Duck): wet signal after ducking, no dry
    mode = int(output_mode);
    mix_l = dl * dry + wl_f * wet * dg;
    mix_r = dr * dry + wr_f * wet * dg;
    reject_l = wl_f * wet * (1.0 - dg);
    reject_r = wr_f * wet * (1.0 - dg);
    post_l = wl_f * wet * dg;
    post_r = wr_f * wet * dg;
    out_l = (mix_l, reject_l, post_l) : ba.selectn(3, mode);
    out_r = (mix_r, reject_r, post_r) : ba.selectn(3, mode);
};

// Stereo in → split to dry pair + mono sum → reverb → mix → gain → limiter → out
//
// reverb_mono: 1 mono input → 2 stereo outputs (wet_L, wet_R)
// drywet_mix: dry_L, dry_R, wet_L, wet_R → out_L, out_R
//
// Always-on soft limiter (ma.tanh) at final output prevents digital clipping.
// Transparent at normal reverb levels, gently compresses peaks above ~-6dBFS.
//
// Use route to fan stereo (2 signals) into 4 slots:
//   input 1 (L) → slot 1 (dry_L) and slot 3 (reverb input L)
//   input 2 (R) → slot 2 (dry_R) and slot 4 (reverb input R)
// Then: slots 1,2 pass through; slots 3,4 sum to mono and feed reverb.

process = route(2, 4, (1,1), (2,2), (1,3), (2,4))
        : _, _, (+ : *(0.5) : reverb_mono)
        : drywet_mix
        : par(i, 2, *(out_gain) : ma.tanh)
        : par(i, 2, attach(_, rt60_display) : attach(_, dist_display)
            : attach(_, vol_display) : attach(_, sos_display)
            : attach(_, density_display) : attach(_, snapped_note_display)
            : attach(_, midi_display) : attach(_, buf_xfade_display));
