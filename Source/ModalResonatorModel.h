#pragma once
#include "ModalRattleModel.h"  // shares MaterialPreset and getMaterials()

// =============================================================================
// ModalResonatorModel
//
// Trigger-based multi-mode IIR resonator bank driven by the same material
// presets as ModalRattleModel.  Used with Noise / Bounce / Sample sources:
// an impact trigger fires a broadband excitation burst that rings through the
// modal bank, producing realistic body resonance.
// =============================================================================
struct ModalResonatorModel
{
    static constexpr int kMaxModes     = ModalRattleModel::kMaxModes;
    static constexpr int kNumMaterials = ModalRattleModel::kNumMaterials;

    static const ModalRattleModel::MaterialPreset* getMaterials()
    { return ModalRattleModel::getMaterials(); }

    static juce::StringArray getMaterialNames()
    { return ModalRattleModel::getMaterialNames(); }

    // --- Modal IIR bank ---
    float y1[kMaxModes] = {}, y2[kMaxModes] = {};
    float a1[kMaxModes] = {}, a2[kMaxModes] = {};
    float modeGain[kMaxModes] = {};
    float toneMod [kMaxModes] = {};
    float panL    [kMaxModes] = {};
    float panR    [kMaxModes] = {};

    double sampleRate   = 44100.0;
    int    numModes     = 8;
    int    lastMaterial = -1;
    float  lastOffsetSt = 1e9f;
    float  lastDecayMs  = -1.0f;

    float feedbackDrive = 0.0f;
    float roughness     = 0.0f;
    float tone          = 0.0f;
    float spread        = 0.0f;

    // 2-sample input delay for bandpass excitation (x[n] - x[n-2])
    float srcDelay1 = 0.0f;
    float srcDelay2 = 0.0f;

    juce::Random rng;

    juce::dsp::StateVariableTPTFilter<float> hpFilter, lpFilter;
    juce::dsp::StateVariableTPTFilter<float> hpFilterR, lpFilterR;
    bool filterEnabled = false;
    bool clipEnabled   = true;

    void prepare (double sr, int maxBlockSize)
    {
        sampleRate = sr;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32)maxBlockSize, 1 };
        hpFilter.setType  (juce::dsp::StateVariableTPTFilterType::highpass);
        hpFilter.setResonance (0.7071f); hpFilter.prepare (spec); hpFilter.reset();
        lpFilter.setType  (juce::dsp::StateVariableTPTFilterType::lowpass);
        lpFilter.setResonance (0.7071f); lpFilter.prepare (spec); lpFilter.reset();
        hpFilterR.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        hpFilterR.setResonance (0.7071f); hpFilterR.prepare (spec); hpFilterR.reset();
        lpFilterR.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        lpFilterR.setResonance (0.7071f); lpFilterR.prepare (spec); lpFilterR.reset();
        updateCoeffs (0, 0.0f, 80.0f);
        for (int m = 0; m < kMaxModes; ++m) y1[m] = y2[m] = 0.0f;
        srcDelay1 = srcDelay2 = 0.0f;
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

    void setFeedbackSat (float drive) { feedbackDrive = drive; }
    void setRoughness   (float r)     { roughness = r; }
    void setSpread      (float s)     { spread = s; applySpread(); }
    void setClipEnabled (bool b)      { clipEnabled = b; }

    void setTone (float t)
    {
        tone = t;
        if (lastMaterial == kNumMaterials - 1)
            updateCoeffs (lastMaterial, lastOffsetSt, lastDecayMs);
        else
        {
            const auto* p   = getMaterials();
            const int   mat = juce::jlimit (0, kNumMaterials - 1,
                                             lastMaterial < 0 ? 0 : lastMaterial);
            for (int m = 0; m < kMaxModes; ++m)
                toneMod[m] = std::pow (p[mat].ratios[m], tone);
        }
    }

    void setFilter (float centreHz, float totalBwSt)
    {
        applyHPLPFilter (hpFilter,  lpFilter,  centreHz, totalBwSt);
        applyHPLPFilter (hpFilterR, lpFilterR, centreHz, totalBwSt);
    }

    // srcIn: source model output. Excitation = 2nd-difference (removes DC / Nyquist).
    void processSample (float srcIn, float& outL, float& outR)
    {
        const float diff = srcIn - srcDelay2;
        srcDelay2 = srcDelay1;
        srcDelay1 = srcIn;

        bool active = std::abs (diff) > 1e-6f;
        if (!active)
        {
            for (int m = 0; m < numModes && !active; ++m)
                active = (std::abs (y1[m]) > 1e-7f || std::abs (y2[m]) > 1e-7f);
        }
        if (!active) { outL = outR = 0.0f; return; }

        const float excite = (roughness > 0.001f)
            ? diff * (1.0f + roughness * (rng.nextFloat() * 2.0f - 1.0f))
            : diff;

        float sumL = 0.0f, sumR = 0.0f;
        for (int m = 0; m < numModes; ++m)
        {
            const float ring = a1[m] * y1[m] + a2[m] * y2[m];
            const float y    = excite * modeGain[m] + ring;
            y2[m] = y1[m];
            if (!std::isfinite (y) || std::abs (y) > 20.0f)
                { y1[m] = y2[m] = 0.0f; continue; }
            y1[m] = y;
            if (std::abs (y1[m]) < 1e-7f) y1[m] = y2[m] = 0.0f;
            const float w = ring * toneMod[m];
            sumL += w * panL[m];
            sumR += w * panR[m];
        }

        if (numModes > 1)
        {
            const float inv = 1.0f / (float)numModes;
            sumL *= inv;
            sumR *= inv;
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

        if (clipEnabled)
        {
            auto softClip = [] (float x) -> float {
                constexpr float knee = 0.75f;
                const float a = std::abs (x);
                if (a <= knee) return x;
                const float over = (a - knee) / (1.0f - knee);
                return std::copysign (knee + (1.0f - knee) * (over / (1.0f + over)), x);
            };
            outL = softClip (outL);
            outR = softClip (outR);
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
            const bool  isUnison  = (material == kNumMaterials - 1);
            const float ratio     = p[material].ratios[m];
            const float fn        = juce::jlimit (20.0f, 18000.0f, base * ratio);
            const float detuneSt  = (isUnison && numModes > 1)
                ? ((float)m / (float)(numModes - 1) - 0.5f) * tone : 0.0f;
            const float fn_det    = (detuneSt != 0.0f)
                ? juce::jlimit (20.0f, 18000.0f, fn * std::pow (2.0f, detuneSt / 12.0f))
                : fn;
            const float omega     = juce::MathConstants<float>::twoPi * fn_det / (float)sampleRate;
            const float r         = std::exp (-6.908f / (decayMs / ratio * 0.001f * (float)sampleRate));
            a1[m]       = 2.0f * r * std::cos (omega);
            a2[m]       = -r * r;
            modeGain[m] = 1.0f / std::sqrt (ratio);
            gainSumSq  += modeGain[m] * modeGain[m];
        }

        const float norm = (gainSumSq > 1e-9f) ? 1.0f / std::sqrt (gainSumSq) : 1.0f;
        for (int m = 0; m < kMaxModes; ++m)
        {
            modeGain[m] *= norm;
            toneMod[m]   = std::pow (p[material].ratios[m], tone);
        }
        applySpread();
    }

    void applySpread()
    {
        const float kSqrt2 = juce::MathConstants<float>::sqrt2;
        for (int m = 0; m < kMaxModes; ++m)
        {
            const float frac  = (numModes > 1)
                ? 0.5f + 0.5f * (float)m / (float)(numModes - 1) : 1.0f;
            const float sign  = (m % 2 == 0) ? -1.0f : 1.0f;
            const float pos   = juce::jlimit (-1.0f, 1.0f, sign * spread * frac);
            const float angle = (pos + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            panL[m] = std::cos (angle) * kSqrt2;
            panR[m] = std::sin (angle) * kSqrt2;
        }
    }
};
