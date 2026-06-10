#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>


// =============================================================================
// TriggerFilter
// Resonant bandpass for per-source trigger detection and bounce drive.
// =============================================================================
struct TriggerFilter
{
    juce::dsp::StateVariableTPTFilter<float> filter;

    void prepare (double sampleRate, int maxBlockSize)
    {
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)maxBlockSize, 1 };
        filter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
        filter.prepare (spec);
        filter.reset();
    }

    bool filterEnabled = true;

    void setFilter (float centreHz, float q)
    {
        filter.setCutoffFrequency (juce::jlimit (20.0f, 20000.0f, centreHz));
        filter.setResonance       (juce::jlimit (0.1f,  10.0f,    q));
    }

    float process (float x) { return filterEnabled ? filter.processSample (0, x) : x; }
};

// =============================================================================
// Shared filter helpers
// =============================================================================

// Sets HP and LP cutoffs from a centre frequency + total bandwidth in semitones.
inline void applyHPLPFilter (juce::dsp::StateVariableTPTFilter<float>& hp,
                              juce::dsp::StateVariableTPTFilter<float>& lp,
                              float centreHz, float totalBwSt)
{
    float half  = totalBwSt / 2.0f;
    float lp_fc = centreHz * std::pow (2.0f,  half / 12.0f);
    float hp_fc = centreHz * std::pow (2.0f, -half / 12.0f);
    lp.setCutoffFrequency (juce::jlimit (20.0f, 20000.0f, lp_fc));
    hp.setCutoffFrequency (juce::jlimit (20.0f, 20000.0f, hp_fc));
}

// =============================================================================
// Saturator
// Tanh waveshaper: y = tanh(d * x) / tanh(d)
//
// Properties:
//   - Passes through (0, 0) at all drive values (no DC offset)
//   - At x = ±1: output = ±1 (preserves headroom reference)
//   - drive = 0: transparent (linear pass-through)
//   - drive = 1: heavy saturation, approaching hard clip
//
// Call setDrive() once per block (computes d and 1/tanh(d)).
// Call process() per sample (one std::tanh call).
// =============================================================================
struct Saturator
{
    float d       = 0.0f;
    float scale   = 1.0f;   // 1 / tanh(d), precomputed

    // drive01: 0 = transparent, 1 = heavy saturation
    // Internally maps to waveshaper drive d in [1, 20] on a log curve.
    void setDrive (float drive01)
    {
        if (drive01 < 0.001f) { d = 0.0f; return; }
        d = std::exp (drive01 * std::log (20.0f));
        float td = std::tanh (d);
        scale = (td > 1e-6f) ? 1.0f / td : 1.0f;
    }

    float process (float x) const
    {
        if (d < 0.001f) return x;
        return std::tanh (d * x) * scale;
    }
};

// =============================================================================
// NoiseModel
// White noise burst shaped by an exponential envelope and a bandpass filter.
// =============================================================================
struct NoiseModel
{
    float burstGain    = 0.0f;
    float decayCoeff   = 0.9f;
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
        float samples = std::max (1.0f, burstMs * 0.001f * (float)sampleRate);
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

        if (! filterEnabled) return noise;
        noise = hpFilter.processSample (0, noise);
        return    lpFilter.processSample (0, noise);
    }
};

// =============================================================================
// ResonatorModel
// Impact-excited 2nd-order IIR resonator with material presets and optional
// tanh saturation in the feedback path.
//
// Feedback saturation: the value stored into y1/y2 each sample is passed
// through the Saturator before feeding back.  At low drive this is transparent;
// at higher drive it:
//   • prevents runaway self-oscillation
//   • adds harmonic richness that fades as the ring decays
//   • makes the decay curve slightly non-linear (brighter at onset, cleaner tail)
// =============================================================================
struct ResonatorModel
{
    struct MaterialPreset { float freqHz, decayMs; const char* name; };

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

    static constexpr int kNumMaterials = 7;

    static juce::StringArray getMaterialNames()
    {
        juce::StringArray arr;
        const auto* presets = getMaterials();
        for (int i = 0; i < kNumMaterials; ++i)
            arr.add (presets[i].name);
        return arr;
    }

    // Resonator state
    float y1 = 0.0f, y2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float normGain = 1.0f;

    int          excSamplesLeft = 0;
    juce::Random rng;

    double sampleRate   = 44100.0;
    int    lastMaterial = -1;
    float  lastOffsetSt = 0.0f;
    float  lastDecayMs  = 80.0f;

    float feedbackDrive = 0.0f;  // 0 = transparent, 1 = heavy feedback saturation

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

    // drive01: 0 = transparent feedback, 1 = heavy feedback saturation
    void setFeedbackSat (float drive01) { feedbackDrive = drive01; }

    void updateCoeffs (int idx, float offsetSemitones, float decayMs)
    {
        idx = juce::jlimit (0, kNumMaterials - 1, idx);
        float baseFreq = getMaterials()[idx].freqHz;
        float freq     = juce::jlimit (80.0f, 18000.0f,
                             baseFreq * std::pow (2.0f, offsetSemitones / 12.0f));
        float omega    = juce::MathConstants<float>::twoPi * freq / (float)sampleRate;
        float r        = std::exp (-6.908f / (decayMs * 0.001f * (float)sampleRate));
        a1 = 2.0f * r * std::cos (omega);
        a2 = -r * r;
        normGain = std::sin (omega) / std::sqrt (3.0f);
    }

    float processSample (float externalExcite = 0.0f)
    {
        bool active = std::abs (y1) > 1e-7f || std::abs (y2) > 1e-7f
                   || excSamplesLeft > 0     || externalExcite != 0.0f;
        if (! active) return 0.0f;

        float x = externalExcite;
        if (excSamplesLeft > 0)
        {
            x += (rng.nextFloat() * 2.0f - 1.0f) * normGain;
            --excSamplesLeft;
        }

        float y = x + a1 * y1 + a2 * y2;
        y2 = y1;
        // Soft-clip feedback: y/(1+drive*|y|) — unity gain at zero, clips at ±1/drive.
        // Unlike tanh-normalised approach, this stays transparent for small y at any drive.
        y1 = (feedbackDrive > 0.001f)
                 ? y / (1.0f + feedbackDrive * std::abs (y))
                 : y;

        if (std::abs (y1) < 1e-7f)
            y1 = y2 = 0.0f;
        else if (std::abs (y2) < 1e-7f)
            y2 = 0.0f;

        return y1;  // output the same value that feeds back
    }
};

// =============================================================================
// BounceModel
// Audio-rate mass-spring-boundary simulation with multi-voice wire mode.
//
// Wire mode (wireCount > 1): runs N parallel physics voices whose output
// filters are spread across a frequency range.  Each voice evolves
// independently because the stochastic perturbation in the physics loop
// naturally de-correlates their positions after the first few impacts.
// This approximates the sound of multiple wire strands (e.g. snare wires)
// that each buzz at slightly different timbres.
//
// State:  pos in [0, gap],  vel updated each sample, per voice
// Sound:  short noise burst scaled by impact velocity, bandpass-filtered
// =============================================================================
struct BounceModel
{
    static constexpr int kMaxWires = 4;

    // Per-voice physics state
    float pos[kMaxWires]              = {};
    float vel[kMaxWires]              = {};
    float impactGain[kMaxWires]       = {};
    int   retriggerCountdown[kMaxWires] = {};

    float impactDecay   = 0.99f;
    bool  filterEnabled = true;
    juce::Random rng;
    double sampleRate = 44100.0;

    // Per-voice output filters — voices are spread across different centre freqs
    juce::dsp::StateVariableTPTFilter<float> hpFilter[kMaxWires], lpFilter[kMaxWires];

    int wireCount = 1;  // active voices (1 = single bounce, 2-4 = wire mode)

    void prepare (double sr, int maxBlockSize)
    {
        sampleRate  = sr;
        impactDecay = std::pow (0.001f, 1.0f / (0.005f * (float)sr)); // 5 ms click

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

    // centreHz  : base passband centre (voice 0)
    // totalBwSt : total bandwidth in semitones per voice
    // nWires    : number of active voices (1–kMaxWires)
    // spreadSt  : total semitone spread across all voices
    //             (e.g. spreadSt=12 with nWires=3 → voices at -6, 0, +6 st)
    void setFilter (float centreHz, float totalBwSt,
                    int nWires = 1, float spreadSt = 0.0f)
    {
        wireCount = juce::jlimit (1, kMaxWires, nWires);

        for (int i = 0; i < kMaxWires; ++i)
        {
            // Distribute voices evenly across [-spreadSt/2, +spreadSt/2]
            float offset = (wireCount > 1)
                ? ((float)i / (float)(wireCount - 1) - 0.5f) * spreadSt
                : 0.0f;
            float voiceFreq = centreHz * std::pow (2.0f, offset / 12.0f);
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
            impactGain[i] = juce::jmin (1.0f, impactVel * 800.0f);
            retriggerCountdown[i] = (int)(0.015 * sampleRate);
        }

        if (impactGain[i] < 1e-5f) return 0.0f;

        float noise = (rng.nextFloat() * 2.0f - 1.0f) * impactGain[i];
        impactGain[i] *= impactDecay;
        if (! filterEnabled) return noise;
        noise = hpFilter[i].processSample (0, noise);
        return  lpFilter[i].processSample (0, noise);
    }
};

// =============================================================================
// ModalRattleModel
//
// Unified physics + modal synthesis model of a resonant object in intermittent
// contact with a vibrating substrate.
//
// Physics:
//   - The substrate position is derived from the (trigger-filtered) drive signal
//     via a leaky integrator, representing the actual displacement of the
//     vibrating surface rather than a fixed boundary.
//   - Contact occurs when the substrate displacement closes the gap to the
//     rattle object: separation = (x_r + gap0) - x_s  < 0
//   - Contact force is Hertzian (one-sided): F = k * penetration^1.5
//     This is physically accurate for metal-on-metal contact and naturally
//     produces a short force pulse whose duration depends on stiffness.
//
// Modal resonance:
//   - The object's sound comes from N IIR resonators (modal bank), each tuned
//     to a mode of the material.  Contact force excites all modes simultaneously
//     with a 1/sqrt(ratio) gain falloff (energy ~ 1/frequency).
//   - Higher modes decay faster: decayMs_n = decayMs / ratio_n — this gives
//     the natural "bright attack, warm tail" character of metal impacts.
//   - During contact, extra velocity damping is applied to all modes, shortening
//     the ring exactly as a real object pressed against a surface would.
//   - Between contacts the modes ring freely.
//
// The coupling between bounce and resonance: x_r (rattler position) is read
// back from the modal sum, so the ongoing resonance directly affects whether
// contact happens — no separate bounce physics is needed.
// =============================================================================
struct ModalRattleModel
{
    static constexpr int kMaxModes    = 12;
    static constexpr int kNumMaterials = 6;

    struct MaterialPreset
    {
        const char* name;
        float baseFreqHz;               // default fundamental frequency
        float ratios[kMaxModes];        // fn = baseFreqHz * ratios[n]
    };

    static const MaterialPreset* getMaterials()
    {
        // Free-free bar: transverse mode ratios (kn*L)^4, kn*L = 4.730, 7.853...
        // Plate: square plate (m^2+n^2) mode ratios, normalised to (1,1)=1
        // Wire: harmonic string modes
        // Shell: cylindrical shell empirical
        // Spring: coil spring (stretched harmonic)
        // Unison: all modes at same frequency (cluster/chorus effect)
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
    float modeGain[kMaxModes]     = {};   // contact force → mode excitation
    float participation[kMaxModes]= {};   // mode displacement → x_r readback

    // --- Substrate integration ---
    float substratePos     = 0.0f;   // leaky integral of drive signal
    float substrateLeak    = 0.9f;   // recomputed in prepare()

    // --- Contact state (readable by processor for visualisation if needed) ---
    bool  inContact = false;

    // --- Output HP/LP filter (controlled by FilterXYPad in SOURCE tab) ---
    juce::dsp::StateVariableTPTFilter<float> hpFilter, lpFilter;

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
    float roughness   = 0.0f;   // contact-force noise (unchanged)

    // --- Modal feedback saturation ---
    bool  modalFeedbackSat = false;
    bool  filterEnabled    = true;

    // --- Convolution feedback (set externally once per block) ---
    float convWet = 0.0f;   // 0 = disabled (today's behavior); 1 = full IR feedback in x_r

    // --- Performance options ---
    bool useClamp    = false;  // replace per-mode isfinite() with jlimit clamp
    bool useFastTanh = false;  // replace std::tanh in contact force with Padé approximant

    // --- Idle gate ---
    bool idleGateEnabled = false;
    bool fullyIdle       = false;
    int  idleCounter     = 0;

    static constexpr int   kIdleThreshSamples = 4096;   // ~93ms hysteresis before sleeping
    static constexpr float kIdleOutputThresh  = 1e-5f;  // ~-100 dB amplitude
    static constexpr float kIdleDriveThresh   = 1e-4f;

    // --- Pitch jitter ---
    float a1_0[kMaxModes]   = {};  // base a1 before any jitter scatter
    float r_mode[kMaxModes] = {};  // pole radius per mode
    float jitter        = 0.0f;   // per-contact frequency scatter (0–1)
    bool  prevInContact = false;

    // --- Tone (spectral tilt) ---
    float toneMod[kMaxModes] = {};  // output scale per mode; 1.0 at tone=0
    float tone = 0.0f;

    // --- Stereo spread ---
    float panL[kMaxModes] = {};  // per-mode L gain; 1.0 at center
    float panR[kMaxModes] = {};  // per-mode R gain; 1.0 at center
    float spread = 0.0f;         // 0 = mono, 1 = fully spread

    // --- Right-channel output filters (L uses hpFilter/lpFilter) ---
    juce::dsp::StateVariableTPTFilter<float> hpFilterR, lpFilterR;

    juce::Random rng;

    void prepare (double sr, int maxBlockSize)
    {
        sampleRate = sr;
        // 20 ms leak time constant: substrate position tracks signal envelope
        substrateLeak = std::exp (-1.0f / (float)(sr * 0.02));

        juce::dsp::ProcessSpec spec { sr, (juce::uint32)maxBlockSize, 1 };
        hpFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        hpFilter.setResonance (0.7071f);
        hpFilter.prepare (spec); hpFilter.reset();
        lpFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
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

    // material: preset index (0–kNumMaterials-1)
    // offsetSt: semitone shift from preset base frequency
    // decayMs:  decay time of the fundamental mode
    // nModes:   number of active modes (1–kMaxModes)
    void setParams (int material, float offsetSt, float decayMs, int nModes)
    {
        nModes   = juce::jlimit (1, kMaxModes, nModes);
        material = juce::jlimit (0, kNumMaterials - 1, material);

        if (material == lastMaterial && offsetSt == lastOffsetSt
            && decayMs == lastDecayMs && nModes == numModes)
            return;

        lastMaterial = material;
        lastOffsetSt = offsetSt;
        lastDecayMs  = decayMs;
        numModes     = nModes;
        updateCoeffs (material, offsetSt, decayMs);
    }

    void setContactParams (float gap, float stiffness, float damp)
    {
        gap0        = gap;
        contactK    = stiffness;
        contactDamp = damp;
    }

    void setRoughness (float r) { roughness = r; }

    void setJitter (float j) { jitter = j; }

    // Spread: 0 = mono, 1.0 = mode 0 hard L/R (others at ±0.5), 2.0 = all modes hard L/R.
    void setSpread (float s)
    {
        spread = s;
        applySpread();
    }

    // Spectral tilt applied to output only — does not affect contact physics.
    // tone > 0 → higher modes louder; tone < 0 → lower modes louder.
    void setTone (float t)
    {
        tone = t;
        const auto* p   = getMaterials();
        const int   mat = juce::jlimit (0, kNumMaterials - 1,
                                         lastMaterial < 0 ? 0 : lastMaterial);
        for (int m = 0; m < kMaxModes; ++m)
            toneMod[m] = std::pow (p[mat].ratios[m], tone);
    }

    void updateCoeffs (int material, float offsetSt, float decayMs)
    {
        const auto* p    = getMaterials();
        float       base = juce::jlimit (20.0f, 4000.0f,
                               p[material].baseFreqHz * std::pow (2.0f, offsetSt / 12.0f));
        float gainSumSq  = 0.0f;

        for (int m = 0; m < kMaxModes; ++m)
        {
            float ratio = p[material].ratios[m];
            float fn    = juce::jlimit (20.0f, 18000.0f, base * ratio);
            float omega = juce::MathConstants<float>::twoPi * fn / (float)sampleRate;

            // Higher modes decay faster (proportional to frequency)
            float mDecay = decayMs / ratio;
            float r      = std::exp (-6.908f / (mDecay * 0.001f * (float)sampleRate));

            a1[m] = 2.0f * r * std::cos (omega);
            a2[m] = -r * r;
            a1_0[m]   = a1[m];   // cache for jitter scatter
            r_mode[m] = r;

            // Gain: energy ~ 1/ratio, amplitude ~ 1/sqrt(ratio)
            float g     = 1.0f / std::sqrt (ratio);
            modeGain[m] = g;
            gainSumSq  += g * g;
        }

        // Normalise so RMS drive is independent of mode count / material
        float norm = (gainSumSq > 1e-9f) ? 1.0f / std::sqrt (gainSumSq) : 1.0f;
        for (int m = 0; m < kMaxModes; ++m)
        {
            modeGain[m]     *= norm;
            participation[m] = modeGain[m];
        }

        // Refresh tone and spread since mode count / ratios may have changed
        setTone (tone);
        applySpread();
    }

private:
    void applySpread()
    {
        const float kSqrt2 = juce::MathConstants<float>::sqrt2;
        for (int m = 0; m < kMaxModes; ++m)
        {
            // Higher modes are wider; lower modes converge toward centre but stay spread.
            // frac: 0.5 at mode 0, 1.0 at the last (highest) mode.
            // At spread=1.0 → highest mode hard L/R, lowest mode at ±0.5.
            // At spread=2.0 → every mode clips to ±1.0 (fully hard L or R).
            const float frac = (numModes > 1)
                ? 0.5f + 0.5f * (float)m / (float)(numModes - 1)
                : 1.0f;
            const float sign = (m % 2 == 0) ? -1.0f : 1.0f;
            const float pos  = juce::jlimit (-1.0f, 1.0f, sign * spread * frac);
            const float angle = (pos + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            panL[m] = std::cos (angle) * kSqrt2;
            panR[m] = std::sin (angle) * kSqrt2;
        }
    }

public:

    // convFB: previous block's convolved output (0.0f when convolution is disabled or no IR loaded).
    //         Blended into x_r so the IR body response participates in gap contact physics.
    void processSample (float driveSignal, float rawGate, float convFB, float& outL, float& outR)
    {
        // Idle gate: skip all physics when output has been silent for kIdleThreshSamples.
        if (idleGateEnabled && fullyIdle)
        {
            if (std::abs (driveSignal) >= kIdleDriveThresh)
            {
                fullyIdle   = false;
                idleCounter = 0;
            }
            else
            {
                substratePos = substratePos * substrateLeak + driveSignal * (1.0f - substrateLeak);
                outL = outR = 0.0f;
                return;
            }
        }

        // 1. Substrate position: leaky integral of the drive signal.
        //    DC gain = 1: a constant drive of amplitude A → substratePos → A.
        substratePos = substratePos * substrateLeak + driveSignal * (1.0f - substrateLeak);

        // 2. Rattler effective position: weighted sum of modal displacements.
        float x_r = 0.0f;
        for (int m = 0; m < numModes; ++m)
            x_r += y1[m] * participation[m];

        // Blend in IR convolution feedback: shifts the contact position toward the
        // IR body's "push-back", making the IR participate in gap contact physics.
        // convWet=0 → identical to previous behavior; convWet=1 → IR fully drives gap.
        if (convWet > 0.0f)
            x_r = x_r * (1.0f - convWet) + convFB * convWet;

        // 3. Contact condition.
        // When no input is present, zero out x_r so the modal ring can't feed back
        // into contact detection and sustain itself indefinitely.
        const bool substrateActive = (std::abs (rawGate) > 1e-3f);
        const float effectiveXr = substrateActive ? x_r : 0.0f;
        const float separation  = (effectiveXr + gap0) - substratePos;
        const float penetration = std::max (0.0f, -separation);
        inContact = (penetration > 1e-9f);

        // 4. Hertzian contact force (one-sided).
        //    Soft-clip penetration via tanh so deep x_r excursions can't create
        //    runaway forces: linear below gap0, saturates smoothly above it.
        //    Roughness adds per-sample noise, simulating surface texture.
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
            F = contactK * softPen * std::sqrt (softPen);  // k * p^1.5
            if (roughness > 0.001f)
                F *= 1.0f + roughness * (rng.nextFloat() * 2.0f - 1.0f);
            F = std::max (0.0f, F);
        }

        // 5. Pitch jitter: on each new contact event, scatter each mode's frequency
        //    independently.  Modes ring out at the new pitch until the next contact.
        //    a1_0 / r_mode always hold the clean base values.
        if (inContact && !prevInContact && jitter > 0.001f)
        {
            const float maxSt = jitter * 6.0f;  // ±6 semitones at full jitter
            for (int m = 0; m < numModes; ++m)
            {
                const float scatterSt = maxSt * (rng.nextFloat() * 2.0f - 1.0f);
                const float cosOmega  = juce::jlimit (-1.0f, 1.0f,
                                            a1_0[m] / (2.0f * r_mode[m]));
                const float omegaNew  = std::acos (cosOmega)
                                        * std::pow (2.0f, scatterSt / 12.0f);
                a1[m] = 2.0f * r_mode[m] * std::cos (omegaNew);
                // a2 unchanged — decay stays constant
            }
        }
        prevInContact = inContact;

        // 6. Drive modal bank.
        const float contactDecay = inContact ? (1.0f - contactDamp * 0.01f) : 1.0f;
        const float sustainDecay = substrateActive ? 1.0f : 0.999f;
        const float decay = contactDecay * sustainDecay;

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

            if (std::abs (y1[m]) < 1e-7f)
                y1[m] = y2[m] = 0.0f;
            else if (std::abs (y2[m]) < 1e-7f)
                y2[m] = 0.0f;

            const float w = y1[m] * toneMod[m];
            sumL += w * panL[m];
            sumR += w * panR[m];
        }

        // 7. Output shaping filter — separate L/R so filter states track their signal
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

        // Update idle counter — sleep after kIdleThreshSamples of near-silence.
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
};

// =============================================================================
// ModalResonatorModel
//
// Trigger-based multi-mode IIR resonator bank driven by the same material
// presets as ModalRattleModel.  Designed for use with Noise / Bounce / Sample
// sources: an impact trigger fires a broadband excitation burst that rings
// through the modal bank, producing a realistic body resonance.
//
// Compared to the simple ResonatorModel this gives:
//   - Multi-mode physics (up to 12 IIR poles) with physically-derived
//     mode ratios per material (Bar, Plate, Wire, Shell, Spring)
//   - Spectral tilt control (Tone) — adjusts relative mode amplitudes
//   - Stereo spread — alternating panning of lower/higher modes
//   - Soft feedback saturation (same 1/(1+drive*|y|) limiter as ResonatorModel)
//   - Source HP/LP filter (same pair as FilterXYPad drives)
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

    float feedbackDrive  = 0.0f;
    float roughness      = 0.0f;
    float tone           = 0.0f;
    float spread         = 0.0f;

    // 2-sample input delay shared across all modes (for bandpass numerator x[n]-x[n-2])
    float srcDelay1 = 0.0f;
    float srcDelay2 = 0.0f;

    juce::Random rng;

    // --- Output HP/LP filter ---
    juce::dsp::StateVariableTPTFilter<float> hpFilter, lpFilter;
    juce::dsp::StateVariableTPTFilter<float> hpFilterR, lpFilterR;
    bool filterEnabled = false;  // no filter XY pad for resonator; disabled until added
    bool clipEnabled   = true;

    void prepare (double sr, int maxBlockSize)
    {
        sampleRate = sr;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32)maxBlockSize, 1 };

        hpFilter.setType  (juce::dsp::StateVariableTPTFilterType::highpass);
        hpFilter.setResonance (0.7071f);
        hpFilter.prepare (spec); hpFilter.reset();

        lpFilter.setType  (juce::dsp::StateVariableTPTFilterType::lowpass);
        lpFilter.setResonance (0.7071f);
        lpFilter.prepare (spec); lpFilter.reset();

        hpFilterR.setType  (juce::dsp::StateVariableTPTFilterType::highpass);
        hpFilterR.setResonance (0.7071f);
        hpFilterR.prepare (spec); hpFilterR.reset();

        lpFilterR.setType  (juce::dsp::StateVariableTPTFilterType::lowpass);
        lpFilterR.setResonance (0.7071f);
        lpFilterR.prepare (spec); lpFilterR.reset();

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

    void setTone (float t)
    {
        tone = t;
        if (lastMaterial == kNumMaterials - 1) // Unison: tone detunes poles
            updateCoeffs (lastMaterial, lastOffsetSt, lastDecayMs);
        else // Other materials: spectral tilt via toneMod
        {
            const auto* p = getMaterials();
            const int mat = juce::jlimit (0, kNumMaterials - 1,
                                          lastMaterial < 0 ? 0 : lastMaterial);
            for (int m = 0; m < kMaxModes; ++m)
                toneMod[m] = std::pow (p[mat].ratios[m], tone);
        }
    }

    void setSpread (float s) { spread = s; applySpread(); }

    void setFilter (float centreHz, float totalBwSt)
    {
        applyHPLPFilter (hpFilter,  lpFilter,  centreHz, totalBwSt);
        applyHPLPFilter (hpFilterR, lpFilterR, centreHz, totalBwSt);
    }

    void setClipEnabled (bool b) { clipEnabled = b; }

    // srcIn: output of the source model (Noise/Bounce/Sample).
    // Excitation is the 2nd-difference (x[n]-x[n-2]) — zeros at DC and Nyquist.
    // Feedback state y1/y2 is stored unmodified so the ring decays naturally as r^n.
    // A hard safety clamp prevents NaN/infinity; saturation is not applied here.
    void processSample (float srcIn, float& outL, float& outR)
    {
        // Bandpass excitation: difference removes DC and prevents energy buildup
        const float diff = srcIn - srcDelay2;
        srcDelay2 = srcDelay1;
        srcDelay1 = srcIn;

        bool active = std::abs (diff) > 1e-6f;
        if (! active)
        {
            for (int m = 0; m < numModes && !active; ++m)
                active = (std::abs (y1[m]) > 1e-7f || std::abs (y2[m]) > 1e-7f);
        }
        if (! active) { outL = outR = 0.0f; return; }

        const float excite = (roughness > 0.001f)
            ? diff * (1.0f + roughness * (rng.nextFloat() * 2.0f - 1.0f))
            : diff;

        float sumL = 0.0f, sumR = 0.0f;
        for (int m = 0; m < numModes; ++m)
        {
            const float ring = a1[m] * y1[m] + a2[m] * y2[m];
            const float y    = excite * modeGain[m] + ring;

            y2[m] = y1[m];
            // Pure feedback: preserve natural r^n ring-down; hard-clamp prevents infinity
            if (! std::isfinite (y) || std::abs (y) > 20.0f)
                { y1[m] = y2[m] = 0.0f; continue; }
            y1[m] = y;

            if (std::abs (y1[m]) < 1e-7f)
                y1[m] = y2[m] = 0.0f;

            const float w = ring * toneMod[m];
            sumL += w * panL[m];
            sumR += w * panR[m];
        }

        // Normalise by mode count so output level is consistent regardless of how many
        // modes are active — without this, high mode counts sum to excessive amplitude.
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
            // Piecewise soft clip: linear below knee, rational limit above.
            // Transparent at low levels — no saturation coloring.
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
            const bool  isUnison = (material == kNumMaterials - 1);
            const float ratio = p[material].ratios[m];
            const float fn    = juce::jlimit (20.0f, 18000.0f, base * ratio);
            // Unison only: tone (-1..+1) spreads all modes linearly across ±0.5 semitones.
            const float detuneSt = (isUnison && numModes > 1)
                ? ((float)m / (float)(numModes - 1) - 0.5f) * tone
                : 0.0f;
            const float fn_det = (detuneSt != 0.0f)
                ? juce::jlimit (20.0f, 18000.0f, fn * std::pow (2.0f, detuneSt / 12.0f))
                : fn;
            const float omega = juce::MathConstants<float>::twoPi * fn_det / (float)sampleRate;
            const float r     = std::exp (-6.908f / (decayMs / ratio * 0.001f * (float)sampleRate));
            a1[m]       = 2.0f * r * std::cos (omega);
            a2[m]       = -r * r;
            modeGain[m] = 1.0f / std::sqrt (ratio);
            gainSumSq  += modeGain[m] * modeGain[m];
        }
        // Normalise so RMS excitation gain is independent of mode count / material.
        const float norm = (gainSumSq > 1e-9f) ? 1.0f / std::sqrt (gainSumSq) : 1.0f;
        for (int m = 0; m < kMaxModes; ++m)
        {
            modeGain[m] *= norm;
            // Spectral tilt (neutral for Unison since ratios are all 1.0).
            toneMod[m] = std::pow (p[material].ratios[m], tone);
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

// =============================================================================
// SampleModel
// One-shot sample playback triggered on each rising-edge event.
// =============================================================================
struct SampleModel
{
    juce::AudioBuffer<float> buffer;
    double playPos       = -1.0;
    double fileRate      = 44100.0;
    double hostRate      = 44100.0;
    bool   filterEnabled = true;

    float  pitchSt   = 0.0f;
    float  gainDb    = 0.0f;
    float  startMs   = 0.0f;
    float  attackMs  = 0.0f;
    float  sustainMs = 0.0f;
    float  decayMs   = 2000.0f;
    double envPos    = 0.0;

    juce::dsp::StateVariableTPTFilter<float> hpFilter, lpFilter;

    void prepare (double sampleRate, int maxBlockSize)
    {
        hostRate = sampleRate;
        playPos  = -1.0;
        envPos   = 0.0;
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)maxBlockSize, 1 };

        hpFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        hpFilter.setResonance (0.7071f);
        hpFilter.prepare (spec); hpFilter.reset();

        lpFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        lpFilter.setResonance (0.7071f);
        lpFilter.prepare (spec); lpFilter.reset();
    }

    void setFilter (float centreHz, float totalBwSt)
    {
        applyHPLPFilter (hpFilter, lpFilter, centreHz, totalBwSt);
    }

    void setPlaybackParams (float pitch_st, float gain_db,
                            float start_ms, float attack_ms,
                            float sustain_ms, float decay_ms)
    {
        pitchSt  = pitch_st;
        gainDb   = gain_db;
        startMs  = start_ms;
        attackMs = attack_ms;
        sustainMs = sustain_ms;
        decayMs  = decay_ms;
    }

    void loadFile (const juce::File& f)
    {
        juce::AudioFormatManager fmt;
        fmt.registerBasicFormats();
        auto reader = std::unique_ptr<juce::AudioFormatReader> (fmt.createReaderFor (f));
        if (! reader) return;
        fileRate = reader->sampleRate;
        buffer.setSize ((int)reader->numChannels, (int)reader->lengthInSamples);
        reader->read (buffer.getArrayOfWritePointers(),
                      buffer.getNumChannels(), 0, buffer.getNumSamples());
    }

    void trigger()
    {
        if (buffer.getNumSamples() > 0)
        {
            const int startSamp = juce::jlimit (0, buffer.getNumSamples() - 1,
                                               (int)(startMs * 0.001f * (float)fileRate));
            playPos = (double)startSamp;
            envPos  = 0.0;
        }
    }

    float processSample()
    {
        if (playPos < 0.0 || buffer.getNumSamples() == 0) return 0.0f;

        const float pitchRatio = std::pow (2.0f, pitchSt / 12.0f);
        const float step  = (float)(fileRate / hostRate) * pitchRatio;
        const int   pos   = (int)playPos;
        const float alpha = (float)(playPos - (double)pos);

        if (pos >= buffer.getNumSamples() - 1) { playPos = -1.0; envPos = 0.0; return 0.0f; }

        float out = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            out += buffer.getSample (ch, pos) * (1.0f - alpha)
                 + buffer.getSample (ch, pos + 1) * alpha;
        out /= (float)buffer.getNumChannels();
        out *= juce::Decibels::decibelsToGain (gainDb);

        // Envelope
        const float t      = (float)envPos;
        const float aSec   = juce::jmax (0.0f, attackMs  * 0.001f);
        const float susSec = juce::jmax (0.0f, sustainMs * 0.001f);
        const float decSec = juce::jmax (0.001f, decayMs * 0.001f);
        float env;
        if (t < aSec)
            env = (aSec > 0.0f) ? t / aSec : 1.0f;
        else if (t < aSec + susSec)
            env = 1.0f;
        else
        {
            const float t2 = t - aSec - susSec;
            env = std::pow (0.001f, t2 / decSec);
            if (env < 1e-4f) { playPos = -1.0; envPos = 0.0; return 0.0f; }
        }
        out *= env;

        playPos += step;
        envPos  += 1.0 / hostRate;

        if (! filterEnabled) return out;
        out = hpFilter.processSample (0, out);
        return lpFilter.processSample (0, out);
    }

    bool hasFile() const { return buffer.getNumSamples() > 0; }
};
