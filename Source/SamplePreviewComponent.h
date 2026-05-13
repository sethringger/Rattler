#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>

class SamplePreviewComponent : public juce::Component
{
public:
    void setSampleData (const juce::AudioBuffer<float>& data, double sr)
    {
        sampleData = data;
        sampleRate = sr;
        samplePeak = 1e-6f;
        for (int ch = 0; ch < data.getNumChannels(); ++ch)
            for (int i = 0; i < data.getNumSamples(); ++i)
                samplePeak = juce::jmax (samplePeak, std::abs (data.getSample (ch, i)));
        repaint();
    }

    void setParams (float attackMs_, float sustainMs_, float decayMs_,
                    float startMs_,  float gainDb_)
    {
        attackMs  = attackMs_;
        sustainMs = sustainMs_;
        decayMs   = decayMs_;
        startMs   = startMs_;
        gainDb    = gainDb_;
        repaint();
    }

    std::function<void(float)> onStartMsChanged;
    std::function<void(float)> onAttackMsChanged;
    std::function<void(float)> onSustainMsChanged;
    std::function<void(float)> onDecayMsChanged;
    std::function<void()>      onAnyHandleDragEnded;
    std::function<void()>      onClicked;   // fired when clicked outside any handle (load/replace)

    void paint (juce::Graphics& g) override
    {
        static const juce::Colour kBg      { 0xff111518 };
        static const juce::Colour kWaveDim { 0xff253040 };
        static const juce::Colour kWave    { 0xff3a6a9a };
        static const juce::Colour kEnv     { 0xffffa040 };
        static const juce::Colour kBorder  { 0xff2a3a4a };
        static const juce::Colour kText    { 0xff555f6a };
        static const juce::Colour kSubtext { 0xff3d4a55 };
        static const juce::Colour kBarBg   { 0xff0c1014 };
        static const juce::Colour kHStart   { 0xff4a9eff };
        static const juce::Colour kHAttack  { 0xff40c060 };
        static const juce::Colour kHSustain { 0xffe0c040 };
        static const juce::Colour kHDecay   { 0xffffa040 };

        const auto bounds = getLocalBounds();
        const int  w = bounds.getWidth();
        const int  h = bounds.getHeight();
        const int  waveH = h - kBarH;
        const int  cy    = waveH / 2;

        g.setColour (kBg);
        g.fillRect (0, 0, w, waveH);
        g.setColour (kBarBg);
        g.fillRect (0, waveH, w, kBarH);
        g.setColour (kBorder);
        g.drawHorizontalLine (waveH, 0.f, (float)w);

        if (sampleData.getNumSamples() == 0)
        {
            g.setColour (kText);
            g.setFont (juce::Font (juce::FontOptions (11.f)));
            g.drawText ("Drop or load a sample file",
                        juce::Rectangle<int> (0, 0, w, waveH),
                        juce::Justification::centred);
            paintValueBar (g, w, waveH, kSubtext, kHStart, kHAttack, kHSustain, kHDecay);
            g.setColour (kBorder);
            g.drawRect (bounds.toFloat(), 1.f);
            if (!isEnabled()) { g.setColour (juce::Colour (0x99000000)); g.fillAll(); }
            return;
        }

        const int   srcLen  = sampleData.getNumSamples();
        const int   numCh   = sampleData.getNumChannels();
        const float gainLin = juce::Decibels::decibelsToGain (gainDb);

        const int startS     = juce::jlimit (0, srcLen - 1,
                                   (int)(startMs / 1000.f * (float)sampleRate));
        const int attackEnd  = juce::jmin (srcLen,
                                   startS + (int)(attackMs / 1000.f * (float)sampleRate));
        const int sustainEnd = juce::jmin (srcLen,
                                   attackEnd + (int)(sustainMs / 1000.f * (float)sampleRate));
        const float decaySec = juce::jmax (0.001f, decayMs / 1000.f);
        const int   decayEnd = juce::jmin (srcLen,
                                   sustainEnd + (int)(decaySec * (float)sampleRate));

        const int startX   = (startS    * w) / srcLen;
        const int attackX  = (attackEnd  * w) / srcLen;
        const int sustainX = (sustainEnd * w) / srcLen;
        const int decayX   = (decayEnd   * w) / srcLen;

        const float attackSec  = attackMs  / 1000.f;
        const float sustainSec = sustainMs / 1000.f;

        // Raw waveform
        for (int px = 0; px < w; ++px)
        {
            const int s0 = (px * srcLen) / w;
            const int s1 = juce::jmin (((px + 1) * srcLen) / w, srcLen - 1);
            float peak = 0.f;
            for (int s = s0; s <= s1; ++s)
                for (int ch = 0; ch < numCh; ++ch)
                    peak = juce::jmax (peak, std::abs (sampleData.getSample (ch, s)));
            const int barH = juce::jlimit (1, cy, (int)(peak * gainLin / samplePeak * cy));
            g.setColour (s0 < startS ? kWaveDim : kWave);
            g.fillRect (px, cy - barH, 1, barH * 2 + 1);
        }

        // Envelope overlay
        for (int px = 0; px < w; ++px)
        {
            const int s0 = (px * srcLen) / w;
            if (s0 < startS) continue;
            const int s1 = juce::jmin (((px + 1) * srcLen) / w, srcLen - 1);
            float peak = 0.f;
            for (int s = s0; s <= s1; ++s)
                for (int ch = 0; ch < numCh; ++ch)
                    peak = juce::jmax (peak, std::abs (sampleData.getSample (ch, s)));

            const float tSrc = (float)(s0 - startS) / (float)sampleRate;
            float envNorm;
            if (tSrc < attackSec)
                envNorm = (attackSec > 0.f) ? tSrc / attackSec : 1.f;
            else if (tSrc < attackSec + sustainSec)
                envNorm = 1.f;
            else
                envNorm = std::pow (0.001f, (tSrc - attackSec - sustainSec) / decaySec);

            const int fullH = juce::jlimit (1, cy, (int)(peak * gainLin / samplePeak * cy));
            const int envH  = (int)((float)fullH * envNorm);
            if (envH < 1) continue;
            g.setColour (juce::Colour (0xffffa040).withAlpha (0.38f));
            g.fillRect (px, cy - envH, 1, envH * 2 + 1);
        }

        // Envelope curves
        {
            juce::Path topPath, botPath;
            bool started = false;
            for (int px = 0; px < w; ++px)
            {
                const int s0 = (px * srcLen) / w;
                if (s0 < startS) continue;
                const float tSrc = (float)(s0 - startS) / (float)sampleRate;
                float envNorm;
                if (tSrc < attackSec)
                    envNorm = (attackSec > 0.f) ? tSrc / attackSec : 1.f;
                else if (tSrc < attackSec + sustainSec)
                    envNorm = 1.f;
                else
                    envNorm = std::pow (0.001f, (tSrc - attackSec - sustainSec) / decaySec);

                const float ey = (float)cy * envNorm;
                if (!started) { topPath.startNewSubPath ((float)px, (float)cy - ey);
                                botPath.startNewSubPath ((float)px, (float)cy + ey);
                                started = true; }
                else          { topPath.lineTo ((float)px, (float)cy - ey);
                                botPath.lineTo ((float)px, (float)cy + ey); }
            }
            if (started)
            {
                g.setColour (kEnv.withAlpha (0.65f));
                g.strokePath (topPath, juce::PathStrokeType (1.5f));
                g.strokePath (botPath, juce::PathStrokeType (1.5f));
            }
        }

        // Handle lines
        auto drawLine = [&] (int x, juce::Colour col, ActiveHandle type)
        {
            const bool hot = (activeHandle == type);
            g.setColour (hot ? col : col.withAlpha (0.5f));
            g.drawVerticalLine (x, 0.f, (float)waveH);
            if (hot)
            {
                g.setColour (col.withAlpha (0.15f));
                g.drawVerticalLine (x - 1, 0.f, (float)waveH);
                g.drawVerticalLine (x + 1, 0.f, (float)waveH);
            }
        };
        drawLine (startX,   kHStart,   ActiveHandle::Start);
        drawLine (attackX,  kHAttack,  ActiveHandle::Attack);
        drawLine (sustainX, kHSustain, ActiveHandle::Sustain);
        drawLine (decayX,   kHDecay,   ActiveHandle::Decay);

        paintValueBar (g, w, waveH, kSubtext, kHStart, kHAttack, kHSustain, kHDecay);

        // Duration label
        {
            const double durSec = (double)srcLen / sampleRate;
            g.setColour (kText);
            g.setFont (juce::Font (juce::FontOptions (10.f)));
            g.drawText (juce::String (durSec, 2) + "s",
                        juce::Rectangle<int> (0, 0, w, waveH).reduced (4, 2),
                        juce::Justification::topRight);
        }

        g.setColour (kBorder);
        g.drawRect (bounds.toFloat(), 1.f);
        if (!isEnabled()) { g.setColour (juce::Colour (0x99000000)); g.fillAll(); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (!isEnabled()) return;

        // No sample loaded: any click opens the load dialog
        if (sampleData.getNumSamples() == 0)
        {
            if (onClicked) onClicked();
            return;
        }

        const int waveH = getHeight() - kBarH;
        if (e.y >= waveH)
        {
            dragMode = DragMode::Vertical;
            const int col = juce::jlimit (0, 3, (e.x * 4) / getWidth());
            const ActiveHandle colMap[4] = {
                ActiveHandle::Start, ActiveHandle::Attack,
                ActiveHandle::Sustain, ActiveHandle::Decay };
            activeHandle = colMap[col];
        }
        else
        {
            dragMode     = DragMode::Horizontal;
            activeHandle = hitTestLine (e.x);
        }

        if (activeHandle != ActiveHandle::None)
        {
            dragStart = e.getPosition();
            switch (activeHandle)
            {
                case ActiveHandle::Start:   dragStartVal = startMs;   break;
                case ActiveHandle::Attack:  dragStartVal = attackMs;  break;
                case ActiveHandle::Sustain: dragStartVal = sustainMs; break;
                case ActiveHandle::Decay:   dragStartVal = decayMs;   break;
                default: break;
            }
            repaint();
        }
        else
        {
            // Clicked in waveform area with no handle nearby — load/replace sample
            if (onClicked) onClicked();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (activeHandle == ActiveHandle::None || !isEnabled()) return;

        const float totalMs = (sampleRate > 0 && sampleData.getNumSamples() > 0)
            ? (float)sampleData.getNumSamples() / (float)sampleRate * 1000.f
            : 5000.f;

        float newVal;
        if (dragMode == DragMode::Horizontal)
        {
            const float msPerPx = totalMs / (float)getWidth();
            newVal = dragStartVal + (float)(e.x - dragStart.x) * msPerPx;
        }
        else
        {
            const float delta = (float)(dragStart.y - e.y);
            const float scale = (activeHandle == ActiveHandle::Attack) ? (500.f / 250.f)
                                                                       : (totalMs / 250.f);
            newVal = dragStartVal + delta * scale;
        }

        switch (activeHandle)
        {
            case ActiveHandle::Start:
                newVal = juce::jlimit (0.f, totalMs, newVal);
                startMs = newVal;
                if (onStartMsChanged)   onStartMsChanged   (newVal);
                break;
            case ActiveHandle::Attack:
                newVal = juce::jlimit (0.f, 500.f, newVal);
                attackMs = newVal;
                if (onAttackMsChanged)  onAttackMsChanged  (newVal);
                break;
            case ActiveHandle::Sustain:
                newVal = juce::jlimit (0.f, totalMs, newVal);
                sustainMs = newVal;
                if (onSustainMsChanged) onSustainMsChanged (newVal);
                break;
            case ActiveHandle::Decay:
                newVal = juce::jlimit (10.f, totalMs, newVal);
                decayMs = newVal;
                if (onDecayMsChanged)   onDecayMsChanged   (newVal);
                break;
            default: break;
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { endDragIfActive(); }
    void visibilityChanged() override { if (!isVisible()) endDragIfActive(); }

private:
    void endDragIfActive()
    {
        if (activeHandle != ActiveHandle::None)
        {
            activeHandle = ActiveHandle::None;
            repaint();
            if (onAnyHandleDragEnded) onAnyHandleDragEnded();
        }
    }

    enum class ActiveHandle { None, Start, Attack, Sustain, Decay };
    enum class DragMode     { Horizontal, Vertical };
    static constexpr int kBarH = 22;

    static juce::String fmtMs (float ms)
    {
        if (ms < 10.f)   return juce::String (ms, 1) + "ms";
        if (ms < 1000.f) return juce::String ((int)ms) + "ms";
        return juce::String (ms / 1000.f, 1) + "s";
    }

    void paintValueBar (juce::Graphics& g, int w, int waveH,
                        juce::Colour subtext,
                        juce::Colour colStart, juce::Colour colAttack,
                        juce::Colour colSustain, juce::Colour colDecay) const
    {
        struct Item { const char* name; float ms; juce::Colour col; ActiveHandle handle; };
        const Item items[4] = {
            { "START",   startMs,   colStart,   ActiveHandle::Start   },
            { "ATTACK",  attackMs,  colAttack,  ActiveHandle::Attack  },
            { "SUSTAIN", sustainMs, colSustain, ActiveHandle::Sustain },
            { "DECAY",   decayMs,   colDecay,   ActiveHandle::Decay   },
        };
        const int colW  = w / 4;
        const int halfH = kBarH / 2;
        for (int i = 0; i < 4; ++i)
        {
            const int x  = i * colW;
            const bool hot = (activeHandle == items[i].handle && dragMode == DragMode::Vertical);
            g.setFont (juce::Font (juce::FontOptions (8.5f)));
            g.setColour (hot ? items[i].col.withAlpha (0.85f) : subtext);
            g.drawText (items[i].name, juce::Rectangle<int> (x, waveH, colW, halfH),
                        juce::Justification::centred);
            g.setFont (juce::Font (juce::FontOptions (9.5f)));
            g.setColour (hot ? items[i].col : items[i].col.withAlpha (0.75f));
            g.drawText (fmtMs (items[i].ms), juce::Rectangle<int> (x, waveH + halfH, colW, halfH),
                        juce::Justification::centred);
        }
    }

    ActiveHandle hitTestLine (int mx) const
    {
        const int w      = getWidth();
        const int srcLen = sampleData.getNumSamples();
        if (srcLen == 0) return ActiveHandle::None;

        const int startS     = juce::jlimit (0, srcLen - 1,
                                   (int)(startMs  / 1000.f * (float)sampleRate));
        const int attackEnd  = juce::jmin (srcLen,
                                   startS + (int)(attackMs  / 1000.f * (float)sampleRate));
        const int sustainEnd = juce::jmin (srcLen,
                                   attackEnd + (int)(sustainMs / 1000.f * (float)sampleRate));
        const int decayEnd   = juce::jmin (srcLen,
                                   sustainEnd + (int)(juce::jmax (0.001f, decayMs / 1000.f)
                                                      * (float)sampleRate));

        const int startX   = (startS    * w) / srcLen;
        const int attackX  = (attackEnd  * w) / srcLen;
        const int sustainX = (sustainEnd * w) / srcLen;
        const int decayX   = (decayEnd   * w) / srcLen;

        constexpr int kTol = 6;
        if (std::abs (mx - attackX)  <= kTol) return ActiveHandle::Attack;
        if (std::abs (mx - sustainX) <= kTol) return ActiveHandle::Sustain;
        if (std::abs (mx - decayX)   <= kTol) return ActiveHandle::Decay;
        if (std::abs (mx - startX)   <= kTol) return ActiveHandle::Start;
        return ActiveHandle::None;
    }

    juce::AudioBuffer<float> sampleData;
    double sampleRate = 44100.0;
    float  samplePeak = 1.f;
    float  attackMs   = 0.f;
    float  sustainMs  = 0.f;
    float  decayMs    = 2000.f;
    float  startMs    = 0.f;
    float  gainDb     = 0.f;

    ActiveHandle     activeHandle = ActiveHandle::None;
    DragMode         dragMode     = DragMode::Horizontal;
    juce::Point<int> dragStart;
    float            dragStartVal = 0.f;
};
