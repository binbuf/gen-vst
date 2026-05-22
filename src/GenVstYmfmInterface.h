#pragma once

#include <cstdint>

#include "ymfm.h"

// ymfm requires one ymfm_interface implementation per chip instance. The YM2612
// has no external ADPCM/PCM memory, so every external-I/O hook is a no-op.
class GenVstYmfmInterface : public ymfm::ymfm_interface
{
public:
    uint8_t ymfm_external_read (ymfm::access_class, uint32_t) override            { return 0; }
    void    ymfm_external_write (ymfm::access_class, uint32_t, uint8_t) override  {}
};
