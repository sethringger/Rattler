#pragma once
#include "ModelCommon.h"

// =============================================================================
// ResonatorModel
// Impact-excited 2nd-order IIR resonator with material presets.
// Used as a lightweight single-mode body resonance for Noise/Bounce modes.
// =============================================================================
struct ResonatorModel
{
    struct MaterialPreset { float freqHz, decayMs; const char* name; };

    static constexpr int kNumMaterials = 7;

    static const MaterialPreset* getMaterials()
    {
        static const MaterialPreset p[] = {
            { 3200.0f,  80.0f, "Metal"    },
            { 5000.0f, 120.0f, "Glass"    },
            { 1800.0f,  40.0f, "Plastic"  },
            {  800.0f,  25.0f, "Wood"     },
            { 2400.0f,  60.0f, "Hardware" },
            {  400.0f,  45.0f, "Body"     },
            {  120.0f,  30.0f, "Sub"      },
        };
        return p;
    }

    static juce::StringArray getMaterialNames()
    {
        juce::StringArray arr;
        const auto* presets = getMaterials();
        for (int i = 0; i < kNumMaterials; ++i)
            arr.add (presets[i].name);
        return arr;
    }

    float y1 = 0.0f, y2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float normGain = 1.0f;

    int          excSamplesLeft = 0;
    juce::Random rng;

    double sampleRate   = 44100.0;
    int    lastMaterial = -1;
    float  lastOffsetSt = 0.0f;
    float  lastDecayMs  = 80.0f;

    float feedbackDrive = 0.0f;

    void prepare (double sr, int /*maxBlockSize*/)
    {
        sampleRate = sr;
        updateCoeffs (0, 0.0f, getMaterials()[0].decayMs);
        y1 = y2 = 0.0f;
        excSamplesLeft = 0;
    }

    void trigger() { excSamplesLeft = 3; }

    void setParams (int materialIdx, float offsetSemitones, float decayMs)
    {
        if (materialIdx == lastMaterial
            && offsetSemitones == lastOffsetSt
            && decayMs == lastDecayMs) return;
        lastMaterial = materialIdx;
        lastOffsetSt = offsetSemitones;
        lastDecayMs  = decayMs;
        updateCoeffs (materialIdx, offsetSemitones, decayMs);
    }

    void setFeedbackSat (float drive01) { feedbackDrive = drive01; }

    void updateCoeffs (int idx, float offsetSemitones, float decayMs)
    {
        idx = juce::jlimit (0, kNumMaterials - 1, idx);
        const float baseFreq = getMaterials()[idx].freqHz;
        const float freq     = juce::jlimit (80.0f, 18000.0f,
                                   baseFreq * std::pow (2.0f, offsetSemitones / 12.0f));
        const float omega    = juce::MathConstants<float>::twoPi * freq / (float)sampleRate;
        const float r        = std::exp (-6.908f / (decayMs * 0.001f * (float)sampleRate));
        a1       = 2.0f * r * std::cos (omega);
        a2       = -r * r;
        normGain = std::sin (omega) / std::sqrt (3.0f);
    }

    float processSample (float externalExcite = 0.0f)
    {
        const bool active = std::abs (y1) > 1e-7f || std::abs (y2) > 1e-7f
                         || excSamplesLeft > 0     || externalExcite != 0.0f;
        if (!active) return 0.0f;

        float x = externalExcite;
        if (excSamplesLeft > 0)
        {
            x += (rng.nextFloat() * 2.0f - 1.0f) * normGain;
            --excSamplesLeft;
        }

        float y = x + a1 * y1 + a2 * y2;
        y2 = y1;
        y1 = (feedbackDrive > 0.001f) ? y / (1.0f + feedbackDrive * std::abs (y)) : y;

        if      (std::abs (y1) < 1e-7f) y1 = y2 = 0.0f;
        else if (std::abs (y2) < 1e-7f) y2 = 0.0f;

        return y1;
    }
};
