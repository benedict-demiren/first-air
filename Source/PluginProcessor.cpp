#include "PluginProcessor.h"
#include "PluginEditor.h"

// Faust headers
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "faust/gui/MapUI.h"

// Generated Faust DSP
#include "FirstAirDSP.h"

// ============================================================================
// Helper: UI visitor that collects parameter metadata from a Faust DSP
// ============================================================================

struct FaustParamCollector : public UI {
    struct ParamEntry {
        std::string path;
        std::string label;
        float init, min, max, step;
        bool isOutput;
        FAUSTFLOAT* zone;
        bool isToggle = false;
    };

    std::vector<ParamEntry> params;
    std::vector<std::string> groupStack;

    void pushGroup(const char* label) {
        groupStack.push_back(label);
    }
    void popGroup() {
        if (!groupStack.empty()) groupStack.pop_back();
    }

    std::string currentPath(const char* label) {
        std::string path;
        for (auto& g : groupStack) {
            if (!g.empty()) {
                path += g + "/";
            }
        }
        path += label;
        return path;
    }

    // Convert a Faust path like "firstair/Space/Length" to a JUCE-friendly ID like "space_length"
    // Strips the top-level plugin name box ("firstair/") which Faust prepends with -vec flag.
    static std::string pathToId(const std::string& path) {
        // Strip the "firstair/" top-level prefix (Faust wraps everything in a box named after the DSP)
        std::string cleanPath = path;
        const std::string prefix = "firstair/";
        if (cleanPath.size() > prefix.size() &&
            cleanPath.compare(0, prefix.size(), prefix) == 0) {
            cleanPath = cleanPath.substr(prefix.size());
        }

        std::string id;
        for (char c : cleanPath) {
            if (c == '/') id += '_';
            else if (c == ' ') id += '_';
            else if (std::isalnum(static_cast<unsigned char>(c))) id += std::tolower(static_cast<unsigned char>(c));
        }
        return id;
    }

    // Layouts
    void openTabBox(const char* label) override { pushGroup(label); }
    void openHorizontalBox(const char* label) override { pushGroup(label); }
    void openVerticalBox(const char* label) override { pushGroup(label); }
    void closeBox() override { popGroup(); }

    // Active widgets (inputs)
    void addButton(const char* label, FAUSTFLOAT* zone) override {
        params.push_back({currentPath(label), label, 0, 0, 1, 1, false, zone});
    }
    void addCheckButton(const char* label, FAUSTFLOAT* zone) override {
        ParamEntry entry = {currentPath(label), label, 0, 0, 1, 1, false, zone};
        entry.isToggle = true;
        params.push_back(entry);
    }
    void addVerticalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) override {
        params.push_back({currentPath(label), label, init, min, max, step, false, zone});
    }
    void addHorizontalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) override {
        params.push_back({currentPath(label), label, init, min, max, step, false, zone});
    }
    void addNumEntry(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) override {
        params.push_back({currentPath(label), label, init, min, max, step, false, zone});
    }

    // Passive widgets (outputs / bargraphs)
    void addHorizontalBargraph(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT min, FAUSTFLOAT max) override {
        params.push_back({currentPath(label), label, 0, min, max, 0, true, zone});
    }
    void addVerticalBargraph(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT min, FAUSTFLOAT max) override {
        params.push_back({currentPath(label), label, 0, min, max, 0, true, zone});
    }

    // Soundfile (unused)
    void addSoundfile(const char*, const char*, Soundfile**) override {}

    // Metadata (unused for now)
    void declare(FAUSTFLOAT*, const char*, const char*) override {}
};

// ============================================================================
// Static helper: create parameter layout by querying a temporary Faust DSP
// ============================================================================

juce::AudioProcessorValueTreeState::ParameterLayout FirstAirProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Create a temporary DSP to discover parameters (heap-allocated — the Faust
    // class contains large delay buffers that would overflow the thread stack)
    auto tempDSP = std::make_unique<FirstAirDSP>();
    tempDSP->init(48000);  // Sample rate doesn't matter for UI discovery

    FaustParamCollector collector;
    tempDSP->buildUserInterface(&collector);

    for (auto& p : collector.params) {
        if (p.isOutput) continue;  // Skip bargraphs (read-only displays)

        std::string id = FaustParamCollector::pathToId(p.path);

        // Faust flattens group paths into widget labels (e.g. "Material/Wall Material").
        // Extract the short display name (after the last '/').
        std::string displayName = p.label;
        auto lastSlash = p.label.rfind('/');
        if (lastSlash != std::string::npos)
            displayName = p.label.substr(lastSlash + 1);

        // Special case: discrete choice parameters (menus)
        // Material choices (11 materials): any param ending in "Wall", "Floor", or "Ceiling"
        static const juce::StringArray materialChoices {
            "Concrete", "Glass", "Wood", "Plaster", "Carpet", "Metal",
            "Brick", "Tile", "Fabric", "Foam", "Marble"
        };
        bool isMaterialChoice = (displayName == "Left Wall" || displayName == "Right Wall" ||
                                 displayName == "Front Wall" || displayName == "Back Wall" ||
                                 displayName == "Floor" || displayName == "Ceiling");
        if (isMaterialChoice) {
            layout.add(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID{id, 1},
                displayName,
                materialChoices,
                static_cast<int>(p.init)
            ));
        } else if (displayName == "Gas") {
            layout.add(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID{id, 1},
                displayName,
                juce::StringArray{"Air", "Helium", "CO2", "SF6", "Methane", "Hydrogen",
                                  "Mars", "Venus", "Saturn", "Titan", "Early Earth",
                                  "Photosphere", "Submarine"},
                static_cast<int>(p.init)
            ));
        } else if (displayName == "Output Mode") {
            layout.add(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID{id, 1},
                displayName,
                juce::StringArray{"Mix", "Duck Reject", "Wet Post-Duck"},
                static_cast<int>(p.init)
            ));
        } else if (p.isToggle) {
            layout.add(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID{id, 1},
                displayName,
                p.init > 0.5f
            ));
        } else {
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{id, 1},
                displayName,
                juce::NormalisableRange<float>(p.min, p.max, p.step),
                p.init
            ));
        }
    }

    return layout;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

FirstAirProcessor::FirstAirProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // Create the real Faust DSP and UI
    faustDSP = std::make_unique<FirstAirDSP>();
    faustUI = std::make_unique<MapUI>();
    faustDSP->buildUserInterface(faustUI.get());

    // Build the paramInfos vector by re-collecting from the live DSP
    FaustParamCollector collector;
    faustDSP->buildUserInterface(&collector);

    for (auto& p : collector.params) {
        FaustParamInfo info;
        // Store the clean path (pathToId already strips "firstair/" prefix)
        info.path = p.path;
        info.juceId = FaustParamCollector::pathToId(p.path);
        info.init = p.init;
        info.min = p.min;
        info.max = p.max;
        info.step = p.step;
        info.zone = p.zone;
        info.isOutput = p.isOutput;
        info.isToggle = p.isToggle;

        // Faust flattens group paths into widget labels (e.g. "Material/Wall Material").
        // Extract group (first component) and short label (last component).
        std::string shortLabel = p.label;
        std::string group;
        auto firstSlash = p.label.find('/');
        if (firstSlash != std::string::npos) {
            group = p.label.substr(0, firstSlash);
            auto lastSlash = p.label.rfind('/');
            shortLabel = p.label.substr(lastSlash + 1);
        }
        info.label = shortLabel;
        info.group = group;

        // Detect choice parameters (per-wall materials, Gas)
        if (shortLabel == "Left Wall" || shortLabel == "Right Wall" ||
            shortLabel == "Front Wall" || shortLabel == "Back Wall" ||
            shortLabel == "Floor" || shortLabel == "Ceiling") {
            info.isChoice = true;
            info.numChoices = 11;
        } else if (shortLabel == "Gas") {
            info.isChoice = true;
            info.numChoices = 13;
        } else if (shortLabel == "Output Mode") {
            info.isChoice = true;
            info.numChoices = 3;
        }

        paramInfos.push_back(info);
    }

    // Cache zone pointers for MIDI pitch override
    for (auto& info : paramInfos) {
        if (info.juceId == "energy_midi")      midiEnableZone = info.zone;
        if (info.juceId == "energy_pitch")     fbPitchZone = info.zone;
        if (info.juceId == "energy_pitch_on")  pitchEnableZone = info.zone;
    }
}

FirstAirProcessor::~FirstAirProcessor() = default;

// ============================================================================
// Audio lifecycle
// ============================================================================

void FirstAirProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    faustDSP->init(static_cast<int>(sampleRate));

    // Rebuild UI mapping after init (zones remain the same but just to be safe)
    faustUI = std::make_unique<MapUI>();
    faustDSP->buildUserInterface(faustUI.get());

    // Re-collect zone pointers
    FaustParamCollector collector;
    faustDSP->buildUserInterface(&collector);
    for (size_t i = 0; i < collector.params.size() && i < paramInfos.size(); ++i) {
        paramInfos[i].zone = collector.params[i].zone;
    }

    // Re-cache MIDI zone pointers after init (zones may have changed)
    midiEnableZone = nullptr;
    fbPitchZone = nullptr;
    pitchEnableZone = nullptr;
    for (auto& info : paramInfos) {
        if (info.juceId == "energy_midi")      midiEnableZone = info.zone;
        if (info.juceId == "energy_pitch")     fbPitchZone = info.zone;
        if (info.juceId == "energy_pitch_on")  pitchEnableZone = info.zone;
    }

    // Clear MIDI note stack on prepare
    midiNoteStack.clear();
}

void FirstAirProcessor::releaseResources() {}

bool FirstAirProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Support stereo in → stereo out, or mono in → stereo out
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    auto inputSet = layouts.getMainInputChannelSet();
    return inputSet == juce::AudioChannelSet::stereo()
        || inputSet == juce::AudioChannelSet::mono();
}

// ============================================================================
// Parameter sync
// ============================================================================

void FirstAirProcessor::syncParametersToFaust()
{
    for (auto& info : paramInfos) {
        if (info.isOutput || info.zone == nullptr) continue;

        if (info.isChoice) {
            auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(
                apvts.getParameter(info.juceId));
            if (choiceParam != nullptr) {
                *info.zone = static_cast<float>(choiceParam->getIndex());
            }
        } else if (info.isToggle) {
            auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(
                apvts.getParameter(info.juceId));
            if (boolParam != nullptr) {
                *info.zone = boolParam->get() ? 1.0f : 0.0f;
            }
        } else {
            auto* param = apvts.getRawParameterValue(info.juceId);
            if (param != nullptr) {
                *info.zone = param->load();
            }
        }
    }
}

// ============================================================================
// Process block
// ============================================================================

void FirstAirProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    syncParametersToFaust();

    // --- MIDI pitch control ---
    // Process MIDI events: last-note-priority monophonic stack.
    // When midi_enable AND pitch_enable are on and notes are held,
    // override the fb_pitch zone pointer with the MIDI note frequency.
    for (const auto metadata : midiMessages) {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn()) {
            // Remove duplicate if already in stack, then push
            auto it = std::find(midiNoteStack.begin(), midiNoteStack.end(), msg.getNoteNumber());
            if (it != midiNoteStack.end())
                midiNoteStack.erase(it);
            midiNoteStack.push_back(msg.getNoteNumber());
        } else if (msg.isNoteOff()) {
            auto it = std::find(midiNoteStack.begin(), midiNoteStack.end(), msg.getNoteNumber());
            if (it != midiNoteStack.end())
                midiNoteStack.erase(it);
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            midiNoteStack.clear();
        }
    }

    // Override Faust pitch zone when MIDI is active + pitch enabled + notes held
    if (midiEnableZone != nullptr && fbPitchZone != nullptr && pitchEnableZone != nullptr) {
        if (*midiEnableZone > 0.5f && *pitchEnableZone > 0.5f && !midiNoteStack.empty()) {
            int note = midiNoteStack.back();
            float freq = 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
            *fbPitchZone = freq;
        }
    }

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    int numSamples = buffer.getNumSamples();

    // If mono input, duplicate to stereo
    if (totalNumInputChannels == 1 && totalNumOutputChannels == 2) {
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
    }

    // Copy input to separate buffer so Faust reads from a clean copy.
    // Faust compute() may overwrite output (aliased to input) before
    // all input samples are consumed, corrupting the signal.
    juce::AudioBuffer<float> inputCopy(buffer);

    float* inputs[2] = { inputCopy.getWritePointer(0), inputCopy.getWritePointer(1) };
    float* outputs[2] = { buffer.getWritePointer(0), buffer.getWritePointer(1) };

    faustDSP->compute(numSamples, inputs, outputs);
}

// ============================================================================
// Editor
// ============================================================================

juce::AudioProcessorEditor* FirstAirProcessor::createEditor()
{
    return new FirstAirEditor(*this);
}

// ============================================================================
// Factory presets
// ============================================================================

struct FactoryPreset {
    const char* name;
    std::map<std::string, float> values;  // juceId → value
};

// Bool params: 0.0 = off, 1.0 = on
// Material choices (per-wall): 0=Concrete, 1=Glass, 2=Wood, 3=Plaster, 4=Carpet,
//   5=Metal, 6=Brick, 7=Tile, 8=Fabric, 9=Foam, 10=Marble
// Gas choices: 0=Air, 1=Helium, 2=CO2, 3=SF6, 4=Methane, 5=Hydrogen,
//   6=Mars, 7=Venus, 8=Saturn, 9=Titan, 10=Early Earth, 11=Photosphere, 12=Submarine
static const std::vector<FactoryPreset> factoryPresets = {
    {"Default Room", {
        {"space_length", 8.0f}, {"space_width", 5.0f}, {"space_height", 3.5f},
        {"space_skew", 0.0f}, {"space_curvature", 0.0f},
        {"material_left_wall", 2.0f}, {"material_right_wall", 2.0f},
        {"material_front_wall", 2.0f}, {"material_back_wall", 2.0f},
        {"material_floor", 2.0f}, {"material_ceiling", 3.0f},
        {"energy_energy", 0.0f}, {"energy_feedback", 0.0f},
        {"energy_tone_on", 0.0f}, {"energy_tone", 0.0f},
        {"energy_pitch_on", 0.0f}, {"energy_midi", 0.0f},
        {"energy_pitch", 20.0f}, {"energy_glide", 0.0f}, {"energy_snap", 0.0f},
        {"energy_freeze", 0.0f}, {"energy_input_freeze", 0.0f}, {"energy_buffer_freeze", 0.0f},
        {"energy_shimmer_on", 0.0f}, {"energy_shimmer", 12.0f},
        {"position_source_x", 0.5f}, {"position_source_y", 0.3f}, {"position_source_z", 0.5f},
        {"position_listener_x", 0.5f}, {"position_listener_y", 0.7f}, {"position_listener_z", 0.5f},
        {"mix_dry_wet", 30.0f}, {"mix_output_level", 0.0f},
        {"mix_predelay", 0.0f}, {"mix_duck", 0.0f}, {"mix_snapback", 200.0f},
        {"mix_width", 100.0f}, {"mix_filter", 20000.0f}, {"mix_filter_env", 0.0f},
        {"mix_output_mode", 0.0f},
        {"atmosphere_temperature", 20.0f}, {"atmosphere_gas", 0.0f},
        {"atmosphere_humidity", 40.0f}, {"atmosphere_pressure", 101.325f},
        {"atmosphere_turbulence", 25.0f},
        {"dynamics_wind", 0.0f}, {"dynamics_convection", 0.0f}, {"dynamics_gusts", 0.0f}
    }},
    {"Cathedral", {
        {"space_length", 60.0f}, {"space_width", 25.0f}, {"space_height", 28.0f},
        {"space_skew", 0.0f}, {"space_curvature", 0.0f},
        {"material_left_wall", 0.0f}, {"material_right_wall", 0.0f},
        {"material_front_wall", 0.0f}, {"material_back_wall", 0.0f},
        {"material_floor", 10.0f}, {"material_ceiling", 0.0f},
        {"energy_energy", 12.0f}, {"energy_feedback", 0.0f},
        {"energy_tone_on", 1.0f}, {"energy_tone", -8.0f},
        {"energy_pitch_on", 0.0f}, {"energy_midi", 0.0f},
        {"energy_pitch", 20.0f}, {"energy_glide", 0.0f}, {"energy_snap", 0.0f},
        {"energy_freeze", 0.0f}, {"energy_input_freeze", 0.0f}, {"energy_buffer_freeze", 0.0f},
        {"energy_shimmer_on", 0.0f}, {"energy_shimmer", 12.0f},
        {"position_source_x", 0.3f}, {"position_source_y", 0.2f}, {"position_source_z", 0.4f},
        {"position_listener_x", 0.5f}, {"position_listener_y", 0.7f}, {"position_listener_z", 0.35f},
        {"mix_dry_wet", 45.0f}, {"mix_output_level", -3.0f},
        {"mix_predelay", 30.0f}, {"mix_duck", 0.0f}, {"mix_snapback", 200.0f},
        {"mix_width", 120.0f}, {"mix_filter", 12000.0f}, {"mix_filter_env", 0.0f},
        {"mix_output_mode", 0.0f},
        {"atmosphere_temperature", 15.0f}, {"atmosphere_gas", 0.0f},
        {"atmosphere_humidity", 60.0f}, {"atmosphere_pressure", 101.325f},
        {"atmosphere_turbulence", 15.0f},
        {"dynamics_wind", 0.0f}, {"dynamics_convection", 0.0f}, {"dynamics_gusts", 0.0f}
    }},
    {"Small Studio", {
        {"space_length", 4.0f}, {"space_width", 3.5f}, {"space_height", 2.8f},
        {"space_skew", 0.0f}, {"space_curvature", 0.0f},
        {"material_left_wall", 2.0f}, {"material_right_wall", 2.0f},
        {"material_front_wall", 8.0f}, {"material_back_wall", 8.0f},
        {"material_floor", 4.0f}, {"material_ceiling", 3.0f},
        {"energy_energy", 0.0f}, {"energy_feedback", 0.0f},
        {"energy_tone_on", 1.0f}, {"energy_tone", 5.0f},
        {"energy_pitch_on", 0.0f}, {"energy_midi", 0.0f},
        {"energy_pitch", 20.0f}, {"energy_glide", 0.0f}, {"energy_snap", 0.0f},
        {"energy_freeze", 0.0f}, {"energy_input_freeze", 0.0f}, {"energy_buffer_freeze", 0.0f},
        {"energy_shimmer_on", 0.0f}, {"energy_shimmer", 12.0f},
        {"position_source_x", 0.5f}, {"position_source_y", 0.4f}, {"position_source_z", 0.5f},
        {"position_listener_x", 0.5f}, {"position_listener_y", 0.6f}, {"position_listener_z", 0.5f},
        {"mix_dry_wet", 20.0f}, {"mix_output_level", 0.0f},
        {"mix_predelay", 5.0f}, {"mix_duck", 0.0f}, {"mix_snapback", 200.0f},
        {"mix_width", 80.0f}, {"mix_filter", 15000.0f}, {"mix_filter_env", 0.0f},
        {"mix_output_mode", 0.0f},
        {"atmosphere_temperature", 22.0f}, {"atmosphere_gas", 0.0f},
        {"atmosphere_humidity", 35.0f}, {"atmosphere_pressure", 101.325f},
        {"atmosphere_turbulence", 10.0f},
        {"dynamics_wind", 0.0f}, {"dynamics_convection", 0.0f}, {"dynamics_gusts", 0.0f}
    }},
    {"Metal Tank", {
        {"space_length", 3.0f}, {"space_width", 3.0f}, {"space_height", 4.0f},
        {"space_skew", 0.0f}, {"space_curvature", 25.0f},
        {"material_left_wall", 5.0f}, {"material_right_wall", 5.0f},
        {"material_front_wall", 5.0f}, {"material_back_wall", 5.0f},
        {"material_floor", 5.0f}, {"material_ceiling", 5.0f},
        {"energy_energy", 30.0f}, {"energy_feedback", 0.0f},
        {"energy_tone_on", 1.0f}, {"energy_tone", 15.0f},
        {"energy_pitch_on", 0.0f}, {"energy_midi", 0.0f},
        {"energy_pitch", 20.0f}, {"energy_glide", 0.0f}, {"energy_snap", 0.0f},
        {"energy_freeze", 0.0f}, {"energy_input_freeze", 0.0f}, {"energy_buffer_freeze", 0.0f},
        {"energy_shimmer_on", 0.0f}, {"energy_shimmer", 12.0f},
        {"position_source_x", 0.5f}, {"position_source_y", 0.5f}, {"position_source_z", 0.5f},
        {"position_listener_x", 0.5f}, {"position_listener_y", 0.5f}, {"position_listener_z", 0.5f},
        {"mix_dry_wet", 50.0f}, {"mix_output_level", -2.0f},
        {"mix_predelay", 0.0f}, {"mix_duck", 0.0f}, {"mix_snapback", 200.0f},
        {"mix_width", 60.0f}, {"mix_filter", 18000.0f}, {"mix_filter_env", -30.0f},
        {"mix_output_mode", 0.0f},
        {"atmosphere_temperature", 20.0f}, {"atmosphere_gas", 2.0f},
        {"atmosphere_humidity", 20.0f}, {"atmosphere_pressure", 101.325f},
        {"atmosphere_turbulence", 35.0f},
        {"dynamics_wind", 0.0f}, {"dynamics_convection", 0.0f}, {"dynamics_gusts", 0.0f}
    }},
    {"Warm Hall", {
        {"space_length", 30.0f}, {"space_width", 18.0f}, {"space_height", 12.0f},
        {"space_skew", 0.0f}, {"space_curvature", 0.0f},
        {"material_left_wall", 3.0f}, {"material_right_wall", 3.0f},
        {"material_front_wall", 3.0f}, {"material_back_wall", 2.0f},
        {"material_floor", 4.0f}, {"material_ceiling", 3.0f},
        {"energy_energy", 18.0f}, {"energy_feedback", 0.0f},
        {"energy_tone_on", 1.0f}, {"energy_tone", -15.0f},
        {"energy_pitch_on", 0.0f}, {"energy_midi", 0.0f},
        {"energy_pitch", 20.0f}, {"energy_glide", 0.0f}, {"energy_snap", 0.0f},
        {"energy_freeze", 0.0f}, {"energy_input_freeze", 0.0f}, {"energy_buffer_freeze", 0.0f},
        {"energy_shimmer_on", 0.0f}, {"energy_shimmer", 12.0f},
        {"position_source_x", 0.4f}, {"position_source_y", 0.25f}, {"position_source_z", 0.5f},
        {"position_listener_x", 0.5f}, {"position_listener_y", 0.65f}, {"position_listener_z", 0.4f},
        {"mix_dry_wet", 35.0f}, {"mix_output_level", -1.0f},
        {"mix_predelay", 15.0f}, {"mix_duck", 0.0f}, {"mix_snapback", 200.0f},
        {"mix_width", 130.0f}, {"mix_filter", 8000.0f}, {"mix_filter_env", -15.0f},
        {"mix_output_mode", 0.0f},
        {"atmosphere_temperature", 22.0f}, {"atmosphere_gas", 0.0f},
        {"atmosphere_humidity", 45.0f}, {"atmosphere_pressure", 101.325f},
        {"atmosphere_turbulence", 20.0f},
        {"dynamics_wind", 0.0f}, {"dynamics_convection", 0.0f}, {"dynamics_gusts", 0.0f}
    }},
    {"Drone Machine", {
        {"space_length", 12.0f}, {"space_width", 8.0f}, {"space_height", 5.0f},
        {"space_skew", 0.0f}, {"space_curvature", 0.0f},
        {"material_left_wall", 5.0f}, {"material_right_wall", 5.0f},
        {"material_front_wall", 5.0f}, {"material_back_wall", 5.0f},
        {"material_floor", 5.0f}, {"material_ceiling", 5.0f},
        {"energy_energy", 55.0f}, {"energy_feedback", 1.0f},
        {"energy_tone_on", 1.0f}, {"energy_tone", -20.0f},
        {"energy_pitch_on", 1.0f}, {"energy_midi", 1.0f},
        {"energy_pitch", 110.0f}, {"energy_glide", 200.0f}, {"energy_snap", 1.0f},
        {"energy_freeze", 0.0f}, {"energy_input_freeze", 0.0f}, {"energy_buffer_freeze", 0.0f},
        {"energy_shimmer_on", 0.0f}, {"energy_shimmer", 12.0f},
        {"position_source_x", 0.5f}, {"position_source_y", 0.5f}, {"position_source_z", 0.5f},
        {"position_listener_x", 0.5f}, {"position_listener_y", 0.5f}, {"position_listener_z", 0.5f},
        {"mix_dry_wet", 70.0f}, {"mix_output_level", -4.0f},
        {"mix_predelay", 0.0f}, {"mix_duck", 0.0f}, {"mix_snapback", 200.0f},
        {"mix_width", 150.0f}, {"mix_filter", 6000.0f}, {"mix_filter_env", 0.0f},
        {"mix_output_mode", 0.0f},
        {"atmosphere_temperature", 20.0f}, {"atmosphere_gas", 3.0f},
        {"atmosphere_humidity", 30.0f}, {"atmosphere_pressure", 101.325f},
        {"atmosphere_turbulence", 50.0f},
        {"dynamics_wind", 0.0f}, {"dynamics_convection", 0.0f}, {"dynamics_gusts", 0.0f}
    }},
    {"Glass Greenhouse", {
        {"space_length", 15.0f}, {"space_width", 10.0f}, {"space_height", 6.0f},
        {"space_skew", 10.0f}, {"space_curvature", -15.0f},
        {"material_left_wall", 1.0f}, {"material_right_wall", 1.0f},
        {"material_front_wall", 1.0f}, {"material_back_wall", 1.0f},
        {"material_floor", 7.0f}, {"material_ceiling", 1.0f},
        {"energy_energy", 8.0f}, {"energy_feedback", 0.0f},
        {"energy_tone_on", 1.0f}, {"energy_tone", 20.0f},
        {"energy_pitch_on", 0.0f}, {"energy_midi", 0.0f},
        {"energy_pitch", 20.0f}, {"energy_glide", 0.0f}, {"energy_snap", 0.0f},
        {"energy_freeze", 0.0f}, {"energy_input_freeze", 0.0f}, {"energy_buffer_freeze", 0.0f},
        {"energy_shimmer_on", 0.0f}, {"energy_shimmer", 12.0f},
        {"position_source_x", 0.3f}, {"position_source_y", 0.3f}, {"position_source_z", 0.4f},
        {"position_listener_x", 0.6f}, {"position_listener_y", 0.7f}, {"position_listener_z", 0.4f},
        {"mix_dry_wet", 40.0f}, {"mix_output_level", -1.0f},
        {"mix_predelay", 10.0f}, {"mix_duck", 0.0f}, {"mix_snapback", 200.0f},
        {"mix_width", 110.0f}, {"mix_filter", 16000.0f}, {"mix_filter_env", 20.0f},
        {"mix_output_mode", 0.0f},
        {"atmosphere_temperature", 30.0f}, {"atmosphere_gas", 0.0f},
        {"atmosphere_humidity", 70.0f}, {"atmosphere_pressure", 101.325f},
        {"atmosphere_turbulence", 20.0f},
        {"dynamics_wind", 0.0f}, {"dynamics_convection", 0.0f}, {"dynamics_gusts", 0.0f}
    }},
    {"Dead Room", {
        {"space_length", 5.0f}, {"space_width", 4.0f}, {"space_height", 3.0f},
        {"space_skew", 0.0f}, {"space_curvature", 0.0f},
        {"material_left_wall", 9.0f}, {"material_right_wall", 9.0f},
        {"material_front_wall", 9.0f}, {"material_back_wall", 9.0f},
        {"material_floor", 4.0f}, {"material_ceiling", 9.0f},
        {"energy_energy", 0.0f}, {"energy_feedback", 0.0f},
        {"energy_tone_on", 0.0f}, {"energy_tone", 0.0f},
        {"energy_pitch_on", 0.0f}, {"energy_midi", 0.0f},
        {"energy_pitch", 20.0f}, {"energy_glide", 0.0f}, {"energy_snap", 0.0f},
        {"energy_freeze", 0.0f}, {"energy_input_freeze", 0.0f}, {"energy_buffer_freeze", 0.0f},
        {"energy_shimmer_on", 0.0f}, {"energy_shimmer", 12.0f},
        {"position_source_x", 0.5f}, {"position_source_y", 0.4f}, {"position_source_z", 0.5f},
        {"position_listener_x", 0.5f}, {"position_listener_y", 0.6f}, {"position_listener_z", 0.5f},
        {"mix_dry_wet", 25.0f}, {"mix_output_level", 0.0f},
        {"mix_predelay", 0.0f}, {"mix_duck", 0.0f}, {"mix_snapback", 200.0f},
        {"mix_width", 90.0f}, {"mix_filter", 10000.0f}, {"mix_filter_env", 0.0f},
        {"mix_output_mode", 0.0f},
        {"atmosphere_temperature", 20.0f}, {"atmosphere_gas", 0.0f},
        {"atmosphere_humidity", 40.0f}, {"atmosphere_pressure", 101.325f},
        {"atmosphere_turbulence", 10.0f},
        {"dynamics_wind", 0.0f}, {"dynamics_convection", 0.0f}, {"dynamics_gusts", 0.0f}
    }},

    // --- Module 3 showcase presets ---

    {"Mars Cavern", {
        {"space_length", 40.0f}, {"space_width", 15.0f}, {"space_height", 8.0f},
        {"space_skew", 20.0f}, {"space_curvature", 30.0f},
        {"material_left_wall", 0.0f}, {"material_right_wall", 0.0f},
        {"material_front_wall", 0.0f}, {"material_back_wall", 0.0f},
        {"material_floor", 0.0f}, {"material_ceiling", 0.0f},
        {"energy_energy", 15.0f}, {"energy_feedback", 0.0f},
        {"energy_tone_on", 1.0f}, {"energy_tone", -10.0f},
        {"energy_pitch_on", 0.0f}, {"energy_midi", 0.0f},
        {"energy_pitch", 20.0f}, {"energy_glide", 0.0f}, {"energy_snap", 0.0f},
        {"energy_freeze", 10.0f}, {"energy_input_freeze", 0.0f}, {"energy_buffer_freeze", 0.0f},
        {"energy_shimmer_on", 0.0f}, {"energy_shimmer", 12.0f},
        {"position_source_x", 0.3f}, {"position_source_y", 0.2f}, {"position_source_z", 0.4f},
        {"position_listener_x", 0.5f}, {"position_listener_y", 0.7f}, {"position_listener_z", 0.4f},
        {"mix_dry_wet", 50.0f}, {"mix_output_level", -2.0f},
        {"mix_predelay", 0.0f}, {"mix_duck", 0.0f}, {"mix_snapback", 200.0f},
        {"mix_width", 140.0f}, {"mix_filter", 5000.0f}, {"mix_filter_env", -20.0f},
        {"mix_output_mode", 0.0f},
        {"atmosphere_temperature", -63.0f}, {"atmosphere_gas", 6.0f},
        {"atmosphere_humidity", 0.0f}, {"atmosphere_pressure", 10.0f},
        {"atmosphere_turbulence", 20.0f},
        {"dynamics_wind", 35.0f}, {"dynamics_convection", 25.0f}, {"dynamics_gusts", 15.0f}
    }},
    {"Venus Depths", {
        {"space_length", 20.0f}, {"space_width", 20.0f}, {"space_height", 6.0f},
        {"space_skew", 0.0f}, {"space_curvature", -20.0f},
        {"material_left_wall", 0.0f}, {"material_right_wall", 0.0f},
        {"material_front_wall", 5.0f}, {"material_back_wall", 5.0f},
        {"material_floor", 10.0f}, {"material_ceiling", 0.0f},
        {"energy_energy", 40.0f}, {"energy_feedback", 0.0f},
        {"energy_tone_on", 1.0f}, {"energy_tone", -20.0f},
        {"energy_pitch_on", 0.0f}, {"energy_midi", 0.0f},
        {"energy_pitch", 20.0f}, {"energy_glide", 0.0f}, {"energy_snap", 0.0f},
        {"energy_freeze", 0.0f}, {"energy_input_freeze", 0.0f}, {"energy_buffer_freeze", 0.0f},
        {"energy_shimmer_on", 0.0f}, {"energy_shimmer", 12.0f},
        {"position_source_x", 0.5f}, {"position_source_y", 0.5f}, {"position_source_z", 0.3f},
        {"position_listener_x", 0.5f}, {"position_listener_y", 0.5f}, {"position_listener_z", 0.5f},
        {"mix_dry_wet", 55.0f}, {"mix_output_level", -3.0f},
        {"mix_predelay", 0.0f}, {"mix_duck", 20.0f}, {"mix_snapback", 400.0f},
        {"mix_width", 80.0f}, {"mix_filter", 3000.0f}, {"mix_filter_env", -40.0f},
        {"mix_output_mode", 0.0f},
        {"atmosphere_temperature", 460.0f}, {"atmosphere_gas", 7.0f},
        {"atmosphere_humidity", 0.0f}, {"atmosphere_pressure", 500.0f},
        {"atmosphere_turbulence", 40.0f},
        {"dynamics_wind", 40.0f}, {"dynamics_convection", 50.0f}, {"dynamics_gusts", 30.0f}
    }},
    {"Titan Rain", {
        {"space_length", 25.0f}, {"space_width", 18.0f}, {"space_height", 10.0f},
        {"space_skew", 5.0f}, {"space_curvature", -10.0f},
        {"material_left_wall", 8.0f}, {"material_right_wall", 8.0f},
        {"material_front_wall", 8.0f}, {"material_back_wall", 8.0f},
        {"material_floor", 9.0f}, {"material_ceiling", 9.0f},
        {"energy_energy", 5.0f}, {"energy_feedback", 0.0f},
        {"energy_tone_on", 1.0f}, {"energy_tone", -25.0f},
        {"energy_pitch_on", 0.0f}, {"energy_midi", 0.0f},
        {"energy_pitch", 20.0f}, {"energy_glide", 0.0f}, {"energy_snap", 0.0f},
        {"energy_freeze", 30.0f}, {"energy_input_freeze", 0.0f}, {"energy_buffer_freeze", 0.0f},
        {"energy_shimmer_on", 0.0f}, {"energy_shimmer", 12.0f},
        {"position_source_x", 0.4f}, {"position_source_y", 0.3f}, {"position_source_z", 0.6f},
        {"position_listener_x", 0.5f}, {"position_listener_y", 0.6f}, {"position_listener_z", 0.4f},
        {"mix_dry_wet", 45.0f}, {"mix_output_level", -1.0f},
        {"mix_predelay", 40.0f}, {"mix_duck", 30.0f}, {"mix_snapback", 800.0f},
        {"mix_width", 120.0f}, {"mix_filter", 7000.0f}, {"mix_filter_env", -30.0f},
        {"mix_output_mode", 0.0f},
        {"atmosphere_temperature", -179.0f}, {"atmosphere_gas", 9.0f},
        {"atmosphere_humidity", 80.0f}, {"atmosphere_pressure", 146.0f},
        {"atmosphere_turbulence", 45.0f},
        {"dynamics_wind", 25.0f}, {"dynamics_convection", 40.0f}, {"dynamics_gusts", 30.0f}
    }},
    {"Solar Corona", {
        {"space_length", 80.0f}, {"space_width", 50.0f}, {"space_height", 25.0f},
        {"space_skew", 0.0f}, {"space_curvature", 50.0f},
        {"material_left_wall", 5.0f}, {"material_right_wall", 5.0f},
        {"material_front_wall", 5.0f}, {"material_back_wall", 5.0f},
        {"material_floor", 5.0f}, {"material_ceiling", 5.0f},
        {"energy_energy", 25.0f}, {"energy_feedback", 0.0f},
        {"energy_tone_on", 1.0f}, {"energy_tone", 30.0f},
        {"energy_pitch_on", 0.0f}, {"energy_midi", 0.0f},
        {"energy_pitch", 20.0f}, {"energy_glide", 0.0f}, {"energy_snap", 0.0f},
        {"energy_freeze", 60.0f}, {"energy_input_freeze", 0.0f}, {"energy_buffer_freeze", 1.0f},
        {"energy_shimmer_on", 1.0f}, {"energy_shimmer", 12.0f},
        {"position_source_x", 0.5f}, {"position_source_y", 0.3f}, {"position_source_z", 0.5f},
        {"position_listener_x", 0.5f}, {"position_listener_y", 0.7f}, {"position_listener_z", 0.5f},
        {"mix_dry_wet", 65.0f}, {"mix_output_level", -4.0f},
        {"mix_predelay", 100.0f}, {"mix_duck", 50.0f}, {"mix_snapback", 1500.0f},
        {"mix_width", 180.0f}, {"mix_filter", 14000.0f}, {"mix_filter_env", 30.0f},
        {"mix_output_mode", 0.0f},
        {"atmosphere_temperature", 1000.0f}, {"atmosphere_gas", 11.0f},
        {"atmosphere_humidity", 0.0f}, {"atmosphere_pressure", 10.0f},
        {"atmosphere_turbulence", 80.0f},
        {"dynamics_wind", 50.0f}, {"dynamics_convection", 70.0f}, {"dynamics_gusts", 50.0f}
    }},
    {"Frozen Cathedral", {
        {"space_length", 50.0f}, {"space_width", 20.0f}, {"space_height", 20.0f},
        {"space_skew", 0.0f}, {"space_curvature", 15.0f},
        {"material_left_wall", 10.0f}, {"material_right_wall", 10.0f},
        {"material_front_wall", 10.0f}, {"material_back_wall", 10.0f},
        {"material_floor", 10.0f}, {"material_ceiling", 10.0f},
        {"energy_energy", 8.0f}, {"energy_feedback", 0.0f},
        {"energy_tone_on", 1.0f}, {"energy_tone", 10.0f},
        {"energy_pitch_on", 0.0f}, {"energy_midi", 0.0f},
        {"energy_pitch", 20.0f}, {"energy_glide", 0.0f}, {"energy_snap", 0.0f},
        {"energy_freeze", 100.0f}, {"energy_input_freeze", 1.0f}, {"energy_buffer_freeze", 0.0f},
        {"energy_shimmer_on", 1.0f}, {"energy_shimmer", 12.0f},
        {"position_source_x", 0.3f}, {"position_source_y", 0.2f}, {"position_source_z", 0.4f},
        {"position_listener_x", 0.5f}, {"position_listener_y", 0.7f}, {"position_listener_z", 0.35f},
        {"mix_dry_wet", 70.0f}, {"mix_output_level", -4.0f},
        {"mix_predelay", 200.0f}, {"mix_duck", 70.0f}, {"mix_snapback", 2000.0f},
        {"mix_width", 160.0f}, {"mix_filter", 10000.0f}, {"mix_filter_env", 0.0f},
        {"mix_output_mode", 0.0f},
        {"atmosphere_temperature", -20.0f}, {"atmosphere_gas", 0.0f},
        {"atmosphere_humidity", 10.0f}, {"atmosphere_pressure", 101.325f},
        {"atmosphere_turbulence", 5.0f},
        {"dynamics_wind", 10.0f}, {"dynamics_convection", 15.0f}, {"dynamics_gusts", 5.0f}
    }},
    {"Submarine Ping", {
        {"space_length", 8.0f}, {"space_width", 4.0f}, {"space_height", 3.0f},
        {"space_skew", 0.0f}, {"space_curvature", 10.0f},
        {"material_left_wall", 5.0f}, {"material_right_wall", 5.0f},
        {"material_front_wall", 5.0f}, {"material_back_wall", 5.0f},
        {"material_floor", 5.0f}, {"material_ceiling", 5.0f},
        {"energy_energy", 20.0f}, {"energy_feedback", 0.0f},
        {"energy_tone_on", 1.0f}, {"energy_tone", 25.0f},
        {"energy_pitch_on", 0.0f}, {"energy_midi", 0.0f},
        {"energy_pitch", 20.0f}, {"energy_glide", 0.0f}, {"energy_snap", 0.0f},
        {"energy_freeze", 0.0f}, {"energy_input_freeze", 0.0f}, {"energy_buffer_freeze", 0.0f},
        {"energy_shimmer_on", 0.0f}, {"energy_shimmer", 12.0f},
        {"position_source_x", 0.5f}, {"position_source_y", 0.5f}, {"position_source_z", 0.5f},
        {"position_listener_x", 0.5f}, {"position_listener_y", 0.5f}, {"position_listener_z", 0.5f},
        {"mix_dry_wet", 45.0f}, {"mix_output_level", -1.0f},
        {"mix_predelay", 20.0f}, {"mix_duck", 0.0f}, {"mix_snapback", 200.0f},
        {"mix_width", 70.0f}, {"mix_filter", 4000.0f}, {"mix_filter_env", 40.0f},
        {"mix_output_mode", 0.0f},
        {"atmosphere_temperature", 10.0f}, {"atmosphere_gas", 12.0f},
        {"atmosphere_humidity", 100.0f}, {"atmosphere_pressure", 300.0f},
        {"atmosphere_turbulence", 15.0f},
        {"dynamics_wind", 0.0f}, {"dynamics_convection", 0.0f}, {"dynamics_gusts", 0.0f}
    }}
};

int FirstAirProcessor::getNumPrograms() { return static_cast<int>(factoryPresets.size()); }
int FirstAirProcessor::getCurrentProgram() { return currentProgram_; }

const juce::String FirstAirProcessor::getProgramName(int index) {
    if (index >= 0 && index < static_cast<int>(factoryPresets.size()))
        return factoryPresets[static_cast<size_t>(index)].name;
    return {};
}

void FirstAirProcessor::setCurrentProgram(int index)
{
    if (index < 0 || index >= static_cast<int>(factoryPresets.size())) return;
    currentProgram_ = index;

    const auto& preset = factoryPresets[static_cast<size_t>(index)];
    for (auto& [paramId, value] : preset.values) {
        if (auto* param = apvts.getParameter(paramId)) {
            // For choice params, convert index to normalized range
            if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param)) {
                choiceParam->setValueNotifyingHost(
                    choiceParam->convertTo0to1(value));
            } else if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param)) {
                boolParam->setValueNotifyingHost(value > 0.5f ? 1.0f : 0.0f);
            } else {
                param->setValueNotifyingHost(
                    param->convertTo0to1(value));
            }
        }
    }
}

// ============================================================================
// User presets
// ============================================================================

juce::File FirstAirProcessor::getUserPresetsFolder()
{
    auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("First Air").getChildFile("Presets");
    folder.createDirectory();
    return folder;
}

void FirstAirProcessor::saveUserPreset(const juce::String& name)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    auto file = getUserPresetsFolder().getChildFile(name + ".xml");
    xml->writeTo(file);
}

bool FirstAirProcessor::loadUserPreset(const juce::String& name)
{
    auto file = getUserPresetsFolder().getChildFile(name + ".xml");
    if (!file.existsAsFile()) return false;
    auto xml = juce::XmlDocument::parse(file);
    if (xml != nullptr && xml->hasTagName(apvts.state.getType())) {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
        return true;
    }
    return false;
}

void FirstAirProcessor::deleteUserPreset(const juce::String& name)
{
    auto file = getUserPresetsFolder().getChildFile(name + ".xml");
    file.deleteFile();
}

juce::StringArray FirstAirProcessor::getUserPresetNames()
{
    juce::StringArray names;
    auto folder = getUserPresetsFolder();
    for (auto& file : folder.findChildFiles(juce::File::findFiles, false, "*.xml"))
        names.add(file.getFileNameWithoutExtension());
    names.sort(true);
    return names;
}

// ============================================================================
// State save/restore
// ============================================================================

void FirstAirProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FirstAirProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// ============================================================================
// Plugin instantiation
// ============================================================================

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FirstAirProcessor();
}
