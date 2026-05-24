#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

// Multi-sample DAC kit (Task 31). Owns 20 sample cells laid out on the Genny
// 4 x 5 note grid (C-3..G-4, chromatic). Each cell holds its own 8-bit PCM
// resampled to one of three rates (8000 / 11025 / 22050) plus the source
// float buffer the resampler regenerates from on a rate change.
//
// Thread model: cells are populated on the message thread (loadCellWav /
// loadCellRawPcm / clearCell). The audio thread reads bytes from the active
// cell's `pcm` vector via DACPlayer. A per-cell pendingSwap flag lets the
// message thread publish new bytes for the next audio block — the swap
// happens at the top of renderAdd, allocator activity stays off the audio
// thread (the OLD pcm ends up in stagingPcm and is freed by the next
// message-thread mutation).
class DACKit
{
public:
    static constexpr int kNumCells  = 20;
    static constexpr int kFirstNote = 48;   // C-3
    static constexpr int kLastNote  = 67;   // G-4

    struct Cell
    {
        // Display name (file name from the loaded WAV, sans path). Empty when
        // the cell is unloaded. Written on the message thread only.
        juce::String name;

        // Active stored rate (Hz, one of 8000 / 11025 / 22050). Defaults to
        // the global dac_rate at the time of load. Mirror of mtRate kept on
        // the audio thread so the per-block samplesPerWrite calc works
        // without re-reading mtRate.
        int rate = 22050;

        // --- Audio-thread-owned live bytes -----------------------------------
        // Read in DACPlayer::fetchNextSampleByte. Written only by the audio
        // thread during the swap inside DACPlayer::renderAdd.
        std::vector<std::uint8_t> pcm;

        // --- Staging area (lock-free swap protocol) --------------------------
        // The message thread fills stagingPcm + stagingRate and sets
        // pendingSwap. DACPlayer's renderAdd checks pendingSwap on the
        // currently-active cell and swaps pcm with stagingPcm.
        std::vector<std::uint8_t> stagingPcm;
        int                       stagingRate = 22050;
        std::atomic<bool>         pendingSwap { false };

        // --- Message-thread mirror (state save + UI peaks) -------------------
        // Same content as pcm modulo one block of audio-thread lag while a
        // swap is pending. Read by getRawPcmData() / state save / peak
        // computations on the message thread.
        std::vector<std::uint8_t> mtPcm;
        int                       mtRate = 22050;

        // Source float buffer the resampler regenerates 8-bit PCM from when
        // the cell's rate changes. Kept so the per-cell rate can be flipped
        // without re-decoding the original WAV.
        std::vector<float> srcFloat;
        double             srcRate = 0.0;

        Cell() = default;
        Cell (const Cell&)            = delete;
        Cell& operator= (const Cell&) = delete;
    };

    DACKit();

    DACKit (const DACKit&)            = delete;
    DACKit& operator= (const DACKit&) = delete;

    // Map a MIDI note <-> cell index (0..19). Returns -1 if outside the
    // C-3..G-4 grid range.
    static int  cellIndexForNote (int midiNote) noexcept;
    static int  noteForCellIndex (int cellIdx)  noexcept;

    // Load a WAV file into cell `idx`. The cell's stored rate defaults to
    // `defaultRateHz` (the current global `dac_rate` apvts value). Returns
    // true on success. Cells that already have a stored rate keep their
    // existing rate so a re-load of the same cell after a rate change keeps
    // playback timing stable.
    bool loadCellWav (int idx, const juce::File& file, int defaultRateHz);

    // Restore raw 8-bit unsigned PCM bytes into cell `idx` at `rateHz`. Used
    // by PluginState restore (Task 31): the saved-state blob carries the PCM
    // bytes directly, so we don't need the original WAV on disk.
    void loadCellRawPcm (int idx,
                         const std::uint8_t* bytes,
                         std::size_t         numBytes,
                         int                 rateHz,
                         const juce::String& name);

    // Clear cell `idx` — empties its PCM, source float, and name. The audio
    // thread next picks up the cleared bytes via the pendingSwap protocol.
    void clearCell (int idx);

    // True if the cell has any PCM bytes loaded.
    bool hasCell (int idx) const noexcept;

    // Audio-thread-safe pointer to cell `idx` — used by DACPlayer to read
    // bytes / pendingSwap during renderAdd. Returns nullptr for out-of-range
    // indices.
    Cell*       cellPtr (int idx)       noexcept;
    const Cell* cellPtr (int idx) const noexcept;

    // Audio-thread-safe lookup by MIDI note. Returns nullptr if the note is
    // outside the grid range OR the cell has no PCM. Used by
    // DACPlayer::trigger.
    Cell*       cellForNote (int midiNote)       noexcept;
    const Cell* cellForNote (int midiNote) const noexcept;

    // Clear every cell. Used by resetAllToDefaults and rack-clear-D-slot
    // (PluginEditor.cpp clearPart for InstrumentType::D).
    void clearAll();

    // Display length in seconds at the cell's stored rate. 0 when empty.
    double getSampleLengthSeconds (int idx) const noexcept;

    // Compute peak magnitudes for the cell's source float buffer (used by
    // the JS waveform widget — not currently rendered, but the data is in
    // the kit info payload for future per-cell waveform previews).
    std::vector<float> computePeaks (int idx, int numBuckets) const;

    // --- State persistence -------------------------------------------------
    // Append per-cell <cell index="..." rate="..." name="..." pcm="..."/>
    // children into `dacEl`. Empty cells are skipped (state stays compact).
    void saveToXml (juce::XmlElement& dacEl) const;

    // Restore every cell from the children of `dacEl`. Pre-existing cells
    // not mentioned in the XML are cleared so the kit reflects exactly what
    // was saved.
    void restoreFromXml (const juce::XmlElement& dacEl);

    // --- Static helpers -----------------------------------------------------
    static std::uint8_t floatTo8BitUnsigned (float s) noexcept;
    static int          normaliseDacRate    (int hz)  noexcept;

private:
    void stageSwap (int idx,
                    std::vector<std::uint8_t> newBytes,
                    int newRate);

    std::vector<std::uint8_t> regenerateBytes (const std::vector<float>& src,
                                               double sourceRate,
                                               int destRate) const;

    std::array<Cell, kNumCells> cells;
};
