#include "temporal_cases.h"
int main() {
    try {
        tsr::TemporalReference contexts[2];
        tsr::TemporalCases([&](int instance, const tsr::FrameInput& frame, const char*) {
            return contexts[instance].Run(frame);
        }, false);
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
