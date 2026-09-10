#include "pch.h"
#include "TsrNgxAdapter.h"

bool PrepareTsrNgxFrame(const NVSDK_NGX_Parameter* parameters,
                       const tsr::integration::NgxConventions& conventions,
                       tsr::integration::NgxFrame& output, std::string& reason)
{
    try {
        if (!parameters) throw std::invalid_argument("Missing NGX parameter interface");
        auto prepared = tsr::integration::ReadNgxFrame(*parameters, conventions);
        reason.clear();
        output = prepared;
        return true;
    } catch (const std::invalid_argument& error) {
        reason = error.what();
        return false;
    }
}
