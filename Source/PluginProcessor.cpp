#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DebugLog.h"

RattlerAudioProcessor::RattlerAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "State", createParameterLayout())
{
    formatManager.registerBasicFormats();
    convPitchRTRatio[0].store (1.f);
    convPitchRTRatio[1].store (1.f);
    convUseRTPitch[0].store (false);
    convUseRTPitch[1].store (false);
}

RattlerAudioProcessor::~RattlerAudioProcessor() {}

// -----------------------------------------------------------------------------
juce::AudioProcessorValueTreeState::ParameterLayout
RattlerAudioProcessor::createParameterLayout()
{
    using Range = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto kResMaterials    = ModalResonatorModel::getMaterialNames();
    const auto kRattleMaterials = ModalRattleModel::getMaterialNames();
    const juce::StringArray kModeNames { "Noise", "Bounce", "Sample", "ModalRattle" };

    for (const auto* p : { "layerA", "layerB" })
    {
        const juce::String px (p);

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            px + "Mode", px + " Mode", kModeNames, 0));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "Level", px + " Level", Range (0.0f, 1.0f, 0.001f), 0.5f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "Sat", px + " Sat", Range (0.0f, 1.0f, 0.001f), 0.0f));

        // Trigger
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "TrigFreq", px + " Trig Freq",
            Range (20.0f, 2000.0f, 0.1f, 0.4f), 80.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "TrigQ", px + " Trig Q", Range (0.5f, 10.0f, 0.01f), 1.5f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "TrigThresh", px + " Thresh",
            Range (-60.0f, 0.0f, 0.1f), -20.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        // Noise
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "NoiseBurst", px + " Burst",
            Range (1.0f, 200.0f, 0.1f, 0.4f), 30.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "NoiseAttack", px + " Noise Attack",
            Range (0.0f, 200.0f, 0.1f, 0.4f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "NoiseFreq", px + " Noise Freq",
            Range (80.0f, 8000.0f, 0.1f, 0.4f), 2500.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "NoiseBw", px + " Noise BW",
            Range (6.0f, 72.0f, 0.1f), 24.0f,
            juce::AudioParameterFloatAttributes().withLabel ("st")));

        // Bounce
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "BounceMass", px + " Mass", Range (0.1f, 10.0f, 0.01f), 1.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "BounceGap", px + " Gap",
            Range (0.005f, 0.5f, 0.001f, 0.4f), 0.05f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "BounceRest", px + " Restitution", Range (0.0f, 0.95f, 0.01f), 0.7f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "BounceFreq", px + " Bounce Freq",
            Range (80.0f, 8000.0f, 0.1f, 0.4f), 800.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "BounceBw", px + " Bounce BW",
            Range (6.0f, 72.0f, 0.1f), 24.0f,
            juce::AudioParameterFloatAttributes().withLabel ("st")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "BounceWires", px + " Wires", Range (1.0f, 4.0f, 1.0f), 1.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "BounceSpread", px + " Wire Spread",
            Range (0.0f, 48.0f, 0.1f), 6.0f,
            juce::AudioParameterFloatAttributes().withLabel ("st")));

        // Sample
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "SampleFreq", px + " Sample Freq",
            Range (80.0f, 8000.0f, 0.1f, 0.4f), 2500.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "SampleBw", px + " Sample BW",
            Range (6.0f, 72.0f, 0.1f), 24.0f,
            juce::AudioParameterFloatAttributes().withLabel ("st")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "SamplePitch", px + " Sample Pitch",
            Range (-24.0f, 24.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("st")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "SampleGain", px + " Sample Gain",
            Range (-24.0f, 12.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "SampleStart", px + " Sample Start",
            Range (0.0f, 5000.0f, 1.0f, 0.4f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "SampleAttack", px + " Sample Attack",
            Range (0.0f, 500.0f, 0.1f, 0.4f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "SampleSustain", px + " Sample Sustain",
            Range (0.0f, 5000.0f, 1.0f, 0.4f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "SampleDecay", px + " Sample Decay",
            Range (10.0f, 5000.0f, 0.1f, 0.4f), 2000.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        // ModalRattle
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "RattleGap", px + " Rattle Gap",
            Range (0.0005f, 0.05f, 0.0001f, 0.4f), 0.005f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "RattleK", px + " Stiffness",
            Range (0.1f, 50.0f, 0.01f, 0.4f), 3.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "RattleDamp", px + " Contact Damp",
            Range (0.0f, 1.0f, 0.001f), 0.2f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "RattleModes", px + " Modes",
            Range (1.0f, 12.0f, 1.0f), 8.0f));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            px + "RattleMat", px + " Rattle Material", kRattleMaterials, 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "RattleTune", px + " Rattle Tune",
            Range (-48.0f, 48.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("st")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "RattleDecay", px + " Rattle Decay",
            Range (5.0f, 2000.0f, 0.1f, 0.4f), 80.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "RattleRough", px + " Roughness",
            Range (0.0f, 1.0f, 0.001f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "RattleJitter", px + " Jitter",
            Range (0.0f, 1.0f, 0.001f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "RattleTone", px + " Tone",
            Range (-1.0f, 1.0f, 0.001f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "RattleSpread", px + " Rattle Spread",
            Range (0.0f, 2.0f, 0.01f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "RattleFilterFreq", px + " Rattle Filter Freq",
            Range (20.0f, 20000.0f, 0.1f, 0.4f), 1200.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "RattleFilterBw", px + " Rattle Filter BW",
            Range (3.0f, 96.0f, 0.1f), 36.0f,
            juce::AudioParameterFloatAttributes().withLabel ("st")));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            px + "RattleModalSat", px + " Modal Feedback Sat", true));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            px + "SourceFilterEnable", px + " Source Filter Enable", true));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            px + "TrigFilterEnable", px + " Trig Filter Enable", true));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            px + "LayerEnable", px + " Layer Enable", true));

        // Convolution (ModalRattle only)
        layout.add (std::make_unique<juce::AudioParameterBool> (
            px + "ConvEnable", px + " Conv Enable", true));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ConvWet", px + " Conv Feedback",
            Range (0.0f, 1.0f, 0.001f), 0.5f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ConvDryWet", px + " Conv Dry/Wet",
            Range (0.0f, 1.0f, 0.001f), 0.5f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ConvPitch", px + " Conv Pitch",
            Range (-24.0f, 24.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("st")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ConvDecay", px + " Conv Decay",
            Range (10.0f, 2000.0f, 0.1f, 0.4f), 2000.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ConvGain", px + " Conv Gain",
            Range (-24.0f, 12.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ConvStart", px + " Conv Start",
            Range (0.0f, 2000.0f, 1.0f, 0.4f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ConvAttack", px + " Conv Attack",
            Range (0.0f, 500.0f, 0.1f, 0.4f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ConvSustain", px + " Conv Sustain",
            Range (0.0f, 2000.0f, 1.0f, 0.4f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ConvSplit", px + " Conv Split",
            Range (0.0f, 50.0f, 0.1f, 0.4f), 20.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        // Resonator (Noise / Bounce / Sample modes)
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ResGain", px + " Res Input Gain",
            Range (0.0f, 1.0f, 0.001f), 1.0f));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            px + "ResClip", px + " Res Soft Clip", true));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ResWet", px + " Res Mix",
            Range (0.0f, 1.0f, 0.001f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ResSat", px + " Res Sat",
            Range (0.0f, 1.0f, 0.001f), 0.5f));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            px + "ResMat", px + " Res Material", kResMaterials, 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ResTune", px + " Res Tune",
            Range (-48.0f, 48.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("st")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ResDecay", px + " Res Decay",
            Range (5.0f, 2000.0f, 0.1f, 0.4f), 80.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ResModes", px + " Res Modes",
            Range (1.0f, 12.0f, 1.0f), 8.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ResRough", px + " Res Roughness",
            Range (0.0f, 1.0f, 0.001f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ResTone", px + " Res Tone",
            Range (-1.0f, 1.0f, 0.001f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            px + "ResSpread", px + " Res Spread",
            Range (0.0f, 2.0f, 0.01f), 0.0f));
    }

    // Global
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "masterMix", "Mix", Range (0.0f, 1.0f, 0.001f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "masterSat", "Master Sat", Range (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        "routing", "Sequential", false));

    return layout;
}

// -----------------------------------------------------------------------------
const juce::String RattlerAudioProcessor::getName()  const { return JucePlugin_Name; }
bool  RattlerAudioProcessor::acceptsMidi()  const { return false; }
bool  RattlerAudioProcessor::producesMidi() const { return false; }
bool  RattlerAudioProcessor::isMidiEffect() const { return false; }
double RattlerAudioProcessor::getTailLengthSeconds() const { return 0.5; }
int   RattlerAudioProcessor::getNumPrograms()          { return 1; }
int   RattlerAudioProcessor::getCurrentProgram()       { return 0; }
void  RattlerAudioProcessor::setCurrentProgram (int)   {}
const juce::String RattlerAudioProcessor::getProgramName (int) { return {}; }
void  RattlerAudioProcessor::changeProgramName (int, const juce::String&) {}

// -----------------------------------------------------------------------------
void RattlerAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    for (auto& layer : layers)
        layer.prepare (sr, samplesPerBlock);
    setLatencySamples ((int) layers[0].convEngine.getLatency());
}

void RattlerAudioProcessor::releaseResources() {}

bool RattlerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

void RattlerAudioProcessor::loadSampleFile (int layerIdx, const juce::File& f)
{
    jassert (layerIdx >= 0 && layerIdx < 2);
    layers[layerIdx].sampleFilePath = f.getFullPathName();
    layers[layerIdx].sampleModel.loadFile (f);
    if (onSampleLoaded) onSampleLoaded (layerIdx);
}

bool RattlerAudioProcessor::sampleHasFile (int layerIdx) const
{
    jassert (layerIdx >= 0 && layerIdx < 2);
    return layers[layerIdx].sampleModel.hasFile();
}

void RattlerAudioProcessor::loadConvIR (int layerIdx, const juce::File& f)
{
    jassert (layerIdx >= 0 && layerIdx < 2);
    auto& layer = layers[layerIdx];

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 0) return;

    const int maxSamples = (int)(reader->sampleRate * 4.0);
    const int numSamples = (int)juce::jmin ((juce::int64)maxSamples, reader->lengthInSamples);

    layer.rawIR.setSize (1, numSamples);
    reader->read (&layer.rawIR, 0, numSamples, 0, true, false);
    layer.rawIRSampleRate = reader->sampleRate;
    layer.irFilePath = f.getFullPathName();

    const juce::String px = (layerIdx == 0) ? "layerA" : "layerB";
    const float pitch   = apvts.getRawParameterValue (px + "ConvPitch")->load();
    const float decay   = apvts.getRawParameterValue (px + "ConvDecay")->load();
    const float gain    = apvts.getRawParameterValue (px + "ConvGain")->load();
    const float start   = apvts.getRawParameterValue (px + "ConvStart")->load();
    const float attack  = apvts.getRawParameterValue (px + "ConvAttack")->load();
    const float sustain = apvts.getRawParameterValue (px + "ConvSustain")->load();
    const float split   = apvts.getRawParameterValue (px + "ConvSplit")->load();
    layer.applyIRProcessing (pitch, decay, start, gain, attack, sustain, split);
    if (onConvIRLoaded) onConvIRLoaded (layerIdx);
}

void RattlerAudioProcessor::reprocessConvIR (int layerIdx)
{
    jassert (layerIdx >= 0 && layerIdx < 2);
    auto& layer = layers[layerIdx];

    if (layer.rawIR.getNumSamples() == 0)
    {
        RLOG ("reprocessConvIR[" + juce::String (layerIdx) + "] ABORT - rawIR empty (IR not loaded)");
        return;
    }

    const juce::String px = (layerIdx == 0) ? "layerA" : "layerB";
    // In RT pitch mode the IR is baked at neutral (0 st); pitch applied per-block instead.
    const float pitch   = convUseRTPitch[layerIdx].load()
                        ? 0.f
                        : apvts.getRawParameterValue (px + "ConvPitch")->load();
    const float decay   = apvts.getRawParameterValue (px + "ConvDecay")->load();
    const float gain    = apvts.getRawParameterValue (px + "ConvGain")->load();
    const float start   = apvts.getRawParameterValue (px + "ConvStart")->load();
    const float attack  = apvts.getRawParameterValue (px + "ConvAttack")->load();
    const float sustain = apvts.getRawParameterValue (px + "ConvSustain")->load();
    const float split   = apvts.getRawParameterValue (px + "ConvSplit")->load();

    RLOG ("reprocessConvIR[" + juce::String (layerIdx) + "]"
          + " pitch="   + juce::String (pitch,   2)
          + " gain="    + juce::String (gain,    2)
          + " decay="   + juce::String (decay,   1)
          + " start="   + juce::String (start,   1)
          + " attack="  + juce::String (attack,  1)
          + " sustain=" + juce::String (sustain, 1)
          + " split="   + juce::String (split,   1)
          + " rawIR="   + juce::String (layer.rawIR.getNumSamples())
          + " rtMode="  + juce::String ((int) convUseRTPitch[layerIdx].load()));

    layer.applyIRProcessing (pitch, decay, start, gain, attack, sustain, split);
}

juce::String RattlerAudioProcessor::getConvIRFilePath (int layerIdx) const
{
    jassert (layerIdx >= 0 && layerIdx < 2);
    return layers[layerIdx].irFilePath;
}

const juce::AudioBuffer<float>& RattlerAudioProcessor::getRawIR (int layerIdx) const
{
    jassert (layerIdx >= 0 && layerIdx < 2);
    return layers[layerIdx].rawIR;
}

double RattlerAudioProcessor::getRawIRSampleRate (int layerIdx) const
{
    jassert (layerIdx >= 0 && layerIdx < 2);
    return layers[layerIdx].rawIRSampleRate;
}

const juce::AudioBuffer<float>& RattlerAudioProcessor::getSampleBuffer (int layerIdx) const
{
    jassert (layerIdx >= 0 && layerIdx < 2);
    return layers[layerIdx].sampleModel.buffer;
}

double RattlerAudioProcessor::getSampleFileRate (int layerIdx) const
{
    jassert (layerIdx >= 0 && layerIdx < 2);
    return layers[layerIdx].sampleModel.fileRate;
}

juce::String RattlerAudioProcessor::getSampleFilePath (int layerIdx) const
{
    jassert (layerIdx >= 0 && layerIdx < 2);
    return layers[layerIdx].sampleFilePath;
}

// -----------------------------------------------------------------------------
RattlerAudioProcessor::LayerParams
RattlerAudioProcessor::readLayerParams (const juce::String& px) const
{
    LayerParams p;
    p.mode    = (LayerMode)(int) apvts.getRawParameterValue (px + "Mode")->load();
    p.layerEn = apvts.getRawParameterValue (px + "LayerEnable")->load() > 0.5f;
    p.level   = apvts.getRawParameterValue (px + "Level")->load();
    p.sat     = apvts.getRawParameterValue (px + "Sat")->load();

    p.trigFreq   = apvts.getRawParameterValue (px + "TrigFreq")->load();
    p.trigQ      = apvts.getRawParameterValue (px + "TrigQ")->load();
    p.trigThresh = juce::Decibels::decibelsToGain (apvts.getRawParameterValue (px + "TrigThresh")->load());

    p.noiseBurst  = apvts.getRawParameterValue (px + "NoiseBurst") ->load();
    p.noiseAttack = apvts.getRawParameterValue (px + "NoiseAttack")->load();
    p.noiseFreq   = apvts.getRawParameterValue (px + "NoiseFreq")  ->load();
    p.noiseBw     = apvts.getRawParameterValue (px + "NoiseBw")    ->load();

    p.bounceMass   = apvts.getRawParameterValue (px + "BounceMass")->load();
    p.bounceGap    = apvts.getRawParameterValue (px + "BounceGap")->load();
    p.bounceRest   = apvts.getRawParameterValue (px + "BounceRest")->load();
    p.bounceFreq   = apvts.getRawParameterValue (px + "BounceFreq")->load();
    p.bounceBw     = apvts.getRawParameterValue (px + "BounceBw")->load();
    p.bounceWires  = (int) std::round (apvts.getRawParameterValue (px + "BounceWires")->load());
    p.bounceSpread = apvts.getRawParameterValue (px + "BounceSpread")->load();

    p.sampleFreq    = apvts.getRawParameterValue (px + "SampleFreq")   ->load();
    p.sampleBw      = apvts.getRawParameterValue (px + "SampleBw")     ->load();
    p.samplePitch   = apvts.getRawParameterValue (px + "SamplePitch")  ->load();
    p.sampleGain    = apvts.getRawParameterValue (px + "SampleGain")   ->load();
    p.sampleStart   = apvts.getRawParameterValue (px + "SampleStart")  ->load();
    p.sampleAttack  = apvts.getRawParameterValue (px + "SampleAttack") ->load();
    p.sampleSustain = apvts.getRawParameterValue (px + "SampleSustain")->load();
    p.sampleDecay   = apvts.getRawParameterValue (px + "SampleDecay")  ->load();

    p.rattleGap      = apvts.getRawParameterValue (px + "RattleGap")->load();
    p.rattleK        = apvts.getRawParameterValue (px + "RattleK")->load();
    p.rattleDamp     = apvts.getRawParameterValue (px + "RattleDamp")->load();
    p.rattleModes    = (int) std::round (apvts.getRawParameterValue (px + "RattleModes")->load());
    p.rattleMat      = (int) apvts.getRawParameterValue (px + "RattleMat")->load();
    p.rattleTune     = apvts.getRawParameterValue (px + "RattleTune")->load();
    p.rattleDecay    = apvts.getRawParameterValue (px + "RattleDecay")->load();
    p.rattleRough    = apvts.getRawParameterValue (px + "RattleRough")->load();
    p.rattleJitter   = apvts.getRawParameterValue (px + "RattleJitter")->load();
    p.rattleTone     = apvts.getRawParameterValue (px + "RattleTone")->load();
    p.rattleSpread   = apvts.getRawParameterValue (px + "RattleSpread")->load();
    p.rattleFiltFreq = apvts.getRawParameterValue (px + "RattleFilterFreq")->load();
    p.rattleFiltBw   = apvts.getRawParameterValue (px + "RattleFilterBw")->load();
    p.rattleModalSat = apvts.getRawParameterValue (px + "RattleModalSat")->load() > 0.5f;
    p.sourceFilterEn = apvts.getRawParameterValue (px + "SourceFilterEnable")->load() > 0.5f;
    p.trigFilterEn   = apvts.getRawParameterValue (px + "TrigFilterEnable")->load() > 0.5f;

    p.convEn      = apvts.getRawParameterValue (px + "ConvEnable")->load() > 0.5f;
    p.convWet     = apvts.getRawParameterValue (px + "ConvWet")->load();
    p.convDryWet  = apvts.getRawParameterValue (px + "ConvDryWet")->load();
    p.convGain    = apvts.getRawParameterValue (px + "ConvGain")->load();
    p.convStart   = apvts.getRawParameterValue (px + "ConvStart")->load();
    p.convAttack  = apvts.getRawParameterValue (px + "ConvAttack")->load();
    p.convSustain = apvts.getRawParameterValue (px + "ConvSustain")->load();
    p.convSplit   = apvts.getRawParameterValue (px + "ConvSplit")->load();

    p.resGain  = apvts.getRawParameterValue (px + "ResGain")->load();
    p.resClip  = apvts.getRawParameterValue (px + "ResClip")->load() > 0.5f;
    p.resWet   = apvts.getRawParameterValue (px + "ResWet")->load();
    p.resSat   = apvts.getRawParameterValue (px + "ResSat")->load();
    p.resMat   = (int) apvts.getRawParameterValue (px + "ResMat")->load();
    p.resTune   = apvts.getRawParameterValue (px + "ResTune")->load();
    p.resDecay  = apvts.getRawParameterValue (px + "ResDecay")->load();
    p.resModes  = (int) apvts.getRawParameterValue (px + "ResModes")->load();
    p.resRough  = apvts.getRawParameterValue (px + "ResRough")->load();
    p.resTone   = apvts.getRawParameterValue (px + "ResTone")->load();
    p.resSpread = apvts.getRawParameterValue (px + "ResSpread")->load();

    return p;
}

// -----------------------------------------------------------------------------
void RattlerAudioProcessor::updateLayerModels (int idx, const LayerParams& p)
{
    auto& L = layers[idx];

    L.trigFilter.setFilter (p.trigFreq, p.trigQ);
    L.trigFilter.filterEnabled = p.trigFilterEn;
    L.sourceSat.setDrive (p.sat);
    L.noiseModel.filterEnabled  = p.sourceFilterEn;
    L.bounceModel.filterEnabled = p.sourceFilterEn;
    L.sampleModel.filterEnabled = p.sourceFilterEn;
    L.resonator.setClipEnabled (p.resClip);
    L.resonator.setParams (p.resMat, p.resTune, p.resDecay, p.resModes);
    L.resonator.setFeedbackSat (p.resSat);
    L.resonator.setRoughness (p.resRough);
    L.resonator.setTone (p.resTone);
    L.resonator.setSpread (p.resSpread);

    switch (p.mode)
    {
        case LayerMode::Noise:
            L.noiseModel.setDecay  (p.noiseBurst, sampleRate);
            L.noiseModel.setAttack (p.noiseAttack, sampleRate);
            L.noiseModel.setFilter (p.noiseFreq, p.noiseBw);
            break;
        case LayerMode::Bounce:
            L.bounceModel.setFilter (p.bounceFreq, p.bounceBw, p.bounceWires, p.bounceSpread);
            break;
        case LayerMode::Sample:
            L.sampleModel.setFilter (p.sampleFreq, p.sampleBw);
            L.sampleModel.setPlaybackParams (p.samplePitch, p.sampleGain,
                                             p.sampleStart, p.sampleAttack,
                                             p.sampleSustain, p.sampleDecay);
            break;
        case LayerMode::ModalRattle:
            L.rattleModel.setParams        (p.rattleMat, p.rattleTune, p.rattleDecay, p.rattleModes);
            L.rattleModel.setContactParams (p.rattleGap, p.rattleK, p.rattleDamp);
            L.rattleModel.setRoughness     (p.rattleRough);
            L.rattleModel.setJitter        (p.rattleJitter);
            L.rattleModel.setTone          (p.rattleTone);
            L.rattleModel.setSpread        (p.rattleSpread);
            L.rattleModel.setFilter        (p.rattleFiltFreq, p.rattleFiltBw);
            L.rattleModel.modalFeedbackSat = p.rattleModalSat;
            L.rattleModel.filterEnabled    = p.sourceFilterEn;
            L.rattleModel.convWet = (p.convEn && (L.contactKernelLen > 0 || L.convHasIR))
                                        ? p.convWet * p.convWet : 0.0f;
            break;
    }
}

// -----------------------------------------------------------------------------
void RattlerAudioProcessor::processLayerSample (int idx, int i, float driveSignal,
                                                 const LayerParams& p, bool convActive,
                                                 float& outL, float& outR)
{
    auto& L = layers[idx];

    const float filt  = L.trigFilter.process (driveSignal);
    const bool  above = filt > p.trigThresh;

    if (above && !L.prevAbove)
    {
        if (p.mode == LayerMode::Noise)  L.noiseModel.trigger();
        if (p.mode == LayerMode::Sample) L.sampleModel.trigger();
    }
    L.prevAbove = above;

    float rawL = 0.0f, rawR = 0.0f;
    switch (p.mode)
    {
        case LayerMode::Noise:
            rawL = L.noiseModel.processSample();
            break;
        case LayerMode::Bounce:
            rawL = L.bounceModel.processSample (filt, p.bounceMass, p.bounceGap, p.bounceRest);
            break;
        case LayerMode::Sample:
            rawL = L.sampleModel.processSample();
            break;
        case LayerMode::ModalRattle:
        {
            // Contact feedback: zero-latency ring-buffer convolution with the contact
            // kernel (first splitMs of the IR, resampled to plugin rate). Falls back to
            // the one-block-delayed FFT output when no contact kernel is present (splitMs=0).
            float convFBRaw = 0.0f;
            const bool hasContactKernel = (p.convEn && L.contactKernelLen > 0);
            if (hasContactKernel)
            {
                const int mask = Layer::kContactKernelMaxLen - 1;
                for (int k = 0; k < L.contactKernelLen; ++k)
                    convFBRaw += L.contactKernel[k]
                               * L.contactRingBuf[(L.contactRingPos - k - 1
                                                   + Layer::kContactKernelMaxLen) & mask];
            }
            else if (convActive)
            {
                if (convUseRTPitch[idx].load())
                {
                    const float pratio = convPitchRTRatio[idx].load();
                    const float rPos   = (float)i * pratio;
                    const int   r0     = juce::jlimit (0, L.prevConvOut.getNumSamples() - 1, (int)rPos);
                    const int   r1     = juce::jlimit (0, L.prevConvOut.getNumSamples() - 1, r0 + 1);
                    const float frac   = rPos - (float)r0;
                    convFBRaw = L.prevConvOut.getSample (0, r0) * (1.f - frac)
                              + L.prevConvOut.getSample (0, r1) * frac;
                }
                else
                {
                    convFBRaw = L.prevConvOut.getSample (0, i);
                }
            }
            const float convFB = std::tanh (convFBRaw * 4.0f) * 0.25f;
            L.rattleModel.processSample (filt, driveSignal, convFB, rawL, rawR);
            rawL *= 2.0f; rawR *= 2.0f;

            // Update ring buffer with current mono rattle output.
            if (hasContactKernel)
            {
                const int mask = Layer::kContactKernelMaxLen - 1;
                L.contactRingBuf[L.contactRingPos] = (rawL + rawR) * 0.5f;
                L.contactRingPos = (L.contactRingPos + 1) & mask;
            }

            // FFT reverb tail: write to scratch buffer, mix from previous block.
            if (convActive)
            {
                L.convScratch.setSample (0, i, rawL);
                L.convScratch.setSample (1, i, rawR);
                if (p.convDryWet > 0.0f)
                {
                    const float convOutL = L.prevConvOut.getSample (0, i);
                    const float convOutR = L.prevConvOut.getSample (1, i);
                    rawL = rawL * (1.0f - p.convDryWet) + convOutL * p.convDryWet;
                    rawR = rawR * (1.0f - p.convDryWet) + convOutR * p.convDryWet;
                }
            }
            break;
        }
    }

    const float satL = L.sourceSat.process (rawL);
    const float satR = (p.mode == LayerMode::ModalRattle)
                           ? L.sourceSat.process (rawR) : satL;

    outL = satL; outR = satR;
    if (p.mode != LayerMode::ModalRattle)
    {
        float resL, resR;
        L.resonator.processSample (satL * p.resGain, resL, resR);
        outL = satL * (1.0f - p.resWet) + resL * p.resWet;
        outR = satR * (1.0f - p.resWet) + resR * p.resWet;

        if (convActive)
        {
            L.convScratch.setSample (0, i, outL);
            L.convScratch.setSample (1, i, outR);
            const float convOutL = L.prevConvOut.getSample (0, i);
            const float convOutR = L.prevConvOut.getSample (1, i);
            outL = outL * (1.0f - p.convDryWet) + convOutL * p.convDryWet;
            outR = outR * (1.0f - p.convDryWet) + convOutR * p.convDryWet;
        }
    }
    if (!p.layerEn) outL = outR = 0.0f;
}

// -----------------------------------------------------------------------------
void RattlerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const float masterMix   = apvts.getRawParameterValue ("masterMix")->load();
    const float masterSatDr = apvts.getRawParameterValue ("masterSat")->load();
    const bool  sequential  = apvts.getRawParameterValue ("routing")->load() > 0.5f;

    const LayerParams pA = readLayerParams ("layerA");
    const LayerParams pB = readLayerParams ("layerB");

    masterSat.setDrive (masterSatDr);
    updateLayerModels (0, pA);
    updateLayerModels (1, pB);

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // convActive gates the FFT reverb tail path. When a contact kernel is present,
    // feedback is handled by the zero-latency ring buffer, so convWet no longer
    // activates the FFT path — only convDryWet does.
    const bool convActiveA = pA.convEn && layers[0].convHasIR
                           && (pA.convDryWet > 0.0f
                               || (pA.mode == LayerMode::ModalRattle
                                   && pA.convWet > 0.0f
                                   && layers[0].contactKernelLen == 0));
    const bool convActiveB = pB.convEn && layers[1].convHasIR
                           && (pB.convDryWet > 0.0f
                               || (pB.mode == LayerMode::ModalRattle
                                   && pB.convWet > 0.0f
                                   && layers[1].contactKernelLen == 0));

    if (convActiveA) layers[0].convScratch.clear();
    if (convActiveB) layers[1].convScratch.clear();

    for (int i = 0; i < numSamples; ++i)
    {
        float monoIn = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            monoIn += buffer.getSample (ch, i);
        monoIn /= (float) numChannels;

        float outAL, outAR;
        processLayerSample (0, i, monoIn, pA, convActiveA, outAL, outAR);

        const float driveSrcB = sequential ? (outAL + outAR) * 0.5f : monoIn;

        float outBL, outBR;
        processLayerSample (1, i, driveSrcB, pB, convActiveB, outBL, outBR);

        const float wetL = masterSat.process (outAL * pA.level + outBL * pB.level);
        const float wetR = masterSat.process (outAR * pA.level + outBR * pB.level);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = buffer.getSample (ch, i);
            const float wet = (ch == 1) ? wetR : wetL;
            buffer.setSample (ch, i, dry * (1.0f - masterMix) + wet * masterMix);
        }
    }

    auto runConv = [&] (int layerIdx)
    {
        auto& layer = layers[layerIdx];
        auto inBlock  = juce::dsp::AudioBlock<float> (layer.convScratch).getSubBlock (0, (size_t)numSamples);
        auto outBlock = juce::dsp::AudioBlock<float> (layer.prevConvOut).getSubBlock (0, (size_t)numSamples);
        layer.convEngine.process (juce::dsp::ProcessContextNonReplacing<float> (inBlock, outBlock));
    };

    if (convActiveA) runConv (0);
    if (convActiveB) runConv (1);
}

// -----------------------------------------------------------------------------
bool RattlerAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* RattlerAudioProcessor::createEditor()
{
    return new RattlerAudioProcessorEditor (*this);
}

void RattlerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    auto xml   = state.createXml();
    xml->setAttribute ("irPathA",     layers[0].irFilePath);
    xml->setAttribute ("irPathB",     layers[1].irFilePath);
    xml->setAttribute ("samplePathA", layers[0].sampleFilePath);
    xml->setAttribute ("samplePathB", layers[1].sampleFilePath);
    copyXmlToBinary (*xml, destData);
}

void RattlerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));

        juce::String pathA    = xml->getStringAttribute ("irPathA");
        juce::String pathB    = xml->getStringAttribute ("irPathB");
        juce::String sampleA  = xml->getStringAttribute ("samplePathA");
        juce::String sampleB  = xml->getStringAttribute ("samplePathB");

        juce::MessageManager::callAsync ([this, pathA, pathB, sampleA, sampleB]
        {
            if (pathA.isNotEmpty())   { juce::File f (pathA);   if (f.existsAsFile()) loadConvIR (0, f); }
            if (pathB.isNotEmpty())   { juce::File f (pathB);   if (f.existsAsFile()) loadConvIR (1, f); }
            if (sampleA.isNotEmpty()) { juce::File f (sampleA); if (f.existsAsFile()) loadSampleFile (0, f); }
            if (sampleB.isNotEmpty()) { juce::File f (sampleB); if (f.existsAsFile()) loadSampleFile (1, f); }
        });
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RattlerAudioProcessor();
}
