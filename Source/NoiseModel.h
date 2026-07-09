#pragma once
#include "ModelCommon.h"

// =============================================================================
// NoiseModel
// White noise burst shaped by an exponential envelope and a bandpass filter.
// =============================================================================
struct NoiseModel
{
    float burstGain     = 0.0f;
    float decayCoeff    = 0.9f;
    int   attackSamples = 0;
    int   attackPos     = 0;
    bool  filterEnabled = true;

    juce::Random rng;
    juce::dsp::StateVariableTPTFilter<float> hpFilter, lpFilter;

    void prepare (double sampleRate, int maxBlockSize)
    {
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)maxBlockSize, 1 };
        hpFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        hpFilter.setResonance (0.7071f);
        hpFilter.prepare (spec); hpFilter.reset();
        lpFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        lpFilter.setResonance (0.7071f);
        lpFilter.prepare (spec); lpFilter.reset();
    }

    void trigger() { burstGain = 0.25f; attackPos = 0; }

    void setDecay (float burstMs, double sampleRate)
    {
        const float samples = std::max (1.0f, burstMs * 0.001f * (float)sampleRate);
        decayCoeff = std::pow (0.001f, 1.0f / samples);
    }

    void setAttack (float attackMs, double sampleRate)
    {
        attackSamples = juce::jmax (0, (int)(attackMs * 0.001f * (float)sampleRate));
    }

    void setFilter (float centreHz, float totalBwSt)
    {
        applyHPLPFilter (hpFilter, lpFilter, centreHz, totalBwSt);
    }

    float processSample()
    {
        if (burstGain < 1e-5f) return 0.0f;
        float noise = (rng.nextFloat() * 2.0f - 1.0f) * burstGain;
        burstGain  *= decayCoeff;

        const float env = (attackSamples > 0 && attackPos < attackSamples)
            ? (float)attackPos / (float)attackSamples : 1.0f;
        ++attackPos;
        noise *= env;

        if (!filterEnabled) return noise;
        noise = hpFilter.processSample (0, noise);
        return   lpFilter.processSample (0, noise);
    }
};
