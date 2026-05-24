#include "VgmLogger.h"

#include <algorithm>
#include <cmath>

namespace
{
    // YYYY-MM-DDTHH-MM-SS.vgm. Colons aren't valid Windows filename chars, so
    // the time separator uses dashes; the sortable ISO date stays intact.
    juce::String timestampedFileName()
    {
        return juce::Time::getCurrentTime().formatted ("%Y-%m-%dT%H-%M-%S")
             + juce::String (".vgm");
    }

    inline void writeU32LE (std::array<std::uint8_t, 0x40>& hdr,
                            std::size_t off, std::uint32_t v)
    {
        hdr[off]     = static_cast<std::uint8_t> ( v        & 0xFF);
        hdr[off + 1] = static_cast<std::uint8_t> ((v >>  8) & 0xFF);
        hdr[off + 2] = static_cast<std::uint8_t> ((v >> 16) & 0xFF);
        hdr[off + 3] = static_cast<std::uint8_t> ((v >> 24) & 0xFF);
    }
}

VgmLogger::VgmLogger() = default;

VgmLogger::~VgmLogger()
{
    // Defensive: if the plugin is destroyed mid-capture, close cleanly so we
    // don't leak the file descriptor or leave the on-disk file headerless.
    if (active.load (std::memory_order_acquire))
    {
        active.store (false, std::memory_order_release);
        stopTimer();
        flushOnce();
        patchHeaderAndClose();
    }
}

void VgmLogger::prepare (double sr) noexcept
{
    if (sr > 0.0)
    {
        hostSampleRate    = sr;
        samplesToVgmTicks = 44100.0 / sr;
    }
}

juce::File VgmLogger::logDirectory()
{
    return juce::File::getSpecialLocation (
                juce::File::SpecialLocationType::userApplicationDataDirectory)
           .getChildFile ("GenVst")
           .getChildFile ("logs");
}

juce::File VgmLogger::makeLogFile()
{
    return logDirectory().getChildFile (timestampedFileName());
}

bool VgmLogger::toggle (juce::String& pathOrErrorOut)
{
    if (active.load (std::memory_order_acquire))
    {
        // Stop: flip the flag first so the audio thread short-circuits its
        // record* calls on the next pass. Two drains catch in-flight events
        // that completed between the flag store and the first flush (the SPSC
        // FIFO uses acquire/release indices so a second pass is sufficient).
        active.store (false, std::memory_order_release);
        stopTimer();
        flushOnce();
        flushOnce();
        patchHeaderAndClose();
        pathOrErrorOut = currentFile.getFullPathName();
        currentFile = juce::File();
        return false;
    }

    // Start. Reset the ring so any orphaned events from a previous capture
    // don't leak into this one's header-prefix.
    fifo.reset();

    const juce::File dir = logDirectory();
    const auto       dirResult = dir.createDirectory();
    if (dirResult.failed())
    {
        pathOrErrorOut = dirResult.getErrorMessage();
        return false;
    }

    currentFile = makeLogFile();
    out = std::make_unique<juce::FileOutputStream> (currentFile);
    if (! out->openedOk())
    {
        pathOrErrorOut = "could not open " + currentFile.getFullPathName();
        out.reset();
        currentFile = juce::File();
        return false;
    }
    // FileOutputStream opens in append mode by default; truncate explicitly
    // so a recycled filename (rare — timestamp at 1 s resolution) starts clean.
    out->setPosition (0);
    out->truncate();

    totalVgmSamples = 0;
    writeHeader();

    active.store (true, std::memory_order_release);
    startTimerHz (10);
    pathOrErrorOut = currentFile.getFullPathName();
    return true;
}

void VgmLogger::recordYm2612VoiceWrite (int partIndex, std::uint8_t reg,
                                        std::uint8_t value) noexcept
{
    if (! isActive()) return;

    // Voice writes always target chip-channel 0; remap to VGM channel
    // (partIndex + 1). Out-of-range part indices (Idle voice writes, never
    // expected in practice) fall back to VGM channel 1.
    const int safePart = (partIndex >= 0 && partIndex < 6) ? partIndex : 0;
    const int subCh    = safePart % 3;
    int       port     = (safePart >= 3) ? 1 : 0;

    std::uint8_t outReg  = reg;
    std::uint8_t outData = value;

    if (reg == 0x28)
    {
        // Key on/off — the 0x28 register lives on port 0 regardless of which
        // bank holds the channel. Data bits 2:0 select the channel: 0..2 =
        // port-0 channels 1..3, 4..6 = port-1 channels 4..6 (bit 2 = port
        // flag). Bits 6:4 carry the operator mask (preserved from the input).
        port = 0;
        const int chSel = (safePart < 3) ? safePart : (4 + (safePart - 3));
        outData = static_cast<std::uint8_t> ((value & 0xF0) | (chSel & 0x07));
    }
    else if (reg == 0x22 || reg == 0x27 || reg == 0x2A || reg == 0x2B)
    {
        // Global registers — LFO (0x22), timer + ch3 mode (0x27), DAC
        // (0x2A/0x2B). Always live on port 0; don't fold partIndex in.
        port = 0;
    }
    else
    {
        // Per-channel / per-operator registers (0x30..0xB6 ranges). The low
        // 2 bits of the register address select the sub-channel within the
        // port. Voice writes always have those bits = 0; OR in the target
        // sub-channel.
        outReg = static_cast<std::uint8_t> ((reg & ~0x03u) | (subCh & 0x03));
    }

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0)
    {
        Event& e = slots[(std::size_t) start1];
        e.type      = (port == 0) ? Event::Type::Ym2612P0 : Event::Type::Ym2612P1;
        e.reg       = outReg;
        e.data      = outData;
        e.waitTicks = 0;
        fifo.finishedWrite (1);
    }
    // Ring full — silently drop the event. The captured file will skip a few
    // writes but stay structurally valid.
}

void VgmLogger::recordPsgWrite (std::uint8_t data) noexcept
{
    if (! isActive()) return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0)
    {
        Event& e = slots[(std::size_t) start1];
        e.type      = Event::Type::Psg;
        e.reg       = 0;
        e.data      = data;
        e.waitTicks = 0;
        fifo.finishedWrite (1);
    }
}

void VgmLogger::recordWaitSamples (int numHostSamples) noexcept
{
    if (! isActive() || numHostSamples <= 0) return;

    const std::uint32_t ticks =
        static_cast<std::uint32_t> (std::lround (
            static_cast<double> (numHostSamples) * samplesToVgmTicks));
    if (ticks == 0) return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0)
    {
        Event& e = slots[(std::size_t) start1];
        e.type      = Event::Type::Wait;
        e.reg       = 0;
        e.data      = 0;
        e.waitTicks = ticks;
        fifo.finishedWrite (1);
    }
}

void VgmLogger::timerCallback()
{
    flushOnce();
}

void VgmLogger::flushOnce()
{
    if (out == nullptr) return;

    const int available = fifo.getNumReady();
    if (available <= 0) return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead (available, start1, size1, start2, size2);

    const auto writeOne = [this] (const Event& e)
    {
        switch (e.type)
        {
            case Event::Type::Ym2612P0:
                out->writeByte (static_cast<char> (0x52));
                out->writeByte (static_cast<char> (e.reg));
                out->writeByte (static_cast<char> (e.data));
                break;
            case Event::Type::Ym2612P1:
                out->writeByte (static_cast<char> (0x53));
                out->writeByte (static_cast<char> (e.reg));
                out->writeByte (static_cast<char> (e.data));
                break;
            case Event::Type::Psg:
                out->writeByte (static_cast<char> (0x50));
                out->writeByte (static_cast<char> (e.data));
                break;
            case Event::Type::Wait:
                writeWaitBytes (*out, e.waitTicks);
                totalVgmSamples += e.waitTicks;
                break;
        }
    };

    for (int i = 0; i < size1; ++i) writeOne (slots[(std::size_t) (start1 + i)]);
    for (int i = 0; i < size2; ++i) writeOne (slots[(std::size_t) (start2 + i)]);

    fifo.finishedRead (size1 + size2);
}

void VgmLogger::writeHeader()
{
    if (out == nullptr) return;

    // 64-byte VGM 1.71 header. EoF offset (0x04) and total samples (0x18)
    // are zero placeholders here; patchHeaderAndClose back-fills them. Every
    // other field is fixed for our YM2612 + SN76489 chip set (Task 29 spec).
    std::array<std::uint8_t, 0x40> hdr {};
    hdr[0] = 'V'; hdr[1] = 'g'; hdr[2] = 'm'; hdr[3] = ' ';

    writeU32LE (hdr, 0x08, 0x00000171u);   // version 1.71
    writeU32LE (hdr, 0x0C, 3579545u);      // SN76489 clock (NTSC)

    // SN76489 noise feedback (LE u16) and shift-register width — Sega VDP
    // PSG variant (matches SN76489Wrapper config).
    hdr[0x28] = 0x09; hdr[0x29] = 0x00;
    hdr[0x2A] = 16;
    hdr[0x2B] = 0;                          // SN76489 flags

    writeU32LE (hdr, 0x2C, 7670454u);       // YM2612 clock (NTSC)

    // VGM data offset, relative to its own position. 0x34 + 0x0C = 0x40, so
    // the data stream starts immediately after the 64-byte header.
    writeU32LE (hdr, 0x34, 0x0Cu);

    out->write (hdr.data(), hdr.size());
}

void VgmLogger::patchHeaderAndClose()
{
    if (out == nullptr) return;

    // VGM end-of-stream marker. After this byte the file is structurally
    // complete; the header back-patches only need the EoF offset + total
    // samples to be valid for playback.
    out->writeByte (static_cast<char> (0x66));

    const auto totalSize = out->getPosition();
    const auto eofRel    = totalSize - 4;

    out->setPosition (kEofOffsetPos);
    out->writeInt (static_cast<int> (eofRel & 0xFFFFFFFFu));

    out->setPosition (kTotalSamPos);
    out->writeInt (static_cast<int> (totalVgmSamples & 0xFFFFFFFFu));

    out->flush();
    out.reset();
}

void VgmLogger::writeWaitBytes (juce::FileOutputStream& fs, std::uint32_t ticks)
{
    while (ticks > 0)
    {
        const std::uint32_t chunk = std::min<std::uint32_t> (ticks, 65535u);
        fs.writeByte  (static_cast<char>  (0x61));
        fs.writeShort (static_cast<short> (chunk & 0xFFFF));
        ticks -= chunk;
    }
}
