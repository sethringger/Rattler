#pragma once
#include <juce_core/juce_core.h>

// Append-only debug log — writes to rattler_debug.log in the repo root.
// Set RATTLER_DEBUG_LOG=0 to disable (compile-time).
#ifndef RATTLER_DEBUG_LOG
#define RATTLER_DEBUG_LOG 0
#endif

#if RATTLER_DEBUG_LOG

struct DebugLog
{
    static juce::File& getFile()
    {
        static juce::File f (juce::File ("C:/Users/Seth Ringger/source/repos/Rattler/rattler_debug.log"));
        return f;
    }

    static void clear()
    {
        getFile().deleteFile();
    }

    static void write (const juce::String& msg)
    {
        static juce::CriticalSection cs;
        const juce::ScopedLock sl (cs);

        const auto ts = juce::Time::getCurrentTime()
                            .toString (false, true, true, true);
        getFile().appendText ("[" + ts + "] " + msg + "\n",
                              false, false, nullptr);
    }
};

#define RLOG(msg) DebugLog::write (juce::String (msg))
#define RLOG_CLEAR() DebugLog::clear()

#else // logging disabled

#define RLOG(msg)    do {} while (false)
#define RLOG_CLEAR() do {} while (false)

#endif
