#pragma once
#include "XYPadBase.h"

/**
 * 2-D HP/LP filter control.
 *
 *   X axis = centre frequency  (true log scale, left=low, right=high)
 *   Y axis = total bandwidth in semitones  (top=wide, bottom=narrow)
 *
 * Displays the combined LP×HP Butterworth (Q=1/√2) frequency response.
 * Double-click resets both parameters to their defaults.
 *
 * xParam = frequency,  yParam = bandwidth in semitones
 */
class FilterXYPad : public XYPadBase
{
public:
    FilterXYPad (juce::RangedAudioParameter& freqP,
                 juce::RangedAudioParameter& bwP,
                 juce::UndoManager* um = nullptr)
        : XYPadBase (freqP, bwP, um) {}

protected:
    void drawContent (juce::Graphics& g, const juce::Rectangle<float>& pad) override
    {
        const juce::Colour gridCol { 0xff2a2a2a };

        const float fMin   = xParam.getNormalisableRange().start;
        const float fMax   = xParam.getNormalisableRange().end;
        const float logMin = std::log (fMin);
        const float logMax = std::log (fMax);

        auto freqToX = [&] (float hz) -> float
        {
            return pad.getX() + (std::log (juce::jlimit (fMin, fMax, hz)) - logMin)
                                / (logMax - logMin) * pad.getWidth();
        };
        auto xToFreq = [&] (float px) -> float
        {
            float t = (px - pad.getX()) / pad.getWidth();
            return fMin * std::pow (fMax / fMin, juce::jlimit (0.f, 1.f, t));
        };

        // Vertical grid at decade / half-decade frequencies
        {
            const float gridF[] = { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f };
            g.setColour (gridCol);
            for (float gf : gridF)
            {
                if (gf <= fMin || gf >= fMax) continue;
                g.drawVerticalLine ((int)freqToX (gf), pad.getY(), pad.getBottom());
            }
        }

        const float fc   = xParam.convertFrom0to1 (xParam.getValue());
        const float bwSt = yParam.convertFrom0to1 (yParam.getValue());
        const float half = bwSt / 2.0f;
        const float lp_fc = fc * std::pow (2.0f,  half / 12.0f);
        const float hp_fc = fc * std::pow (2.0f, -half / 12.0f);

        const float Q = 0.7071f;
        auto lpMag = [&] (float f) -> float
        {
            float u = f / lp_fc;
            float d = std::sqrt ((1.f - u * u) * (1.f - u * u) + (u / Q) * (u / Q));
            return (d > 1e-6f) ? 1.0f / d : 0.f;
        };
        auto hpMag = [&] (float f) -> float
        {
            float u = f / hp_fc;
            float d = std::sqrt ((1.f - u * u) * (1.f - u * u) + (u / Q) * (u / Q));
            return (d > 1e-6f) ? (u * u) / d : 0.f;
        };

        juce::Path curvePath;
        const int nPts = juce::jmax (2, (int)pad.getWidth());
        bool first = true;

        for (int xi = 0; xi < nPts; ++xi)
        {
            float px    = pad.getX() + (float)xi;
            float fHere = xToFreq (px);
            float mag   = juce::jlimit (0.f, 1.f, lpMag (fHere) * hpMag (fHere));
            float py    = pad.getBottom() - mag * pad.getHeight() * 0.82f;
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
        const float fMin = xParam.getNormalisableRange().start;
        const float fMax = xParam.getNormalisableRange().end;
        const float fc   = xParam.convertFrom0to1 (xParam.getValue());
        const float bwNorm = yParam.getValue();

        float dotX = pad.getX() + (std::log (juce::jlimit (fMin, fMax, fc)) - std::log (fMin))
                                  / (std::log (fMax) - std::log (fMin)) * pad.getWidth();
        float dotY = pad.getY() + (1.0f - bwNorm) * pad.getHeight();
        return { dotX, dotY };
    }

    void dragTo (juce::Point<float> pos) override
    {
        const float fMin = xParam.getNormalisableRange().start;
        const float fMax = xParam.getNormalisableRange().end;
        auto padBounds = getLocalBounds().toFloat().reduced (2.f);

        float nx = juce::jlimit (0.f, 1.f, (pos.x - padBounds.getX()) / padBounds.getWidth());
        float ny = juce::jlimit (0.f, 1.f, (pos.y - padBounds.getY()) / padBounds.getHeight());

        xAttach.setValueAsPartOfGesture (fMin * std::pow (fMax / fMin, nx));
        yAttach.setValueAsPartOfGesture (yParam.convertFrom0to1 (1.0f - ny));
    }

    void drawExtra (juce::Graphics& g, const juce::Rectangle<float>& pad) override
    {
        const float fMin   = xParam.getNormalisableRange().start;
        const float fMax   = xParam.getNormalisableRange().end;
        const float logMin = std::log (fMin);
        const float logMax = std::log (fMax);

        auto freqToX = [&] (float hz) -> float {
            return pad.getX() + (std::log (juce::jlimit (fMin, fMax, hz)) - logMin)
                                / (logMax - logMin) * pad.getWidth();
        };

        struct { float f; const char* lbl; } ticks[] = {
            { 50.f,    "50"   }, { 100.f,  "100"  }, { 200.f,  "200"  },
            { 500.f,   "500"  }, { 1000.f, "1k"   }, { 2000.f, "2k"   },
            { 5000.f,  "5k"   }, { 10000.f,"10k"  }
        };

        g.setFont  (juce::Font (juce::FontOptions (8.5f)));
        g.setColour (juce::Colour (0xff4a4a4a));
        for (auto& t : ticks)
        {
            if (t.f <= fMin || t.f >= fMax) continue;
            float px = freqToX (t.f);
            g.drawText (t.lbl, (int)px - 12, (int)pad.getBottom() - 11, 24, 10,
                        juce::Justification::centred, false);
        }

        // Y-axis direction hints at corners (don't overlap tick labels at bottom)
        g.setColour (juce::Colour (0xff3a3a3a));
        g.drawText ("Wide", (int)pad.getX() + 2, (int)pad.getY() + 2,
                    35, 10, juce::Justification::centredLeft, false);
        g.drawText ("Narrow", (int)pad.getX() + 2, (int)pad.getBottom() - 23,
                    45, 10, juce::Justification::centredLeft, false);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterXYPad)
};
