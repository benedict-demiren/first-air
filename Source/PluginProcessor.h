#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <string>
#include <map>

// Forward-declare the Faust DSP class and MapUI
// (actual includes in .cpp to keep headers clean)
class FirstAirDSP;
class MapUI;

class FirstAirProcessor : public juce::AudioProcessor
{
public:
    FirstAirProcessor();
    ~FirstAirProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // --- User presets ---
    static juce::File getUserPresetsFolder();
    void saveUserPreset(const juce::String& name);
    bool loadUserPreset(const juce::String& name);
    void deleteUserPreset(const juce::String& name);
    juce::StringArray getUserPresetNames();

    // Parameter metadata — public so the editor can build UI from it
    struct FaustParamInfo {
        std::string path;    // Faust path like "Space/Length"
        std::string label;   // Short label like "Length"
        std::string juceId;  // JUCE param ID like "space_length"
        std::string group;   // Top-level group like "[0]Space", "[2]Energy", etc.
        float init, min, max, step;
        float* zone = nullptr;       // Pointer into Faust DSP memory
        bool isOutput = false;       // true for bargraphs (read-only)
        bool isChoice = false;       // true for menu/choice params
        bool isToggle = false;       // true for checkboxes
        int numChoices = 0;
    };

    std::vector<FaustParamInfo> paramInfos;

private:
    // Faust DSP instance
    std::unique_ptr<FirstAirDSP> faustDSP;
    std::unique_ptr<MapUI> faustUI;

    // Parameter bridge
    juce::AudioProcessorValueTreeState apvts;
    int currentProgram_ = 0;

    // Build the APVTS layout from Faust parameters
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Sync JUCE → Faust parameter values before each block
    void syncParametersToFaust();

    // MIDI state for monophonic last-note-priority pitch control
    std::vector<int> midiNoteStack;       // Held notes (last = active)
    float* midiEnableZone = nullptr;      // Zone pointer for midi_enable checkbox
    float* fbPitchZone = nullptr;         // Zone pointer for fb_pitch slider
    float* pitchEnableZone = nullptr;     // Zone pointer for pitch_enable checkbox

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FirstAirProcessor)
};
