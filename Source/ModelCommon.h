#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// =============================================================================
// TriggerFilter
// Resonant bandpass for per-source trigger detection and bounce drive.
// =============================================================================
struct TriggerFilter
{
    juce::dsp::StateVariableTPTFilter<float> filter;
    bool filterEnabled = true;

    void prepare (double sampleRate, int maxBlockSize)
    {
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)maxBlockSize, 1 };
        filter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
        filter.prepare (spec);
        filter.reset();
    }

    void setFilter (float centreHz, float q)
    {
        filter.setCutoffFrequency (juce::jlimit (20.0f, 20000.0f, centreHz));
        filter.setResonance       (juce::jlimit (0.1f,  10.0f,    q));
    }

    float process (float x) { return filterEnabled ? filter.processSample (0, x) : x; }
};

// =============================================================================
// applyHPLPFilter
// Sets HP and LP cutoffs from a centre frequency + total bandwidth in semitones.
// =============================================================================
inline void applyHPLPFilter (juce::dsp::StateVariableTPTFilter<float>& hp,
                              juce::dsp::StateVariableTPTFilter<float>& lp,
                              float centreHz, float totalBwSt)
{
    const float half  = totalBwSt / 2.0f;
    const float lp_fc = centreHz * std::pow (2.0f,  half / 12.0f);
    const float hp_fc = centreHz * std::pow (2.0f, -half / 12.0f);
    lp.setCutoffFrequency (juce::jlimit (20.0f, 20000.0f, lp_fc));
    hp.setCutoffFrequency (juce::jlimit (20.0f, 20000.0f, hp_fc));
}

// =============================================================================
// Saturator
// Tanh waveshaper: y = tanh(d * x) / tanh(d)
// drive01: 0 = transparent, 1 = heavy saturation
// =============================================================================
struct Saturator
{
    float d     = 0.0f;
    float scale = 1.0f;   // 1 / tanh(d), precomputed

    void setDrive (float drive01)
    {
        if (drive01 < 0.001f) { d = 0.0f; return; }
        d = std::exp (drive01 * std::log (20.0f));
        const float td = std::tanh (d);
        scale = (td > 1e-6f) ? 1.0f / td : 1.0f;
    }

    float process (float x) const
    {
        if (d < 0.001f) return x;
        return std::tanh (d * x) * scale;
    }
};
