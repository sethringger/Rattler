#pragma once
#include "ModelCommon.h"
#include <juce_audio_formats/juce_audio_formats.h>

// =============================================================================
// SampleModel
// One-shot sample playback triggered on each rising-edge event.
// Supports pitch shift, start offset, and an attack/sustain/decay envelope.
// =============================================================================
struct SampleModel
{
    juce::AudioBuffer<float> buffer;
    double playPos       = -1.0;
    double fileRate      = 44100.0;
    double hostRate      = 44100.0;
    bool   filterEnabled = true;

    float  pitchSt    = 0.0f;
    float  gainDb     = 0.0f;
    float  startMs    = 0.0f;
    float  attackMs   = 0.0f;
    float  sustainMs  = 0.0f;
    float  decayMs    = 2000.0f;
    double envPos     = 0.0;

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
        pitchSt   = pitch_st;
        gainDb    = gain_db;
        startMs   = start_ms;
        attackMs  = attack_ms;
        sustainMs = sustain_ms;
        decayMs   = decay_ms;
    }

    void loadFile (const juce::File& f)
    {
        juce::AudioFormatManager fmt;
        fmt.registerBasicFormats();
        auto reader = std::unique_ptr<juce::AudioFormatReader> (fmt.createReaderFor (f));
        if (!reader) return;
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

        if (!filterEnabled) return out;
        out = hpFilter.processSample (0, out);
        return lpFilter.processSample (0, out);
    }

    bool hasFile() const { return buffer.getNumSamples() > 0; }
};
