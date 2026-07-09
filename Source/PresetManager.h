#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class RattlerAudioProcessor;
class IRManager;

class PresetManager
{
public:
    explicit PresetManager (RattlerAudioProcessor& p);

    juce::File        getPresetsFolder() const;
    juce::StringArray getPresetNames()   const;
    int               getCurrentIndex()  const { return currentIndex; }
    juce::String      getCurrentName()   const { return currentName; }

    void loadPreset    (int index);
    void nextPreset();
    void prevPreset();
    void saveCurrentAs (const juce::String& name);
    void deletePreset  (int index);

    void setCurrentName (const juce::String& name) { currentName = name; }
    void setIRManager   (IRManager* m)              { irManager = m; }

    std::function<void()> onChange;

private:
    RattlerAudioProcessor&  processor;
    IRManager*              irManager = nullptr;
    juce::Array<juce::File> files;
    int                     currentIndex = -1;
    juce::String            currentName  = "Untitled";

    void scan();
    void extractFactoryPresetsIfNeeded();
};
