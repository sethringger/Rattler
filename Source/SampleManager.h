#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class RattlerAudioProcessor;

class SampleManager
{
public:
    explicit SampleManager (RattlerAudioProcessor& p);

    juce::File                      getUserSamplesFolder() const;
    const juce::Array<juce::File>&  getFiles()             const { return files; }
    int                             getNumEntries()         const { return files.size(); }
    juce::StringArray               getSampleNames()        const;

    void loadEntry       (int entryIdx, int layerIdx);
    int  addUserSample   (const juce::File& srcFile, int layerIdx);
    int  findCurrentIndex (int layerIdx) const;
    void scan();

private:
    RattlerAudioProcessor&  processor;
    juce::Array<juce::File> files;
};
