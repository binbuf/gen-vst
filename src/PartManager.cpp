#include "PartManager.h"

#include <algorithm>

PartManager::PartManager()
{
    // Default binding: part i answers MIDI channel i + 1.
    for (int i = 0; i < kNumParts; ++i)
        parts[static_cast<std::size_t> (i)].midiChannel = i + 1;

    // Task 22 — default rack state: a single FM row on slot 0 (MIDI channel
    // 1). All other slots start inactive. The rack widget renders one row on
    // first launch.
    for (auto& a : fmActive) a.store (false, std::memory_order_relaxed);
    for (auto& a : sqActive) a.store (false, std::memory_order_relaxed);
    for (auto& a : dActive)  a.store (false, std::memory_order_relaxed);
    fmActive[0].store (true, std::memory_order_relaxed);

    // Task 27 — seed the rack order with the default FM-0 slot so the widget
    // gets a row to render on first launch.
    rackOrder.push_back ({ InstrumentType::FM, 0 });
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

// --- Rack slot active state (Task 22) ----------------------------------------

bool PartManager::isSlotActive (SlotId slot) const noexcept
{
    switch (slot.type)
    {
        case InstrumentType::FM:
            if (slot.index < 0 || slot.index >= kNumRackFmSlots) return false;
            return fmActive[static_cast<std::size_t> (slot.index)].load (std::memory_order_relaxed);
        case InstrumentType::SQ:
            if (slot.index < 0 || slot.index >= kNumRackSqSlots) return false;
            return sqActive[static_cast<std::size_t> (slot.index)].load (std::memory_order_relaxed);
        case InstrumentType::D:
            if (slot.index < 0 || slot.index >= kNumRackDSlots) return false;
            return dActive[static_cast<std::size_t> (slot.index)].load (std::memory_order_relaxed);
    }
    return false;
}

void PartManager::setSlotActive (SlotId slot, bool active)
{
    std::atomic<bool>* target = nullptr;
    switch (slot.type)
    {
        case InstrumentType::FM:
            if (slot.index < 0 || slot.index >= kNumRackFmSlots) return;
            target = &fmActive[static_cast<std::size_t> (slot.index)];
            break;
        case InstrumentType::SQ:
            if (slot.index < 0 || slot.index >= kNumRackSqSlots) return;
            target = &sqActive[static_cast<std::size_t> (slot.index)];
            break;
        case InstrumentType::D:
            if (slot.index < 0 || slot.index >= kNumRackDSlots) return;
            target = &dActive[static_cast<std::size_t> (slot.index)];
            break;
    }
    if (target == nullptr) return;
    const bool was = target->load (std::memory_order_relaxed);
    if (was == active) return;
    target->store (active, std::memory_order_relaxed);

    // Mirror the change into the rack ordering vector. A newly-activated slot
    // appears at the end (the rack widget's default for "+ ADD INSTRUMENT" is
    // bottom of the list); a removed slot drops out of the order.
    if (active)
    {
        const auto it = std::find (rackOrder.begin(), rackOrder.end(), slot);
        if (it == rackOrder.end())
            rackOrder.push_back (slot);
    }
    else
    {
        const auto it = std::find (rackOrder.begin(), rackOrder.end(), slot);
        if (it != rackOrder.end())
            rackOrder.erase (it);
    }

    sendChangeMessage();
}

std::optional<PartManager::SlotId> PartManager::getFreeSlot (InstrumentType type) const noexcept
{
    const int n = slotPoolSize (type);
    for (int i = 0; i < n; ++i)
    {
        const SlotId s { type, i };
        if (! isSlotActive (s)) return s;
    }
    return std::nullopt;
}

int PartManager::slotPoolSize (InstrumentType type) noexcept
{
    switch (type)
    {
        case InstrumentType::FM: return kNumRackFmSlots;
        case InstrumentType::SQ: return kNumRackSqSlots;
        case InstrumentType::D:  return kNumRackDSlots;
    }
    return 0;
}

void PartManager::reorderSlot (int fromIndex, int toIndex)
{
    const int n = static_cast<int> (rackOrder.size());
    if (fromIndex < 0 || fromIndex >= n) return;
    if (toIndex   < 0 || toIndex   >= n) return;
    if (fromIndex == toIndex) return;

    const auto entry = rackOrder[static_cast<std::size_t> (fromIndex)];
    rackOrder.erase  (rackOrder.begin() + fromIndex);
    rackOrder.insert (rackOrder.begin() + toIndex, entry);
    sendChangeMessage();
}

void PartManager::setRackOrder (std::vector<SlotId> newOrder)
{
    // Used by state restore — drop the constructor seed and replace with the
    // saved ordering. Active bitmaps must be set independently; this function
    // does not toggle them.
    rackOrder = std::move (newOrder);
    sendChangeMessage();
}
