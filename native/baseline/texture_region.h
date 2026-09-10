#pragma once
#include "reference.h"
namespace tsr {
inline void ValidateRegion(Size texture,Size extent,uint32_t x,uint32_t y) {
    Count(texture);Count(extent);
    if (x>texture.width || y>texture.height || extent.width>texture.width-x || extent.height>texture.height-y)
        throw std::invalid_argument("Texture region out of bounds");
}
struct OutputRegion { Size size; uint32_t sourceX=0,sourceY=0,destinationX=0,destinationY=0; };
static_assert(sizeof(OutputRegion)==24);
}
