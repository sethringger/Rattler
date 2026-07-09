#include "SampleManager.h"
#include "PluginProcessor.h"

SampleManager::SampleManager (RattlerAudioProcessor& p) : processor (p)
{
    getUserSamplesFolder().createDirectory();
    scan();
}

juce::File SampleManager::getUserSamplesFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Rattler")
               .getChildFile ("IRs");
}

juce::StringArray SampleManager::getSampleNames() const
{
    juce::StringArray names;
    for (auto& f : files)
        names.add (f.getFileNameWithoutExtension());
    return names;
}

void SampleManager::scan()
{
    files.clear();
    getUserSamplesFolder().findChildFiles (files, juce::File::findFiles, false,
                                           "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    files.sort();
}

int SampleManager::findCurrentIndex (int layerIdx) const
{
    const juce::String curPath = processor.getSampleFilePath (layerIdx);
    if (curPath.isEmpty()) return -1;

    const juce::File curFile (curPath);
    const juce::File samplesFolder = getUserSamplesFolder();

    if (curFile.getParentDirectory() != samplesFolder) return -1;

    for (int i = 0; i < files.size(); ++i)
        if (files[i].getFullPathName() == curFile.getFullPathName())
            return i;
    return -1;
}

void SampleManager::loadEntry (int entryIdx, int layerIdx)
{
    if (entryIdx < 0 || entryIdx >= files.size()) return;
    processor.loadSampleFile (layerIdx, files[entryIdx]);
}

int SampleManager::addUserSample (const juce::File& srcFile, int layerIdx)
{
    const juce::File dest = getUserSamplesFolder().getChildFile (srcFile.getFileName());
    if (dest != srcFile && !dest.existsAsFile())
        srcFile.copyFileTo (dest);

    const juce::File& toLoad = dest.existsAsFile() ? dest : srcFile;
    processor.loadSampleFile (layerIdx, toLoad);
    scan();

    for (int i = 0; i < files.size(); ++i)
        if (files[i].getFullPathName() == toLoad.getFullPathName())
            return i;
    return -1;
}
