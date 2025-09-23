#include <cstdint>
#include <cstddef>
#include <vector>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <stdexcept>

#include "agb_vk.h"
#include "agb_bridge.h"

int main() try
{
    using std::cout;
    using std::cerr;

    //--- Diagnostics so failures aren't invisible ---------------------------------
    cout << "frame_viewer starting\n";
    cout << "CWD: " << std::filesystem::current_path().string() << "\n";
#ifdef SHADER_SPV_PATH
    cout << "SHADER_SPV_PATH: " << SHADER_SPV_PATH << "\n";
#endif

    //--- Create renderer + build the exact demo scene in host memory --------------
    AgbVkCtx* ctx = agbvk_create();
    AgbHwState hw{};
    agb_init_hw(hw);                 // fill VRAM/pal/OAM/BG params/windows/FX/scan/affine (host)  :contentReference[oaicite:1]{index=1}
    agb_sync_to_renderer(hw, ctx);   // copy host state into the 11 SSBOs (descriptor 1..10)       :contentReference[oaicite:2]{index=2}

    //--- Dispatch one frame (push-consts mirror original prototype) ---------------
    constexpr uint32_t FB_W = 240;
    constexpr uint32_t FB_H = 160;
    constexpr uint32_t MAP_W = 32;
    constexpr uint32_t MAP_H = 32;
    constexpr uint32_t OBJ_CHAR_BASE = 32 * 1024; // bytes into VRAM where OBJ tiles live
    constexpr uint32_t OBJ_MAP_MODE = 0;         // 0 = 2D mapping, 1 = 1D

    agbvk_dispatch_frame(ctx, FB_W, FB_H, MAP_W, MAP_H, OBJ_CHAR_BASE, OBJ_MAP_MODE);  // :contentReference[oaicite:3]{index=3}

    //--- Readback and write PPM (RGB from RGBA8) ----------------------------------
    std::vector<uint32_t> rgba(FB_W * FB_H);
    agbvk_readback_rgba(ctx, rgba.data(), rgba.size());                                  // :contentReference[oaicite:4]{index=4}

    std::ofstream ppm("hello_frame.ppm", std::ios::binary);
    if (!ppm) throw std::runtime_error("Cannot open hello_frame.ppm for writing.");
    ppm << "P6\n" << FB_W << " " << FB_H << "\n255\n";
    for (uint32_t px : rgba) {
        unsigned char r = static_cast<unsigned char>((px >> 0) & 0xFF);
        unsigned char g = static_cast<unsigned char>((px >> 8) & 0xFF);
        unsigned char b = static_cast<unsigned char>((px >> 16) & 0xFF);
        ppm.put(r).put(g).put(b);
    }
    ppm.close();
    cout << "Wrote hello_frame.ppm in: " << std::filesystem::current_path().string() << "\n";

    //--- Cleanup ------------------------------------------------------------------
    agbvk_destroy(ctx);
    return 0;
}
catch (const std::exception& e)
{
    std::ofstream log("frame_viewer.error.txt");
    log << "Exception: " << e.what() << "\n";
    log.close();
    std::cerr << "Exception: " << e.what() << "\n";
    return 1;
}
