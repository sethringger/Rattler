#pragma once

#include "PluginProcessor.h"
#include "FilterXYPad.h"
#include "TriggerXYPad.h"
#include "TuneDecayPad.h"
#include "IRPreviewComponent.h"
#include "SamplePreviewComponent.h"

// Pill-style toggle button look — replaces the default checkbox tick with a
// compact LED dot + label drawn as a rounded button that lights up when active.
struct PillToggleLookAndFeel : public juce::LookAndFeel_V4
{
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& btn,
                           bool isMouseOver, bool /*isButtonDown*/) override
    {
        const bool  on      = btn.getToggleState();
        const bool  enabled = btn.isEnabled();
        auto        b       = btn.getLocalBounds();

        const juce::Colour accent  { 0xff4a9eff };
        const juce::Colour bgOn    { 0xff162030 };
        const juce::Colour bgOff   { 0xff161616 };
        const juce::Colour border  = on  ? accent.withAlpha (enabled ? 1.f : 0.4f)
                                         : juce::Colour (0xff333333);
        const juce::Colour textCol = on  ? accent.withAlpha (enabled ? 1.f : 0.45f)
                                         : juce::Colour (enabled ? 0xff555555 : 0xff383838);

        // Background
        auto bf = b.toFloat().reduced (0.5f);
        g.setColour ((on ? bgOn : bgOff).withMultipliedBrightness (isMouseOver ? 1.15f : 1.f));
        g.fillRoundedRectangle (bf, 4.f);

        // Border
        g.setColour (border);
        g.drawRoundedRectangle (bf, 4.f, 1.f);

        // LED dot (left side)
        const int  ledD  = 6;
        const int  ledX  = b.getX() + 7;
        const int  ledY  = b.getCentreY() - ledD / 2;
        g.setColour (on ? accent.withAlpha (enabled ? 1.f : 0.45f)
                        : juce::Colour (0xff2a2a2a));
        g.fillEllipse ((float)ledX, (float)ledY, (float)ledD, (float)ledD);
        if (on && enabled)
        {
            g.setColour (accent.withAlpha (0.3f));
            g.fillEllipse ((float)(ledX - 2), (float)(ledY - 2),
                           (float)(ledD + 4),  (float)(ledD + 4));
        }

        // Label
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.setColour (textCol);
        g.drawFittedText (btn.getButtonText(),
                          b.withLeft (ledX + ledD + 5).withRight (b.getRight() - 4),
                          juce::Justification::centredLeft, 1);
    }
};

class RattlerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    public juce::FileDragAndDropTarget
{
public:
    RattlerAudioProcessorEditor (RattlerAudioProcessor&);
    ~RattlerAudioProcessorEditor() override;

    void paint   (juce::Graphics&) override;
    void resized ()                override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    RattlerAudioProcessor& audioProcessor;

    // --- Global strip ---
    juce::Slider       masterMixSlider;
    juce::Label        masterMixLabel;
    juce::Slider       masterSatSlider;
    juce::Label        masterSatLabel;
    juce::TextButton routingParallelBtn;
    juce::TextButton routingSeqBtn;

    // --- Tab buttons ---
    juce::TextButton sourceTabBtn, triggerTabBtn, resonatorTabBtn, convolutionTabBtn;
    int currentTab = 0; // 0=SOURCE, 1=TRIGGER, 2=RESONATOR, 3=CONVOLUTION

    // --- Vertical layer enable button ---
    struct VerticalButton : public juce::Component
    {
        juce::String text;
        bool active = true;
        std::function<void()> onClick;

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds();
            g.setColour (active ? juce::Colour (0xff1e2e3e) : juce::Colour (0xff1e1e1e));
            g.fillRoundedRectangle (b.toFloat(), 3.f);
            g.setColour (active ? juce::Colour (0xff4a9eff) : juce::Colour (0xff444444));
            g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 3.f, 1.f);

            juce::GlyphArrangement ga;
            ga.addFittedText (juce::Font (juce::FontOptions (9.f).withStyle ("Bold")),
                              text, 0.f, 0.f, (float)b.getHeight(), (float)b.getWidth(),
                              juce::Justification::centred, 1);
            juce::Path p;
            ga.createPath (p);
            p.applyTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi)
                                  .translated (0.f, (float)b.getHeight()));
            g.setColour (active ? juce::Colour (0xff4a9eff) : juce::Colour (0xff444444));
            g.fillPath (p);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (onClick) onClick();
        }
    };

    // --- Per-layer UI ---
    struct LayerUI
    {
        // Mode selector (always visible)
        juce::ComboBox modeCombo;

        // SOURCE TAB — common (all modes)
        juce::Slider       levelSlider, satSlider;
        juce::Label        levelLabel,  satLabel;
        juce::ToggleButton sourceFilterEnableToggle;

        // SOURCE TAB — Noise mode
        juce::Slider   noiseBurstSlider;
        juce::Label    noiseBurstLabel;
        juce::Slider   noiseAttackSlider;
        juce::Label    noiseAttackLabel;
        std::unique_ptr<FilterXYPad> noiseFilterXY;

        // SOURCE TAB — Bounce mode
        juce::Slider   bounceMassSlider, bounceGapSlider, bounceRestSlider;
        juce::Slider   bounceWiresSlider, bounceSpreadSlider;
        juce::Label    bounceMassLabel, bounceGapLabel, bounceRestLabel;
        juce::Label    bounceWiresLabel, bounceSpreadLabel;
        std::unique_ptr<FilterXYPad> bounceFilterXY;

        // SOURCE TAB — Sample mode
        juce::Slider   samplePitchSlider, sampleGainSlider;
        juce::Label    samplePitchLabel,  sampleGainLabel;
        // Hidden backing sliders for envelope handle values
        juce::Slider   sampleStartSlider,   sampleAttackSlider;
        juce::Slider   sampleSustainSlider, sampleDecaySlider;
        juce::Label    sampleStartLabel,    sampleAttackLabel;
        juce::Label    sampleSustainLabel,  sampleDecayLabel;
        std::unique_ptr<SamplePreviewComponent> samplePreview;
        std::unique_ptr<FilterXYPad> sampleFilterXY;

        // SOURCE TAB — ModalRattle mode
        juce::Slider   rattleGapSlider, rattleKSlider, rattleJitterSlider;
        juce::Label    rattleGapLabel,  rattleKLabel,  rattleJitterLabel;
        std::unique_ptr<FilterXYPad> rattleFilterXY;

        // TRIGGER TAB
        juce::Slider              trigThreshSlider;
        juce::Label               trigThreshLabel;
        juce::ToggleButton        trigFilterEnableToggle;
        std::unique_ptr<TriggerXYPad> trigXY;

        // RESONATOR TAB — Noise / Bounce / Sample modes
        juce::Slider       resGainSlider, resWetSlider, resSatSlider;
        juce::Label        resGainLabel,  resWetLabel,  resSatLabel;
        juce::ToggleButton resClipToggle;
        juce::Slider    resModesSlider, resRoughSlider, resToneSlider, resSpreadSlider;
        juce::Label     resModesLabel,  resRoughLabel,  resToneLabel,  resSpreadLabel;
        juce::ComboBox  resMaterialCombo;
        std::unique_ptr<TuneDecayPad> resPad;

        // RESONATOR TAB — ModalRattle mode
        juce::Slider       modesSlider, dampSlider, roughSlider, toneSlider, spreadSlider;
        juce::Label        modesLabel,  dampLabel,  roughLabel,  toneLabel,  spreadLabel;
        juce::ComboBox     rattleMaterialCombo;
        juce::ToggleButton rattleModalSatToggle;
        std::unique_ptr<TuneDecayPad> rattleResPad;

        // CONVOLUTION TAB — ModalRattle mode only
        juce::ToggleButton convEnableToggle;
        juce::Slider       convWetSlider;
        juce::Label        convWetLabel;
        juce::Slider       convDryWetSlider;
        juce::Label        convDryWetLabel;
        juce::Slider       convPitchSlider;
        juce::Label        convPitchLabel;
        juce::Slider       convGainSlider;
        juce::Label        convGainLabel;
        // Hidden backing sliders — values come from IRPreviewComponent handles, not knobs
        juce::Slider       convDecaySlider;
        juce::Label        convDecayLabel;
        juce::Slider       convAttackSlider;
        juce::Label        convAttackLabel;
        juce::Slider       convSustainSlider;
        juce::Label        convSustainLabel;
        juce::Slider       convStartSlider;
        juce::Label        convStartLabel;
        juce::TextButton   loadConvIRBtn;
        std::unique_ptr<IRPreviewComponent> irPreview;
        std::unique_ptr<juce::FileChooser> convFileChooser;

        // APVTS attachments
        using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        std::unique_ptr<ComboAtt>  modeAttach;
        std::unique_ptr<SliderAtt> levelAttach, satAttach;
        std::unique_ptr<SliderAtt> noiseBurstAttach, noiseAttackAttach;
        std::unique_ptr<SliderAtt> bounceMassAttach, bounceGapAttach, bounceRestAttach;
        std::unique_ptr<SliderAtt> bounceWiresAttach, bounceSpreadAttach;
        std::unique_ptr<SliderAtt> samplePitchAttach, sampleGainAttach;
        std::unique_ptr<SliderAtt> sampleStartAttach, sampleAttackAttach;
        std::unique_ptr<SliderAtt> sampleSustainAttach, sampleDecayAttach;
        std::unique_ptr<SliderAtt> rattleGapAttach, rattleKAttach, rattleJitterAttach;
        std::unique_ptr<SliderAtt> trigThreshAttach;
        std::unique_ptr<SliderAtt> resGainAttach, resWetAttach, resSatAttach;
        std::unique_ptr<SliderAtt> resModesAttach, resRoughAttach, resToneAttach, resSpreadAttach;
        std::unique_ptr<ComboAtt>  resMaterialAttach;
        std::unique_ptr<SliderAtt> modesAttach, dampAttach, roughAttach, toneAttach, spreadAttach;
        std::unique_ptr<ComboAtt>  rattleMaterialAttach;
        using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
        std::unique_ptr<ButtonAtt> resClipAttach;
        std::unique_ptr<ButtonAtt> rattleModalSatAttach;
        std::unique_ptr<ButtonAtt> sourceFilterEnableAttach;
        std::unique_ptr<ButtonAtt> trigFilterEnableAttach;
        std::unique_ptr<ButtonAtt> convEnableAttach;
        std::unique_ptr<SliderAtt> convWetAttach;
        std::unique_ptr<SliderAtt> convDryWetAttach;
        std::unique_ptr<SliderAtt> convPitchAttach;
        std::unique_ptr<SliderAtt> convGainAttach;
        std::unique_ptr<SliderAtt> convDecayAttach;
        std::unique_ptr<SliderAtt> convAttackAttach;
        std::unique_ptr<SliderAtt> convSustainAttach;
        std::unique_ptr<SliderAtt> convStartAttach;

        // ParameterAttachment fires AFTER the value lands in APVTS — used instead
        // of onValueChange to reliably trigger IR reprocessing for all conv params.
        using ParamAtt = juce::ParameterAttachment;
        std::unique_ptr<ParamAtt> convPitchReprocessAtt;
        std::unique_ptr<ParamAtt> convGainReprocessAtt;
        std::unique_ptr<ParamAtt> convDecayReprocessAtt;
        std::unique_ptr<ParamAtt> convStartReprocessAtt;
        std::unique_ptr<ParamAtt> convAttackReprocessAtt;
        std::unique_ptr<ParamAtt> convSustainReprocessAtt;

        std::unique_ptr<ParamAtt> sampleStartPreviewAtt, sampleAttackPreviewAtt;
        std::unique_ptr<ParamAtt> sampleSustainPreviewAtt, sampleDecayPreviewAtt;
        std::unique_ptr<ParamAtt> sampleGainPreviewAtt;

        std::unique_ptr<juce::FileChooser> fileChooser;

        bool suppressConvReprocess = false;

        // Pitch update mode: 0=60Hz (every change), 1=10Hz (throttled), 2=RT (per-block)
        int         convPitchMode        = 0;
        juce::int64 lastPitchReprocessMs = 0;
        juce::TextButton convPitch60Btn, convPitch10Btn, convPitchRTBtn;
    };

    // Must be declared before layerUIs so it outlives all ToggleButtons that reference it.
    PillToggleLookAndFeel pillToggleLnf;

    LayerUI layerUIs[2];

    // Global attachments
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAtt> masterMixAttach, masterSatAttach;
    std::unique_ptr<juce::ParameterAttachment> routingAttach;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    VerticalButton layerBtns[2];
    std::unique_ptr<juce::ParameterAttachment> layerEnableAttach[2];

    // --- Helpers ---
    void setupKnob  (juce::Slider&, const juce::String& label, juce::Label&);
    void setupCombo (juce::ComboBox&);
    void switchToTab (int tab);
    void layoutKnob (juce::Slider&, juce::Label&, juce::Rectangle<int>);

    void setupLayerUI       (int idx, juce::AudioProcessorValueTreeState&);
    void setLayerVisible    (int idx, bool v);
    void setLayerEnabled    (int idx, bool enabled);
    void applyConvEnabled   (int idx, bool on);
    void layoutLayer        (int idx);
    void updateIRPreview     (int idx);
    void updateSamplePreview (int idx);

    RattlerAudioProcessor::LayerMode getLayerMode (int idx) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RattlerAudioProcessorEditor)
};
