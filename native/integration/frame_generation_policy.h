#pragma once
#include <cstdint>
namespace tsr::integration {
// Counts generated frames, not total output frames. Only a successful backend
// call may publish a new active count. Unsupported requests never reach it.
template<class Apply>
bool ApplyGeneratedFrameCount(uint32_t requested,int maximum,int& active,Apply apply) {
    if(maximum<1 || requested<1 || requested>static_cast<uint32_t>(maximum))return false;
    if(!apply(requested))return false;
    active=static_cast<int>(requested);
    return true;
}
}
