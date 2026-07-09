#include "PresetManager.h"
#include "IRManager.h"
#include "PluginProcessor.h"
#include <BinaryData.h>


PresetManager::PresetManager (RattlerAudioProcessor& p) : processor (p)
{
    getPresetsFolder().createDirectory();
    extractFactoryPresetsIfNeeded();
    scan();

    currentName = processor.apvts.state
        .getProperty ("presetName", "Untitled").toString();
    if (currentName.isEmpty()) currentName = "Untitled";

    const auto names = getPresetNames();
    for (int i = 0; i < names.size(); ++i)
        if (names[i] == currentName) { currentIndex = i; break; }
}

juce::File PresetManager::getPresetsFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Rattler")
               .getChildFile ("Presets");
}

juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray names;
    for (auto& f : files)
        names.add (f.getFileNameWithoutExtension());
    return names;
}

void PresetManager::loadPreset (int index)
{
    if (index < 0 || index >= files.size()) return;

    auto xml = juce::XmlDocument::parse (files[index]);
    if (!xml || !xml->hasTagName (processor.apvts.state.getType())) return;

    processor.apvts.replaceState (juce::ValueTree::fromXml (*xml));

    const juce::String irA  = xml->getStringAttribute ("irPathA");
    const juce::String irB  = xml->getStringAttribute ("irPathB");
    const juce::String samA = xml->getStringAttribute ("samplePathA");
    const juce::String samB = xml->getStringAttribute ("samplePathB");

    IRManager* irm = irManager;
    juce::MessageManager::callAsync ([this, irm, irA, irB, samA, samB]
    {
        if (irA.isNotEmpty())
        {
            if (irm) irm->loadFromPortable (irA, 0);
            else     { juce::File f (irA); if (f.existsAsFile()) processor.loadConvIR (0, f); }
        }
        if (irB.isNotEmpty())
        {
            if (irm) irm->loadFromPortable (irB, 1);
            else     { juce::File f (irB); if (f.existsAsFile()) processor.loadConvIR (1, f); }
        }
        if (samA.isNotEmpty()) { juce::File f (samA); if (f.existsAsFile()) processor.loadSampleFile (0, f); }
        if (samB.isNotEmpty()) { juce::File f (samB); if (f.existsAsFile()) processor.loadSampleFile (1, f); }
    });

    currentIndex = index;
    currentName  = files[index].getFileNameWithoutExtension();
    processor.apvts.state.setProperty ("presetName", currentName, nullptr);

    if (onChange) onChange();
}

void PresetManager::nextPreset()
{
    if (files.isEmpty()) return;
    loadPreset ((currentIndex + 1) % files.size());
}

void PresetManager::prevPreset()
{
    if (files.isEmpty()) return;
    loadPreset ((currentIndex - 1 + files.size()) % files.size());
}

void PresetManager::saveCurrentAs (const juce::String& name)
{
    const juce::String safe = name.trim().replaceCharacters ("\\/:*?\"<>|", "_________");
    if (safe.isEmpty()) return;

    // Use portable IR paths if IRManager is wired up
    auto irPath = [&] (int layerIdx) -> juce::String
    {
        const juce::String abs = processor.getConvIRFilePath (layerIdx);
        return irManager ? irManager->toPortable (abs) : abs;
    };

    auto state = processor.apvts.copyState();
    auto xml   = state.createXml();
    xml->setAttribute ("irPathA",     irPath (0));
    xml->setAttribute ("irPathB",     irPath (1));
    xml->setAttribute ("samplePathA", processor.getSampleFilePath (0));
    xml->setAttribute ("samplePathB", processor.getSampleFilePath (1));

    const auto file = getPresetsFolder().getChildFile (safe + ".rattlerpreset");
    xml->writeTo (file);

    currentName = safe;
    processor.apvts.state.setProperty ("presetName", currentName, nullptr);

    scan();

    currentIndex = -1;
    const auto names = getPresetNames();
    for (int i = 0; i < names.size(); ++i)
        if (names[i] == safe) { currentIndex = i; break; }

    if (onChange) onChange();
}

void PresetManager::deletePreset (int index)
{
    if (index < 0 || index >= files.size()) return;
    files[index].deleteFile();

    const bool wasCurrent = (index == currentIndex);
    scan();

    if (wasCurrent)
    {
        currentIndex = -1;
        currentName  = "Untitled";
        processor.apvts.state.setProperty ("presetName", currentName, nullptr);
    }
    else if (currentIndex > index)
    {
        --currentIndex;
    }

    if (onChange) onChange();
}

void PresetManager::scan()
{
    files.clear();
    getPresetsFolder().findChildFiles (files, juce::File::findFiles, false, "*.rattlerpreset");
    files.sort();

    if (currentIndex >= files.size())
        currentIndex = files.isEmpty() ? -1 : files.size() - 1;
}

void PresetManager::extractFactoryPresetsIfNeeded()
{
    const juce::File folder = getPresetsFolder();
    const int n = RattlerBinaryData::namedResourceListSize;

    for (int i = 0; i < n; ++i)
    {
        const juce::String original (RattlerBinaryData::originalFilenames[i]);
        if (!original.endsWithIgnoreCase (".rattlerpreset")) continue;

        const juce::File dest = folder.getChildFile (original);
        if (dest.existsAsFile()) continue;

        int dataSize = 0;
        const void* data = RattlerBinaryData::getNamedResource (
            RattlerBinaryData::namedResourceList[i], dataSize);
        if (!data || dataSize <= 0) continue;

        dest.replaceWithData (data, (size_t)dataSize);
    }
}
