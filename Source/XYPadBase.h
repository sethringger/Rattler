#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

/**
 * Base class for all 2-D parameter XY pads.
 *
 * Handles:  parameter attachments, repaint-on-change, mouse gestures,
 *           double-click reset, and the shared paint skeleton
 *           (background, border, crosshair, dot).
 *
 * Subclasses implement:
 *   drawContent()    — grid lines and any frequency-response curve (called before the dot)
 *   getDotPosition() — current dot coordinates in component-local pixels
 *   dragTo()         — convert a mouse position to parameter values
 *   drawExtra()      — optional extra paint after the dot (default: no-op)
 */
class XYPadBase : public juce::Component
{
public:
    XYPadBase (juce::RangedAudioParameter& xP,
               juce::RangedAudioParameter& yP,
               juce::UndoManager* um = nullptr)
        : xParam (xP), yParam (yP),
          xAttach (xP, [this] (float) { repaint(); }, um),
          yAttach (yP, [this] (float) { repaint(); }, um)
    {
        xAttach.sendInitialUpdate();
        yAttach.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
    }

    ~XYPadBase() override = default;

    // Sealed — subclasses customise via drawContent / getDotPosition / drawExtra.
    void paint (juce::Graphics& g) override final
    {
        const juce::Colour bgCol   { 0xff111111 };
        const juce::Colour gridCol { 0xff2a2a2a };

        auto b   = getLocalBounds().toFloat();
        auto pad = b.reduced (2.f);

        g.setColour (bgCol);
        g.fillRoundedRectangle (b, 4.f);
        g.setColour (gridCol);
        g.drawRoundedRectangle (b.reduced (0.5f), 4.f, 1.f);

        drawContent (g, pad);

        auto dot = getDotPosition (pad);

        g.setColour (accentColour.withAlpha (0.28f));
        g.drawVerticalLine   ((int)dot.x, pad.getY(),    pad.getBottom());
        g.drawHorizontalLine ((int)dot.y, pad.getX(),    pad.getRight());

        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillEllipse (dot.x - 5.f, dot.y - 5.f, 10.f, 10.f);
        g.setColour   (accentColour);
        g.fillEllipse (dot.x - 3.5f, dot.y - 3.5f, 7.f, 7.f);

        drawExtra (g, pad);

        if (! isEnabled())
        {
            g.setColour (juce::Colour (0xaa111111));
            g.fillRoundedRectangle (b, 4.f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        xAttach.beginGesture();
        yAttach.beginGesture();
        dragTo (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override { dragTo (e.position); }

    void mouseUp (const juce::MouseEvent&) override
    {
        xAttach.endGesture();
        yAttach.endGesture();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        xAttach.beginGesture();
        yAttach.beginGesture();
        xAttach.setValueAsPartOfGesture (xParam.convertFrom0to1 (xParam.getDefaultValue()));
        yAttach.setValueAsPartOfGesture (yParam.convertFrom0to1 (yParam.getDefaultValue()));
        xAttach.endGesture();
        yAttach.endGesture();
    }

protected:
    juce::RangedAudioParameter& xParam;
    juce::RangedAudioParameter& yParam;
    juce::ParameterAttachment   xAttach;
    juce::ParameterAttachment   yAttach;
    juce::Colour accentColour { 0xff4a9eff };

    // Grid lines + frequency-response curve — called before the crosshair and dot.
    virtual void drawContent (juce::Graphics&, const juce::Rectangle<float>& pad) = 0;

    // Returns the pixel coordinates of the dot for the current parameter values.
    virtual juce::Point<float> getDotPosition (const juce::Rectangle<float>& pad) const = 0;

    // Translates a mouse position into parameter values and pushes them via the attachments.
    virtual void dragTo (juce::Point<float> pos) = 0;

    // Optional extra paint drawn after the dot (e.g. axis labels).  Default: no-op.
    virtual void drawExtra (juce::Graphics&, const juce::Rectangle<float>&) {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XYPadBase)
};
