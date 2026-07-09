#pragma once
#include "ModelCommon.h"

// =============================================================================
// BounceModel
// Audio-rate mass-spring-boundary simulation with multi-voice wire mode.
//
// Wire mode (wireCount > 1): runs N parallel physics voices whose output
// filters are spread across a frequency range, approximating multiple wire
// strands (e.g. snare wires) that each buzz at slightly different timbres.
// =============================================================================
struct BounceModel
{
    static constexpr int kMaxWires = 4;

    float pos[kMaxWires]              = {};
    float vel[kMaxWires]              = {};
    float impactGain[kMaxWires]       = {};
    int   retriggerCountdown[kMaxWires] = {};

    float  impactDecay   = 0.99f;
    bool   filterEnabled = true;
    int    wireCount     = 1;
    double sampleRate    = 44100.0;

    juce::Random rng;
    juce::dsp::StateVariableTPTFilter<float> hpFilter[kMaxWires], lpFilter[kMaxWires];

    void prepare (double sr, int maxBlockSize)
    {
        sampleRate  = sr;
        impactDecay = std::pow (0.001f, 1.0f / (0.005f * (float)sr));

        for (int i = 0; i < kMaxWires; ++i)
        {
            pos[i] = vel[i] = impactGain[i] = 0.0f;
            retriggerCountdown[i] = 0;

            juce::dsp::ProcessSpec spec { sr, (juce::uint32)maxBlockSize, 1 };
            hpFilter[i].setType (juce::dsp::StateVariableTPTFilterType::highpass);
            hpFilter[i].setResonance (0.7071f);
            hpFilter[i].prepare (spec); hpFilter[i].reset();
            lpFilter[i].setType (juce::dsp::StateVariableTPTFilterType::lowpass);
            lpFilter[i].setResonance (0.7071f);
            lpFilter[i].prepare (spec); lpFilter[i].reset();
        }
    }

    void setFilter (float centreHz, float totalBwSt,
                    int nWires = 1, float spreadSt = 0.0f)
    {
        wireCount = juce::jlimit (1, kMaxWires, nWires);
        for (int i = 0; i < kMaxWires; ++i)
        {
            const float offset    = (wireCount > 1)
                ? ((float)i / (float)(wireCount - 1) - 0.5f) * spreadSt : 0.0f;
            const float voiceFreq = centreHz * std::pow (2.0f, offset / 12.0f);
            applyHPLPFilter (hpFilter[i], lpFilter[i], voiceFreq, totalBwSt);
        }
    }

    float processSample (float filteredDrive, float mass, float gap, float restitution)
    {
        float out = 0.0f;
        for (int i = 0; i < wireCount; ++i)
            out += processVoice (i, filteredDrive, mass, gap, restitution);
        return (wireCount > 1) ? out / (float)wireCount : out;
    }

private:
    float processVoice (int i, float filteredDrive, float mass, float gap, float restitution)
    {
        constexpr float driveScale = 0.00008f;
        constexpr float damping    = 0.05f;

        float acc = (filteredDrive * driveScale) / mass - vel[i] * damping;
        vel[i] += acc;
        pos[i] += vel[i];
        vel[i] += (rng.nextFloat() * 2.0f - 1.0f) * 0.000005f;

        if (retriggerCountdown[i] > 0) --retriggerCountdown[i];

        float impactVel = 0.0f;
        if (pos[i] < 0.0f)
        {
            pos[i]    = 0.0f;
            impactVel = std::abs (vel[i]);
            vel[i]    = -vel[i] * restitution;
        }
        else if (pos[i] > gap)
        {
            pos[i]    = gap;
            impactVel = std::abs (vel[i]);
            vel[i]    = -vel[i] * restitution;
        }

        if (impactVel > 0.0002f && retriggerCountdown[i] == 0)
        {
            impactGain[i]         = juce::jmin (1.0f, impactVel * 800.0f);
            retriggerCountdown[i] = (int)(0.015 * sampleRate);
        }

        if (impactGain[i] < 1e-5f) return 0.0f;

        float noise = (rng.nextFloat() * 2.0f - 1.0f) * impactGain[i];
        impactGain[i] *= impactDecay;
        if (!filterEnabled) return noise;
        noise = hpFilter[i].processSample (0, noise);
        return  lpFilter[i].processSample (0, noise);
    }
};
