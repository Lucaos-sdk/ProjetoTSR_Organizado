#include "dx12_context.h"
int main(int argc, char** argv) {
    try {
        bool warp = false, debug = false, full = false, textures = false;
        for (int i=1; i<argc; ++i) {
            const std::string arg(argv[i]);
            if (arg == "--warp") warp = true;
            else if (arg == "--debug") debug = true;
            else if (arg == "--full") full = true;
            else if (arg == "--textures") textures = true;
            else throw std::invalid_argument("Usage: tsr_dx12_baseline [--warp] [--debug] [--full] [--textures]");
        }
        Context context(warp, debug, textures);
        context.Run({1,1}, {13,7});
        context.Run({2,2}, {3,3});
        context.Run({7,5}, {7,5});
        context.Run({17,9}, {37,23});
        context.Run({1,7}, {9,17});
        // Exercise large coordinates cheaply, including the RX 7600 pixel-513 regression.
        context.Run({1920,1}, {3840,1});
        context.Run({1,1080}, {1,2160});
        context.Run({1919,3}, {3839,7});
        if (full) context.Run({1920,1080}, {3840,2160});
        std::cout << "Spatial baseline only; single-dispatch timings are not TSR/game performance.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
