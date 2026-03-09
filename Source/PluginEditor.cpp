#include "PluginEditor.h"

// ============================================================================
// ContentComponent
// ============================================================================

void ContentComponent::recalculateHeight()
{
    // Height is set externally by layoutControls; just repaint
    repaint();
}

// ============================================================================
// Helper: strip leading "[N]" index prefix from Faust group names
// ============================================================================

static juce::String cleanGroupName(const std::string& group)
{
    auto s = group;
    if (!s.empty() && s[0] == '[') {
        auto closeBracket = s.find(']');
        if (closeBracket != std::string::npos)
            s = s.substr(closeBracket + 1);
    }
    return juce::String(s);
}

// ============================================================================
// Helper: MIDI note number → note name string (e.g. 69 → "A4", 60 → "C4")
// ============================================================================

static juce::String midiNoteToName(int note)
{
    static const char* noteNames[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    if (note < 0 || note > 127) return "";
    int pitchClass = note % 12;
    int octave = (note / 12) - 1;
    return juce::String(noteNames[pitchClass]) + juce::String(octave);
}

// ============================================================================
// RoomVisualizationComponent — isometric 3D wireframe room
// ============================================================================

void RoomVisualizationComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff080808));  // Near-black, slightly lighter for contrast

    auto bounds = getLocalBounds().toFloat().reduced(8.0f);
    float cx = bounds.getCentreX();
    float cy = bounds.getCentreY() + bounds.getHeight() * 0.1f;  // Shift down slightly

    // Read room dimensions from zone pointers (safe: aligned float read is atomic)
    float length = roomLengthZone ? *roomLengthZone : 8.0f;
    float width  = roomWidthZone  ? *roomWidthZone  : 5.0f;
    float height = roomHeightZone ? *roomHeightZone : 3.5f;
    float skew   = roomSkewZone   ? *roomSkewZone   : 0.0f;
    float curve  = roomCurveZone  ? *roomCurveZone  : 0.0f;

    // Source and listener positions (0-1 normalized)
    float sX = srcXZone ? *srcXZone : 0.5f;
    float sY = srcYZone ? *srcYZone : 0.3f;
    float sZ = srcZZone ? *srcZZone : 0.5f;
    float lX = lisXZone ? *lisXZone : 0.5f;
    float lY = lisYZone ? *lisYZone : 0.7f;
    float lZ = lisZZone ? *lisZZone : 0.5f;

    // Calculate scale to fit room in bounds
    float maxDim = std::max({length, width, height});
    float availableSize = std::min(bounds.getWidth(), bounds.getHeight()) * 0.55f;
    float scale = availableSize / (maxDim + 0.01f);

    // Normalize room dims to centered coordinates
    float hw = width * 0.5f;   // half-width (x axis)
    float hl = length * 0.5f;  // half-length (y axis)
    float hh = height;          // full height (z axis, 0 at floor)

    // Skew offset: top face shifts relative to bottom
    float skewOffset = (skew / 100.0f) * hw * 0.5f;

    // --- Draw room wireframe ---
    // 8 corners of the room box
    // Bottom face (z=0): 4 corners
    auto b0 = project(-hw, -hl, 0, scale, cx, cy);  // front-left
    auto b1 = project( hw, -hl, 0, scale, cx, cy);  // front-right
    auto b2 = project( hw,  hl, 0, scale, cx, cy);  // back-right
    auto b3 = project(-hw,  hl, 0, scale, cx, cy);  // back-left

    // Top face (z=height): 4 corners, shifted by skew
    auto t0 = project(-hw + skewOffset, -hl, hh, scale, cx, cy);
    auto t1 = project( hw + skewOffset, -hl, hh, scale, cx, cy);
    auto t2 = project( hw + skewOffset,  hl, hh, scale, cx, cy);
    auto t3 = project(-hw + skewOffset,  hl, hh, scale, cx, cy);

    // Edge color
    auto edgeColour = juce::Colour(0xff446688);
    g.setColour(edgeColour.withAlpha(0.3f));

    // Fill faces for depth perception (back faces slightly visible)
    // Floor
    {
        juce::Path floor;
        floor.startNewSubPath(b0); floor.lineTo(b1); floor.lineTo(b2); floor.lineTo(b3);
        floor.closeSubPath();
        g.setColour(juce::Colour(0xff0f0f1a).withAlpha(0.6f));
        g.fillPath(floor);
    }

    // Left wall (visible in isometric)
    {
        juce::Path leftWall;
        leftWall.startNewSubPath(b0); leftWall.lineTo(b3); leftWall.lineTo(t3); leftWall.lineTo(t0);
        leftWall.closeSubPath();
        g.setColour(juce::Colour(0xff111122).withAlpha(0.5f));
        g.fillPath(leftWall);
    }

    // Back wall
    {
        juce::Path backWall;
        backWall.startNewSubPath(b3); backWall.lineTo(b2); backWall.lineTo(t2); backWall.lineTo(t3);
        backWall.closeSubPath();
        g.setColour(juce::Colour(0xff151528).withAlpha(0.4f));
        g.fillPath(backWall);
    }

    // Draw edges
    g.setColour(edgeColour);
    float lineThickness = 1.5f;

    // Bottom face edges
    g.drawLine(b0.x, b0.y, b1.x, b1.y, lineThickness);
    g.drawLine(b1.x, b1.y, b2.x, b2.y, lineThickness);
    g.drawLine(b2.x, b2.y, b3.x, b3.y, lineThickness);
    g.drawLine(b3.x, b3.y, b0.x, b0.y, lineThickness);

    // Top face edges
    g.drawLine(t0.x, t0.y, t1.x, t1.y, lineThickness);
    g.drawLine(t1.x, t1.y, t2.x, t2.y, lineThickness);
    g.drawLine(t2.x, t2.y, t3.x, t3.y, lineThickness);
    g.drawLine(t3.x, t3.y, t0.x, t0.y, lineThickness);

    // Vertical edges (pillars)
    g.drawLine(b0.x, b0.y, t0.x, t0.y, lineThickness);
    g.drawLine(b1.x, b1.y, t1.x, t1.y, lineThickness);
    g.drawLine(b2.x, b2.y, t2.x, t2.y, lineThickness);
    g.drawLine(b3.x, b3.y, t3.x, t3.y, lineThickness);

    // Apply curvature to visible edges (bow them)
    if (std::abs(curve) > 5.0f) {
        float bowAmount = (curve / 100.0f) * maxDim * 0.15f;
        g.setColour(edgeColour.withAlpha(0.4f));

        // Bow the front-bottom edge
        auto midB01 = project(0, -hl + bowAmount, 0, scale, cx, cy);
        juce::Path bowedFront;
        bowedFront.startNewSubPath(b0);
        bowedFront.quadraticTo(midB01, b1);
        g.strokePath(bowedFront, juce::PathStrokeType(1.0f));

        // Bow the left-bottom edge
        auto midB03 = project(-hw - bowAmount, 0, 0, scale, cx, cy);
        juce::Path bowedLeft;
        bowedLeft.startNewSubPath(b0);
        bowedLeft.quadraticTo(midB03, b3);
        g.strokePath(bowedLeft, juce::PathStrokeType(1.0f));

        // Bow a vertical edge
        auto midV0 = project(-hw - bowAmount * 0.5f, -hl, hh * 0.5f, scale, cx, cy);
        juce::Path bowedVert;
        bowedVert.startNewSubPath(b0);
        bowedVert.quadraticTo(midV0, t0);
        g.strokePath(bowedVert, juce::PathStrokeType(1.0f));
    }

    // --- Source dot (orange) ---
    float srcPosX = (sX - 0.5f) * width;
    float srcPosY = (sY - 0.5f) * length;
    float srcPosZ = sZ * height;
    // Apply skew interpolation for height
    float srcSkew = skewOffset * (srcPosZ / (height + 0.01f));
    auto srcPt = project(srcPosX + srcSkew, srcPosY, srcPosZ, scale, cx, cy);

    g.setColour(juce::Colour(0xffff8833));
    g.fillEllipse(srcPt.x - 5, srcPt.y - 5, 10, 10);
    g.setColour(juce::Colour(0xffff8833).withAlpha(0.3f));
    g.fillEllipse(srcPt.x - 8, srcPt.y - 8, 16, 16);

    // Source label
    g.setColour(juce::Colour(0xffff8833));
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("SRC", juce::Rectangle<float>(srcPt.x - 12, srcPt.y - 18, 24, 12),
               juce::Justification::centred);

    // --- Listener dot (green) ---
    float lisPosX = (lX - 0.5f) * width;
    float lisPosY = (lY - 0.5f) * length;
    float lisPosZ = lZ * height;
    float lisSkew = skewOffset * (lisPosZ / (height + 0.01f));
    auto lisPt = project(lisPosX + lisSkew, lisPosY, lisPosZ, scale, cx, cy);

    g.setColour(juce::Colour(0xff33cc66));
    g.fillEllipse(lisPt.x - 5, lisPt.y - 5, 10, 10);
    g.setColour(juce::Colour(0xff33cc66).withAlpha(0.3f));
    g.fillEllipse(lisPt.x - 8, lisPt.y - 8, 16, 16);

    // Listener label
    g.setColour(juce::Colour(0xff33cc66));
    g.drawText("LIS", juce::Rectangle<float>(lisPt.x - 12, lisPt.y - 18, 24, 12),
               juce::Justification::centred);

    // --- Direct path line (faint) ---
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawLine(srcPt.x, srcPt.y, lisPt.x, lisPt.y, 1.0f);

    // --- Dimension labels ---
    g.setColour(juce::Colour(0xff667799));
    g.setFont(juce::FontOptions(11.0f));

    // Length (along Y axis)
    auto lStart = project(hw + 0.3f, -hl, 0, scale, cx, cy);
    auto lEnd   = project(hw + 0.3f,  hl, 0, scale, cx, cy);
    auto lMid   = juce::Point<float>((lStart.x + lEnd.x) * 0.5f, (lStart.y + lEnd.y) * 0.5f);
    g.drawText(juce::String(length, 1) + "m", juce::Rectangle<float>(lMid.x - 20, lMid.y - 6, 40, 12),
               juce::Justification::centred);

    // Width (along X axis)
    auto wStart = project(-hw, -hl - 0.3f, 0, scale, cx, cy);
    auto wEnd   = project( hw, -hl - 0.3f, 0, scale, cx, cy);
    auto wMid   = juce::Point<float>((wStart.x + wEnd.x) * 0.5f, (wStart.y + wEnd.y) * 0.5f);
    g.drawText(juce::String(width, 1) + "m", juce::Rectangle<float>(wMid.x - 20, wMid.y - 6, 40, 12),
               juce::Justification::centred);

    // Height (along Z axis)
    auto hBase = project(-hw - 0.3f, -hl, 0, scale, cx, cy);
    auto hTop  = project(-hw - 0.3f + skewOffset, -hl, hh, scale, cx, cy);
    auto hMid  = juce::Point<float>((hBase.x + hTop.x) * 0.5f, (hBase.y + hTop.y) * 0.5f);
    g.drawText(juce::String(height, 1) + "m", juce::Rectangle<float>(hMid.x - 25, hMid.y - 6, 30, 12),
               juce::Justification::centred);
}

// ============================================================================
// Constructor
// ============================================================================

FirstAirEditor::FirstAirEditor(FirstAirProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&firstAirLnF);

    auto& apvts = processor.getAPVTS();
    std::string lastGroup;

    // --- Build controls from parameter metadata ---
    for (auto& info : processor.paramInfos) {
        // Find the snapped note bargraph zone pointer
        if (info.isOutput && info.label == "Snapped Note") {
            snappedNoteZone = info.zone;
            continue;  // Don't create a control for it
        }

        if (info.isOutput) continue;  // Skip other bargraphs

        // Wire up zone pointers for room visualization
        if (info.label == "Length")     roomViz.roomLengthZone = info.zone;
        if (info.label == "Width")      roomViz.roomWidthZone  = info.zone;
        if (info.label == "Height")     roomViz.roomHeightZone = info.zone;
        if (info.label == "Skew")       roomViz.roomSkewZone   = info.zone;
        if (info.label == "Curvature")  roomViz.roomCurveZone  = info.zone;
        if (info.label == "Source X")   roomViz.srcXZone  = info.zone;
        if (info.label == "Source Y")   roomViz.srcYZone  = info.zone;
        if (info.label == "Source Z")   roomViz.srcZZone  = info.zone;
        if (info.label == "Listener X") roomViz.lisXZone  = info.zone;
        if (info.label == "Listener Y") roomViz.lisYZone  = info.zone;
        if (info.label == "Listener Z") roomViz.lisZZone  = info.zone;

        // Track pitch-related param IDs for visibility logic
        if (info.label == "Pitch On") {
            hasPitchEnable = true;
            pitchEnableId = juce::String(info.juceId);
        }
        if (info.label == "Snap") {
            hasSnapEnable = true;
            snapEnableId = juce::String(info.juceId);
        }

        // Insert group header when group changes
        if (info.group != lastGroup) {
            lastGroup = info.group;

            // Default collapsed state: Position, Material, and Dynamics collapsed
            auto groupName = cleanGroupName(info.group);
            bool startCollapsed = (groupName == "Position" || groupName == "Material" || groupName == "Dynamics");
            if (groupCollapsed.find(info.group) == groupCollapsed.end())
                groupCollapsed[info.group] = startCollapsed;

            auto header = std::make_unique<ClickableGroupHeader>(
                cleanGroupName(info.group), groupCollapsed[info.group]);

            // On toggle: update collapsed state and relayout
            std::string grp = info.group;
            header->onToggle = [this, grp]() {
                // Find the header for this group and read its state
                for (auto& h : groupHeaders) {
                    if (h->getText().endsWith(cleanGroupName(grp))) {
                        groupCollapsed[grp] = h->isCollapsed();
                        break;
                    }
                }
                layoutControls();
            };

            contentComponent.addAndMakeVisible(*header);

            controlOrder.push_back({ControlEntry::GroupHeader,
                                    static_cast<int>(groupHeaders.size()), info.group});
            groupHeaders.push_back(std::move(header));

            // Add material macro ComboBox right after the Material group header
            if (groupName == "Material")
                contentComponent.addAndMakeVisible(materialMacro);
        }

        // Create the appropriate control
        if (info.isChoice) {
            auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(
                apvts.getParameter(info.juceId));
            if (!choiceParam) continue;

            ParamComboBox pc;
            pc.combo = std::make_unique<juce::ComboBox>();
            pc.label = std::make_unique<juce::Label>();
            pc.group = info.group;

            auto& choices = choiceParam->choices;
            for (int i = 0; i < choices.size(); ++i)
                pc.combo->addItem(choices[i], i + 1);

            pc.label->setText(info.label, juce::dontSendNotification);
            pc.label->setJustificationType(juce::Justification::centredRight);
            pc.label->setColour(juce::Label::textColourId, juce::Colours::lightgrey);

            pc.attachment = std::make_unique<juce::ComboBoxParameterAttachment>(
                *choiceParam, *pc.combo, nullptr);

            contentComponent.addAndMakeVisible(*pc.combo);
            contentComponent.addAndMakeVisible(*pc.label);

            controlOrder.push_back({ControlEntry::ComboBox,
                                    static_cast<int>(comboBoxes.size()), info.group});
            comboBoxes.push_back(std::move(pc));

        } else if (info.isToggle) {
            auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(
                apvts.getParameter(info.juceId));
            if (!boolParam) continue;

            ParamToggle pt;
            pt.button = std::make_unique<juce::ToggleButton>(juce::String(info.label));
            pt.button->setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
            pt.group = info.group;

            pt.attachment = std::make_unique<juce::ButtonParameterAttachment>(
                *boolParam, *pt.button, nullptr);

            contentComponent.addAndMakeVisible(*pt.button);

            controlOrder.push_back({ControlEntry::Toggle,
                                    static_cast<int>(toggles.size()), info.group});
            toggles.push_back(std::move(pt));

        } else {
            auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(
                apvts.getParameter(info.juceId));
            if (!floatParam) continue;

            ParamSlider ps;
            ps.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                                        juce::Slider::TextBoxRight);
            ps.label = std::make_unique<juce::Label>();
            ps.group = info.group;

            ps.label->setText(info.label, juce::dontSendNotification);
            ps.label->setJustificationType(juce::Justification::centredRight);
            ps.label->setColour(juce::Label::textColourId, juce::Colours::lightgrey);

            ps.attachment = std::make_unique<juce::SliderParameterAttachment>(
                *floatParam, *ps.slider, nullptr);

            contentComponent.addAndMakeVisible(*ps.slider);
            contentComponent.addAndMakeVisible(*ps.label);

            controlOrder.push_back({ControlEntry::Slider,
                                    static_cast<int>(sliders.size()), info.group});
            sliders.push_back(std::move(ps));
        }
    }

    // --- Material macro ComboBox ---
    // Provides quick "set all walls" presets
    materialMacro.addItem("Custom", 1);
    materialMacro.addSectionHeading("--- Single Material ---");
    static const juce::StringArray matNames {
        "Concrete", "Glass", "Wood", "Plaster", "Carpet", "Metal",
        "Brick", "Tile", "Fabric", "Foam", "Marble"
    };
    for (int i = 0; i < matNames.size(); ++i)
        materialMacro.addItem("All " + matNames[i], 100 + i);
    materialMacro.addSectionHeading("--- Room Presets ---");
    materialMacro.addItem("Studio", 200);       // Wood walls, Carpet floor, Plaster ceiling
    materialMacro.addItem("Concert Hall", 201);  // Plaster walls, Carpet floor, Plaster ceiling
    materialMacro.addItem("Bathroom", 202);      // Tile everywhere
    materialMacro.addItem("Padded Cell", 203);   // Foam everywhere
    materialMacro.addItem("Cathedral", 204);     // Concrete walls, Marble floor, Concrete ceiling
    materialMacro.setSelectedId(1, juce::dontSendNotification);
    materialMacro.onChange = [this]() {
        int id = materialMacro.getSelectedId();
        if (id < 2) return;  // "Custom" — do nothing

        materialMacroChanging = true;
        auto& apvts = processor.getAPVTS();

        // Find wall material param IDs
        static const char* wallIds[] = {
            "material_left_wall", "material_right_wall",
            "material_front_wall", "material_back_wall",
            "material_floor", "material_ceiling"
        };

        auto setWalls = [&](int left, int right, int front, int back, int floor, int ceiling) {
            int vals[] = {left, right, front, back, floor, ceiling};
            for (int i = 0; i < 6; ++i) {
                if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(wallIds[i])))
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(vals[i])));
            }
        };

        if (id >= 100 && id < 200) {
            // "All X" — set all 6 walls to the same material
            int mat = id - 100;
            setWalls(mat, mat, mat, mat, mat, mat);
        } else {
            switch (id) {
                case 200: setWalls(2, 2, 2, 2, 4, 3); break;  // Studio
                case 201: setWalls(3, 3, 3, 3, 4, 3); break;  // Concert Hall
                case 202: setWalls(7, 7, 7, 7, 7, 7); break;  // Bathroom
                case 203: setWalls(9, 9, 9, 9, 9, 9); break;  // Padded Cell
                case 204: setWalls(0, 0, 0, 0, 10, 0); break; // Cathedral
            }
        }
        materialMacroChanging = false;
    };
    // materialMacro is added to contentComponent later during group insertion

    // --- Preset selector ---
    numFactoryPresets = processor.getNumPrograms();
    auto rebuildPresetList = [this]() {
        presetSelector.clear(juce::dontSendNotification);
        for (int i = 0; i < numFactoryPresets; ++i)
            presetSelector.addItem(processor.getProgramName(i), i + 1);

        // Add user presets
        auto userNames = processor.getUserPresetNames();
        if (userNames.size() > 0) {
            presetSelector.addSeparator();
            for (int i = 0; i < userNames.size(); ++i)
                presetSelector.addItem(userNames[i], 1000 + i);
        }
    };
    rebuildPresetList();
    presetSelector.setSelectedId(processor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetSelector.onChange = [this]() { presetChanged(); };
    addAndMakeVisible(presetSelector);

    // --- Save preset button ---
    savePresetButton.onClick = [this, rebuildPresetList]() {
        auto w = std::make_shared<juce::AlertWindow>(
            "Save Preset", "Enter a name for this preset:", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor("name", "", "Preset Name:");
        w->addButton("Save", 1);
        w->addButton("Cancel", 0);
        w->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, w, rebuildPresetList](int result) {
                if (result == 1) {
                    auto name = w->getTextEditorContents("name").trim();
                    if (name.isNotEmpty()) {
                        processor.saveUserPreset(name);
                        // Rebuild preset list and select the saved preset
                        presetSelector.clear(juce::dontSendNotification);
                        for (int i = 0; i < numFactoryPresets; ++i)
                            presetSelector.addItem(processor.getProgramName(i), i + 1);
                        auto userNames = processor.getUserPresetNames();
                        if (userNames.size() > 0) {
                            presetSelector.addSeparator();
                            for (int i = 0; i < userNames.size(); ++i) {
                                presetSelector.addItem(userNames[i], 1000 + i);
                                if (userNames[i] == name)
                                    presetSelector.setSelectedId(1000 + i, juce::dontSendNotification);
                            }
                        }
                    }
                }
            }));
    };
    addAndMakeVisible(savePresetButton);

    // --- Delete preset button ---
    deletePresetButton.onClick = [this, rebuildPresetList]() {
        int id = presetSelector.getSelectedId();
        if (id < 1000) return;  // Can only delete user presets
        auto name = presetSelector.getText();
        processor.deleteUserPreset(name);
        // Rebuild list
        presetSelector.clear(juce::dontSendNotification);
        for (int i = 0; i < numFactoryPresets; ++i)
            presetSelector.addItem(processor.getProgramName(i), i + 1);
        auto userNames = processor.getUserPresetNames();
        if (userNames.size() > 0) {
            presetSelector.addSeparator();
            for (int i = 0; i < userNames.size(); ++i)
                presetSelector.addItem(userNames[i], 1000 + i);
        }
        presetSelector.setSelectedId(1, juce::dontSendNotification);
    };
    addAndMakeVisible(deletePresetButton);

    // --- Pitch note display ---
    pitchNoteLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    pitchNoteLabel.setColour(juce::Label::textColourId, juce::Colour(0xffccddaa));
    pitchNoteLabel.setJustificationType(juce::Justification::centredLeft);
    contentComponent.addAndMakeVisible(pitchNoteLabel);

    // --- Viewport setup ---
    viewport.setViewedComponent(&contentComponent, false);  // false = don't own
    viewport.setScrollBarsShown(true, false);  // vertical only
    addAndMakeVisible(viewport);

    // --- Room visualization ---
    addAndMakeVisible(roomViz);

    // --- Window size (wider to accommodate visualization panel) ---
    setSize(900, 800);

    // --- Start timer for preset sync + pitch note display + viz update ---
    startTimerHz(15);
}

FirstAirEditor::~FirstAirEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

// ============================================================================
// Preset change callback
// ============================================================================

void FirstAirEditor::presetChanged()
{
    int id = presetSelector.getSelectedId();
    if (id >= 1 && id < 1000) {
        // Factory preset
        processor.setCurrentProgram(id - 1);
    } else if (id >= 1000) {
        // User preset
        auto name = presetSelector.getText();
        processor.loadUserPreset(name);
    }
}

// ============================================================================
// Timer: sync preset selector + update pitch note display + repaint viz
// ============================================================================

void FirstAirEditor::timerCallback()
{
    // Sync preset selector if host changed program externally
    int current = processor.getCurrentProgram();
    if (presetSelector.getSelectedId() != current + 1)
        presetSelector.setSelectedId(current + 1, juce::dontSendNotification);

    // Update pitch note display
    if (snappedNoteZone != nullptr) {
        auto& apvts = processor.getAPVTS();

        // Check if both Pitch On and Snap are enabled
        bool pitchOn = false, snapOn = false;
        if (hasPitchEnable) {
            if (auto* p = apvts.getRawParameterValue(pitchEnableId))
                pitchOn = p->load() > 0.5f;
        }
        if (hasSnapEnable) {
            if (auto* p = apvts.getRawParameterValue(snapEnableId))
                snapOn = p->load() > 0.5f;
        }

        if (pitchOn && snapOn) {
            int noteNum = static_cast<int>(*snappedNoteZone);
            pitchNoteLabel.setText(midiNoteToName(noteNum), juce::dontSendNotification);
            pitchNoteLabel.setVisible(true);
        } else {
            pitchNoteLabel.setVisible(false);
        }
    } else {
        pitchNoteLabel.setVisible(false);
    }

    // Repaint room visualization (reads zone pointers for live update)
    roomViz.repaint();
}

// ============================================================================
// Paint
// ============================================================================

void FirstAirEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    // Title bar — use the custom typeface directly for guaranteed font rendering
    g.setColour(juce::Colour(0xffdddddd));
    g.setFont(firstAirLnF.makeFont(22.0f));
    g.drawText("F I R S T   A I R", getLocalBounds().removeFromTop(40),
               juce::Justification::centred);
}

// ============================================================================
// Layout
// ============================================================================

void FirstAirEditor::resized()
{
    auto area = getLocalBounds();

    // Fixed title area (40px)
    area.removeFromTop(40);

    // Preset selector bar (30px, with padding)
    auto presetArea = area.removeFromTop(30).reduced(10, 2);
    auto delBounds = presetArea.removeFromRight(40);
    presetArea.removeFromRight(4);
    auto saveBounds = presetArea.removeFromRight(50);
    presetArea.removeFromRight(4);
    presetSelector.setBounds(presetArea);
    savePresetButton.setBounds(saveBounds);
    deletePresetButton.setBounds(delBounds);

    // Small gap
    area.removeFromTop(4);

    // Split remaining area: left = controls (viewport), right = visualization
    const int vizWidth = 280;
    auto vizArea = area.removeFromRight(vizWidth);
    roomViz.setBounds(vizArea.reduced(4));

    // Viewport fills the left side
    viewport.setBounds(area);

    // Layout controls inside the content component
    layoutControls();
}

void FirstAirEditor::layoutControls()
{
    const int viewWidth = viewport.getWidth() - viewport.getScrollBarThickness() - 4;
    const int rowHeight = 26;
    const int headerHeight = 30;
    const int labelWidth = 120;
    const int gap = 2;
    const int pitchNoteWidth = 50;

    int y = 6;  // top padding inside content

    for (auto& entry : controlOrder) {
        bool isCollapsed = false;
        auto it = groupCollapsed.find(entry.group);
        if (it != groupCollapsed.end())
            isCollapsed = it->second;

        switch (entry.type) {
            case ControlEntry::GroupHeader: {
                y += 4;  // Extra spacing before header
                auto& header = groupHeaders[static_cast<size_t>(entry.index)];
                header->setBounds(10, y, viewWidth - 20, headerHeight);
                header->setVisible(true);
                y += headerHeight + 2;

                // Material macro ComboBox appears right after Material group header
                auto grpName = cleanGroupName(entry.group);
                if (grpName == "Material") {
                    if (isCollapsed) {
                        materialMacro.setVisible(false);
                    } else {
                        materialMacro.setBounds(10 + labelWidth + 4, y,
                                                 viewWidth - labelWidth - 24, rowHeight);
                        materialMacro.setVisible(true);
                        y += rowHeight + gap;
                    }
                }
                break;
            }
            case ControlEntry::ComboBox: {
                auto& pc = comboBoxes[static_cast<size_t>(entry.index)];
                if (isCollapsed) {
                    pc.label->setVisible(false);
                    pc.combo->setVisible(false);
                } else {
                    pc.label->setBounds(10, y, labelWidth, rowHeight);
                    pc.combo->setBounds(10 + labelWidth + 4, y, viewWidth - labelWidth - 24, rowHeight);
                    pc.label->setVisible(true);
                    pc.combo->setVisible(true);
                    y += rowHeight + gap;
                }
                break;
            }
            case ControlEntry::Toggle: {
                auto& pt = toggles[static_cast<size_t>(entry.index)];
                if (isCollapsed) {
                    pt.button->setVisible(false);
                    // Hide pitch note label if Snap toggle is hidden
                    if (pt.button->getButtonText() == "Snap")
                        pitchNoteLabel.setVisible(false);
                } else {
                    int toggleX = 10 + labelWidth + 4;
                    int toggleFullW = viewWidth - labelWidth - 24;

                    // Snap toggle: shrink to make room for note name display
                    if (pt.button->getButtonText() == "Snap") {
                        int toggleW = 90;  // enough for checkbox + "Snap" text
                        pt.button->setBounds(toggleX, y, toggleW, rowHeight);
                        pitchNoteLabel.setBounds(toggleX + toggleW + 6, y,
                                                 pitchNoteWidth, rowHeight);
                    } else {
                        pt.button->setBounds(toggleX, y, toggleFullW, rowHeight);
                    }
                    pt.button->setVisible(true);

                    y += rowHeight + gap;
                }
                break;
            }
            case ControlEntry::Slider: {
                auto& ps = sliders[static_cast<size_t>(entry.index)];
                if (isCollapsed) {
                    ps.label->setVisible(false);
                    ps.slider->setVisible(false);
                } else {
                    ps.label->setBounds(10, y, labelWidth, rowHeight);
                    ps.slider->setBounds(10 + labelWidth + 4, y, viewWidth - labelWidth - 24, rowHeight);
                    ps.label->setVisible(true);
                    ps.slider->setVisible(true);
                    y += rowHeight + gap;
                }
                break;
            }
        }
    }

    y += 10;  // bottom padding

    // Set content component size (viewport scrolls if this exceeds viewport height)
    contentComponent.setSize(viewWidth, y);
}
