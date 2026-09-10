#include "runtime_options.h"
#include <iostream>
int main() {
    try {
        const auto size=tsr::ParseResolution("1920x1080");
        if (size.width!=1920 || size.height!=1080 || tsr::ParseAdapter("0")!=0) throw std::runtime_error("Parse mismatch");
        for (auto value:{"0x1080","1920x0","16385x1080","-1x10","10x10junk","10x","10","1x2x3","4294967296x1"}) {
            bool rejected=false;
            try { tsr::ParseResolution(value); } catch(const std::invalid_argument&) { rejected=true; }
            if (!rejected) throw std::runtime_error("Invalid resolution accepted");
        }
        bool rejected=false;
        try { tsr::ParseAdapter("-1"); } catch(const std::invalid_argument&) { rejected=true; }
        if (!rejected) throw std::runtime_error("Negative adapter accepted");
        std::cout << "PASS explicit resolution and adapter options\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
