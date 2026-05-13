#pragma once
#include "XYPadBase.h"

/**
 * XY pad for a resonant bandpass trigger filter.
 *
 *   X axis = centre frequency  (true log scale, left=low, right=high)
 *   Y axis = Q / resonance     (true log scale, top=narrow/high Q, bottom=wide/low Q)
 *
 * Displays the 2nd-order bandpass frequency response: |H(u)| = (u/Q) / √((1−u²)² + (u/Q)²)
 * Double-click resets both parameters to their defaults.
 *
 * xParam = centre frequency,  yParam = Q resonance
 */
class TriggerXYPad : public XYPadBase
{
public:
    TriggerXYPad (juce::RangedAudioParameter& freqP,
                  juce::RangedAudioParameter& qP,
                  juce::UndoManager* um = nullptr)
        : XYPadBase (freqP, qP, um) {}

protected:
    void drawContent (juce::Graphics& g, const juce::Rectangle<float>& pad) override
    {
        const juce::Colour gridCol { 0xff2a2a2a };

        const float fMin    = xParam.getNormalisableRange().start;
        const float fMax    = xParam.getNormalisableRange().end;
        const float logFMin = std::log (fMin);
        const float logFMax = std::log (fMax);

        const float qMin    = yParam.getNormalisableRange().start;
        const float qMax    = yParam.getNormalisableRange().end;
        const float logQMin = std::log (qMin);
        const float logQMax = std::log (qMax);

        auto freqToX = [&] (float hz) -> float
        {
            return pad.getX() + (std::log (juce::jlimit (fMin, fMax, hz)) - logFMin)
                                / (logFMax - logFMin) * pad.getWidth();
        };
        auto xToFreq = [&] (float px) -> float
        {
            float t = (px - pad.getX()) / pad.getWidth();
            return fMin * std::pow (fMax / fMin, juce::jlimit (0.f, 1.f, t));
        };
        auto qToY = [&] (float q) -> float
        {
            float norm = (std::log (juce::jlimit (qMin, qMax, q)) - logQMin) / (logQMax - logQMin);
            return pad.getBottom() - norm * pad.getHeight(); // inverted: high Q → near top
        };

        // Vertical grid at decade / half-decade frequencies
        {
            const float gridF[] = { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f };
            g.setColour (gridCol);
            for (float gf : gridF)
            {
                if (gf <= fMin || gf >= fMax) continue;
                g.drawVerticalLine ((int)freqToX (gf), pad.getY(), pad.getBottom());
            }
        }

        // Horizontal grid at key Q values
        {
            const float gridQ[] = { 0.5f, 1.0f, 2.0f, 5.0f };
            g.setColour (gridCol);
            for (float gq : gridQ)
            {
                if (gq < qMin || gq > qMax) continue;
                g.drawHorizontalLine ((int)qToY (gq), pad.getX(), pad.getRight());
            }
        }

        const float fc = xParam.convertFrom0to1 (xParam.getValue());
        const float q  = yParam.convertFrom0to1 (yParam.getValue());

        // Resonant bandpass: |H(u)| = u / sqrt((1-u²)² + (u/Q)²), u = f/fc.
        // Zero at DC, peaks at fc, falls back at high freq — bandpass shape.
        // Peak magnitude = Q, so the spike visually rises with resonance.
        auto bpMag = [&] (float f) -> float
        {
            float u = f / fc;
            float d = std::sqrt ((1.f - u * u) * (1.f - u * u) + (u / q) * (u / q));
            return (d > 1e-6f) ? u / d : 0.f;
        };

        // dB range: Q_max (+20 dB) maps to full height; floor (-12 dB) at bottom.
        // Frequencies far from fc fall below the floor and draw at the baseline.
        const float dbMax   = 20.0f * std::log10 (qMax);
        const float dbFloor = -12.0f;
        const float dbRange = dbMax - dbFloor;

        juce::Path curvePath;
        const int nPts = juce::jmax (2, (int)pad.getWidth());
        bool first = true;

        for (int xi = 0; xi < nPts; ++xi)
        {
            float px    = pad.getX() + (float)xi;
            float fHere = xToFreq (px);
            float mag   = bpMag (fHere);
            float magDb = (mag > 1e-6f) ? 20.0f * std::log10 (mag) : dbFloor;
            float norm  = juce::jlimit (0.0f, 1.0f, (magDb - dbFloor) / dbRange);
            float py    = pad.getBottom() - norm * pad.getHeight() * 0.82f;
            if (first) { curvePath.startNewSubPath (px, py); first = false; }
            else         curvePath.lineTo            (px, py);
        }

        juce::Path fill = curvePath;
        fill.lineTo (pad.getRight(), pad.getBottom());
        fill.lineTo (pad.getX(),     pad.getBottom());
        fill.closeSubPath();

        g.setColour (accentColour.withAlpha (0.14f));
        g.fillPath  (fill);
        g.setColour (accentColour.withAlpha (0.72f));
        g.strokePath (curvePath, juce::PathStrokeType (1.5f));
    }

    juce::Point<float> getDotPosition (const juce::Rectangle<float>& pad) const override
    {
        const float fMin    = xParam.getNormalisableRange().start;
        const float fMax    = xParam.getNormalisableRange().end;
        const float qMin    = yParam.getNormalisableRange().start;
        const float qMax    = yParam.getNormalisableRange().end;
        const float fc      = xParam.convertFrom0to1 (xParam.getValue());
        const float q       = yParam.convertFrom0to1 (yParam.getValue());

        float dotX = pad.getX() + (std::log (juce::jlimit (fMin, fMax, fc)) - std::log (fMin))
                                  / (std::log (fMax) - std::log (fMin)) * pad.getWidth();
        float norm = (std::log (juce::jlimit (qMin, qMax, q)) - std::log (qMin))
                     / (std::log (qMax) - std::log (qMin));
        float dotY = pad.getBottom() - norm * pad.getHeight();
        return { dotX, dotY };
    }

    void dragTo (juce::Point<float> pos) override
    {
        const float fMin    = xParam.getNormalisableRange().start;
        const float fMax    = xParam.getNormalisableRange().end;
        const float qMin    = yParam.getNormalisableRange().start;
        const float qMax    = yParam.getNormalisableRange().end;
        auto padBounds = getLocalBounds().toFloat().reduced (2.f);

        float nx = juce::jlimit (0.f, 1.f, (pos.x - padBounds.getX()) / padBounds.getWidth());
        float ny = juce::jlimit (0.f, 1.f, (pos.y - padBounds.getY()) / padBounds.getHeight());

        // True log freq from X
        xAttach.setValueAsPartOfGesture (fMin * std::pow (fMax / fMin, nx));

        // True log Q from Y, inverted (top = high Q)
        float qVal = std::exp (std::log (qMin) + (1.0f - ny) * (std::log (qMax) - std::log (qMin)));
        yAttach.setValueAsPartOfGesture (juce::jlimit (qMin, qMax, qVal));
    }

    void drawExtra (juce::Graphics& g, const juce::Rectangle<float>& pad) override
    {
        const float fMin    = xParam.getNormalisableRange().start;
        const float fMax    = xParam.getNormalisableRange().end;
        const float logFMin = std::log (fMin);
        const float logFMax = std::log (fMax);

        auto freqToX = [&] (float hz) -> float {
            return pad.getX() + (std::log (juce::jlimit (fMin, fMax, hz)) - logFMin)
                                / (logFMax - logFMin) * pad.getWidth();
        };

        struct { float f; const char* lbl; } ticks[] = {
            { 50.f, "50" }, { 100.f, "100" }, { 200.f, "200" },
            { 500.f, "500" }, { 1000.f, "1k" }, { 2000.f, "2k" }
        };

        g.setFont   (juce::Font (juce::FontOptions (8.5f)));
        g.setColour (juce::Colour (0xff4a4a4a));
        for (auto& t : ticks)
        {
            if (t.f <= fMin || t.f >= fMax) continue;
            float px = freqToX (t.f);
            g.drawText (t.lbl, (int)px - 12, (int)pad.getBottom() - 11, 24, 10,
                        juce::Justification::centred, false);
        }

        g.setColour (juce::Colour (0xff3a3a3a));
        g.drawText ("High Q", (int)pad.getX() + 2, (int)pad.getY() + 2,
                    45, 10, juce::Justification::centredLeft, false);
        g.drawText ("Low Q", (int)pad.getX() + 2, (int)pad.getBottom() - 23,
                    45, 10, juce::Justification::centredLeft, false);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggerXYPad)
};
