#include <vector>
#include "agb_vk.h"
#include "agb_bridge.h"
#include "gba_port.h"

int main() {
    // 1) Bring up renderer
    AgbVkCtx* ctx = agbvk_create();

    // 2) Snapshot empty HAL → renderer (still draws nothing interesting)
    AgbHwState hw{};
    gba::snapshot_to(hw);
    agb_sync_to_renderer(hw, ctx);

    // 3) Dispatch once with the same push-consts as frame_viewer
    agbvk_dispatch_frame(ctx, 240, 160, 32, 32, 32 * 1024, /*objMapMode*/0);
    std::vector<uint32_t> rgba(240 * 160);
    agbvk_readback_rgba(ctx, rgba.data(), rgba.size());  // pixels available here  // :contentReference[oaicite:4]{index=4}

    agbvk_destroy(ctx);
    return 0;
}
