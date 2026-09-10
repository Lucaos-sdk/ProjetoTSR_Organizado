#pragma once
#include "../../../../native/integration/ngx_frame_adapter.h"

// Preparation only: no GPU commands or feature selection. Output is assigned
// only after every metadata check succeeds. The caller handles fallback.
bool PrepareTsrNgxFrame(const NVSDK_NGX_Parameter* parameters,
                       const tsr::integration::NgxConventions& conventions,
                       tsr::integration::NgxFrame& output, std::string& reason);
