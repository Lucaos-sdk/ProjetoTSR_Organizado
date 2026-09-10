#include "output_history_cases.h"
int main() {
    try {
        tsr::OutputHistoryReference contexts[2];
        tsr::OutputHistoryCases([&](int i,const tsr::FrameInput& f,const char*) { return contexts[i].Run(f); },false);
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
