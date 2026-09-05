#include "reference.h"
#include <iostream>

void require(bool ok) { if (!ok) throw std::runtime_error("Reference test failed"); }
int main() {
    try {
        using namespace tsr;
        const Pixel hdr = {-3, 5000, .25f, 1};
        for (auto p : Bilinear({hdr}, {1,1}, {13,7})) require(p == hdr);
        const auto fixture = Fixture({7,5});
        require(Bilinear(fixture, {7,5}, {7,5}) == fixture);
        // Independently known 2x2 -> 3x3 values: corners and center average.
        const std::vector<Pixel> source = {{{0,0,0,1}},{{4,4,4,1}},{{8,8,8,1}},{{12,12,12,1}}};
        const auto out = Bilinear(source, {2,2}, {3,3});
        require(out[0][0] == 0 && out[2][0] == 4 && out[6][0] == 8 && out[8][0] == 12);
        require(out[4][0] == 6 && out[1][0] == 2 && out[3][0] == 4);
        bool rejected = false;
        try { Bilinear({}, {0,1}, {1,1}); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected);
        rejected = false;
        try { Bilinear({}, {1,1}, {1,1}); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected);
        auto bad = source; bad[0][0] = std::numeric_limits<float>::infinity();
        rejected = false;
        try { Verify(bad, source); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected);
        std::cout << "PASS: identity, HDR constant, interpolation, edges, invalid input and nonfinite rejection\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
