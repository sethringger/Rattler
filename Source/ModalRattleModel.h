#pragma once
#include "ModelCommon.h"

// =============================================================================
// ModalRattleModel
//
// Unified physics + modal synthesis model of a resonant object in intermittent
// contact with a vibrating substrate.
//
// Physics:
//   - Substrate position is a leaky integral of the drive signal (displacement).
//   - Contact occurs when the substrate closes the gap to the rattle object.
//   - Contact force is Hertzian (one-sided, k * penetration^1.5).
//
// Modal resonance:
//   - N IIR resonators (modal bank) tuned to material mode ratios.
//   - Higher modes decay faster (decayMs / ratio).
//   - During contact, extra velocity damping shortens the ring.
//   - Pitch jitter scatters each mode's frequency on new contact.
// =============================================================================
struct ModalRattleModel
{
    static constexpr int kMaxModes    = 12;
    static constexpr int kNumMaterials = 6;

    struct MaterialPreset
    {
        const char* name;
        float baseFreqHz;
        float ratios[kMaxModes];
    };

    static const MaterialPreset* getMaterials()
    {
        static const MaterialPreset p[] = {
            { "Bar",    200.0f, { 1.0f,  2.757f,  5.404f,  8.933f, 13.344f, 18.648f,
                                  24.844f, 31.936f, 39.924f, 48.808f, 58.589f, 69.267f } },
            { "Plate",  500.0f, { 1.0f,  2.50f,   4.00f,   5.00f,   6.50f,   8.00f,
                                   9.00f,  10.00f,  12.50f,  13.00f,  16.00f,  18.00f  } },
            { "Wire",   150.0f, { 1.0f,  2.0f,    3.0f,    4.0f,    5.0f,    6.0f,
                                   7.0f,   8.0f,    9.0f,   10.0f,   11.0f,   12.0f   } },
            { "Shell",  800.0f, { 1.0f,  1.78f,   2.93f,   4.38f,   6.12f,   8.15f,
                                  10.47f, 13.08f,  16.00f,  19.22f,  22.84f,  26.75f  } },
            { "Spring", 300.0f, { 1.0f,  2.08f,   3.22f,   4.42f,   5.68f,   7.00f,
                                   8.38f,  9.82f,  11.32f,  12.88f,  14.50f,  16.18f  } },
            { "Unison", 440.0f, { 1.0f,  1.0f,    1.0f,    1.0f,    1.0f,    1.0f,
                                   1.0f,   1.0f,    1.0f,    1.0f,    1.0f,    1.0f   } },
        };
        return p;
    }

    static juce::StringArray getMaterialNames()
    {
        juce::StringArray arr;
        const auto* p = getMaterials();
        for (int i = 0; i < kNumMaterials; ++i)
            arr.add (p[i].name);
        return arr;
    }

    // --- Modal IIR state ---
    float y1[kMaxModes] = {}, y2[kMaxModes] = {};
    float a1[kMaxModes] = {}, a2[kMaxModes] = {};
    float modeGain[kMaxModes]      = {};
    float participation[kMaxModes] = {};

    // --- Substrate ---
    float substratePos  = 0.0f;
    float substrateLeak = 0.9f;

    bool inContact = false;

    // --- Output filter (L and R) ---
    juce::dsp::StateVariableTPTFilter<float> hpFilter, lpFilter;
    juce::dsp::StateVariableTPTFilter<float> hpFilterR, lpFilterR;

    // --- Cached params for change detection ---
    double sampleRate   = 44100.0;
    int    numModes     = 8;
    int    lastMaterial = -1;
    float  lastOffsetSt = 1e9f;
    float  lastDecayMs  = -1.0f;

    // --- Contact params ---
    float gap0        = 0.005f;
    float contactK    = 3.0f;
    float contactDamp = 0.2f;
    float roughness   = 0.0f;

    // --- Options ---
    bool  modalFeedbackSat = false;
    bool  filterEnabled    = true;
    float convWet          = 0.0f;
    bool  useClamp         = false;
    bool  useFastTanh      = false;

    // --- Idle gate ---
    bool idleGateEnabled = false;
    bool fullyIdle       = false;
    int  idleCounter     = 0;
    static constexpr int   kIdleThreshSamples = 4096;
    static constexpr float kIdleOutputThresh  = 1e-5f;
    static constexpr float kIdleDriveThresh   = 1e-4f;

    // --- Jitter ---
    float a1_0[kMaxModes]   = {};
    float r_mode[kMaxModes] = {};
    float jitter            = 0.0f;
    bool  prevInContact     = false;

    // --- Tone and spread ---
    float toneMod[kMaxModes] = {};
    float tone = 0.0f;
    float panL[kMaxModes] = {};
    float panR[kMaxModes] = {};
    float spread = 0.0f;

    juce::Random rng;

    void prepare (double sr, int maxBlockSize)
    {
        sampleRate    = sr;
        substrateLeak = std::exp (-1.0f / (float)(sr * 0.02));

        juce::dsp::ProcessSpec spec { sr, (juce::uint32)maxBlockSize, 1 };
        hpFilter.setType  (juce::dsp::StateVariableTPTFilterType::highpass);
        hpFilter.setResonance (0.7071f);
        hpFilter.prepare (spec); hpFilter.reset();
        lpFilter.setType  (juce::dsp::StateVariableTPTFilterType::lowpass);
        lpFilter.setResonance (0.7071f);
        lpFilter.prepare (spec); lpFilter.reset();
        hpFilterR.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        hpFilterR.setResonance (0.7071f);
        hpFilterR.prepare (spec); hpFilterR.reset();
        lpFilterR.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        lpFilterR.setResonance (0.7071f);
        lpFilterR.prepare (spec); lpFilterR.reset();

        updateCoeffs (0, 0.0f, 80.0f);
        reset();
    }

    void reset()
    {
        substratePos  = 0.0f;
        inContact     = false;
        prevInContact = false;
        fullyIdle     = false;
        idleCounter   = 0;
        hpFilter.reset();  lpFilter.reset();
        hpFilterR.reset(); lpFilterR.reset();
        for (int m = 0; m < kMaxModes; ++m)
        {
            y1[m] = y2[m] = 0.0f;
            if (a1_0[m] != 0.0f) a1[m] = a1_0[m];
        }
    }

    void setFilter (float centreHz, float totalBwSt)
    {
        applyHPLPFilter (hpFilter,  lpFilter,  centreHz, totalBwSt);
        applyHPLPFilter (hpFilterR, lpFilterR, centreHz, totalBwSt);
    }

    void setParams (int material, float offsetSt, float decayMs, int nModes)
    {
        nModes   = juce::jlimit (1, kMaxModes, nModes);
        material = juce::jlimit (0, kNumMaterials - 1, material);
        if (material == lastMaterial && offsetSt == lastOffsetSt
            && decayMs == lastDecayMs && nModes == numModes) return;
        lastMaterial = material;
        lastOffsetSt = offsetSt;
        lastDecayMs  = decayMs;
        numModes     = nModes;
        updateCoeffs (material, offsetSt, decayMs);
    }

    void setContactParams (float gap, float stiffness, float damp)
    {
        gap0 = gap; contactK = stiffness; contactDamp = damp;
    }

    void setRoughness (float r) { roughness = r; }
    void setJitter    (float j) { jitter = j; }

    void setSpread (float s) { spread = s; applySpread(); }

    void setTone (float t)
    {
        tone = t;
        const auto* p   = getMaterials();
        const int   mat = juce::jlimit (0, kNumMaterials - 1,
                                         lastMaterial < 0 ? 0 : lastMaterial);
        for (int m = 0; m < kMaxModes; ++m)
            toneMod[m] = std::pow (p[mat].ratios[m], tone);
    }

    // convFB: previous block's convolved output blended into x_r contact position.
    void processSample (float driveSignal, float rawGate, float convFB,
                        float& outL, float& outR)
    {
        if (idleGateEnabled && fullyIdle)
        {
            if (std::abs (driveSignal) >= kIdleDriveThresh)
                { fullyIdle = false; idleCounter = 0; }
            else
            {
                substratePos = substratePos * substrateLeak
                             + driveSignal * (1.0f - substrateLeak);
                outL = outR = 0.0f;
                return;
            }
        }

        substratePos = substratePos * substrateLeak + driveSignal * (1.0f - substrateLeak);

        float x_r = 0.0f;
        for (int m = 0; m < numModes; ++m)
            x_r += y1[m] * participation[m];

        if (convWet > 0.0f)
            x_r = x_r * (1.0f - convWet) + convFB * convWet;

        const bool  substrateActive = (std::abs (rawGate) > 1e-3f);
        const float effectiveXr     = substrateActive ? x_r : 0.0f;
        const float separation      = (effectiveXr + gap0) - substratePos;
        const float penetration     = std::max (0.0f, -separation);
        inContact = (penetration > 1e-9f);

        float F = 0.0f;
        if (inContact)
        {
            float softPen;
            if (useFastTanh)
            {
                const float x  = penetration / gap0;
                const float x2 = x * x;
                softPen = gap0 * x * (27.0f + x2) / (27.0f + 9.0f * x2);
            }
            else
            {
                softPen = gap0 * std::tanh (penetration / gap0);
            }
            F = contactK * softPen * std::sqrt (softPen);
            if (roughness > 0.001f)
                F *= 1.0f + roughness * (rng.nextFloat() * 2.0f - 1.0f);
            F = std::max (0.0f, F);
        }

        if (inContact && !prevInContact && jitter > 0.001f)
        {
            const float maxSt = jitter * 6.0f;
            for (int m = 0; m < numModes; ++m)
            {
                const float scatterSt = maxSt * (rng.nextFloat() * 2.0f - 1.0f);
                const float cosOmega  = juce::jlimit (-1.0f, 1.0f,
                                            a1_0[m] / (2.0f * r_mode[m]));
                const float omegaNew  = std::acos (cosOmega)
                                        * std::pow (2.0f, scatterSt / 12.0f);
                a1[m] = 2.0f * r_mode[m] * std::cos (omegaNew);
            }
        }
        prevInContact = inContact;

        const float contactDecay = inContact ? (1.0f - contactDamp * 0.01f) : 1.0f;
        const float sustainDecay = substrateActive ? 1.0f : 0.999f;
        const float decay        = contactDecay * sustainDecay;

        float sumL = 0.0f, sumR = 0.0f;
        for (int m = 0; m < numModes; ++m)
        {
            const float excite = F * modeGain[m];
            const float y      = excite + a1[m] * y1[m] + a2[m] * y2[m];
            y2[m] = y1[m];

            if (useClamp)
                y1[m] = juce::jlimit (-1e6f, 1e6f, y * decay);
            else if (std::isfinite (y))
                y1[m] = y * decay;
            else
                y1[m] = y2[m] = 0.0f;

            if (modalFeedbackSat)
                y1[m] = y1[m] / (1.0f + std::abs (y1[m]));

            if      (std::abs (y1[m]) < 1e-7f) y1[m] = y2[m] = 0.0f;
            else if (std::abs (y2[m]) < 1e-7f) y2[m] = 0.0f;

            const float w = y1[m] * toneMod[m];
            sumL += w * panL[m];
            sumR += w * panR[m];
        }

        if (filterEnabled)
        {
            outL = hpFilter.processSample  (0, sumL);
            outL = lpFilter.processSample  (0, outL);
            outR = hpFilterR.processSample (0, sumR);
            outR = lpFilterR.processSample (0, outR);
        }
        else
        {
            outL = sumL;
            outR = sumR;
        }

        if (idleGateEnabled)
        {
            if (std::abs (outL) + std::abs (outR) < kIdleOutputThresh
                && std::abs (driveSignal) < kIdleDriveThresh)
            {
                if (++idleCounter >= kIdleThreshSamples)
                {
                    fullyIdle = true;
                    for (int m = 0; m < kMaxModes; ++m)
                        y1[m] = y2[m] = 0.0f;
                    hpFilter.reset(); lpFilter.reset();
                    hpFilterR.reset(); lpFilterR.reset();
                }
            }
            else
            {
                idleCounter = 0;
            }
        }
    }

private:
    void updateCoeffs (int material, float offsetSt, float decayMs)
    {
        const auto* p    = getMaterials();
        const float base = juce::jlimit (20.0f, 4000.0f,
                               p[material].baseFreqHz * std::pow (2.0f, offsetSt / 12.0f));
        float gainSumSq = 0.0f;

        for (int m = 0; m < kMaxModes; ++m)
        {
            const float ratio = p[material].ratios[m];
            const float fn    = juce::jlimit (20.0f, 18000.0f, base * ratio);
            const float omega = juce::MathConstants<float>::twoPi * fn / (float)sampleRate;
            const float mDecay = decayMs / ratio;
            const float r      = std::exp (-6.908f / (mDecay * 0.001f * (float)sampleRate));
            a1[m] = 2.0f * r * std::cos (omega);
            a2[m] = -r * r;
            a1_0[m]   = a1[m];
            r_mode[m] = r;
            const float g = 1.0f / std::sqrt (ratio);
            modeGain[m]  = g;
            gainSumSq   += g * g;
        }

        const float norm = (gainSumSq > 1e-9f) ? 1.0f / std::sqrt (gainSumSq) : 1.0f;
        for (int m = 0; m < kMaxModes; ++m)
        {
            modeGain[m]     *= norm;
            participation[m] = modeGain[m];
        }

        setTone (tone);
        applySpread();
    }

    void applySpread()
    {
        const float kSqrt2 = juce::MathConstants<float>::sqrt2;
        for (int m = 0; m < kMaxModes; ++m)
        {
            const float frac = (numModes > 1)
                ? 0.5f + 0.5f * (float)m / (float)(numModes - 1) : 1.0f;
            const float sign  = (m % 2 == 0) ? -1.0f : 1.0f;
            const float pos   = juce::jlimit (-1.0f, 1.0f, sign * spread * frac);
            const float angle = (pos + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            panL[m] = std::cos (angle) * kSqrt2;
            panR[m] = std::sin (angle) * kSqrt2;
        }
    }
};
