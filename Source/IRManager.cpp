#include "IRManager.h"
#include "PluginProcessor.h"

// When factory IRs are added via juce_add_binary_data, include BinaryData.h here:
// #include <BinaryData.h>

IRManager::IRManager (RattlerAudioProcessor& p) : processor (p)
{
    getUserIRFolder().createDirectory();
    scan();
}

juce::File IRManager::getUserIRFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Rattler")
               .getChildFile ("IRs");
}

void IRManager::scan()
{
    entries.clear();

    // ── Factory IRs (from BinaryData) ────────────────────────────────────────
    // Populated once factory WAV files are added to CMakeLists juce_add_binary_data.
    // Example entry format when ready:
    //   Entry e;
    //   e.source    = Entry::Source::Factory;
    //   e.name      = "Cathedral Plate";
    //   e.binaryKey = "CathedralPlate_wav";
    //   entries.add (e);

    // ── User IRs (from disk) ─────────────────────────────────────────────────
    juce::Array<juce::File> files;
    getUserIRFolder().findChildFiles (files, juce::File::findFiles, false,
                                     "*.wav;*.aif;*.aiff;*.flac");
    files.sort();

    for (auto& f : files)
    {
        Entry e;
        e.source = Entry::Source::User;
        e.name   = f.getFileNameWithoutExtension();
        e.file   = f;
        entries.add (e);
    }
}

void IRManager::loadEntry (int idx, int layerIdx)
{
    if (idx < 0 || idx >= entries.size()) return;
    const auto& e = entries[idx];

    if (e.source == Entry::Source::User)
    {
        if (e.file.existsAsFile())
            processor.loadConvIR (layerIdx, e.file);
    }
    else
    {
        // Factory: load from BinaryData memory block.
        // This will be wired up once BinaryData is added.
        // processor.loadConvIRFromMemory (layerIdx, BinaryData::<key>, BinaryData::<key>Size);
    }
}

int IRManager::addUserIR (const juce::File& srcFile, int layerIdx)
{
    const auto dest = getUserIRFolder().getChildFile (srcFile.getFileName());

    // Copy to user IR folder (skip if it's already there)
    if (dest != srcFile && !dest.existsAsFile())
        srcFile.copyFileTo (dest);

    processor.loadConvIR (layerIdx, dest.existsAsFile() ? dest : srcFile);

    scan();

    // Return index of the newly added entry
    const juce::String stem = srcFile.getFileNameWithoutExtension();
    for (int i = 0; i < entries.size(); ++i)
        if (entries[i].name == stem) return i;
    return -1;
}

int IRManager::findCurrentIndex (int layerIdx) const
{
    const juce::String cur = processor.getConvIRFilePath (layerIdx);
    if (cur.isEmpty()) return -1;

    const juce::File curFile (cur);
    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        if (e.source == Entry::Source::User && e.file == curFile)
            return i;
        if (e.source == Entry::Source::Factory && e.name == curFile.getFileNameWithoutExtension())
            return i;
    }
    return -1;
}

juce::String IRManager::toPortable (const juce::String& absolutePath) const
{
    if (absolutePath.isEmpty()) return {};
    const juce::File f (absolutePath);

    // Already a portable string (shouldn't happen but be safe)
    if (absolutePath.startsWith ("user:") || absolutePath.startsWith ("factory:"))
        return absolutePath;

    if (f.isAChildOf (getUserIRFolder()))
        return "user:" + f.getFileName();

    // Check factory entries by name
    for (const auto& e : entries)
        if (e.source == Entry::Source::Factory && e.name == f.getFileNameWithoutExtension())
            return "factory:" + e.binaryKey;

    return absolutePath;  // absolute path fallback for unmanaged files
}

void IRManager::loadFromPortable (const juce::String& portable, int layerIdx)
{
    if (portable.isEmpty()) return;

    if (portable.startsWith ("user:"))
    {
        const juce::File f = getUserIRFolder().getChildFile (portable.substring (5));
        if (f.existsAsFile()) processor.loadConvIR (layerIdx, f);
        return;
    }

    if (portable.startsWith ("factory:"))
    {
        const juce::String key = portable.substring (8);
        // Find the factory entry and load from BinaryData
        for (int i = 0; i < entries.size(); ++i)
            if (entries[i].source == Entry::Source::Factory && entries[i].binaryKey == key)
                { loadEntry (i, layerIdx); return; }
        return;
    }

    // Fallback: treat as absolute path (legacy presets)
    const juce::File f (portable);
    if (f.existsAsFile()) processor.loadConvIR (layerIdx, f);
}
