#pragma once
#include "XYPadBase.h"

/**
 * XY pad for per-source resonator Tune × Decay.
 *
 *   X axis = tune offset in semitones  (linear, full param range; left=lower, right=higher)
 *   Y axis = decay time                (uses param's own skew; top=long, bottom=short)
 *
 * Double-click resets both parameters to their defaults.
 *
 * xParam = tune offset (semitones),  yParam = decay (ms)
 */
class TuneDecayPad : public XYPadBase
{
public:
    TuneDecayPad (juce::RangedAudioParameter& tuneP,
                  juce::RangedAudioParameter& decayP,
                  juce::UndoManager* um = nullptr)
        : XYPadBase (tuneP, decayP, um)
    {
        accentColour = juce::Colour (0xffff9f4a); // orange accent for resonators
    }

    void setShowDanger (bool show)
    {
        if (showDanger != show) { showDanger = show; repaint(); }
    }

protected:
    void drawContent (juce::Graphics& g, const juce::Rectangle<float>& pad) override
    {
        // Danger zone overlay — top = long decay, dangerous without feedback saturation
        if (showDanger)
        {
            const float fadeH = pad.getHeight() * 0.55f;
            juce::ColourGradient grad (juce::Colour (0x66ff2222), pad.getX(), pad.getY(),
                                       juce::Colour (0x00ff2222), pad.getX(), pad.getY() + fadeH,
                                       false);
            g.setGradientFill (grad);
            g.fillRect (pad);

            g.setFont (juce::Font (juce::FontOptions (8.5f).withStyle ("Bold")));
            g.setColour (juce::Colour (0xaaff4444));
            g.drawText ("NO SAT - DANGER", (int)pad.getX(), (int)pad.getY() + 3,
                        (int)pad.getWidth(), 12, juce::Justification::centred, false);
        }

        const juce::Colour gridCol { 0xff2a2a2a };

        // Vertical grid — octave semitone markers
        const float gridSt[] = { -36.f, -24.f, -12.f, 0.f, 12.f, 24.f };
        for (float st : gridSt)
        {
            float nx = xParam.convertTo0to1 (st);
            if (nx < 0.f || nx > 1.f) continue;
            float px = pad.getX() + nx * pad.getWidth();
            g.setColour (st == 0.f ? juce::Colour (0xff3a3a3a) : gridCol);
            g.drawVerticalLine ((int)px, pad.getY(), pad.getBottom());
        }

        // Horizontal grid — decade decay markers
        const float gridMs[] = { 10.f, 30.f, 100.f, 300.f, 1000.f };
        g.setColour (gridCol);
        for (float ms : gridMs)
        {
            float norm = yParam.convertTo0to1 (ms);
            if (norm < 0.f || norm > 1.f) continue;
            float py = pad.getBottom() - norm * pad.getHeight();
            g.drawHorizontalLine ((int)py, pad.getX(), pad.getRight());
        }
    }

    juce::Point<float> getDotPosition (const juce::Rectangle<float>& pad) const override
    {
        float dotX = pad.getX()      + xParam.getValue() * pad.getWidth();
        float dotY = pad.getBottom() - yParam.getValue() * pad.getHeight(); // Y inverted
        return { dotX, dotY };
    }

    void dragTo (juce::Point<float> pos) override
    {
        auto padBounds = getLocalBounds().toFloat().reduced (2.f);
        float nx = juce::jlimit (0.f, 1.f, (pos.x - padBounds.getX()) / padBounds.getWidth());
        float ny = juce::jlimit (0.f, 1.f, (pos.y - padBounds.getY()) / padBounds.getHeight());
        xAttach.setValueAsPartOfGesture (xParam.convertFrom0to1 (nx));
        yAttach.setValueAsPartOfGesture (yParam.convertFrom0to1 (1.0f - ny)); // Y inverted
    }

    void drawExtra (juce::Graphics& g, const juce::Rectangle<float>& pad) override
    {
        g.setFont   (juce::Font (juce::FontOptions (8.5f)));
        g.setColour (juce::Colour (0xff4a4a4a));

        // Semitone labels at vertical grid lines (bottom edge)
        const float gridSt[]      = { -36.f, -24.f, -12.f, 0.f, 12.f, 24.f };
        const char* gridStLbl[]   = { "-36",  "-24",  "-12", "0", "+12", "+24" };
        for (int i = 0; i < 6; ++i)
        {
            float nx = xParam.convertTo0to1 (gridSt[i]);
            if (nx < 0.f || nx > 1.f) continue;
            float px = pad.getX() + nx * pad.getWidth();
            g.drawText (gridStLbl[i], (int)px - 12, (int)pad.getBottom() - 11, 24, 10,
                        juce::Justification::centred, false);
        }

        // Decay labels at horizontal grid lines (left edge)
        struct { float ms; const char* lbl; } decTicks[] = {
            { 10.f, "10" }, { 30.f, "30" }, { 100.f, "100" }, { 300.f, "300" }, { 1000.f, "1s" }
        };
        for (auto& t : decTicks)
        {
            float norm = yParam.convertTo0to1 (t.ms);
            if (norm < 0.f || norm > 1.f) continue;
            float py = pad.getBottom() - norm * pad.getHeight();
            g.drawText (t.lbl, (int)pad.getX() + 2, (int)py - 5, 22, 10,
                        juce::Justification::centredLeft, false);
        }

        // Y-axis direction hint (decay labels at left already imply the axis)
        g.setColour (juce::Colour (0xff3a3a3a));
        g.drawText ("Long", (int)pad.getRight() - 28, (int)pad.getY() + 2,
                    26, 10, juce::Justification::centredRight, false);
    }

private:
    bool showDanger = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuneDecayPad)
};
