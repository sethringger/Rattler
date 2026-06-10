#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class RattlerAudioProcessor;

class IRManager
{
public:
    struct Entry
    {
        enum class Source { Factory, User } source;
        juce::String name;       // stem, no extension
        juce::File   file;       // User entries only
        juce::String binaryKey;  // Factory entries only (BinaryData resource key)
    };

    explicit IRManager (RattlerAudioProcessor& p);

    juce::File getUserIRFolder() const;

    const juce::Array<Entry>& getEntries()   const { return entries; }
    int                       getNumEntries() const { return entries.size(); }

    // Load an entry into the given layer
    void loadEntry (int entryIndex, int layerIdx);

    // Copy a dropped file into the user IR folder, load it, return its new index (-1 on failure)
    int addUserIR (const juce::File& srcFile, int layerIdx);

    // Find which entry index is currently loaded in a layer (-1 if none / unmanaged)
    int findCurrentIndex (int layerIdx) const;

    // Convert an absolute path to a portable "user:..." or "factory:..." string
    juce::String toPortable (const juce::String& absolutePath) const;

    // Load from a portable path string (handles user:, factory:, and raw absolute paths)
    void loadFromPortable (const juce::String& portable, int layerIdx);

    void scan();

private:
    RattlerAudioProcessor& processor;
    juce::Array<Entry>     entries;
};
