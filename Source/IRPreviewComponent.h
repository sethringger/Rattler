#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>

class IRPreviewComponent : public juce::Component
{
public:
    void setIRData (const juce::AudioBuffer<float>& ir, double sr)
    {
        irData       = ir;
        irSampleRate = sr;
        irPeak       = 0.f;
        for (int i = 0; i < ir.getNumSamples(); ++i)
            irPeak = juce::jmax (irPeak, std::abs (ir.getSample (0, i)));
        if (irPeak < 1e-6f) irPeak = 1.f;
        repaint();
    }

    void setParams (float attackMs_, float sustainMs_, float decayMs_,
                    float startMs_,  float gainDb_,    float pitchSt_ = 0.f)
    {
        attackMs  = attackMs_;
        sustainMs = sustainMs_;
        decayMs   = decayMs_;
        startMs   = startMs_;
        gainDb    = gainDb_;
        pitchSt   = pitchSt_;
        repaint();
    }

    std::function<void(float)> onStartMsChanged;
    std::function<void(float)> onAttackMsChanged;
    std::function<void(float)> onSustainMsChanged;
    std::function<void(float)> onDecayMsChanged;
    // Fires once when the user releases a handle — use this to trigger reprocessing
    // rather than triggering on every drag event.
    std::function<void()> onAnyHandleDragEnded;

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

        if (irData.getNumSamples() == 0)
        {
            g.setColour (kText);
            g.setFont (juce::Font (juce::FontOptions (11.f)));
            g.drawText ("Drop or load an IR file",
                        juce::Rectangle<int> (0, 0, w, waveH),
                        juce::Justification::centred);
            paintValueBar (g, w, waveH, kSubtext, kHStart, kHAttack, kHSustain, kHDecay);
            g.setColour (kBorder);
            g.drawRect (bounds.toFloat(), 1.f);
            if (!isEnabled()) { g.setColour (juce::Colour (0x99000000)); g.fillAll(); }
            return;
        }

        const int   srcLen  = irData.getNumSamples();
        const float gainLin = juce::Decibels::decibelsToGain (gainDb);

        const int startS     = juce::jlimit (0, srcLen - 1,
                                   (int)(startMs / 1000.f * (float)irSampleRate));
        const int attackEnd  = juce::jmin (srcLen,
                                   startS + (int)(attackMs / 1000.f * (float)irSampleRate));
        const int sustainEnd = juce::jmin (srcLen,
                                   attackEnd + (int)(sustainMs / 1000.f * (float)irSampleRate));
        const float decaySec = juce::jmax (0.001f, decayMs / 1000.f);
        const int   decayEnd = juce::jmin (srcLen,
                                   sustainEnd + (int)(decaySec * (float)irSampleRate));

        const int startX   = (startS    * w) / srcLen;
        const int attackX  = (attackEnd  * w) / srcLen;
        const int sustainX = (sustainEnd * w) / srcLen;
        const int decayX   = (decayEnd   * w) / srcLen;

        const float attackSec  = attackMs  / 1000.f;
        const float sustainSec = sustainMs / 1000.f;

        // --- Layer 1: Raw IR waveform ---
        for (int px = 0; px < w; ++px)
        {
            const int s0 = (px * srcLen) / w;
            const int s1 = juce::jmin (((px + 1) * srcLen) / w, srcLen - 1);
            float peak = 0.f;
            for (int s = s0; s <= s1; ++s)
                peak = juce::jmax (peak, std::abs (irData.getSample (0, s)));
            const int barH = juce::jlimit (1, cy, (int)(peak * gainLin / irPeak * cy));
            g.setColour (s0 < startS ? kWaveDim : kWave);
            g.fillRect (px, cy - barH, 1, barH * 2 + 1);
        }

        // --- Layer 2: Envelope-applied IR overlay (amber, low alpha) ---
        for (int px = 0; px < w; ++px)
        {
            const int s0 = (px * srcLen) / w;
            if (s0 < startS) continue;
            const int s1 = juce::jmin (((px + 1) * srcLen) / w, srcLen - 1);
            float peak = 0.f;
            for (int s = s0; s <= s1; ++s)
                peak = juce::jmax (peak, std::abs (irData.getSample (0, s)));

            const float tSrc = (float)(s0 - startS) / (float)irSampleRate;
            float envNorm;
            if (tSrc < attackSec)
                envNorm = (attackSec > 0.f) ? tSrc / attackSec : 1.f;
            else if (tSrc < attackSec + sustainSec)
                envNorm = 1.f;
            else
                envNorm = std::pow (0.001f, (tSrc - attackSec - sustainSec) / decaySec);

            const int fullH = juce::jlimit (1, cy, (int)(peak * gainLin / irPeak * cy));
            const int envH  = (int)((float)fullH * envNorm);
            if (envH < 1) continue;
            g.setColour (juce::Colour (0xffffa040).withAlpha (0.38f));
            g.fillRect (px, cy - envH, 1, envH * 2 + 1);
        }

        // --- Layer 3: ASD envelope curves (semi-transparent) ---
        {
            juce::Path topPath, botPath;
            bool started = false;

            for (int px = 0; px < w; ++px)
            {
                const int s0 = (px * srcLen) / w;
                if (s0 < startS) continue;

                const float tSrc = (float)(s0 - startS) / (float)irSampleRate;
                float envNorm;
                if (tSrc < attackSec)
                    envNorm = (attackSec > 0.f) ? tSrc / attackSec : 1.f;
                else if (tSrc < attackSec + sustainSec)
                    envNorm = 1.f;
                else
                    envNorm = std::pow (0.001f, (tSrc - attackSec - sustainSec) / decaySec);

                const float ey = (float)cy * envNorm;
                if (!started)
                {
                    topPath.startNewSubPath ((float)px, (float)cy - ey);
                    botPath.startNewSubPath ((float)px, (float)cy + ey);
                    started = true;
                }
                else
                {
                    topPath.lineTo ((float)px, (float)cy - ey);
                    botPath.lineTo ((float)px, (float)cy + ey);
                }
            }
            if (started)
            {
                g.setColour (kEnv.withAlpha (0.65f));
                g.strokePath (topPath, juce::PathStrokeType (1.5f));
                g.strokePath (botPath, juce::PathStrokeType (1.5f));
            }
        }

        // --- Layer 4: Indicator lines — thin vertical lines only, no boxes ---
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

        // --- Value bar ---
        paintValueBar (g, w, waveH, kSubtext, kHStart, kHAttack, kHSustain, kHDecay);

        // --- Effective duration label (top-right of waveform) ---
        {
            const float ratio = std::powf (2.f, pitchSt / 12.f);
            const double effSec = (double)(srcLen - startS) / irSampleRate / (double)ratio;
            g.setColour (kText);
            g.setFont (juce::Font (juce::FontOptions (10.f)));
            g.drawText (juce::String (effSec, 2) + "s",
                        juce::Rectangle<int> (0, 0, w, waveH).reduced (4, 2),
                        juce::Justification::topRight);
        }

        g.setColour (kBorder);
        g.drawRect (bounds.toFloat(), 1.f);

        if (!isEnabled())
        {
            g.setColour (juce::Colour (0x99000000));
            g.fillAll();
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (irData.getNumSamples() == 0 || !isEnabled()) return;

        const int waveH = getHeight() - kBarH;

        if (e.y >= waveH)
        {
            // Value bar — up/down drag
            dragMode = DragMode::Vertical;
            const int col = juce::jlimit (0, 3, (e.x * 4) / getWidth());
            const ActiveHandle colMap[4] = {
                ActiveHandle::Start, ActiveHandle::Attack,
                ActiveHandle::Sustain, ActiveHandle::Decay
            };
            activeHandle = colMap[col];
        }
        else
        {
            // Waveform — left/right drag on a vertical line
            dragMode = DragMode::Horizontal;
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
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (activeHandle == ActiveHandle::None || !isEnabled()) return;

        float newVal;

        if (dragMode == DragMode::Horizontal)
        {
            // Line dragged left/right: delta maps linearly to ms
            const float srcLen  = (float)irData.getNumSamples();
            const float totalMs = srcLen / (float)irSampleRate * 1000.f;
            const float msPerPx = totalMs / (float)getWidth();
            const float delta   = (float)(e.x - dragStart.x) * msPerPx;
            newVal = dragStartVal + delta;
        }
        else
        {
            // Value label dragged up/down: up = increase
            const float delta = (float)(dragStart.y - e.y);
            const float scale = (activeHandle == ActiveHandle::Attack) ? (500.f / 250.f)
                                                                       : (2000.f / 250.f);
            newVal = dragStartVal + delta * scale;
        }

        switch (activeHandle)
        {
            case ActiveHandle::Start:
                newVal = juce::jlimit (0.f, 2000.f, newVal);
                startMs = newVal;
                if (onStartMsChanged)   onStartMsChanged   (newVal);
                break;
            case ActiveHandle::Attack:
                newVal = juce::jlimit (0.f, 500.f, newVal);
                attackMs = newVal;
                if (onAttackMsChanged)  onAttackMsChanged  (newVal);
                break;
            case ActiveHandle::Sustain:
                newVal = juce::jlimit (0.f, 2000.f, newVal);
                sustainMs = newVal;
                if (onSustainMsChanged) onSustainMsChanged (newVal);
                break;
            case ActiveHandle::Decay:
                newVal = juce::jlimit (10.f, 2000.f, newVal);
                decayMs = newVal;
                if (onDecayMsChanged)   onDecayMsChanged   (newVal);
                break;
            default: break;
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        endDragIfActive();
    }

    // Safety net: if the DAW intercepts mouseUp (e.g. during automation-record),
    // treat visibility loss as an implicit drag-end to prevent the suppress flag
    // from getting permanently stuck.
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
            const int x = i * colW;
            const bool hot = (activeHandle == items[i].handle && dragMode == DragMode::Vertical);

            // Label
            g.setFont (juce::Font (juce::FontOptions (8.5f)));
            g.setColour (hot ? items[i].col.withAlpha (0.85f) : subtext);
            g.drawText (items[i].name,
                        juce::Rectangle<int> (x, waveH, colW, halfH),
                        juce::Justification::centred);
            // Value
            g.setFont (juce::Font (juce::FontOptions (9.5f)));
            g.setColour (hot ? items[i].col : items[i].col.withAlpha (0.75f));
            g.drawText (fmtMs (items[i].ms),
                        juce::Rectangle<int> (x, waveH + halfH, colW, halfH),
                        juce::Justification::centred);
        }
    }

    // Hit-test within the waveform area: find the vertical line within kLineTol px.
    // Priority order: attack, sustain, decay, start (matches typical left-to-right order).
    ActiveHandle hitTestLine (int mx) const
    {
        const int w      = getWidth();
        const int srcLen = irData.getNumSamples();
        if (srcLen == 0) return ActiveHandle::None;

        const int startS     = juce::jlimit (0, srcLen - 1,
                                   (int)(startMs  / 1000.f * (float)irSampleRate));
        const int attackEnd  = juce::jmin (srcLen,
                                   startS + (int)(attackMs  / 1000.f * (float)irSampleRate));
        const int sustainEnd = juce::jmin (srcLen,
                                   attackEnd + (int)(sustainMs / 1000.f * (float)irSampleRate));
        const int decayEnd   = juce::jmin (srcLen,
                                   sustainEnd + (int)(juce::jmax (0.001f, decayMs / 1000.f)
                                                      * (float)irSampleRate));

        const int startX   = (startS    * w) / srcLen;
        const int attackX  = (attackEnd  * w) / srcLen;
        const int sustainX = (sustainEnd * w) / srcLen;
        const int decayX   = (decayEnd   * w) / srcLen;

        constexpr int kTol = 6;

        // When lines overlap, priority: attack → sustain → decay → start
        if (std::abs (mx - attackX)  <= kTol) return ActiveHandle::Attack;
        if (std::abs (mx - sustainX) <= kTol) return ActiveHandle::Sustain;
        if (std::abs (mx - decayX)   <= kTol) return ActiveHandle::Decay;
        if (std::abs (mx - startX)   <= kTol) return ActiveHandle::Start;
        return ActiveHandle::None;
    }

    juce::AudioBuffer<float> irData;
    double irSampleRate = 44100.0;
    float  irPeak    = 1.f;
    float  attackMs  = 0.f;
    float  sustainMs = 0.f;
    float  decayMs   = 2000.f;
    float  startMs   = 0.f;
    float  gainDb    = 0.f;
    float  pitchSt   = 0.f;

    ActiveHandle     activeHandle = ActiveHandle::None;
    DragMode         dragMode     = DragMode::Horizontal;
    juce::Point<int> dragStart;
    float            dragStartVal = 0.f;
};
