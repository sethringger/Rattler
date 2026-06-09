#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Models.h"

class RattlerAudioProcessor : public juce::AudioProcessor
{
public:
    RattlerAudioProcessor();
    ~RattlerAudioProcessor() override;

    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName()        const override;
    bool  acceptsMidi()                 const override;
    bool  producesMidi()                const override;
    bool  isMidiEffect()                const override;
    double getTailLengthSeconds()       const override;

    int getNumPrograms()                       override;
    int getCurrentProgram()                    override;
    void setCurrentProgram (int)               override;
    const juce::String getProgramName (int)    override;
    void changeProgramName (int, const juce::String&) override;

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int)   override;

    void loadSampleFile (int layerIdx, const juce::File& f);
    bool sampleHasFile  (int layerIdx) const;

    void loadConvIR         (int layerIdx, const juce::File& f);
    void reprocessConvIR    (int layerIdx);
    juce::String getConvIRFilePath (int layerIdx) const;

    const juce::AudioBuffer<float>& getRawIR           (int layerIdx) const;
    double                          getRawIRSampleRate  (int layerIdx) const;

    const juce::AudioBuffer<float>& getSampleBuffer    (int layerIdx) const;
    double                          getSampleFileRate   (int layerIdx) const;
    juce::String                    getSampleFilePath   (int layerIdx) const;

    std::function<void(int /*layerIdx*/)> onConvIRLoaded;
    std::function<void(int /*layerIdx*/)> onSampleLoaded;

    // Written from UI thread, read from audio thread — controls real-time pitch shift mode.
    std::atomic<float> convPitchRTRatio[2];
    std::atomic<bool>  convUseRTPitch[2];

    enum class LayerMode { Noise = 0, Bounce, Sample, ModalRattle };

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    double sampleRate = 44100.0;
    juce::AudioFormatManager formatManager;

    struct Layer
    {
        TriggerFilter    trigFilter;
        bool             prevAbove = false;

        NoiseModel       noiseModel;
        BounceModel      bounceModel;
        SampleModel      sampleModel;
        ModalRattleModel rattleModel;

        ModalResonatorModel resonator;
        Saturator        sourceSat;

        juce::dsp::Convolution   convEngine;
        juce::AudioBuffer<float> convScratch;
        juce::AudioBuffer<float> prevConvOut;
        bool                     convHasIR = false;

        juce::AudioBuffer<float> rawIR;
        juce::String             irFilePath;
        double                   rawIRSampleRate = 44100.0;
        juce::String             sampleFilePath;

        double sampleRate = 44100.0;

        static constexpr int kContactKernelMaxLen = 8192;
        float contactKernel[kContactKernelMaxLen] = {};
        int   contactKernelLen = 0;
        float contactRingBuf[kContactKernelMaxLen] = {};
        int   contactRingPos = 0;

        void prepare (double sr, int maxBlock)
        {
            sampleRate = sr;
            trigFilter .prepare (sr, maxBlock);
            noiseModel .prepare (sr, maxBlock);
            bounceModel.prepare (sr, maxBlock);
            sampleModel.prepare (sr, maxBlock);
            rattleModel.prepare (sr, maxBlock);
            resonator  .prepare (sr, maxBlock);

            juce::dsp::ProcessSpec convSpec { sr, (juce::uint32)maxBlock, 2 };
            convEngine.prepare (convSpec);
            convScratch.setSize (2, maxBlock);
            prevConvOut.setSize (2, maxBlock);
            prevConvOut.clear();

            contactKernelLen = 0;
            contactRingPos   = 0;
            juce::zeromem (contactRingBuf, sizeof (contactRingBuf));
        }

        void applyIRProcessing (float pitchSt, float decayMs,
                                float startMs = 0.0f, float gainDb = 0.0f,
                                float attackMs = 0.0f, float sustainMs = 0.0f,
                                float splitMs = 20.0f)
        {
            if (rawIR.getNumSamples() == 0) return;

            const float ratio     = std::pow (2.0f, pitchSt / 12.0f);
            const int   srcLen    = rawIR.getNumSamples();
            const int   startSamp = juce::jmin ((int)(startMs / 1000.0f * (float)rawIRSampleRate),
                                                srcLen - 1);
            const int   usedSrc   = juce::jmax (1, srcLen - startSamp);
            const int   newLen    = juce::jmax (1, (int)((float)usedSrc / ratio));

            juce::AudioBuffer<float> processed (1, newLen);
            const float gainLin   = juce::Decibels::decibelsToGain (gainDb);
            const float attackSec  = juce::jmax (0.0f,   attackMs  / 1000.0f);
            const float sustainSec = juce::jmax (0.0f,   sustainMs / 1000.0f);
            const float decaySec   = juce::jmax (0.001f, decayMs   / 1000.0f);

            for (int i = 0; i < newLen; ++i)
            {
                const float srcIdx = (float)startSamp + (float)i * ratio;
                const int   s0     = (int)srcIdx;
                const int   s1     = s0 + 1;
                const float frac   = srcIdx - (float)s0;
                const float v0     = (s0 < srcLen) ? rawIR.getSample (0, s0) : 0.0f;
                const float v1     = (s1 < srcLen) ? rawIR.getSample (0, s1) : 0.0f;
                const float sample = v0 + frac * (v1 - v0);

                const float tSrc = (srcIdx - (float)startSamp) / (float)rawIRSampleRate;
                float env;
                if (tSrc < attackSec)
                    env = (attackSec > 0.0f) ? tSrc / attackSec : 1.0f;
                else if (tSrc < attackSec + sustainSec)
                    env = 1.0f;
                else
                    env = std::pow (0.001f, (tSrc - attackSec - sustainSec) / decaySec);

                processed.setSample (0, i, sample * env);
            }

            // L2 normalise — keeps feedback loop stable regardless of IR length/content.
            float l2 = 0.0f;
            for (int i = 0; i < newLen; ++i)
                l2 += processed.getSample (0, i) * processed.getSample (0, i);
            l2 = std::sqrt (l2);
            if (l2 < 1e-6f) l2 = 1.0f;
            const float scale = gainLin / l2;
            for (int i = 0; i < newLen; ++i)
                processed.setSample (0, i, processed.getSample (0, i) * scale);

            // ── Split: contact kernel (zero-latency FIR) vs reverb tail (FFT) ──
            // splitSampIR is measured in IR samples at rawIRSampleRate.
            // The contact kernel is then resampled to the plugin's sampleRate for
            // direct per-sample ring-buffer convolution.
            const int splitSampIR = juce::jmin (
                (int)(splitMs / 1000.0f * (float)rawIRSampleRate), newLen);
            const int fadeSampsIR = juce::jmin (64, splitSampIR);

            // Build contact kernel, resampled from IR rate to plugin rate.
            const float srRatio = (float)sampleRate / (float)rawIRSampleRate;
            const int kernLen   = juce::jmin ((int)((float)splitSampIR * srRatio + 0.5f),
                                              kContactKernelMaxLen);
            juce::zeromem (contactKernel, sizeof (contactKernel));
            contactKernelLen = 0;
            if (kernLen > 0 && splitSampIR > 0)
            {
                const int fadeStartKern = (int)((float)(splitSampIR - fadeSampsIR) * srRatio);
                for (int i = 0; i < kernLen; ++i)
                {
                    const float srcPos = (float)i / srRatio;
                    const int   ks0    = (int)srcPos;
                    const int   ks1    = juce::jmin (ks0 + 1, splitSampIR - 1);
                    const float kfrac  = srcPos - (float)ks0;
                    float v = processed.getSample (0, ks0) * (1.0f - kfrac)
                            + processed.getSample (0, ks1) * kfrac;
                    if (fadeSampsIR > 0 && i >= fadeStartKern)
                    {
                        const int   fadeLen = juce::jmax (1, kernLen - fadeStartKern);
                        const float t       = (float)(i - fadeStartKern) / (float)fadeLen;
                        v *= 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * t));
                    }
                    contactKernel[i] = v;
                }
                contactKernelLen = kernLen;
            }

            // Build reverb tail with complementary cosine fade-in at the split point.
            const int tailStartIR = juce::jmax (0, splitSampIR - fadeSampsIR);
            const int tailLen     = newLen - tailStartIR;
            if (tailLen > 0)
            {
                juce::AudioBuffer<float> tail (1, tailLen);
                for (int i = 0; i < tailLen; ++i)
                {
                    float v = processed.getSample (0, tailStartIR + i);
                    if (fadeSampsIR > 0 && i < fadeSampsIR)
                    {
                        const float t = (float)i / (float)fadeSampsIR;
                        v *= 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi * t));
                    }
                    tail.setSample (0, i, v);
                }
                convEngine.loadImpulseResponse (std::move (tail),
                    rawIRSampleRate,
                    juce::dsp::Convolution::Stereo::no,
                    juce::dsp::Convolution::Trim::yes,
                    juce::dsp::Convolution::Normalise::no);
                convHasIR = true;
            }
            else
            {
                convHasIR = false;
            }
        }
    };

    // All per-layer parameter values read from APVTS once per block.
    struct LayerParams
    {
        LayerMode mode;
        bool  layerEn;
        float level, sat;

        float trigFreq, trigQ, trigThresh;

        float noiseBurst, noiseAttack, noiseFreq, noiseBw;

        float bounceMass, bounceGap, bounceRest, bounceFreq, bounceBw;
        int   bounceWires;
        float bounceSpread;

        float sampleFreq, sampleBw;
        float samplePitch, sampleGain;
        float sampleStart, sampleAttack, sampleSustain, sampleDecay;

        float rattleGap, rattleK, rattleDamp;
        int   rattleModes, rattleMat;
        float rattleTune, rattleDecay, rattleRough, rattleJitter, rattleTone, rattleSpread;
        float rattleFiltFreq, rattleFiltBw;
        bool  rattleModalSat, sourceFilterEn, trigFilterEn;

        bool  convEn;
        float convWet, convDryWet, convGain, convStart, convAttack, convSustain, convSplit;

        float resGain, resWet, resSat;
        bool  resClip;
        int   resMat, resModes;
        float resTune, resDecay, resRough, resTone, resSpread;
    };

    LayerParams readLayerParams    (const juce::String& prefix) const;
    void        updateLayerModels  (int idx, const LayerParams& p);
    void        processLayerSample (int idx, int sampleIdx, float driveSignal,
                                    const LayerParams& p, bool convActive,
                                    float& outL, float& outR);

    Layer     layers[2];
    Saturator masterSat;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RattlerAudioProcessor)
};
