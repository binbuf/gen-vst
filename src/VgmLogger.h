#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

// Captures every YM2612 + SN76489 register write into a .vgm-format file on
// disk (Task 29). The logger is an audio-thread producer: chip-write paths in
// Voice + SN76489Engine call the record* methods, which push fixed-size events
// into a SPSC ring buffer. A message-thread juce::Timer drains the ring at
// ~10 Hz and appends the encoded bytes to the open file. The audio thread
// never touches the filesystem.
//
// File path: <userAppData>/GenVst/logs/<timestamp>.vgm. The logs folder is
// auto-created on first capture. On stop, the cumulative total-samples field
// and the EoF offset are back-patched into the 64-byte header.
//
// Voice writes always target chip-channel 0 of a dedicated ymfm instance
// (ADR-0010); recordYm2612VoiceWrite() remaps that to the VGM channel matching
// the voice's partIndex so playback of the logged file produces the same
// multi-channel layout the listener heard live. Polyphony within a single part
// is collapsed to one channel — the captured VGM is best for monophonic
// playback (see Task 29 *Out of scope*).
class VgmLogger : private juce::Timer
{
public:
    VgmLogger();
    ~VgmLogger() override;

    VgmLogger (const VgmLogger&)            = delete;
    VgmLogger& operator= (const VgmLogger&) = delete;

    // Message thread. Cache the host sample rate for the per-block wait-tick
    // conversion (VGM ticks at 44 100 Hz; processBlock counts host samples).
    // Safe to call while logging is inactive or active.
    void prepare (double hostSampleRate) noexcept;

    // Message thread. Flip the active state. On start: open the file, write
    // the header, start the flush timer. On stop: drain the ring, append
    // 0x66 EoF, back-patch the header, close the file. Returns the new
    // active state; sets `pathOrErrorOut` to the file path (on success) or
    // an error message (on failure).
    bool toggle (juce::String& pathOrErrorOut);

    // Lock-free, audio-thread safe. True between start and stop.
    bool isActive() const noexcept { return active.load (std::memory_order_acquire); }

    // Audio-thread entry points. All lock-free, no allocation. recordYm2612
    // takes the Voice's partIndex (0..5) so writes destined for chip-channel-0
    // can be remapped to VGM channel (partIndex+1). recordPsg passes the
    // SN76489 protocol byte through unchanged. recordWaitSamples converts a
    // host-rate sample count to VGM 44 100 Hz ticks and queues a wait event.
    void recordYm2612VoiceWrite (int partIndex, std::uint8_t reg, std::uint8_t value) noexcept;
    void recordPsgWrite          (std::uint8_t data) noexcept;
    void recordWaitSamples       (int numHostSamples) noexcept;

    // Test-only: synchronously drain the ring into the open file. Production
    // code uses the timer; tests call this directly so they don't need a
    // running message loop. Safe to call when inactive (no-op).
    void flushPendingWritesForTest() { flushOnce(); }

private:
    void timerCallback() override;

    // Drain everything currently available in the ring into the open file.
    // Called by the message-thread timer (production) and by the test helper.
    void flushOnce();

    // Write the 64-byte VGM 1.71 header (EoF offset + total samples are
    // placeholders, patched at stop).
    void writeHeader();

    // Append 0x66 EoF, back-patch the EoF offset (file_size - 4) and the
    // total-samples field, flush + close the output stream.
    void patchHeaderAndClose();

    // Emit one or more 0x61 wait commands so any tick count up to 2^32 - 1
    // is representable (each command's payload is a 16-bit operand).
    void writeWaitBytes (juce::FileOutputStream& fs, std::uint32_t ticks);

    static juce::File logDirectory();
    static juce::File makeLogFile();

    // Fixed-size event slot. Both `data` payloads (YM2612 reg/value, PSG
    // byte, wait ticks) ride here to keep the ring trivially copyable; an
    // 8-byte slot is plenty for a SPSC ring sized at kRingCapacity.
    struct Event
    {
        enum class Type : std::uint8_t { Ym2612P0, Ym2612P1, Psg, Wait };
        Type           type      = Type::Wait;
        std::uint8_t   reg       = 0;
        std::uint8_t   data      = 0;
        std::uint8_t   pad       = 0;
        std::uint32_t  waitTicks = 0;
    };
    static constexpr int kRingCapacity = 8192;

    juce::AbstractFifo                  fifo { kRingCapacity };
    std::array<Event, kRingCapacity>    slots {};

    std::atomic<bool>                   active { false };

    // Message-thread-owned file handle. Audio thread never touches this.
    std::unique_ptr<juce::FileOutputStream> out;
    juce::File                          currentFile;

    double         hostSampleRate    = 44100.0;
    double         samplesToVgmTicks = 1.0;

    // Cumulative wait ticks emitted so far in the current capture. Back-
    // patched into the total-samples field at stop time.
    std::uint64_t  totalVgmSamples = 0;

    // Header byte offsets that need back-patching at stop.
    static constexpr int kEofOffsetPos = 0x04;
    static constexpr int kTotalSamPos  = 0x18;
};
