#include "PluginEditor.h"
#include "Models.h"
#include "DebugLog.h"

// =============================================================================
// Colours
// =============================================================================
static const juce::Colour kBg      { 0xff1a1a1a };
static const juce::Colour kSection { 0xff222222 };
static const juce::Colour kAccent  { 0xff4a9eff };
static const juce::Colour kText    { 0xffcccccc };
static const juce::Colour kSubtext { 0xff777777 };
static const juce::Colour kGrid    { 0xff2a2a2a };

// =============================================================================
// Layout constants
// =============================================================================
static constexpr int kW        = 640;
static constexpr int kGlobalH  = 80;
static constexpr int kTabBarH  = 28;
static constexpr int kRowH     = 250;
static constexpr int kRowGap   = 10;   // vertical gap between the two layer rows
static constexpr int kTabContH = kRowH * 2 + kRowGap;
static constexpr int kH        = kGlobalH + kTabBarH + kTabContH;
static constexpr int kLeftW    = 300;
static constexpr int kPad      = 8;    // window-edge to outer-box gap
static constexpr int kBoxInset = 6;    // inset from outer box to its contents
static constexpr int kLBtnW    = kBoxInset + 18 + kBoxInset; // left-gap(6) + btn(18) + right-gap(6) = 30
static constexpr int kContentY = kGlobalH + kTabBarH;
static constexpr int kHeaderH  = 24;   // top of each row: mode combo strip

// =============================================================================
// Constructor
// =============================================================================
RattlerAudioProcessorEditor::RattlerAudioProcessorEditor (RattlerAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    RLOG_CLEAR();
    RLOG ("=== Rattler editor opened ===");

    auto& apvts = p.apvts;

    // ── Global strip ─────────────────────────────────────────────────────────
    setupKnob (masterMixSlider, "Mix", masterMixLabel);
    addAndMakeVisible (masterMixSlider);
    addAndMakeVisible (masterMixLabel);
    masterMixAttach = std::make_unique<SliderAtt> (apvts, "masterMix", masterMixSlider);

    setupKnob (masterSatSlider, "Saturation", masterSatLabel);
    addAndMakeVisible (masterSatSlider);
    addAndMakeVisible (masterSatLabel);
    masterSatAttach = std::make_unique<SliderAtt> (apvts, "masterSat", masterSatSlider);

    auto setupRoutingBtn = [&] (juce::TextButton& btn, const juce::String& label)
    {
        btn.setButtonText (label);
        btn.setClickingTogglesState (false);
        btn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff1e1e1e));
        btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1e3050));
        btn.setColour (juce::TextButton::textColourOffId,  kSubtext);
        btn.setColour (juce::TextButton::textColourOnId,   kAccent);
        addAndMakeVisible (btn);
    };
    setupRoutingBtn (routingParallelBtn, "PARALLEL");
    setupRoutingBtn (routingSeqBtn,      "SEQUENTIAL");

    routingAttach = std::make_unique<juce::ParameterAttachment> (
        *p.apvts.getParameter ("routing"),
        [this] (float v)
        {
            const bool isSeq = v > 0.5f;
            routingParallelBtn.setToggleState (! isSeq, juce::dontSendNotification);
            routingSeqBtn     .setToggleState (  isSeq, juce::dontSendNotification);
        });
    routingAttach->sendInitialUpdate();

    routingParallelBtn.onClick = [this]
    {
        routingAttach->beginGesture();
        routingAttach->setValueAsPartOfGesture (0.0f);
        routingAttach->endGesture();
    };
    routingSeqBtn.onClick = [this]
    {
        routingAttach->beginGesture();
        routingAttach->setValueAsPartOfGesture (1.0f);
        routingAttach->endGesture();
    };

    // Advanced toggle — pure UI state, not saved to APVTS
    advancedToggle.setButtonText ("Advanced");
    advancedToggle.setLookAndFeel (&pillToggleLnf);
    advancedToggle.onClick = [this]
    {
        showAdvanced = advancedToggle.getToggleState();
        resized();
        repaint();
    };
    addAndMakeVisible (advancedToggle);

    // Gear tab button
    auto setupGearBtn = [&] (juce::TextButton& btn)
    {
        btn.setButtonText (juce::String::fromUTF8 ("\xe2\x9a\x99"));
        btn.setClickingTogglesState (false);
        btn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff1a1a1a));
        btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2a2a2a));
        btn.setColour (juce::TextButton::textColourOffId,  kSubtext);
        btn.setColour (juce::TextButton::textColourOnId,   kAccent);
        addAndMakeVisible (btn);
    };
    setupGearBtn (gearTabBtn);
    gearTabBtn.onClick = [this] { switchToTab (4); };

    // Perf toggles — hidden by default, shown in gear tab
    modalClampToggle.setButtonText ("Clamp Guard");
    modalClampToggle.setLookAndFeel (&pillToggleLnf);
    addChildComponent (modalClampToggle);
    modalClampAttach = std::make_unique<ButtonAtt> (apvts, "modalClamp", modalClampToggle);

    fastTanhToggle.setButtonText ("Fast Tanh");
    fastTanhToggle.setLookAndFeel (&pillToggleLnf);
    addChildComponent (fastTanhToggle);
    fastTanhAttach = std::make_unique<ButtonAtt> (apvts, "fastTanh", fastTanhToggle);

    idleGateToggle.setButtonText ("Idle Gate");
    idleGateToggle.setLookAndFeel (&pillToggleLnf);
    addChildComponent (idleGateToggle);
    idleGateAttach = std::make_unique<ButtonAtt> (apvts, "idleGate", idleGateToggle);

    convSkipToggle.setButtonText ("Conv Skip");
    convSkipToggle.setLookAndFeel (&pillToggleLnf);
    addChildComponent (convSkipToggle);
    convSkipAttach = std::make_unique<ButtonAtt> (apvts, "convSkip", convSkipToggle);

    // ── Tab buttons ──────────────────────────────────────────────────────────
    auto setupTabBtn = [&] (juce::TextButton& btn, const juce::String& text)
    {
        btn.setButtonText (text);
        btn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff1e1e1e));
        btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2a2a2a));
        btn.setColour (juce::TextButton::textColourOffId,  kSubtext);
        btn.setColour (juce::TextButton::textColourOnId,   kText);
        addAndMakeVisible (btn);
    };
    setupTabBtn (sourceTabBtn,      "SOURCE");
    setupTabBtn (triggerTabBtn,     "TRIGGER");
    setupTabBtn (resonatorTabBtn,   "RESONATOR");
    setupTabBtn (convolutionTabBtn, "CONVOLUTION");
    sourceTabBtn   .onClick = [this] { switchToTab (0); };
    triggerTabBtn  .onClick = [this] { switchToTab (1); };
    resonatorTabBtn.onClick = [this] { switchToTab (2); };
    convolutionTabBtn.onClick = [this] { switchToTab (3); };

    // ── Layer UIs ────────────────────────────────────────────────────────────
    setupLayerUI (0, apvts);
    setupLayerUI (1, apvts);

    // ── Layer enable vertical buttons ─────────────────────────────────────────
    for (int i = 0; i < 2; ++i)
    {
        const juce::String px = (i == 0) ? "layerA" : "layerB";
        layerBtns[i].text = (i == 0) ? "LAYER A" : "LAYER B";

        layerEnableAttach[i] = std::make_unique<juce::ParameterAttachment> (
            *p.apvts.getParameter (px + "LayerEnable"),
            [this, i] (float v)
            {
                const bool active = (v > 0.5f);
                layerBtns[i].active = active;
                layerBtns[i].repaint();
                setLayerEnabled (i, active);
            });
        layerEnableAttach[i]->sendInitialUpdate();

        layerBtns[i].onClick = [this, i]
        {
            const bool newActive = !layerBtns[i].active;
            layerEnableAttach[i]->beginGesture();
            layerEnableAttach[i]->setValueAsPartOfGesture (newActive ? 1.0f : 0.0f);
            layerEnableAttach[i]->endGesture();
        };
        addAndMakeVisible (layerBtns[i]);
    }

    // Restore IR button label and preview if a project was loaded before the editor opened
    for (int i = 0; i < 2; ++i)
    {
        const juce::String path = audioProcessor.getConvIRFilePath (i);
        if (path.isNotEmpty())
        {
            layerUIs[i].loadConvIRBtn.setButtonText (juce::File (path).getFileNameWithoutExtension());
            updateIRPreview (i);
        }
    }

    // Keep preview in sync when IR is (re-)loaded asynchronously (e.g. project restore)
    audioProcessor.onConvIRLoaded = [this] (int idx)
    {
        juce::MessageManager::callAsync ([this, idx] { updateIRPreview (idx); });
    };

    audioProcessor.onSampleLoaded = [this] (int idx)
    {
        juce::MessageManager::callAsync ([this, idx] { updateSamplePreview (idx); });
    };

    switchToTab (0);
    setSize (kW, kH);
}

RattlerAudioProcessorEditor::~RattlerAudioProcessorEditor() {}

// =============================================================================
// setupLayerUI — called once per layer index from the constructor
// =============================================================================
void RattlerAudioProcessorEditor::setupLayerUI (int idx,
                                                 juce::AudioProcessorValueTreeState& apvts)
{
    auto& ui = layerUIs[idx];
    const juce::String p = (idx == 0) ? "layerA" : "layerB";

    // Mode combo
    setupCombo (ui.modeCombo);
    ui.modeCombo.addItem ("Noise",       1);
    ui.modeCombo.addItem ("Bounce",      2);
    ui.modeCombo.addItem ("Sample",      3);
    ui.modeCombo.addItem ("ModalRattle", 4);
    addAndMakeVisible (ui.modeCombo); // always visible
    ui.modeAttach = std::make_unique<LayerUI::ComboAtt> (apvts, p + "Mode", ui.modeCombo);
    ui.modeCombo.onChange = [this] { resized(); repaint(); };

    // Common: Level + Saturation
    setupKnob (ui.levelSlider, "Level",      ui.levelLabel);
    setupKnob (ui.satSlider,   "Saturation", ui.satLabel);
    ui.levelAttach = std::make_unique<LayerUI::SliderAtt> (apvts, p + "Level", ui.levelSlider);
    ui.satAttach   = std::make_unique<LayerUI::SliderAtt> (apvts, p + "Sat",   ui.satSlider);
    addChildComponent (ui.levelSlider); addChildComponent (ui.levelLabel);
    addChildComponent (ui.satSlider);   addChildComponent (ui.satLabel);

    // Noise mode
    setupKnob (ui.noiseBurstSlider,  "Burst",  ui.noiseBurstLabel);
    setupKnob (ui.noiseAttackSlider, "Attack", ui.noiseAttackLabel);
    ui.noiseBurstAttach  = std::make_unique<LayerUI::SliderAtt> (apvts, p + "NoiseBurst",  ui.noiseBurstSlider);
    ui.noiseAttackAttach = std::make_unique<LayerUI::SliderAtt> (apvts, p + "NoiseAttack", ui.noiseAttackSlider);
    ui.noiseFilterXY = std::make_unique<FilterXYPad> (
        *apvts.getParameter (p + "NoiseFreq"), *apvts.getParameter (p + "NoiseBw"));
    addChildComponent (ui.noiseBurstSlider);  addChildComponent (ui.noiseBurstLabel);
    addChildComponent (ui.noiseAttackSlider); addChildComponent (ui.noiseAttackLabel);
    addChildComponent (*ui.noiseFilterXY);

    // Bounce mode
    setupKnob (ui.bounceMassSlider,  "Mass",   ui.bounceMassLabel);
    setupKnob (ui.bounceGapSlider,   "Gap",    ui.bounceGapLabel);
    setupKnob (ui.bounceRestSlider,  "Rest.",  ui.bounceRestLabel);
    setupKnob (ui.bounceWiresSlider, "Wires",  ui.bounceWiresLabel);
    setupKnob (ui.bounceSpreadSlider,"Spread", ui.bounceSpreadLabel);
    ui.bounceMassAttach   = std::make_unique<LayerUI::SliderAtt> (apvts, p + "BounceMass",   ui.bounceMassSlider);
    ui.bounceGapAttach    = std::make_unique<LayerUI::SliderAtt> (apvts, p + "BounceGap",    ui.bounceGapSlider);
    ui.bounceRestAttach   = std::make_unique<LayerUI::SliderAtt> (apvts, p + "BounceRest",   ui.bounceRestSlider);
    ui.bounceWiresAttach  = std::make_unique<LayerUI::SliderAtt> (apvts, p + "BounceWires",  ui.bounceWiresSlider);
    ui.bounceSpreadAttach = std::make_unique<LayerUI::SliderAtt> (apvts, p + "BounceSpread", ui.bounceSpreadSlider);
    ui.bounceFilterXY = std::make_unique<FilterXYPad> (
        *apvts.getParameter (p + "BounceFreq"), *apvts.getParameter (p + "BounceBw"));
    addChildComponent (ui.bounceMassSlider);   addChildComponent (ui.bounceMassLabel);
    addChildComponent (ui.bounceGapSlider);    addChildComponent (ui.bounceGapLabel);
    addChildComponent (ui.bounceRestSlider);   addChildComponent (ui.bounceRestLabel);
    addChildComponent (ui.bounceWiresSlider);  addChildComponent (ui.bounceWiresLabel);
    addChildComponent (ui.bounceSpreadSlider); addChildComponent (ui.bounceSpreadLabel);
    addChildComponent (*ui.bounceFilterXY);

    // Sample mode
    setupKnob (ui.samplePitchSlider, "Pitch", ui.samplePitchLabel);
    setupKnob (ui.sampleGainSlider,  "Gain",  ui.sampleGainLabel);
    ui.samplePitchAttach   = std::make_unique<LayerUI::SliderAtt> (apvts, p + "SamplePitch",   ui.samplePitchSlider);
    ui.sampleGainAttach    = std::make_unique<LayerUI::SliderAtt> (apvts, p + "SampleGain",    ui.sampleGainSlider);
    ui.sampleStartAttach   = std::make_unique<LayerUI::SliderAtt> (apvts, p + "SampleStart",   ui.sampleStartSlider);
    ui.sampleAttackAttach  = std::make_unique<LayerUI::SliderAtt> (apvts, p + "SampleAttack",  ui.sampleAttackSlider);
    ui.sampleSustainAttach = std::make_unique<LayerUI::SliderAtt> (apvts, p + "SampleSustain", ui.sampleSustainSlider);
    ui.sampleDecayAttach   = std::make_unique<LayerUI::SliderAtt> (apvts, p + "SampleDecay",   ui.sampleDecaySlider);

    ui.samplePreview = std::make_unique<SamplePreviewComponent>();
    ui.samplePreview->onStartMsChanged   = [this, idx] (float v) { layerUIs[idx].sampleStartSlider  .setValue (v, juce::sendNotification); };
    ui.samplePreview->onAttackMsChanged  = [this, idx] (float v) { layerUIs[idx].sampleAttackSlider .setValue (v, juce::sendNotification); };
    ui.samplePreview->onSustainMsChanged = [this, idx] (float v) { layerUIs[idx].sampleSustainSlider.setValue (v, juce::sendNotification); };
    ui.samplePreview->onDecayMsChanged   = [this, idx] (float v) { layerUIs[idx].sampleDecaySlider  .setValue (v, juce::sendNotification); };
    ui.samplePreview->onClicked = [this, idx]
    {
        auto& ui2 = layerUIs[idx];
        ui2.fileChooser = std::make_unique<juce::FileChooser> (
            "Load rattle sample",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory),
            "*.wav;*.aiff;*.aif;*.flac;*.mp3");
        ui2.fileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, idx] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f.existsAsFile())
                    audioProcessor.loadSampleFile (idx, f);
            });
    };

    {
        const auto refreshPrev = [this, idx] (float) { updateSamplePreview (idx); };
        ui.sampleStartPreviewAtt   = std::make_unique<LayerUI::ParamAtt> (*apvts.getParameter (p + "SampleStart"),   refreshPrev);
        ui.sampleAttackPreviewAtt  = std::make_unique<LayerUI::ParamAtt> (*apvts.getParameter (p + "SampleAttack"),  refreshPrev);
        ui.sampleSustainPreviewAtt = std::make_unique<LayerUI::ParamAtt> (*apvts.getParameter (p + "SampleSustain"), refreshPrev);
        ui.sampleDecayPreviewAtt   = std::make_unique<LayerUI::ParamAtt> (*apvts.getParameter (p + "SampleDecay"),   refreshPrev);
        ui.sampleGainPreviewAtt    = std::make_unique<LayerUI::ParamAtt> (*apvts.getParameter (p + "SampleGain"),    refreshPrev);
    }

    ui.sampleFilterXY = std::make_unique<FilterXYPad> (
        *apvts.getParameter (p + "SampleFreq"), *apvts.getParameter (p + "SampleBw"));
    addChildComponent (ui.samplePitchSlider); addChildComponent (ui.samplePitchLabel);
    addChildComponent (ui.sampleGainSlider);  addChildComponent (ui.sampleGainLabel);
    addChildComponent (*ui.samplePreview);
    addChildComponent (*ui.sampleFilterXY);

    // ModalRattle mode
    setupKnob (ui.rattleGapSlider,     "Gap",       ui.rattleGapLabel);
    setupKnob (ui.rattleKSlider,       "Stiffness", ui.rattleKLabel);
    setupKnob (ui.rattleJitterSlider,  "Jitter",    ui.rattleJitterLabel);
    ui.rattleGapAttach    = std::make_unique<LayerUI::SliderAtt> (apvts, p + "RattleGap",    ui.rattleGapSlider);
    ui.rattleKAttach      = std::make_unique<LayerUI::SliderAtt> (apvts, p + "RattleK",      ui.rattleKSlider);
    ui.rattleJitterAttach = std::make_unique<LayerUI::SliderAtt> (apvts, p + "RattleJitter", ui.rattleJitterSlider);
    ui.rattleFilterXY = std::make_unique<FilterXYPad> (
        *apvts.getParameter (p + "RattleFilterFreq"), *apvts.getParameter (p + "RattleFilterBw"));
    ui.rattleDCAttach    = std::make_unique<LayerUI::ButtonAtt> (apvts, p + "RattleDCEnable", ui.rattleDCToggle);
    ui.rattleDCPreAttach = std::make_unique<LayerUI::ButtonAtt> (apvts, p + "RattleDCPre",    ui.rattleDCPreToggle);
    ui.rattleDCToggle   .setButtonText ("DC Block");
    ui.rattleDCPreToggle.setButtonText ("Pre-Sat");
    ui.rattleDCToggle   .setLookAndFeel (&pillToggleLnf);
    ui.rattleDCPreToggle.setLookAndFeel (&pillToggleLnf);
    addChildComponent (ui.rattleGapSlider);    addChildComponent (ui.rattleGapLabel);
    addChildComponent (ui.rattleKSlider);      addChildComponent (ui.rattleKLabel);
    addChildComponent (ui.rattleJitterSlider); addChildComponent (ui.rattleJitterLabel);
    addChildComponent (ui.rattleDCToggle);
    addChildComponent (ui.rattleDCPreToggle);
    addChildComponent (*ui.rattleFilterXY);

    // Trigger tab
    setupKnob (ui.trigThreshSlider, "Threshold", ui.trigThreshLabel);
    ui.trigThreshAttach = std::make_unique<LayerUI::SliderAtt> (apvts, p + "TrigThresh", ui.trigThreshSlider);
    ui.trigXY = std::make_unique<TriggerXYPad> (
        *apvts.getParameter (p + "TrigFreq"), *apvts.getParameter (p + "TrigQ"));
    ui.trigFilterEnableAttach = std::make_unique<LayerUI::ButtonAtt> (apvts, p + "TrigFilterEnable", ui.trigFilterEnableToggle);
    ui.trigFilterEnableToggle.setButtonText ("Filter");
    ui.trigFilterEnableToggle.setLookAndFeel (&pillToggleLnf);
    ui.trigFilterEnableToggle.onStateChange = [this, idx]
    {
        if (!layerBtns[idx].active) return;
        const bool en = layerUIs[idx].trigFilterEnableToggle.getToggleState();
        layerUIs[idx].trigXY->setEnabled (en);
        layerUIs[idx].trigXY->repaint();
    };
    {
        const bool initEn = apvts.getRawParameterValue (p + "TrigFilterEnable")->load() > 0.5f;
        ui.trigXY->setEnabled (initEn);
    }
    addChildComponent (ui.trigThreshSlider); addChildComponent (ui.trigThreshLabel);
    addChildComponent (ui.trigFilterEnableToggle);
    addChildComponent (*ui.trigXY);

    // Resonator tab — Noise/Bounce/Sample modes
    setupKnob  (ui.resGainSlider,   "Input",   ui.resGainLabel);
    setupKnob  (ui.resWetSlider,    "Dry/Wet", ui.resWetLabel);
    setupKnob  (ui.resModesSlider,  "Modes",   ui.resModesLabel);
    setupKnob  (ui.resRoughSlider,  "Rough",   ui.resRoughLabel);
    setupKnob  (ui.resToneSlider,   "Tone",    ui.resToneLabel);
    setupKnob  (ui.resSpreadSlider, "Spread",  ui.resSpreadLabel);
    setupCombo (ui.resMaterialCombo);
    {
        const auto names = ModalResonatorModel::getMaterialNames();
        for (int i = 0; i < names.size(); ++i)
            ui.resMaterialCombo.addItem (names[i], i + 1);
    }
    ui.resGainAttach     = std::make_unique<LayerUI::SliderAtt> (apvts, p + "ResGain",   ui.resGainSlider);
    ui.resClipAttach     = std::make_unique<LayerUI::ButtonAtt> (apvts, p + "ResClip",   ui.resClipToggle);
    ui.resWetAttach      = std::make_unique<LayerUI::SliderAtt> (apvts, p + "ResWet",    ui.resWetSlider);
    ui.resSatAttach      = std::make_unique<LayerUI::SliderAtt> (apvts, p + "ResSat",    ui.resSatSlider);
    ui.resModesAttach    = std::make_unique<LayerUI::SliderAtt> (apvts, p + "ResModes",  ui.resModesSlider);
    ui.resRoughAttach    = std::make_unique<LayerUI::SliderAtt> (apvts, p + "ResRough",  ui.resRoughSlider);
    ui.resToneAttach     = std::make_unique<LayerUI::SliderAtt> (apvts, p + "ResTone",   ui.resToneSlider);
    ui.resSpreadAttach   = std::make_unique<LayerUI::SliderAtt> (apvts, p + "ResSpread", ui.resSpreadSlider);
    ui.resMaterialAttach = std::make_unique<LayerUI::ComboAtt>  (apvts, p + "ResMat",   ui.resMaterialCombo);
    ui.resMaterialCombo.onChange = [&ui]()
    {
        const bool unison = (ui.resMaterialCombo.getSelectedItemIndex()
                             == ModalResonatorModel::kNumMaterials - 1);
        ui.resToneLabel.setText (unison ? "Detune" : "Tone", juce::dontSendNotification);
    };
    ui.resClipToggle.setButtonText ("Soft Clip");
    ui.resClipToggle.setLookAndFeel (&pillToggleLnf);
    ui.resPad = std::make_unique<TuneDecayPad> (
        *apvts.getParameter (p + "ResTune"), *apvts.getParameter (p + "ResDecay"));
    addChildComponent (ui.resGainSlider);    addChildComponent (ui.resGainLabel);
    addChildComponent (ui.resClipToggle);
    addChildComponent (ui.resWetSlider);     addChildComponent (ui.resWetLabel);
    addChildComponent (ui.resModesSlider);   addChildComponent (ui.resModesLabel);
    addChildComponent (ui.resRoughSlider);   addChildComponent (ui.resRoughLabel);
    addChildComponent (ui.resToneSlider);    addChildComponent (ui.resToneLabel);
    addChildComponent (ui.resSpreadSlider);  addChildComponent (ui.resSpreadLabel);
    addChildComponent (ui.resMaterialCombo);
    addChildComponent (*ui.resPad);

    // Resonator tab — ModalRattle mode
    setupKnob  (ui.modesSlider,  "Modes",     ui.modesLabel);
    setupKnob  (ui.dampSlider,   "Damping",   ui.dampLabel);
    setupKnob  (ui.roughSlider,  "Roughness", ui.roughLabel);
    setupKnob  (ui.toneSlider,   "Tone",      ui.toneLabel);
    setupKnob  (ui.spreadSlider, "Spread",    ui.spreadLabel);
    setupCombo (ui.rattleMaterialCombo);
    {
        const auto names = ModalRattleModel::getMaterialNames();
        for (int i = 0; i < names.size(); ++i)
            ui.rattleMaterialCombo.addItem (names[i], i + 1);
    }
    ui.modesAttach          = std::make_unique<LayerUI::SliderAtt> (apvts, p + "RattleModes",  ui.modesSlider);
    ui.dampAttach           = std::make_unique<LayerUI::SliderAtt> (apvts, p + "RattleDamp",   ui.dampSlider);
    ui.roughAttach          = std::make_unique<LayerUI::SliderAtt> (apvts, p + "RattleRough",  ui.roughSlider);
    ui.toneAttach           = std::make_unique<LayerUI::SliderAtt> (apvts, p + "RattleTone",   ui.toneSlider);
    ui.spreadAttach         = std::make_unique<LayerUI::SliderAtt> (apvts, p + "RattleSpread", ui.spreadSlider);
    ui.rattleMaterialAttach  = std::make_unique<LayerUI::ComboAtt>   (apvts, p + "RattleMat",       ui.rattleMaterialCombo);
    ui.rattleModalSatAttach  = std::make_unique<LayerUI::ButtonAtt>  (apvts, p + "RattleModalSat",   ui.rattleModalSatToggle);
    ui.rattleModalSatToggle.setButtonText ("Feedback Saturation");
    ui.rattleModalSatToggle.setLookAndFeel (&pillToggleLnf);
    ui.rattleResPad = std::make_unique<TuneDecayPad> (
        *apvts.getParameter (p + "RattleTune"), *apvts.getParameter (p + "RattleDecay"));
    ui.rattleModalSatToggle.onStateChange = [this, idx]
    {
        layerUIs[idx].rattleResPad->setShowDanger (!layerUIs[idx].rattleModalSatToggle.getToggleState());
    };
    ui.rattleResPad->setShowDanger (apvts.getRawParameterValue (p + "RattleModalSat")->load() < 0.5f);
    ui.sourceFilterEnableAttach = std::make_unique<LayerUI::ButtonAtt> (apvts, p + "SourceFilterEnable", ui.sourceFilterEnableToggle);
    ui.sourceFilterEnableToggle.setButtonText ("Filter");
    ui.sourceFilterEnableToggle.setLookAndFeel (&pillToggleLnf);
    // Wire toggle to grey out whichever filter pad is visible
    ui.sourceFilterEnableToggle.onStateChange = [this, idx]
    {
        if (!layerBtns[idx].active) return;
        const bool en = layerUIs[idx].sourceFilterEnableToggle.getToggleState();
        layerUIs[idx].noiseFilterXY ->setEnabled (en); layerUIs[idx].noiseFilterXY ->repaint();
        layerUIs[idx].bounceFilterXY->setEnabled (en); layerUIs[idx].bounceFilterXY->repaint();
        layerUIs[idx].sampleFilterXY->setEnabled (en); layerUIs[idx].sampleFilterXY->repaint();
        layerUIs[idx].rattleFilterXY->setEnabled (en); layerUIs[idx].rattleFilterXY->repaint();
    };
    // Apply initial state
    {
        const bool initEn = apvts.getRawParameterValue (p + "SourceFilterEnable")->load() > 0.5f;
        ui.noiseFilterXY ->setEnabled (initEn);
        ui.bounceFilterXY->setEnabled (initEn);
        ui.sampleFilterXY->setEnabled (initEn);
        ui.rattleFilterXY->setEnabled (initEn);
    }

    addChildComponent (ui.modesSlider);          addChildComponent (ui.modesLabel);
    addChildComponent (ui.dampSlider);           addChildComponent (ui.dampLabel);
    addChildComponent (ui.roughSlider);          addChildComponent (ui.roughLabel);
    addChildComponent (ui.toneSlider);           addChildComponent (ui.toneLabel);
    addChildComponent (ui.spreadSlider);         addChildComponent (ui.spreadLabel);
    addChildComponent (ui.rattleMaterialCombo);
    addChildComponent (ui.rattleModalSatToggle);
    addChildComponent (*ui.rattleResPad);
    addChildComponent (ui.sourceFilterEnableToggle);

    // Convolution tab — ModalRattle only
    ui.convEnableAttach = std::make_unique<LayerUI::ButtonAtt> (apvts, p + "ConvEnable", ui.convEnableToggle);
    ui.convEnableToggle.setButtonText ("Enable Convolution");
    ui.convEnableToggle.setLookAndFeel (&pillToggleLnf);

    // Visible knobs
    setupKnob (ui.convWetSlider,    "Feedback", ui.convWetLabel);
    setupKnob (ui.convDryWetSlider, "Dry/Wet",  ui.convDryWetLabel);
    setupKnob (ui.convPitchSlider,  "Pitch",    ui.convPitchLabel);
    setupKnob (ui.convGainSlider,   "Gain",     ui.convGainLabel);

    ui.convWetAttach    = std::make_unique<LayerUI::SliderAtt> (apvts, p + "ConvWet",    ui.convWetSlider);
    ui.convDryWetAttach = std::make_unique<LayerUI::SliderAtt> (apvts, p + "ConvDryWet", ui.convDryWetSlider);
    ui.convPitchAttach  = std::make_unique<LayerUI::SliderAtt> (apvts, p + "ConvPitch",  ui.convPitchSlider);
    ui.convGainAttach   = std::make_unique<LayerUI::SliderAtt> (apvts, p + "ConvGain",   ui.convGainSlider);

    // Hidden backing sliders — driven by IRPreviewComponent handles
    setupKnob (ui.convDecaySlider,   "", ui.convDecayLabel);
    setupKnob (ui.convAttackSlider,  "", ui.convAttackLabel);
    setupKnob (ui.convSustainSlider, "", ui.convSustainLabel);
    setupKnob (ui.convStartSlider,   "", ui.convStartLabel);

    ui.convDecayAttach   = std::make_unique<LayerUI::SliderAtt> (apvts, p + "ConvDecay",   ui.convDecaySlider);
    ui.convAttackAttach  = std::make_unique<LayerUI::SliderAtt> (apvts, p + "ConvAttack",  ui.convAttackSlider);
    ui.convSustainAttach = std::make_unique<LayerUI::SliderAtt> (apvts, p + "ConvSustain", ui.convSustainSlider);
    ui.convStartAttach   = std::make_unique<LayerUI::SliderAtt> (apvts, p + "ConvStart",   ui.convStartSlider);

    setupKnob (ui.convSplitSlider, "Kernel", ui.convSplitLabel);
    ui.convSplitSlider.setTextValueSuffix (" ms");
    ui.convSplitAttach = std::make_unique<LayerUI::SliderAtt> (apvts, p + "ConvSplit", ui.convSplitSlider);
    addChildComponent (ui.convSplitSlider); addChildComponent (ui.convSplitLabel);

    // ParameterAttachment fires its callback AFTER the value is written to APVTS,
    // making it reliable for triggering reprocessing regardless of call order.
    {
        // Non-pitch params: suppress during handle drag, fire immediately otherwise.
        auto makeAtt = [&] (const juce::String& paramId) {
            return std::make_unique<juce::ParameterAttachment> (
                *apvts.getParameter (paramId),
                [this, idx, paramId] (float newVal) {
                    juce::ignoreUnused (newVal, paramId);
                    if (!layerUIs[idx].suppressConvReprocess)
                    {
                        RLOG ("EnvAtt[" + juce::String (idx) + "] " + paramId
                              + "=" + juce::String (newVal, 3) + " => reprocess");
                        audioProcessor.reprocessConvIR (idx);
                        updateIRPreview (idx);
                    }
                    else
                    {
                        RLOG ("EnvAtt[" + juce::String (idx) + "] " + paramId
                              + "=" + juce::String (newVal, 3) + " SUPPRESSED");
                    }
                });
        };
        // Envelope shape params: suppress during handle drag (the drag itself sets these).
        ui.convDecayReprocessAtt   = makeAtt (p + "ConvDecay");
        ui.convStartReprocessAtt   = makeAtt (p + "ConvStart");
        ui.convAttackReprocessAtt  = makeAtt (p + "ConvAttack");
        ui.convSustainReprocessAtt = makeAtt (p + "ConvSustain");
        ui.convSplitReprocessAtt   = makeAtt (p + "ConvSplit");

        // Gain: IRPreview never controls this, so never suppress it.
        ui.convGainReprocessAtt = std::make_unique<juce::ParameterAttachment> (
            *apvts.getParameter (p + "ConvGain"),
            [this, idx] (float newGain) {
                juce::ignoreUnused (newGain);
                RLOG ("GainAtt[" + juce::String (idx) + "] gain=" + juce::String (newGain, 2)
                      + " suppress=" + juce::String ((int) layerUIs[idx].suppressConvReprocess));
                audioProcessor.reprocessConvIR (idx);
                updateIRPreview (idx);
            });

        // Pitch: mode-aware (60Hz / 10Hz / RT).
        // Neither pitch nor gain check suppressConvReprocess — the IRPreview only controls
        // the envelope shape (decay/start/attack/sustain), not pitch or gain.
        ui.convPitchReprocessAtt = std::make_unique<juce::ParameterAttachment> (
            *apvts.getParameter (p + "ConvPitch"),
            [this, idx] (float newPitchSt) {
                auto& ui2 = layerUIs[idx];
                RLOG ("PitchAtt[" + juce::String (idx) + "] st=" + juce::String (newPitchSt, 2)
                      + " mode=" + juce::String (ui2.convPitchMode)
                      + " suppress=" + juce::String ((int) ui2.suppressConvReprocess));
                switch (ui2.convPitchMode)
                {
                    case 0: // 60Hz — immediate
                        audioProcessor.reprocessConvIR (idx);
                        updateIRPreview (idx);
                        break;
                    case 1: // 10Hz — throttled
                    {
                        const juce::int64 now = juce::Time::currentTimeMillis();
                        if (now - ui2.lastPitchReprocessMs >= 100)
                        {
                            ui2.lastPitchReprocessMs = now;
                            audioProcessor.reprocessConvIR (idx);
                            updateIRPreview (idx);
                        }
                        else
                        {
                            RLOG ("PitchAtt[" + juce::String (idx) + "] 10Hz throttled");
                        }
                        break;
                    }
                    case 2: // RT — update per-block pitch ratio, no IR reload
                        audioProcessor.convPitchRTRatio[idx].store (
                            std::pow (2.f, newPitchSt / 12.f));
                        break;
                    default: break;
                }
            });
    }

    // Pitch mode buttons (60Hz / 10Hz / RT)
    {
        auto setupPitchBtn = [&] (juce::TextButton& btn, const juce::String& label)
        {
            btn.setButtonText (label);
            btn.setClickingTogglesState (false);
            btn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff1a1a1a));
            btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1e3050));
            btn.setColour (juce::TextButton::textColourOffId,  kSubtext);
            btn.setColour (juce::TextButton::textColourOnId,   kAccent);
            addChildComponent (btn);
        };
        setupPitchBtn (ui.convPitch60Btn, "60Hz");
        setupPitchBtn (ui.convPitch10Btn, "10Hz");
        setupPitchBtn (ui.convPitchRTBtn, "RT");

        auto switchPitchMode = [this, idx, p] (int newMode)
        {
            auto& ui2 = layerUIs[idx];
            const int oldMode = ui2.convPitchMode;
            ui2.convPitchMode = newMode;
            ui2.convPitch60Btn.setToggleState (newMode == 0, juce::dontSendNotification);
            ui2.convPitch10Btn.setToggleState (newMode == 1, juce::dontSendNotification);
            ui2.convPitchRTBtn.setToggleState (newMode == 2, juce::dontSendNotification);
            if (newMode == 2 && oldMode != 2)
            {
                // Entering RT mode: set flag first so reprocess bakes at pitch=0
                const float pitchSt = audioProcessor.apvts
                                        .getRawParameterValue (p + "ConvPitch")->load();
                audioProcessor.convUseRTPitch[idx].store (true);
                audioProcessor.convPitchRTRatio[idx].store (std::pow (2.f, pitchSt / 12.f));
                audioProcessor.reprocessConvIR (idx);
            }
            else if (newMode != 2 && oldMode == 2)
            {
                // Leaving RT mode: clear flag so reprocess bakes pitch into IR
                audioProcessor.convUseRTPitch[idx].store (false);
                audioProcessor.reprocessConvIR (idx);
            }
        };

        ui.convPitch60Btn.onClick = [switchPitchMode] { switchPitchMode (0); };
        ui.convPitch10Btn.onClick = [switchPitchMode] { switchPitchMode (1); };
        ui.convPitchRTBtn.onClick = [switchPitchMode] { switchPitchMode (2); };

        // Default: 60Hz selected
        ui.convPitch60Btn.setToggleState (true, juce::dontSendNotification);
    }

    // IRPreview handle callbacks: write to APVTS — ParameterAttachment triggers reprocess.
    ui.irPreview = std::make_unique<IRPreviewComponent>();

    auto setParamDirect = [this] (const juce::String& paramId, float value)
    {
        if (auto* param = audioProcessor.apvts.getParameter (paramId))
        {
            const auto range = audioProcessor.apvts.getParameterRange (paramId);
            param->beginChangeGesture();
            param->setValueNotifyingHost (range.convertTo0to1 (value));
            param->endChangeGesture();
        }
    };

    // Suppress reprocessing during handle drag; fire once when drag ends.
    ui.irPreview->onStartMsChanged   = [this, idx, p, setParamDirect] (float ms) {
        RLOG ("IRHandle[" + juce::String (idx) + "] START=" + juce::String (ms, 1) + " -> suppress=true");
        layerUIs[idx].suppressConvReprocess = true;
        setParamDirect (p + "ConvStart",   ms);
    };
    ui.irPreview->onAttackMsChanged  = [this, idx, p, setParamDirect] (float ms) {
        RLOG ("IRHandle[" + juce::String (idx) + "] ATTACK=" + juce::String (ms, 1) + " -> suppress=true");
        layerUIs[idx].suppressConvReprocess = true;
        setParamDirect (p + "ConvAttack",  ms);
    };
    ui.irPreview->onSustainMsChanged = [this, idx, p, setParamDirect] (float ms) {
        RLOG ("IRHandle[" + juce::String (idx) + "] SUSTAIN=" + juce::String (ms, 1) + " -> suppress=true");
        layerUIs[idx].suppressConvReprocess = true;
        setParamDirect (p + "ConvSustain", ms);
    };
    ui.irPreview->onDecayMsChanged   = [this, idx, p, setParamDirect] (float ms) {
        RLOG ("IRHandle[" + juce::String (idx) + "] DECAY=" + juce::String (ms, 1) + " -> suppress=true");
        layerUIs[idx].suppressConvReprocess = true;
        setParamDirect (p + "ConvDecay",   ms);
    };
    ui.irPreview->onAnyHandleDragEnded = [this, idx, p] {
        layerUIs[idx].suppressConvReprocess = false;
        RLOG ("IRHandle[" + juce::String (idx) + "] drag ended -> suppress=false, reprocess"
              + " gain="  + juce::String (audioProcessor.apvts.getRawParameterValue (p + "ConvGain") ->load(), 2)
              + " pitch=" + juce::String (audioProcessor.apvts.getRawParameterValue (p + "ConvPitch")->load(), 2));
        audioProcessor.reprocessConvIR (idx);
        updateIRPreview (idx);
    };

    // Wiring done after irPreview is created
    ui.convEnableToggle.onStateChange = [this, idx]
    {
        applyConvEnabled (idx, layerUIs[idx].convEnableToggle.getToggleState());
    };
    applyConvEnabled (idx, apvts.getRawParameterValue (p + "ConvEnable")->load() > 0.5f);

    ui.loadConvIRBtn.setButtonText ("Load IR");
    ui.loadConvIRBtn.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff333333));
    ui.loadConvIRBtn.setColour (juce::TextButton::buttonOnColourId, kAccent);
    ui.loadConvIRBtn.setColour (juce::TextButton::textColourOffId,  kText);
    ui.loadConvIRBtn.onClick = [this, idx, p]
    {
        auto& ui2 = layerUIs[idx];
        ui2.convFileChooser = std::make_unique<juce::FileChooser> (
            "Load impulse response",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory),
            "*.wav;*.aiff;*.aif;*.flac");
        ui2.convFileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, idx] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f.existsAsFile())
                {
                    audioProcessor.loadConvIR (idx, f);
                    layerUIs[idx].loadConvIRBtn.setButtonText (f.getFileNameWithoutExtension());
                    updateIRPreview (idx);
                }
            });
    };
    // Visible controls
    addChildComponent (ui.convEnableToggle);
    addChildComponent (ui.convWetSlider);    addChildComponent (ui.convWetLabel);
    addChildComponent (ui.convDryWetSlider); addChildComponent (ui.convDryWetLabel);
    addChildComponent (ui.convPitchSlider);  addChildComponent (ui.convPitchLabel);
    addChildComponent (ui.convGainSlider);   addChildComponent (ui.convGainLabel);
    addChildComponent (*ui.irPreview);
    addChildComponent (ui.loadConvIRBtn);
    // Hidden APVTS-backing sliders (never set visible)
    addChildComponent (ui.convDecaySlider);
    addChildComponent (ui.convAttackSlider);
    addChildComponent (ui.convSustainSlider);
    addChildComponent (ui.convStartSlider);
}

// =============================================================================
// Helpers
// =============================================================================
void RattlerAudioProcessorEditor::setupKnob (juce::Slider& s,
                                              const juce::String& labelText,
                                              juce::Label& l)
{
    s.setLookAndFeel (&knobLnf);
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 13);
    s.setColour (juce::Slider::thumbColourId,            kAccent);
    s.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    s.setColour (juce::Slider::textBoxTextColourId,      kText);
    s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff111111));
    s.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colour (0xff111111));

    l.setText (labelText, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::Font (juce::FontOptions (11.0f)));
    l.setColour (juce::Label::textColourId, kSubtext);
}

void RattlerAudioProcessorEditor::setupCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2d2d2d));
    c.setColour (juce::ComboBox::textColourId,       kText);
    c.setColour (juce::ComboBox::arrowColourId,      kSubtext);
    c.setColour (juce::ComboBox::outlineColourId,    kGrid);
}

void RattlerAudioProcessorEditor::layoutKnob (juce::Slider& s, juce::Label& l,
                                               juce::Rectangle<int> r)
{
    const int lblH = 13;
    l.setBounds (r.getX(), r.getBottom() - lblH, r.getWidth(), lblH);
    s.setBounds (r.getX(), r.getY(), r.getWidth(), r.getHeight() - lblH);
}

RattlerAudioProcessor::LayerMode
RattlerAudioProcessorEditor::getLayerMode (int idx) const
{
    const juce::String p = (idx == 0) ? "layerAMode" : "layerBMode";
    return (RattlerAudioProcessor::LayerMode)(int)
        audioProcessor.apvts.getRawParameterValue (p)->load();
}

// =============================================================================
// setLayerVisible — hides/shows all of a layer's tab content (not the mode combo)
// =============================================================================
void RattlerAudioProcessorEditor::setLayerVisible (int idx, bool v)
{
    auto& ui = layerUIs[idx];

    // Common source
    ui.levelSlider.setVisible (v); ui.levelLabel.setVisible (v);
    ui.satSlider  .setVisible (v); ui.satLabel  .setVisible (v);
    ui.sourceFilterEnableToggle.setVisible (v);

    // Mode-specific source
    ui.noiseBurstSlider .setVisible (v); ui.noiseBurstLabel .setVisible (v);
    ui.noiseAttackSlider.setVisible (v); ui.noiseAttackLabel.setVisible (v);
    ui.noiseFilterXY->setVisible (v);

    ui.bounceMassSlider  .setVisible (v); ui.bounceMassLabel  .setVisible (v);
    ui.bounceGapSlider   .setVisible (v); ui.bounceGapLabel   .setVisible (v);
    ui.bounceRestSlider  .setVisible (v); ui.bounceRestLabel  .setVisible (v);
    ui.bounceWiresSlider .setVisible (v); ui.bounceWiresLabel .setVisible (v);
    ui.bounceSpreadSlider.setVisible (v); ui.bounceSpreadLabel.setVisible (v);
    ui.bounceFilterXY->setVisible (v);

    ui.samplePitchSlider   .setVisible (v); ui.samplePitchLabel   .setVisible (v);
    ui.sampleGainSlider    .setVisible (v); ui.sampleGainLabel    .setVisible (v);
    ui.samplePreview->setVisible (v);
    ui.sampleFilterXY->setVisible (v);

    ui.rattleGapSlider   .setVisible (v); ui.rattleGapLabel   .setVisible (v);
    ui.rattleKSlider     .setVisible (v); ui.rattleKLabel     .setVisible (v);
    ui.rattleJitterSlider.setVisible (v); ui.rattleJitterLabel.setVisible (v);
    ui.rattleDCToggle    .setVisible (v);
    ui.rattleDCPreToggle .setVisible (v);
    ui.rattleFilterXY->setVisible (v);

    // Trigger
    ui.trigThreshSlider.setVisible (v);      ui.trigThreshLabel.setVisible (v);
    ui.trigFilterEnableToggle.setVisible (v);
    ui.trigXY->setVisible (v);

    // Resonator — simple
    ui.resGainSlider   .setVisible (v); ui.resGainLabel   .setVisible (v);
    ui.resClipToggle   .setVisible (v);
    ui.resWetSlider    .setVisible (v); ui.resWetLabel    .setVisible (v);
    ui.resModesSlider  .setVisible (v); ui.resModesLabel  .setVisible (v);
    ui.resRoughSlider  .setVisible (v); ui.resRoughLabel  .setVisible (v);
    ui.resToneSlider   .setVisible (v); ui.resToneLabel   .setVisible (v);
    ui.resSpreadSlider .setVisible (v); ui.resSpreadLabel .setVisible (v);
    ui.resMaterialCombo.setVisible (v);
    ui.resPad->setVisible (v);

    // Resonator — modal rattle
    ui.modesSlider        .setVisible (v); ui.modesLabel .setVisible (v);
    ui.dampSlider         .setVisible (v); ui.dampLabel  .setVisible (v);
    ui.roughSlider        .setVisible (v); ui.roughLabel .setVisible (v);
    ui.toneSlider         .setVisible (v); ui.toneLabel  .setVisible (v);
    ui.spreadSlider       .setVisible (v); ui.spreadLabel.setVisible (v);
    ui.rattleMaterialCombo.setVisible (v);
    ui.rattleModalSatToggle.setVisible (v);
    ui.rattleResPad->setVisible (v);

    // Convolution (hidden backing sliders never toggled here)
    ui.convEnableToggle.setVisible (v);
    ui.convWetSlider   .setVisible (v); ui.convWetLabel   .setVisible (v);
    ui.convDryWetSlider.setVisible (v); ui.convDryWetLabel.setVisible (v);
    ui.convSplitSlider .setVisible (v); ui.convSplitLabel .setVisible (v);
    ui.convPitchSlider .setVisible (v); ui.convPitchLabel .setVisible (v);
    ui.convGainSlider  .setVisible (v); ui.convGainLabel  .setVisible (v);
    ui.convPitch60Btn  .setVisible (v);
    ui.convPitch10Btn  .setVisible (v);
    ui.convPitchRTBtn  .setVisible (v);
    ui.irPreview->setVisible (v);
    ui.loadConvIRBtn   .setVisible (v);
}

// =============================================================================
// setLayerEnabled — enable/disable all controls in a layer row
// =============================================================================
void RattlerAudioProcessorEditor::setLayerEnabled (int idx, bool en)
{
    auto& ui = layerUIs[idx];

    // Standard controls: JUCE draws them greyed-out when disabled
    ui.modeCombo             .setEnabled (en);
    ui.levelSlider           .setEnabled (en);
    ui.satSlider             .setEnabled (en);
    ui.sourceFilterEnableToggle.setEnabled (en);
    ui.noiseBurstSlider      .setEnabled (en);
    ui.noiseAttackSlider     .setEnabled (en);
    ui.samplePitchSlider     .setEnabled (en);
    ui.sampleGainSlider      .setEnabled (en);
    ui.samplePreview->setEnabled (en); ui.samplePreview->repaint();
    ui.bounceMassSlider      .setEnabled (en);
    ui.bounceGapSlider       .setEnabled (en);
    ui.bounceRestSlider      .setEnabled (en);
    ui.bounceWiresSlider     .setEnabled (en);
    ui.bounceSpreadSlider    .setEnabled (en);
    ui.rattleGapSlider       .setEnabled (en);
    ui.rattleKSlider         .setEnabled (en);
    ui.rattleJitterSlider    .setEnabled (en);
    ui.rattleDCToggle        .setEnabled (en);
    ui.rattleDCPreToggle     .setEnabled (en);
    ui.trigThreshSlider      .setEnabled (en);
    ui.trigFilterEnableToggle.setEnabled (en);
    ui.resGainSlider         .setEnabled (en);
    ui.resClipToggle         .setEnabled (en);
    ui.resWetSlider          .setEnabled (en);
    ui.resMaterialCombo      .setEnabled (en);
    ui.modesSlider           .setEnabled (en);
    ui.dampSlider            .setEnabled (en);
    ui.roughSlider           .setEnabled (en);
    ui.toneSlider            .setEnabled (en);
    ui.spreadSlider          .setEnabled (en);
    ui.rattleMaterialCombo   .setEnabled (en);
    ui.rattleModalSatToggle  .setEnabled (en);
    ui.convEnableToggle      .setEnabled (en);
    ui.loadConvIRBtn         .setEnabled (en);
    applyConvEnabled (idx, en && ui.convEnableToggle.getToggleState());

    // XY pads: respect their individual filter toggles when re-enabling
    const bool srcFilt  = en && ui.sourceFilterEnableToggle.getToggleState();
    const bool trigFilt = en && ui.trigFilterEnableToggle  .getToggleState();

    ui.noiseFilterXY ->setEnabled (srcFilt);  ui.noiseFilterXY ->repaint();
    ui.bounceFilterXY->setEnabled (srcFilt);  ui.bounceFilterXY->repaint();
    ui.sampleFilterXY->setEnabled (srcFilt);  ui.sampleFilterXY->repaint();
    ui.rattleFilterXY->setEnabled (srcFilt);  ui.rattleFilterXY->repaint();
    ui.trigXY        ->setEnabled (trigFilt); ui.trigXY        ->repaint();
    ui.resPad        ->setEnabled (en);       ui.resPad        ->repaint();
    ui.rattleResPad  ->setEnabled (en);       ui.rattleResPad  ->repaint();
}

// =============================================================================
// applyConvEnabled — enable/disable every convolution control (bypass-driven)
// =============================================================================
void RattlerAudioProcessorEditor::applyConvEnabled (int idx, bool on)
{
    RLOG ("applyConvEnabled[" + juce::String (idx) + "] on=" + juce::String ((int) on));
    auto& ui = layerUIs[idx];
    ui.convWetSlider   .setEnabled (on); ui.convWetLabel   .setEnabled (on);
    ui.convDryWetSlider.setEnabled (on); ui.convDryWetLabel.setEnabled (on);
    ui.convSplitSlider .setEnabled (on); ui.convSplitLabel .setEnabled (on);
    ui.convPitchSlider .setEnabled (on); ui.convPitchLabel .setEnabled (on);
    ui.convGainSlider  .setEnabled (on); ui.convGainLabel  .setEnabled (on);
    if (ui.irPreview) { ui.irPreview->setEnabled (on); ui.irPreview->repaint(); }
}

// =============================================================================
// layoutLayer — positions all controls for one layer given the current tab/mode
// =============================================================================
void RattlerAudioProcessorEditor::layoutLayer (int idx)
{
    auto& ui = layerUIs[idx];
    using Mode = RattlerAudioProcessor::LayerMode;
    const Mode mode = getLayerMode (idx);

    const int rowY   = kContentY + idx * (kRowH + kRowGap);
    const int bodyY  = rowY + kHeaderH;
    const int bodyH  = kRowH - kHeaderH - kPad;

    // knobX: start of the knob/control area, after the vertical layer button + gap
    const int knobX     = kPad + kLBtnW;
    const int knobAreaW = kLeftW - knobX; // width available for knobs/combos

    // Mode combo — top flush with content box top edge
    ui.modeCombo.setBounds (knobX, rowY + kBoxInset, knobAreaW, kHeaderH - kBoxInset);

    const int xyX    = kLeftW + kPad;
    const int xyW    = kW - xyX - kPad - kBoxInset; // respect content box right inset
    const int xyPadV = kBoxInset; // XY pad vertical inset matches content box

    // Hide everything first, then show what's needed
    setLayerVisible (idx, false);

    if (currentTab == 0)
    {
        // Source tab — common 2-knob layout (overridden per mode below)
        const int slot2  = knobAreaW / 2;
        const int kw2    = slot2 - 3;
        layoutKnob (ui.levelSlider, ui.levelLabel, { knobX,          bodyY, kw2, bodyH });
        layoutKnob (ui.satSlider,   ui.satLabel,   { knobX + slot2,  bodyY, kw2, bodyH });
        ui.levelSlider.setVisible (true); ui.levelLabel.setVisible (true);
        ui.satSlider  .setVisible (true); ui.satLabel  .setVisible (true);

        // Filter-enable toggle — top-right corner of the XY area
        const int fEnH = 14, fEnW = 56;
        ui.sourceFilterEnableToggle.setBounds (xyX + xyW - fEnW, rowY + xyPadV, fEnW, fEnH);
        ui.sourceFilterEnableToggle.setVisible (true);
        const int padY = rowY + xyPadV + fEnH + 1;
        const int padH = kRowH - xyPadV * 2 - fEnH - 1;

        switch (mode)
        {
        case Mode::Noise:
        {
            const int slot4 = knobAreaW / 4;
            const int kw4   = slot4 - 2;
            layoutKnob (ui.levelSlider,       ui.levelLabel,       { knobX + slot4 * 0, bodyY, kw4, bodyH });
            layoutKnob (ui.noiseBurstSlider,  ui.noiseBurstLabel,  { knobX + slot4 * 1, bodyY, kw4, bodyH });
            layoutKnob (ui.noiseAttackSlider, ui.noiseAttackLabel, { knobX + slot4 * 2, bodyY, kw4, bodyH });
            layoutKnob (ui.satSlider,         ui.satLabel,         { knobX + slot4 * 3, bodyY, kw4, bodyH });
            ui.noiseBurstSlider .setVisible (true); ui.noiseBurstLabel .setVisible (true);
            ui.noiseAttackSlider.setVisible (true); ui.noiseAttackLabel.setVisible (true);
            ui.noiseFilterXY->setBounds (xyX, padY, xyW, padH);
            ui.noiseFilterXY->setVisible (true);
            break;
        }
        case Mode::Bounce:
        {
            // Row 1: Level | Sat | Wires | Spread
            // Row 2: Mass | Gap | Rest
            const int halfH = bodyH / 2;
            const int slot4 = knobAreaW / 4;
            const int kw4   = slot4 - 2;
            const int slot3 = knobAreaW / 3;
            const int kw3   = slot3 - 3;
            layoutKnob (ui.levelSlider,        ui.levelLabel,        { knobX + slot4 * 0, bodyY,         kw4, halfH });
            layoutKnob (ui.satSlider,          ui.satLabel,          { knobX + slot4 * 1, bodyY,         kw4, halfH });
            layoutKnob (ui.bounceWiresSlider,  ui.bounceWiresLabel,  { knobX + slot4 * 2, bodyY,         kw4, halfH });
            layoutKnob (ui.bounceSpreadSlider, ui.bounceSpreadLabel, { knobX + slot4 * 3, bodyY,         kw4, halfH });
            layoutKnob (ui.bounceMassSlider,   ui.bounceMassLabel,   { knobX + slot3 * 0, bodyY + halfH, kw3, halfH });
            layoutKnob (ui.bounceGapSlider,    ui.bounceGapLabel,    { knobX + slot3 * 1, bodyY + halfH, kw3, halfH });
            layoutKnob (ui.bounceRestSlider,   ui.bounceRestLabel,   { knobX + slot3 * 2, bodyY + halfH, kw3, halfH });
            ui.bounceMassSlider  .setVisible (true); ui.bounceMassLabel  .setVisible (true);
            ui.bounceGapSlider   .setVisible (true); ui.bounceGapLabel   .setVisible (true);
            ui.bounceRestSlider  .setVisible (true); ui.bounceRestLabel  .setVisible (true);
            ui.bounceWiresSlider .setVisible (true); ui.bounceWiresLabel .setVisible (true);
            ui.bounceSpreadSlider.setVisible (true); ui.bounceSpreadLabel.setVisible (true);
            ui.bounceFilterXY->setBounds (xyX, padY, xyW, padH);
            ui.bounceFilterXY->setVisible (true);
            break;
        }
        case Mode::Sample:
        {
            // Left panel: Level | Sat | Pitch | Gain — full height, no load button
            const int slot4 = knobAreaW / 4;
            const int kw4   = slot4 - 2;
            layoutKnob (ui.levelSlider,       ui.levelLabel,       { knobX + slot4 * 0, bodyY, kw4, bodyH });
            layoutKnob (ui.satSlider,         ui.satLabel,         { knobX + slot4 * 1, bodyY, kw4, bodyH });
            layoutKnob (ui.samplePitchSlider, ui.samplePitchLabel, { knobX + slot4 * 2, bodyY, kw4, bodyH });
            layoutKnob (ui.sampleGainSlider,  ui.sampleGainLabel,  { knobX + slot4 * 3, bodyY, kw4, bodyH });
            ui.samplePitchSlider.setVisible (true); ui.samplePitchLabel.setVisible (true);
            ui.sampleGainSlider .setVisible (true); ui.sampleGainLabel .setVisible (true);
            // Right panel: sample preview (clickable to load) top 60%, filter XY bottom 40%
            const int prevH = padH * 6 / 10;
            const int fxyH  = padH - prevH - 2;
            ui.samplePreview->setBounds (xyX, padY, xyW, prevH);
            ui.samplePreview->setVisible (true);
            ui.sampleFilterXY->setBounds (xyX, padY + prevH + 2, xyW, fxyH);
            ui.sampleFilterXY->setVisible (true);
            break;
        }
        case Mode::ModalRattle:
        {
            const int stripH    = showAdvanced ? 18 : 0;
            const int stripGap  = showAdvanced ? 2  : 0;
            const int knobbodyY = bodyY + stripH + stripGap;
            const int knobbodyH = bodyH - stripH - stripGap;
            const int halfH     = knobbodyH / 2;
            const int slot3     = knobAreaW / 3;
            const int kw3       = slot3 - 3;
            if (showAdvanced)
            {
                const int dcPreX = knobX + kw2 + 4;
                ui.rattleDCToggle   .setBounds (knobX,  bodyY, kw2,                    stripH);
                ui.rattleDCPreToggle.setBounds (dcPreX, bodyY, knobAreaW - kw2 - 4,    stripH);
                ui.rattleDCToggle   .setVisible (true);
                ui.rattleDCPreToggle.setVisible (true);
            }
            layoutKnob (ui.levelSlider,        ui.levelLabel,        { knobX,              knobbodyY,         kw2, halfH });
            layoutKnob (ui.satSlider,          ui.satLabel,          { knobX + slot2,      knobbodyY,         kw2, halfH });
            layoutKnob (ui.rattleGapSlider,    ui.rattleGapLabel,    { knobX + slot3 * 0,  knobbodyY + halfH, kw3, halfH });
            layoutKnob (ui.rattleKSlider,      ui.rattleKLabel,      { knobX + slot3 * 1,  knobbodyY + halfH, kw3, halfH });
            layoutKnob (ui.rattleJitterSlider, ui.rattleJitterLabel, { knobX + slot3 * 2,  knobbodyY + halfH, kw3, halfH });
            ui.rattleGapSlider   .setVisible (true); ui.rattleGapLabel   .setVisible (true);
            ui.rattleKSlider     .setVisible (true); ui.rattleKLabel     .setVisible (true);
            ui.rattleJitterSlider.setVisible (true); ui.rattleJitterLabel.setVisible (true);
            ui.rattleFilterXY->setBounds (xyX, padY, xyW, padH);
            ui.rattleFilterXY->setVisible (true);
            break;
        }
        }
    }
    else if (currentTab == 1)
    {
        // Trigger tab — same regardless of mode
        const int kw = 58;
        layoutKnob (ui.trigThreshSlider, ui.trigThreshLabel, { knobX, bodyY, kw, bodyH });

        const int fEnH = 14, fEnW = 56;
        ui.trigFilterEnableToggle.setBounds (xyX + xyW - fEnW, rowY + xyPadV, fEnW, fEnH);
        const int trigPadY = rowY + xyPadV + fEnH + 1;
        const int trigPadH = kRowH - xyPadV * 2 - fEnH - 1;
        ui.trigXY->setBounds (xyX, trigPadY, xyW, trigPadH);

        ui.trigThreshSlider       .setVisible (true); ui.trigThreshLabel.setVisible (true);
        ui.trigFilterEnableToggle .setVisible (true);
        ui.trigXY                ->setVisible (true);
    }
    else if (currentTab == 2)
    {
        // Resonator tab — depends on mode
        const int comboH = 24;

        if (mode == Mode::ModalRattle)
        {
            // Feedback Sat toggle strip at top, then two rows of knobs
            const int stripH    = 18;
            const int knobbodyY = bodyY + stripH + 2;
            const int knobbodyH = bodyH - stripH - 2;
            const int halfH     = knobbodyH / 2;
            const int slot3     = knobAreaW / 3;
            const int kw3       = slot3 - 3;
            ui.rattleModalSatToggle.setBounds (knobX, bodyY, kw3 * 2, stripH);
            layoutKnob (ui.modesSlider,  ui.modesLabel,  { knobX + slot3 * 0, knobbodyY,         kw3, halfH });
            layoutKnob (ui.dampSlider,   ui.dampLabel,   { knobX + slot3 * 1, knobbodyY,         kw3, halfH });
            layoutKnob (ui.roughSlider,  ui.roughLabel,  { knobX + slot3 * 2, knobbodyY,         kw3, halfH });
            layoutKnob (ui.toneSlider,   ui.toneLabel,   { knobX + slot3 * 0, knobbodyY + halfH, kw3, halfH });
            layoutKnob (ui.spreadSlider, ui.spreadLabel, { knobX + slot3 * 1, knobbodyY + halfH, kw3, halfH });

            const int matGap    = 3;
            const int padTop    = rowY + xyPadV + comboH + matGap;
            const int padHeight = kRowH - xyPadV * 2 - comboH - matGap;
            ui.rattleMaterialCombo.setBounds (xyX, rowY + xyPadV, xyW, comboH);
            ui.rattleResPad->setBounds (xyX, padTop, xyW, padHeight);
            ui.rattleModalSatToggle.setVisible (true);
            ui.modesSlider        .setVisible (true); ui.modesLabel .setVisible (true);
            ui.dampSlider         .setVisible (true); ui.dampLabel  .setVisible (true);
            ui.roughSlider        .setVisible (true); ui.roughLabel .setVisible (true);
            ui.toneSlider         .setVisible (true); ui.toneLabel  .setVisible (true);
            ui.spreadSlider       .setVisible (true); ui.spreadLabel.setVisible (true);
            ui.rattleMaterialCombo.setVisible (true);
            ui.rattleResPad->setVisible (true);
        }
        else
        {
            // Left panel: soft clip toggle strip, then row 1 = 3 knobs (Modes|Rough|Tone),
            // row 2 = 3 knobs (Spread|Input|Dry/Wet)
            const int stripH    = 18;
            const int knobbodyY = bodyY + stripH + 2;
            const int knobbodyH = bodyH - stripH - 2;
            const int halfH     = knobbodyH / 2;
            const int slot3     = knobAreaW / 3;
            const int kw3       = slot3 - 3;

            ui.resClipToggle.setBounds (knobX, bodyY, knobAreaW, stripH);
            layoutKnob (ui.resModesSlider,  ui.resModesLabel,  { knobX + slot3 * 0, knobbodyY,         kw3, halfH });
            layoutKnob (ui.resRoughSlider,  ui.resRoughLabel,  { knobX + slot3 * 1, knobbodyY,         kw3, halfH });
            layoutKnob (ui.resToneSlider,   ui.resToneLabel,   { knobX + slot3 * 2, knobbodyY,         kw3, halfH });
            layoutKnob (ui.resSpreadSlider, ui.resSpreadLabel, { knobX + slot3 * 0, knobbodyY + halfH, kw3, halfH });
            layoutKnob (ui.resGainSlider,   ui.resGainLabel,   { knobX + slot3 * 1, knobbodyY + halfH, kw3, halfH });
            layoutKnob (ui.resWetSlider,    ui.resWetLabel,    { knobX + slot3 * 2, knobbodyY + halfH, kw3, halfH });

            // Right panel: material combo above TuneDecayPad
            const int matGap    = 3;
            const int padTop    = rowY + xyPadV + comboH + matGap;
            const int padHeight = kRowH - xyPadV * 2 - comboH - matGap;
            ui.resMaterialCombo.setBounds (xyX, rowY + xyPadV, xyW, comboH);
            ui.resPad->setBounds (xyX, padTop, xyW, padHeight);

            ui.resClipToggle   .setVisible (true);
            ui.resGainSlider   .setVisible (true); ui.resGainLabel   .setVisible (true);
            ui.resWetSlider    .setVisible (true); ui.resWetLabel    .setVisible (true);
            ui.resModesSlider  .setVisible (true); ui.resModesLabel  .setVisible (true);
            ui.resRoughSlider  .setVisible (true); ui.resRoughLabel  .setVisible (true);
            ui.resToneSlider   .setVisible (true); ui.resToneLabel   .setVisible (true);
            ui.resSpreadSlider .setVisible (true); ui.resSpreadLabel .setVisible (true);
            ui.resMaterialCombo.setVisible (true);
            ui.resPad->setVisible (true);
            {
                const bool unison = (ui.resMaterialCombo.getSelectedItemIndex()
                                     == ModalResonatorModel::kNumMaterials - 1);
                ui.resToneLabel.setText (unison ? "Detune" : "Tone", juce::dontSendNotification);
            }
        }
    }
    else if (currentTab == 3)
    {
        // Convolution tab — available for all modes
        // ModalRattle: 4 knobs (Feedback | DryWet | Pitch | Gain)
        // Other modes: 3 knobs (DryWet | Pitch | Gain) — no feedback loop
        const bool hasConvFB = (mode == Mode::ModalRattle);

        const int stripH     = 18;
        const int modeStripH = showAdvanced ? 14 : 0;
        const int modeGap    = showAdvanced ? 2  : 0;
        const int knobbodyY  = bodyY + stripH + 3;
        const int knobbodyH  = bodyH - stripH - 3 - modeStripH - modeGap;

        ui.convEnableToggle.setBounds (knobX, bodyY, knobAreaW, stripH);
        ui.convEnableToggle.setVisible (true);

        if (hasConvFB)
        {
            // 2 rows: row1 = Feedback | Dry/Wet | Kernel; row2 = Pitch | Gain
            const int halfH  = knobbodyH / 2;
            const int slot3  = knobAreaW / 3;
            const int kw3    = slot3 - 3;
            layoutKnob (ui.convWetSlider,    ui.convWetLabel,    { knobX + slot3 * 0, knobbodyY,         kw3, halfH });
            layoutKnob (ui.convDryWetSlider, ui.convDryWetLabel, { knobX + slot3 * 1, knobbodyY,         kw3, halfH });
            layoutKnob (ui.convSplitSlider,  ui.convSplitLabel,  { knobX + slot3 * 2, knobbodyY,         kw3, halfH });

            const int slot2  = knobAreaW / 2;
            const int kw2    = slot2 - 3;
            const int row2H  = knobbodyH - halfH;
            const int knobH2 = row2H - modeStripH - 2;
            layoutKnob (ui.convPitchSlider,  ui.convPitchLabel,  { knobX + slot2 * 0, knobbodyY + halfH, kw2, knobH2 });
            layoutKnob (ui.convGainSlider,   ui.convGainLabel,   { knobX + slot2 * 1, knobbodyY + halfH, kw2, knobH2 });

            const int modeY  = knobbodyY + halfH + knobH2 + 2;
            const int btnW   = kw2 / 3;
            const int pitchX = knobX + slot2 * 0;
            ui.convPitch60Btn.setBounds (pitchX,           modeY, btnW,          modeStripH);
            ui.convPitch10Btn.setBounds (pitchX + btnW,    modeY, btnW,          modeStripH);
            ui.convPitchRTBtn.setBounds (pitchX + btnW*2,  modeY, kw2 - btnW*2, modeStripH);

            ui.convWetSlider   .setVisible (true); ui.convWetLabel   .setVisible (true);
            ui.convSplitSlider .setVisible (true); ui.convSplitLabel .setVisible (true);
        }
        else
        {
            const int slot3  = knobAreaW / 3;
            const int kw3    = slot3 - 3;
            layoutKnob (ui.convDryWetSlider, ui.convDryWetLabel, { knobX + slot3 * 0, knobbodyY, kw3, knobbodyH });
            layoutKnob (ui.convPitchSlider,  ui.convPitchLabel,  { knobX + slot3 * 1, knobbodyY, kw3, knobbodyH });
            layoutKnob (ui.convGainSlider,   ui.convGainLabel,   { knobX + slot3 * 2, knobbodyY, kw3, knobbodyH });

            const int modeY  = knobbodyY + knobbodyH + 2;
            const int btnW   = kw3 / 3;
            const int pitchX = knobX + slot3 * 1;
            ui.convPitch60Btn.setBounds (pitchX,           modeY, btnW,          modeStripH);
            ui.convPitch10Btn.setBounds (pitchX + btnW,    modeY, btnW,          modeStripH);
            ui.convPitchRTBtn.setBounds (pitchX + btnW*2,  modeY, kw3 - btnW*2, modeStripH);
        }

        ui.convDryWetSlider.setVisible (true); ui.convDryWetLabel.setVisible (true);
        ui.convPitchSlider .setVisible (true); ui.convPitchLabel .setVisible (true);
        ui.convGainSlider  .setVisible (true); ui.convGainLabel  .setVisible (true);
        ui.convPitch60Btn  .setVisible (showAdvanced);
        ui.convPitch10Btn  .setVisible (showAdvanced);
        ui.convPitchRTBtn  .setVisible (showAdvanced);

        // Right panel: IR preview + load button
        const int btnH     = 22;
        const int previewH = kRowH - xyPadV * 2 - btnH - 4;
        ui.irPreview->setBounds    (xyX, rowY + xyPadV, xyW, previewH);
        ui.loadConvIRBtn.setBounds (xyX, rowY + xyPadV + previewH + 4, xyW, btnH);
        ui.irPreview->setVisible (true);
        ui.loadConvIRBtn.setVisible (true);
    }
}

// =============================================================================
// switchToTab
// =============================================================================
void RattlerAudioProcessorEditor::switchToTab (int tab)
{
    currentTab = tab;
    sourceTabBtn     .setToggleState (tab == 0, juce::dontSendNotification);
    triggerTabBtn    .setToggleState (tab == 1, juce::dontSendNotification);
    resonatorTabBtn  .setToggleState (tab == 2, juce::dontSendNotification);
    convolutionTabBtn.setToggleState (tab == 3, juce::dontSendNotification);
    gearTabBtn       .setToggleState (tab == 4, juce::dontSendNotification);
    resized();
    repaint();
}

// =============================================================================
// paint
// =============================================================================
void RattlerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    g.setColour (kGrid);
    g.drawHorizontalLine (kGlobalH - 1, 0.0f, (float)kW);

    g.setColour (kAccent);
    g.setFont (juce::Font (juce::FontOptions (18.0f).withStyle ("Bold")));
    g.drawText ("RATTLER", 10, 0, 100, kGlobalH, juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xff1e1e1e));
    g.fillRect (0, kGlobalH, kW, kTabBarH);

    // Accent underline on active tab
    {
        const int gearW    = 36;
        const int mainTabW = (kW - gearW) / 4;
        int ax, aw;
        if (currentTab < 4) { ax = currentTab * mainTabW; aw = mainTabW; }
        else                 { ax = kW - gearW;            aw = gearW;    }
        g.setColour (kAccent);
        g.fillRect (ax, kGlobalH + kTabBarH - 2, aw, 2);
    }

    g.setColour (kGrid);
    g.drawHorizontalLine (kGlobalH + kTabBarH, 0.0f, (float)kW);

    // Gear tab: paint section header, skip layer boxes
    if (currentTab == 4)
    {
        g.setColour (kSubtext);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("PERFORMANCE", kPad + 40, kContentY + 10, 200, 16,
                    juce::Justification::centredLeft);
        return;
    }

    // Two-box layout per layer row
    {
        constexpr int   knobX  = kPad + kLBtnW;
        constexpr float outerR = 5.f;
        constexpr float innerR = 3.f;
        constexpr float bi     = (float)kBoxInset;

        for (int row = 0; row < 2; ++row)
        {
            const float ry = (float)(kContentY + row * (kRowH + kRowGap));

            g.setColour (juce::Colour (row == 0 ? 0xff1c1c1c : 0xff1f1f1f));
            g.fillRoundedRectangle (kPad + 0.5f, ry + 0.5f,
                                    kW - kPad * 2 - 1.f, kRowH - 1.f, outerR);
            g.setColour (juce::Colour (0xff303030));
            g.drawRoundedRectangle (kPad + 0.5f, ry + 0.5f,
                                    kW - kPad * 2 - 1.f, kRowH - 1.f, outerR, 1.f);

            const float cx = (float)knobX;
            const float cw = (float)(kW - knobX - kPad) - bi;
            g.setColour (juce::Colour (row == 0 ? 0xff171717 : 0xff1a1a1a));
            g.fillRoundedRectangle (cx + 0.5f, ry + bi,
                                    cw - 1.f, kRowH - bi * 2.f, innerR);
            g.setColour (juce::Colour (0xff282828));
            g.drawRoundedRectangle (cx + 0.5f, ry + bi,
                                    cw - 1.f, kRowH - bi * 2.f, innerR, 1.f);
        }
    }

}

// =============================================================================
// resized
// =============================================================================
void RattlerAudioProcessorEditor::resized()
{
    // ── Global strip ─────────────────────────────────────────────────────────
    {
        const int y  = 4;
        const int h  = kGlobalH - 8;
        const int kw = 60;
        layoutKnob (masterMixSlider, masterMixLabel, { 108,      y, kw, h });
        layoutKnob (masterSatSlider, masterSatLabel, { 108+kw+4, y, kw, h });

        const int togY   = (kGlobalH - 26) / 2;
        const int routeX = 108 + kw * 2 + 16;
        routingParallelBtn.setBounds (routeX,      togY, 85, 26);
        routingSeqBtn     .setBounds (routeX + 85, togY, 95, 26);
        advancedToggle    .setBounds (routeX + 85 + 95 + 12, togY, 100, 26);
    }

    // ── Tab buttons (4 main + 1 gear) ────────────────────────────────────────
    {
        const int gearW   = 36;
        const int mainTabW = (kW - gearW) / 4;
        sourceTabBtn     .setBounds (0,            kGlobalH, mainTabW,       kTabBarH);
        triggerTabBtn    .setBounds (mainTabW,     kGlobalH, mainTabW,       kTabBarH);
        resonatorTabBtn  .setBounds (mainTabW * 2, kGlobalH, mainTabW,       kTabBarH);
        convolutionTabBtn.setBounds (mainTabW * 3, kGlobalH, mainTabW,       kTabBarH);
        gearTabBtn       .setBounds (kW - gearW,   kGlobalH, gearW,          kTabBarH);
    }

    // ── Layer enable buttons ─────────────────────────────────────────────────
    for (int i = 0; i < 2; ++i)
    {
        layerBtns[i].setBounds (kPad + kBoxInset,
                                kContentY + i * (kRowH + kRowGap) + kBoxInset,
                                18, kRowH - kBoxInset * 2);
        layerBtns[i].setVisible (currentTab != 4);
    }

    // ── Gear tab content ──────────────────────────────────────────────────────
    if (currentTab == 4)
    {
        setLayerVisible (0, false);
        setLayerVisible (1, false);

        const int gx   = kPad + 40;
        const int gy   = kContentY + 30;
        const int gtW  = 130, gtH = 26, gtGap = 8;
        modalClampToggle.setBounds (gx,              gy,          gtW, gtH);
        fastTanhToggle  .setBounds (gx + gtW + gtGap, gy,          gtW, gtH);
        idleGateToggle  .setBounds (gx,              gy + gtH + gtGap, gtW, gtH);
        convSkipToggle  .setBounds (gx + gtW + gtGap, gy + gtH + gtGap, gtW, gtH);
        modalClampToggle.setVisible (true);
        fastTanhToggle  .setVisible (true);
        idleGateToggle  .setVisible (true);
        convSkipToggle  .setVisible (true);
        return;
    }

    // Hide perf toggles when not in gear tab
    modalClampToggle.setVisible (false);
    fastTanhToggle  .setVisible (false);
    idleGateToggle  .setVisible (false);
    convSkipToggle  .setVisible (false);

    // ── Layer content ─────────────────────────────────────────────────────────
    for (int i = 0; i < 2; ++i)
        layoutLayer (i);
}

// =============================================================================
// updateIRPreview
// =============================================================================
void RattlerAudioProcessorEditor::updateIRPreview (int idx)
{
    auto& ui = layerUIs[idx];
    if (!ui.irPreview) return;
    const juce::String p = (idx == 0) ? "layerA" : "layerB";
    ui.irPreview->setIRData (audioProcessor.getRawIR (idx),
                             audioProcessor.getRawIRSampleRate (idx));
    ui.irPreview->setParams (
        audioProcessor.apvts.getRawParameterValue (p + "ConvAttack") ->load(),
        audioProcessor.apvts.getRawParameterValue (p + "ConvSustain")->load(),
        audioProcessor.apvts.getRawParameterValue (p + "ConvDecay")  ->load(),
        audioProcessor.apvts.getRawParameterValue (p + "ConvStart")  ->load(),
        audioProcessor.apvts.getRawParameterValue (p + "ConvGain")   ->load(),
        audioProcessor.apvts.getRawParameterValue (p + "ConvPitch")  ->load());
}

void RattlerAudioProcessorEditor::updateSamplePreview (int idx)
{
    auto& ui = layerUIs[idx];
    if (!ui.samplePreview) return;
    const juce::String p = (idx == 0) ? "layerA" : "layerB";
    ui.samplePreview->setSampleData (audioProcessor.getSampleBuffer (idx),
                                     audioProcessor.getSampleFileRate (idx));
    ui.samplePreview->setParams (
        audioProcessor.apvts.getRawParameterValue (p + "SampleAttack") ->load(),
        audioProcessor.apvts.getRawParameterValue (p + "SampleSustain")->load(),
        audioProcessor.apvts.getRawParameterValue (p + "SampleDecay")  ->load(),
        audioProcessor.apvts.getRawParameterValue (p + "SampleStart")  ->load(),
        audioProcessor.apvts.getRawParameterValue (p + "SampleGain")   ->load());
}

// =============================================================================
// FileDragAndDropTarget
// =============================================================================
bool RattlerAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".wav")  || f.endsWithIgnoreCase (".aif")
         || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac"))
            return true;
    return false;
}

void RattlerAudioProcessorEditor::filesDropped (const juce::StringArray& files, int /*x*/, int y)
{
    const int layerIdx = (y >= kContentY + kRowH + kRowGap) ? 1 : 0;
    for (const auto& path : files)
    {
        juce::File f (path);
        if (f.existsAsFile())
        {
            if (currentTab == 3)
            {
                audioProcessor.loadConvIR (layerIdx, f);
                layerUIs[layerIdx].loadConvIRBtn.setButtonText (f.getFileNameWithoutExtension());
                updateIRPreview (layerIdx);
            }
            else
            {
                audioProcessor.loadSampleFile (layerIdx, f);
                // onSampleLoaded callback handles button text + preview update
            }
            break;
        }
    }
}
