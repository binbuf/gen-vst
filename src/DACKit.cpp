#include "DACKit.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <utility>

namespace
{
    constexpr int kDefaultDacRate = 22050;

    juce::String encodeBase64 (const std::uint8_t* data, std::size_t size)
    {
        if (data == nullptr || size == 0) return {};
        juce::MemoryOutputStream stream;
        juce::Base64::convertToBase64 (stream, data, size);
        return stream.toString();
    }

    std::vector<std::uint8_t> decodeBase64 (const juce::String& s)
    {
        juce::MemoryOutputStream stream;
        if (! juce::Base64::convertFromBase64 (stream, s))
            return {};
        const auto* bytes = static_cast<const std::uint8_t*> (stream.getData());
        return { bytes, bytes + stream.getDataSize() };
    }
}

DACKit::DACKit() = default;

int DACKit::cellIndexForNote (int midiNote) noexcept
{
    if (midiNote < kFirstNote || midiNote > kLastNote) return -1;
    return midiNote - kFirstNote;
}

int DACKit::noteForCellIndex (int cellIdx) noexcept
{
    return kFirstNote + cellIdx;
}

DACKit::Cell* DACKit::cellPtr (int idx) noexcept
{
    if (idx < 0 || idx >= kNumCells) return nullptr;
    return &cells[(std::size_t) idx];
}

const DACKit::Cell* DACKit::cellPtr (int idx) const noexcept
{
    if (idx < 0 || idx >= kNumCells) return nullptr;
    return &cells[(std::size_t) idx];
}

DACKit::Cell* DACKit::cellForNote (int midiNote) noexcept
{
    const int idx = cellIndexForNote (midiNote);
    if (idx < 0) return nullptr;
    auto* c = cellPtr (idx);
    if (c == nullptr) return nullptr;
    // "Loaded" means there's bytes the next renderAdd can stream:
    //   * pcm is non-empty (already-drained bytes are about to play), OR
    //   * stagingPcm is non-empty AND a swap is pending (the audio thread
    //     will swap them into pcm at the top of the next renderAdd).
    // stagingPcm without pendingSwap is residual from the audio thread's
    // most recent swap (`pcm.swap(stagingPcm)` leaves the OLD pcm there)
    // and is inert — those bytes won't reach pcm unless the message thread
    // restages them.
    if (! c->pcm.empty()) return c;
    if (c->pendingSwap.load (std::memory_order_acquire)
        && ! c->stagingPcm.empty())
        return c;
    return nullptr;
}

const DACKit::Cell* DACKit::cellForNote (int midiNote) const noexcept
{
    return const_cast<DACKit*> (this)->cellForNote (midiNote);
}

bool DACKit::hasCell (int idx) const noexcept
{
    if (idx < 0 || idx >= kNumCells) return false;
    const auto& c = cells[(std::size_t) idx];
    return ! c.mtPcm.empty();
}

double DACKit::getSampleLengthSeconds (int idx) const noexcept
{
    if (idx < 0 || idx >= kNumCells) return 0.0;
    const auto& c = cells[(std::size_t) idx];
    if (c.mtPcm.empty() || c.mtRate <= 0) return 0.0;
    return static_cast<double> (c.mtPcm.size()) / static_cast<double> (c.mtRate);
}

bool DACKit::loadCellWav (int idx, const juce::File& file, int defaultRateHz)
{
    if (idx < 0 || idx >= kNumCells) return false;
    if (! file.existsAsFile())        return false;

    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (mgr.createReaderFor (file));
    if (reader == nullptr) return false;

    const auto numSamples = static_cast<int> (reader->lengthInSamples);
    const auto numChans   = static_cast<int> (reader->numChannels);
    if (numSamples <= 0 || numChans <= 0) return false;

    juce::AudioBuffer<float> raw (numChans, numSamples);
    if (! reader->read (&raw, 0, numSamples, 0, true, numChans > 1))
        return false;

    auto& c = cells[(std::size_t) idx];
    c.srcRate = reader->sampleRate;
    c.srcFloat.assign (static_cast<std::size_t> (numSamples), 0.0f);

    const float invChans = 1.0f / static_cast<float> (numChans);
    for (int ch = 0; ch < numChans; ++ch)
    {
        const float* src = raw.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            c.srcFloat[(std::size_t) i] += src[i] * invChans;
    }

    c.name  = file.getFileName();
    // Preserve a previously-set rate so flipping the global default doesn't
    // implicitly retune already-loaded cells. Fresh cells inherit the global.
    if (c.mtRate <= 0) c.mtRate = normaliseDacRate (defaultRateHz);
    else               c.mtRate = normaliseDacRate (c.mtRate);
    c.rate = c.mtRate;

    auto newPcm = regenerateBytes (c.srcFloat, c.srcRate, c.mtRate);
    c.mtPcm = newPcm;
    stageSwap (idx, std::move (newPcm), c.mtRate);
    return ! c.mtPcm.empty();
}

void DACKit::loadCellRawPcm (int idx,
                             const std::uint8_t* bytes,
                             std::size_t         numBytes,
                             int                 rateHz,
                             const juce::String& name)
{
    if (idx < 0 || idx >= kNumCells) return;
    auto& c = cells[(std::size_t) idx];

    if (bytes == nullptr || numBytes == 0)
    {
        clearCell (idx);
        return;
    }

    c.mtRate = normaliseDacRate (rateHz);
    c.rate   = c.mtRate;
    c.name   = name;

    // Reconstruct a mono float source so a later rate change can resample
    // without needing the original WAV. The reconstruction is exact-ish to
    // the 8-bit granularity the chip actually plays — fine for both the
    // resampler and any future waveform display.
    c.srcRate  = static_cast<double> (c.mtRate);
    c.srcFloat.assign (numBytes, 0.0f);
    for (std::size_t i = 0; i < numBytes; ++i)
        c.srcFloat[i] = (static_cast<int> (bytes[i]) - 128) / 127.0f;

    c.mtPcm.assign (bytes, bytes + numBytes);
    stageSwap (idx, c.mtPcm, c.mtRate);
}

void DACKit::clearCell (int idx)
{
    if (idx < 0 || idx >= kNumCells) return;
    auto& c = cells[(std::size_t) idx];

    c.mtPcm.clear();
    c.srcFloat.clear();
    c.srcRate = 0.0;
    c.name    = juce::String();

    // Stage an empty buffer at the existing rate so DACPlayer's next render
    // sees an empty pcm and stops playing. We do NOT zero rate, so a later
    // re-load picks up the previously-stored rate by default.
    stageSwap (idx, {}, c.mtRate);
}

void DACKit::clearAll()
{
    for (int i = 0; i < kNumCells; ++i)
        clearCell (i);
}

void DACKit::stageSwap (int idx,
                        std::vector<std::uint8_t> newBytes,
                        int newRate)
{
    auto& c = cells[(std::size_t) idx];

    // Wait for the audio thread to consume any previous pending swap on
    // this cell before we overwrite its staging area. Capped at 1 s so a
    // host that has stopped calling processBlock cannot deadlock; the
    // race window after the cap is narrow (the audio thread's swap is a
    // single vector::swap + rate copy) and only matters if the cell is the
    // active one. Matches the legacy DACPlayer::stageSwap contract.
    const auto t0 = std::chrono::steady_clock::now();
    while (c.pendingSwap.load (std::memory_order_acquire))
    {
        if (std::chrono::steady_clock::now() - t0
              > std::chrono::milliseconds (1000))
            break;
        std::this_thread::yield();
    }

    c.stagingPcm  = std::move (newBytes);
    c.stagingRate = normaliseDacRate (newRate);
    c.pendingSwap.store (true, std::memory_order_release);
}

std::vector<std::uint8_t>
DACKit::regenerateBytes (const std::vector<float>& src,
                         double sourceRate, int destRate) const
{
    std::vector<std::uint8_t> out;
    if (src.empty() || sourceRate <= 0.0 || destRate <= 0)
        return out;

    const int srcCount = static_cast<int> (src.size());
    const double ratio = sourceRate / static_cast<double> (destRate);
    const int dstCount = std::max (1, static_cast<int> (std::ceil (srcCount / ratio)));

    out.assign (static_cast<std::size_t> (dstCount), 0x80);

    // Nearest-sample resampling — adequate for 8-bit DAC playback. Matches
    // the legacy DACPlayer resampler so factory test fixtures stay valid.
    for (int i = 0; i < dstCount; ++i)
    {
        const double srcPos = i * ratio;
        const int    sIdx   = std::clamp (static_cast<int> (std::round (srcPos)),
                                          0, srcCount - 1);
        out[(std::size_t) i] =
            floatTo8BitUnsigned (src[(std::size_t) sIdx]);
    }
    return out;
}

std::vector<float> DACKit::computePeaks (int idx, int numBuckets) const
{
    std::vector<float> out;
    if (idx < 0 || idx >= kNumCells || numBuckets <= 0) return out;
    const auto& c = cells[(std::size_t) idx];
    if (c.srcFloat.empty()) return out;

    out.assign ((std::size_t) numBuckets, 0.0f);
    const std::size_t total = c.srcFloat.size();
    for (int b = 0; b < numBuckets; ++b)
    {
        const std::size_t lo = ((std::size_t) b     * total) / (std::size_t) numBuckets;
        const std::size_t hi = ((std::size_t) (b+1) * total) / (std::size_t) numBuckets;
        float peak = 0.0f;
        for (std::size_t i = lo; i < hi; ++i)
            peak = std::max (peak, std::fabs (c.srcFloat[i]));
        out[(std::size_t) b] = std::min (1.0f, peak);
    }
    return out;
}

void DACKit::saveToXml (juce::XmlElement& dacEl) const
{
    for (int i = 0; i < kNumCells; ++i)
    {
        const auto& c = cells[(std::size_t) i];
        if (c.mtPcm.empty()) continue;

        auto* el = dacEl.createNewChildElement ("cell");
        el->setAttribute ("index", i);
        el->setAttribute ("rate",  c.mtRate);
        if (c.name.isNotEmpty())
            el->setAttribute ("name", c.name);
        el->setAttribute ("pcm",
            encodeBase64 (c.mtPcm.data(), c.mtPcm.size()));
    }
}

void DACKit::restoreFromXml (const juce::XmlElement& dacEl)
{
    // Two passes: load every cell in the XML first, then clear anything
    // else that's still loaded. Doing the loads before the clears means a
    // cell mentioned in the XML never gets a "clear then load" sequence,
    // which would block 1 s on the second stageSwap waiting for the first
    // swap's pendingSwap=true to drain. The single-load path keeps the
    // existing pendingSwap state stable across both operations.
    std::array<bool, kNumCells> loaded {};

    for (auto* el : dacEl.getChildWithTagNameIterator ("cell"))
    {
        const int idx = el->getIntAttribute ("index", -1);
        if (idx < 0 || idx >= kNumCells) continue;

        const auto pcmStr = el->getStringAttribute ("pcm");
        if (pcmStr.isEmpty()) continue;

        const auto bytes = decodeBase64 (pcmStr);
        if (bytes.empty()) continue;

        const int rate  = el->getIntAttribute ("rate", kDefaultDacRate);
        const auto name = el->getStringAttribute ("name");
        loadCellRawPcm (idx, bytes.data(), bytes.size(), rate, name);
        loaded[(std::size_t) idx] = true;
    }

    for (int i = 0; i < kNumCells; ++i)
        if (! loaded[(std::size_t) i] && hasCell (i))
            clearCell (i);
}

std::uint8_t DACKit::floatTo8BitUnsigned (float s) noexcept
{
    const float clamped = juce::jlimit (-1.0f, 1.0f, s);
    const int   signed8 = static_cast<int> (std::lround (clamped * 127.0f));
    return static_cast<std::uint8_t> (std::clamp (signed8 + 128, 0, 255));
}

int DACKit::normaliseDacRate (int hz) noexcept
{
    if (hz == 8000 || hz == 11025 || hz == 22050)
        return hz;
    return kDefaultDacRate;
}
