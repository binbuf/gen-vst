#include "PartManager.h"

PartManager::PartManager()
{
    // Default binding: part i answers MIDI channel i + 1.
    for (int i = 0; i < kNumParts; ++i)
        parts[static_cast<std::size_t> (i)].midiChannel = i + 1;
}

void PartManager::loadPatch (int part, const Patch& patch)
{
    parts[static_cast<std::size_t> (part)].patch = patch;
}

const Patch& PartManager::getPatch (int part) const
{
    return parts[static_cast<std::size_t> (part)].patch;
}

int PartManager::midiChannelForPart (int part) const
{
    return parts[static_cast<std::size_t> (part)].midiChannel;
}

int PartManager::partForMidiChannel (int channel) const
{
    for (int i = 0; i < kNumParts; ++i)
        if (parts[static_cast<std::size_t> (i)].midiChannel == channel)
            return i;

    return -1;
}
