#include <vulkan/vulkan.h>
#include <vector>
#include <stdexcept>
#include <fstream>
#include <iostream>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cmath>

#ifndef SHADER_SPV_PATH
#define SHADER_SPV_PATH "compose_frame.comp.spv"
#endif

static const uint32_t FB_W = 240;
static const uint32_t FB_H = 160;

// Simple error helper
static void vkCheck(VkResult r, const char* msg) {
    if (r != VK_SUCCESS) {
        std::cerr << "Vulkan error: " << msg << " (" << r << ")\n";
        std::exit(1);
    }
}

static std::vector<char> readFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error(std::string("Cannot open file: ") + path);
    f.seekg(0, std::ios::end);
    size_t size = (size_t)f.tellg();
    f.seekg(0);
    std::vector<char> buf(size);
    f.read(buf.data(), size);
    return buf;
}

uint32_t findMemoryType(VkPhysicalDevice phys, uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    std::cerr << "No suitable memory type found.\n";
    std::exit(2);
}

struct Buffer {
    VkDevice device{};
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    VkDeviceSize size{};

    void create(VkPhysicalDevice phys, VkDevice dev, VkDeviceSize sz, VkBufferUsageFlags usage, VkMemoryPropertyFlags props) {
        device = dev;
        size = sz;
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = sz;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCheck(vkCreateBuffer(dev, &bi, nullptr, &buffer), "vkCreateBuffer");

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(dev, buffer, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = findMemoryType(phys, req.memoryTypeBits, props);
        vkCheck(vkAllocateMemory(dev, &ai, nullptr, &memory), "vkAllocateMemory");
        vkCheck(vkBindBufferMemory(dev, buffer, memory, 0), "vkBindBufferMemory");
    }

    void* map() {
        void* p = nullptr;
        vkCheck(vkMapMemory(device, memory, 0, size, 0, &p), "vkMapMemory");
        return p;
    }
    void unmap() { vkUnmapMemory(device, memory); }

    void destroy() {
        if (buffer) vkDestroyBuffer(device, buffer, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
        size = 0;
    }
};

struct PushConsts {
    uint32_t fbWidth, fbHeight;
    uint32_t mapWidth, mapHeight;
    uint32_t objCharBase;
    uint32_t objMapMode; // 1 = 1D mapping
};

struct BGParamHost {
    uint32_t charBase, screenBase, hofs, vofs, pri, enabled, _pad0, _pad1;
};

int main() {
    // 1) Instance
    VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "hello_frame";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "none";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    VkInstance instance{};
    vkCheck(vkCreateInstance(&ici, nullptr, &instance), "vkCreateInstance");

    // 2) Physical device + queue family with compute
    uint32_t pdCount = 0;
    vkEnumeratePhysicalDevices(instance, &pdCount, nullptr);
    if (pdCount == 0) { std::cerr << "No Vulkan devices.\n"; return 3; }
    std::vector<VkPhysicalDevice> pds(pdCount);
    vkEnumeratePhysicalDevices(instance, &pdCount, pds.data());

    VkPhysicalDevice phys = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex = 0;
    for (auto pd : pds) {
        uint32_t n = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, nullptr);
        std::vector<VkQueueFamilyProperties> qfp(n);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, qfp.data());
        for (uint32_t i = 0; i < n; ++i) {
            if (qfp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                phys = pd;
                queueFamilyIndex = i;
                break;
            }
        }
        if (phys) break;
    }
    if (!phys) { std::cerr << "No compute-capable queue.\n"; return 4; }

    // 3) Device + queue
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = queueFamilyIndex;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;

    VkDevice device{};
    vkCheck(vkCreateDevice(phys, &dci, nullptr, &device), "vkCreateDevice");

    VkQueue queue{};
    vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);

    // 4) Buffers: out, vram (uint-per-byte), pal
    const size_t outPixels = FB_W * FB_H;
    Buffer outBuf, vramBuf, palBuf, bgBuf, palObjBuf, oamBuf, winBuf, fxBuf, scanBuf, affBuf, objAffBuf;

    outBuf.create(phys, device, outPixels * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // vram: 96KB "bytes", but we store a uint per byte => 96KB entries * 4 bytes
    const size_t VRAM_BYTES = 96 * 1024;
    vramBuf.create(phys, device, VRAM_BYTES * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // pal: 1KB bytes -> uint-per-byte
    const size_t PAL_BYTES = 1024;
    palBuf.create(phys, device, PAL_BYTES * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // 4 BGs * 8 uints * 4 bytes = 128 bytes
    bgBuf.create(phys, device, 4 * 8 * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // OBJ palette: 512 bytes -> uint-per-byte
    palObjBuf.create(phys, device, 512 * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // OAM: 1KB -> uint-per-byte
    oamBuf.create(phys, device, 1024 * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Windows SSBO (64 bytes is ample for our struct)
    winBuf.create(phys, device, 64,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Color-math regs
    fxBuf.create(phys, device, 16, // 3 dwords; 16 for alignment
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Per‑scanline (160 lines * ~80 bytes)
    scanBuf.create(phys, device, 160 * 80,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Affine params (4 BGs * 6 ints)
    affBuf.create(phys, device, 4 * 6 * sizeof(int32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // 32 sets * (pa,pb,pc,pd) * int32
    objAffBuf.create(phys, device, 32 * 4 * sizeof(int32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);







    // Choose distinct regions in our synthetic "VRAM"
    const uint32_t charBase0 = 0;
    const uint32_t charBase1 = 8 * 1024;
    const uint32_t charBase2 = 16 * 1024;    // reserved for future use
    const uint32_t charBase3 = 24 * 1024;    // reserved for future use
    const uint32_t screenBase0 = 64 * 1024;
    const uint32_t screenBase1 = 72 * 1024;
    const uint32_t screenBase2 = 80 * 1024;    // reserved
    const uint32_t screenBase3 = 88 * 1024;    // reserved

    const uint32_t objCharBase = 32 * 1024; // leave BG char areas at 0..24KB as-is

    const uint32_t mapWidth = 32;
    const uint32_t mapHeight = 32;

    const uint32_t pri0 = 1, pri1 = 0, pri2 = 2, pri3 = 3;
    const uint32_t hofs0 = 12, vofs0 = 7;
    const uint32_t hofs1 = 100, vofs1 = 32;
    {
        // Wipe VRAM & palette
        std::memset(vramBuf.map(), 0, VRAM_BYTES * sizeof(uint32_t));
        vramBuf.unmap();
        std::memset(palBuf.map(), 0, PAL_BYTES * sizeof(uint32_t));
        palBuf.unmap();

        // Helper to write little-endian 16-bit into "uint-per-byte" buffers
        auto write16_to = [](uint32_t* base, uint32_t byteOffset, uint16_t v) {
            base[byteOffset + 0] = (uint8_t)(v & 0xFF);
            base[byteOffset + 1] = (uint8_t)((v >> 8) & 0xFF);
            };

        // === Fill BG0 tiles: tile0 -> color index 1, tile1 -> color index 2 ===
        {
            uint32_t* v = (uint32_t*)vramBuf.map();
            // tile 0 (nibble 1 everywhere)
            for (uint32_t row = 0; row < 8; ++row) {
                for (uint32_t col = 0; col < 4; ++col) {
                    v[charBase0 + row * 4 + col] = 0x11; // two pixels of palette index 1
                }
            }
            // tile 1 (nibble 2 everywhere)
            const uint32_t tile1Base0 = charBase0 + 32;
            for (uint32_t row = 0; row < 8; ++row) {
                for (uint32_t col = 0; col < 4; ++col) {
                    v[tile1Base0 + row * 4 + col] = 0x22; // palette index 2
                }
            }
            vramBuf.unmap();
        }

        // === Fill BG1 tiles: tile0 -> color index 3 (red), tile1 -> transparent (nibble 0) ===
        {
            uint32_t* v = (uint32_t*)vramBuf.map();
            // tile 0 (nibble 3 everywhere)
            for (uint32_t row = 0; row < 8; ++row) {
                for (uint32_t col = 0; col < 4; ++col) {
                    v[charBase1 + row * 4 + col] = 0x33; // palette index 3
                }
            }
            // tile 1 (nibble 0 everywhere) => fully transparent BG texel
            const uint32_t tile1Base1 = charBase1 + 32;
            for (uint32_t row = 0; row < 8; ++row) {
                for (uint32_t col = 0; col < 4; ++col) {
                    v[tile1Base1 + row * 4 + col] = 0x00;
                }
            }
            vramBuf.unmap();
        }

        // BG2 8bpp tile 0 = 4x4 blocks (coarser) using indices {1,4}
        {
            uint32_t* v = (uint32_t*)vramBuf.map();
            for (uint32_t row = 0; row < 8; ++row)
                for (uint32_t col = 0; col < 8; ++col) {
                    bool blk = ((row / 2) ^ (col / 2)) & 1;
                    v[charBase2 + row * 8 + col] = blk ? 1 : 4;
                }
            vramBuf.unmap();
        }



        // OBJ tiles: tile 0..3 = solid nibble 1
        {
            uint32_t* v = (uint32_t*)vramBuf.map();
            for (uint32_t t = 0; t < 4; ++t) {
                uint32_t base = objCharBase + t * 32u;
                for (uint32_t row = 0; row < 8; ++row)
                    for (uint32_t col = 0; col < 4; ++col)
                        v[base + row * 4 + col] = 0x11; // 4bpp: two pixels of palette index 1
            }
            vramBuf.unmap();
        }

        // OBJ tiles for an 8bpp sprite: tiles 16..19 = solid index 2
        {
            uint32_t* v = (uint32_t*)vramBuf.map();
            const uint32_t baseTile = 16; // keep 4bpp tiles at 0..3; 8bpp at 16..19
            for (uint32_t t = 0; t < 4; ++t) {
                uint32_t base = objCharBase + (baseTile + t) * 64u; // 8bpp: 64 bytes/tile
                for (uint32_t row = 0; row < 8; ++row)
                    for (uint32_t col = 0; col < 8; ++col)
                        v[base + row * 8 + col] = 2; // palette index 2
            }
            vramBuf.unmap();
        }




        // === BG0 screenblock: 32x32 entries => checkerboard of tile0 and tile1
        {
            uint32_t* v = (uint32_t*)vramBuf.map();
            for (uint32_t ty = 0; ty < mapHeight; ++ty) {
                for (uint32_t tx = 0; tx < mapWidth; ++tx) {
                    uint16_t tileIndex = ((tx + ty) & 1) ? 1 : 0; // tile 0 or 1
                    uint16_t palBank = (tx & 1);         // or (ty & 1), or ((tx/2 + ty/2) & 1) for bigger patches
                    uint16_t attrs = tileIndex | (palBank << 12);
                    uint32_t off = screenBase0 + 2u * (ty * mapWidth + tx);
                    v[off + 0] = (uint8_t)(attrs & 0xFF);
                    v[off + 1] = (uint8_t)((attrs >> 8) & 0xFF);
                }
            }
            vramBuf.unmap();
        }

        // === BG1 screenblock: default transparent tile (1), then a 10x10 red rect of tile 0
        {
            uint32_t* v = (uint32_t*)vramBuf.map();
            for (uint32_t ty = 0; ty < mapHeight; ++ty) {
                for (uint32_t tx = 0; tx < mapWidth; ++tx) {
                    uint16_t attrs = 1; // tile index 1 = transparent tile
                    uint32_t off = screenBase1 + 2u * (ty * mapWidth + tx);
                    v[off + 0] = (uint8_t)(attrs & 0xFF);
                    v[off + 1] = (uint8_t)((attrs >> 8) & 0xFF);
                }
            }
            // Centered-ish 10x10 rect of tile 0 (red), with some flips for variety
            uint32_t startTx = 10, startTy = 5;
            for (uint32_t ty = 0; ty < 10; ++ty) {
                for (uint32_t tx = 0; tx < 10; ++tx) {
                    uint16_t attrs = 0; // tile 0
                    if (tx & 1) attrs |= (1u << 10); // H flip
                    if (ty & 1) attrs |= (1u << 11); // V flip
                    uint32_t off = screenBase1 + 2u * ((startTy + ty) * mapWidth + (startTx + tx));
                    v[off + 0] = (uint8_t)(attrs & 0xFF);
                    v[off + 1] = (uint8_t)((attrs >> 8) & 0xFF);
                }
            }
            vramBuf.unmap();
        }

        // BG2 1‑byte map: fill 32x32 with tile index 0
        {
            uint32_t* v = (uint32_t*)vramBuf.map();
            for (uint32_t ty = 0; ty < mapHeight; ++ty)
                for (uint32_t tx = 0; tx < mapWidth; ++tx)
                    v[screenBase2 + ty * mapWidth + tx] = 0; // one byte per entry
            vramBuf.unmap();
        }



        // === Palette: set a few colors (BGR555) ===
        {
            uint32_t* p = (uint32_t*)palBuf.map();
            auto write16 = [&](uint32_t byteOffset, uint16_t v) {
                p[byteOffset + 0] = (uint8_t)(v & 0xFF);
                p[byteOffset + 1] = (uint8_t)((v >> 8) & 0xFF);
                };

            write16(0 * 2, 0x4210); // backdrop gray
            // Bank 0
            write16(1 * 2, 0x0000); // idx1: black
            write16(2 * 2, 0x7FFF); // idx2: white
            write16(3 * 2, 0x001F); // idx3: red
            write16(4 * 2, 0x03FF); // idx 4 = yellow
            write16((16 + 1) * 2, 0x03E0); // idx1: green
            write16((16 + 2) * 2, 0x7C00); // idx2: blue
            palBuf.unmap();
        }

        // OBJ palette: set index 1 to magenta (BGR555 0x7C1F)
        {
            uint32_t* p = (uint32_t*)palObjBuf.map();
            auto write16 = [&](uint32_t byteOffset, uint16_t v) {
                p[byteOffset + 0] = (uint8_t)(v & 0xFF);
                p[byteOffset + 1] = (uint8_t)((v >> 8) & 0xFF);
                };
            std::memset(p, 0, 512 * sizeof(uint32_t));     // pal[0] stays transparent
            write16(1 * 2, 0x7C1F); // OBJ pal idx 1 (magenta)
            write16(2 * 2, 0x7FE0); // OBJ pal idx 2 = cyan (B+G)

            palObjBuf.unmap();
        }

        // One OBJ in OAM entry 0
        {
            uint32_t* o = (uint32_t*)oamBuf.map();
            std::memset(o, 0, 1024 * sizeof(uint32_t));
            auto w16 = [&](uint32_t byteOffset, uint16_t v) {
                o[byteOffset + 0] = (uint8_t)(v & 0xFF);
                o[byteOffset + 1] = (uint8_t)((v >> 8) & 0xFF);
                };

            // Hide every OBJ by default (attr0 bits 9:8 = 10b)
            for (uint32_t i = 0; i < 128; ++i) {
                w16(i * 8 + 0, 0x0200); // attr0 = hidden
            }

            uint16_t y = 12;                 // on-screen Y
            uint16_t x = 12;                 // on-screen X
            uint16_t shape_square = 0 << 14; // attr0 bits 14-15
            uint16_t four_bpp = 0 << 13; // attr0 bit 13 = 0 -> 4bpp
            uint16_t affine_off = 0 << 8;  // attr0 bit 8 = 0 -> no affine
            uint16_t attr0 = (y & 0x00FF) | affine_off | four_bpp | shape_square;

            uint16_t size_16 = 1 << 14;      // attr1 bits 14-15 size=1 => 16x16 for square
            uint16_t hflip = 0, vflip = 0;       // attr1 bits 12/13 (only when not affine)
            uint16_t attr1 = (x & 0x01FF) | hflip | vflip | size_16;

            uint16_t tileIndex = 0;          // OBJ base tile number
            uint16_t objPri = 1 << 10;    // OBJ priority=1 (0 is in front)
            uint16_t palBank = 0 << 12;    // 4bpp palette bank=0
            uint16_t attr2 = tileIndex | objPri | palBank;

            // write to OAM entry 0 (8 bytes: A0,A1,A2,pad)
            w16(0, attr0); w16(2, attr1); w16(4, attr2);
            oamBuf.unmap();

            // Entry 1: 8bpp cyan block, 16×16, overlapping BGs & entry 0 to test priority/ties
            {
                uint32_t* o = (uint32_t*)oamBuf.map();
                auto w16 = [&](uint32_t byteOffset, uint16_t v) {
                    o[byteOffset + 0] = (uint8_t)(v & 0xFF);
                    o[byteOffset + 1] = (uint8_t)((v >> 8) & 0xFF);
                    };

                uint16_t y = 18;                 // place within the red region
                uint16_t x = 18;
                uint16_t shape_square = 0 << 14;
                uint16_t four_bpp = 0 << 13; // 4bpp tile 0 we already filled
                uint16_t objModeWin = 2 << 10; // attr0 bits 11:10 = 10b -> OBJ-window
                uint16_t affine_off = 0 << 8;  // not hidden; no affine
                uint16_t attr0 = (y & 0x00FF) | affine_off | objModeWin | four_bpp | shape_square;

                uint16_t size_16 = 1 << 14;      // 16×16
                uint16_t attr1 = (x & 0x01FF) | size_16;

                uint16_t tileIndex = 0;          // any non-zero pixels in this tile create coverage
                uint16_t objPri = 1 << 10;    // priority not used for mask, harmless
                uint16_t palBank = 0 << 12;
                uint16_t attr2 = tileIndex | objPri | palBank;

                // write OAM entry 1
                w16(8 + 0, attr0); w16(8 + 2, attr1); w16(8 + 4, attr2);
                oamBuf.unmap();
            }

            {
                uint32_t* o = (uint32_t*)oamBuf.map();
                auto w16 = [&](uint32_t byteOffset, uint16_t v) {
                    o[byteOffset + 0] = (uint8_t)(v & 0xFF);
                    o[byteOffset + 1] = (uint8_t)((v >> 8) & 0xFF);
                    };

                uint16_t y = 24;
                uint16_t x = 44;
                uint16_t shape_square = 0 << 14;
                uint16_t color_8bpp = 1 << 13;
                uint16_t affine_on = 1 << 8;
                uint16_t double_size = 1 << 9;
                uint16_t objSemi = 1 << 10; // optional: keep semi-transparent to test blending
                uint16_t objMosaic = 1 << 12; // NEW: OBJ mosaic enable

                uint16_t attr0 = (y & 0x00FF) | affine_on | double_size | color_8bpp | objMosaic | objSemi | shape_square;

                uint16_t size_16 = 1 << 14;        // 16x16
                uint16_t affIndex = 0;              // uses OBJ affine set 0 (you already filled)
                uint16_t attr1 = (x & 0x01FF) | size_16 | (affIndex << 9);

                uint16_t baseTile = 16;             // your cyan tiles at 16..19 (8bpp)
                uint16_t objPri = 1 << 10;
                uint16_t attr2 = baseTile | objPri;

                // write OAM entry #2 (offset 16)
                w16(16 + 0, attr0); w16(16 + 2, attr1); w16(16 + 4, attr2);
                oamBuf.unmap();
            }

            {
                uint32_t* o = (uint32_t*)oamBuf.map();
                auto w16 = [&](uint32_t off, uint16_t v) { o[off + 0] = (uint8_t)(v & 0xFF); o[off + 1] = (uint8_t)(v >> 8); };

                // Entry #3 (offset 24): 32x16, 4bpp, base tile 0 -> draws two tile columns
                uint16_t y = 40, x = 24;
                uint16_t shape_wide = 1u << 14;  // wide
                uint16_t size_32x16 = 1u << 14;  // in 'wide' shape, size=1 → 32x16
                uint16_t attr0 = (y & 0xFF) | /*normal*/0 | /*4bpp*/0 | shape_wide;
                uint16_t attr1 = (x & 0x1FF) | size_32x16;
                uint16_t attr2 = 0 | (1u << 10);   // base tile 0, priority=1
                w16(24 + 0, attr0); w16(24 + 2, attr1); w16(24 + 4, attr2);
                oamBuf.unmap();
            }








        }

        // --- Window registers (WIN0/WIN1 + masks) ---
        struct WinStateHost {
            uint32_t win0[4];  // x1,y1,x2,y2 (exclusive)
            uint32_t win1[4];
            uint32_t winIn0, winIn1, winOut, winObj;
        };
        WinStateHost W{};
        W.win0[0] = 8;  W.win0[1] = 8;  W.win0[2] = 112; W.win0[3] = 56;   // a box over the red BG1
        
        W.win1[0] = 0;  W.win1[1] = 0;  W.win1[2] = 0;   W.win1[3] = 0;    // disabled

        // bit layout: 0=BG0,1=BG1,2=BG2,3=BG3,4=OBJ,5=ColorEffect
        W.winIn0 = (1u << 0) | (1u << 1) | (1u << 4) | (1u << 5); // inside WIN0: allow BG1 + OBJ + ColorEffect
        W.winOut = 0x1Fu;                       // outside: BG0..BG3 + OBJ (no ColorEffect)
        W.winIn1 = 0u;                          // unused
        W.winObj = (1u << 0) | (1u << 5);           // OBJ-window: allow BG0 + ColorEffect (nice demo cut-out)

        void* wp = winBuf.map();  std::memcpy(wp, &W, sizeof(W));  winBuf.unmap();

        struct FxRegsHost { uint32_t bldcnt, bldalpha, bldy, mosaic; } FX{};

        FX.bldcnt = (1u << 1) | (2u << 6); // mode=10b brighten
        FX.bldalpha = (8u) | (8u << 8);    // EVA=8, EVB=8 (50/50)
        FX.bldy = 8u;
        uint32_t bgH = 3, bgV = 3;   // size = N+1 → 4x4 for BGs
        uint32_t objH = 3, objV = 3; // 4x4 for OBJs
        FX.mosaic = (bgH & 0xF) | ((bgV & 0xF) << 4) | ((objH & 0xF) << 8) | ((objV & 0xF) << 12);

        void* fxp = fxBuf.map();  std::memcpy(fxp, &FX, sizeof(FX));  fxBuf.unmap();

        struct ScanlineHost {
            uint32_t hofs[4], vofs[4];
            uint32_t win0x1, win0x2, _pad0, _pad1;
            uint32_t win1x1, win1x2, _pad2, _pad3;
            uint32_t bldcnt, bldalpha, bldy, flags; // flags=0 → no per-line overrides
        };
        {
            std::vector<ScanlineHost> lines(160, {});
            for (int y = 0; y < 160; ++y) {
                // base
                lines[y].hofs[0] = hofs0;  lines[y].vofs[0] = vofs0;
                lines[y].hofs[1] = hofs1;  lines[y].vofs[1] = vofs1;
                lines[y].hofs[2] = 0;      lines[y].vofs[2] = 0;
                lines[y].hofs[3] = 0;      lines[y].vofs[3] = 0;

                // wavy BG0 scanline scroll
                float phase = float(y) * 3.14159265f / 16.0f;
                lines[y].hofs[0] = hofs0 + (int)(4.0f * std::sin(phase));

                // keep window slit constant for now (but wire x1/x2 in case you try HBlank splits later)
                lines[y].win0x1 = 8; lines[y].win0x2 = 112;

                // let global FX stand, we just override scroll:
                lines[y].bldcnt = 0; lines[y].bldalpha = 0; lines[y].bldy = 0;

                lines[y].flags = 1u; // bit0=scroll override enabled
            }
            void* sp = scanBuf.map(); std::memcpy(sp, lines.data(), lines.size() * sizeof(ScanlineHost)); scanBuf.unmap();

        }

        // Affine: identity for all BGs (you can rotate later)
        struct AffineParamHost { int32_t refX, refY, pa, pb, pc, pd; };
        auto fx8 = [](float f)->int32_t { return (int32_t)std::lround(f * 256.0f); };

        float deg = 30.0f;
        float scale = 0.75f;
        float rad = deg * 3.14159265358979323846f / 180.0f;

        float cs = std::cos(rad) * scale;
        float sn = std::sin(rad) * scale;

        int32_t pa = fx8(cs), pb = fx8(-sn);
        int32_t pc = fx8(sn), pd = fx8(cs);

        // Pivot around screen center (x0,y0) → BG center (u0,v0)
        int x0 = 120, y0 = 80;
        int u0 = 32 * 8 / 2, v0 = 32 * 8 / 2;
        int32_t refX = (u0 << 8) - pa * x0 - pb * y0;
        int32_t refY = (v0 << 8) - pc * x0 - pd * y0;

        AffineParamHost A[4]{};
        A[2] = { refX, refY, pa, pb, pc, pd };
        // memcpy to affBuf (binding 9)


        void* ap = affBuf.map(); std::memcpy(ap, A, sizeof(A)); affBuf.unmap();

        // OBJ‑affine sets (32). Use set 0 rotated so it’s visible.
        struct ObjAffHost { int32_t pa, pb, pc, pd; };

        ObjAffHost OA[32];
        for (int i = 0; i < 32; ++i) OA[i] = { fx8(1.f), 0, 0, fx8(1.f) }; // identity

        {   // inner scope to avoid name collisions with BG‑affine temps
            float degO = 30.0f, scaleO = 1.0f;
            float radO = degO * 3.14159265358979323846f / 180.0f;
            float csO = std::cos(radO) * scaleO;
            float snO = std::sin(radO) * scaleO;
            OA[0] = { fx8(csO), fx8(-snO), fx8(snO), fx8(csO) };  // set 0 rotates
        }

        void* oap = objAffBuf.map();
        std::memcpy(oap, OA, sizeof(OA));
        objAffBuf.unmap();

    }




        {
            const uint32_t BG_FLAG_AFFINE = 1u;
            const uint32_t BG_FLAG_WRAP = 2u;
            const uint32_t BG_FLAG_MOSAIC = 4u;   // NEW



            BGParamHost params[4] = {
    { charBase0, screenBase0, hofs0, vofs0, 2, 1, 0, 0 },                                   // BG0 text
    { charBase1, screenBase1, hofs1, vofs1, 1, 1, BG_FLAG_MOSAIC, 0 },                      // BG1 text + mosaic
    { charBase2, screenBase2, 0, 0, 1, 1, BG_FLAG_AFFINE | BG_FLAG_WRAP /*|BG_FLAG_MOSAIC*/, 0 }, // BG2 affine (optionally mosaic too)
    { charBase3, screenBase3, 0, 0, 3, 0, 0, 0 },
            };






            void* p = bgBuf.map();
            std::memcpy(p, params, sizeof(params));
            bgBuf.unmap();
        }


        // 6) Descriptor set layout
        VkDescriptorSetLayoutBinding bOut{ };
        bOut.binding = 0; bOut.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; bOut.descriptorCount = 1;
        bOut.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding bVRAM{ };
        bVRAM.binding = 1; bVRAM.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; bVRAM.descriptorCount = 1;
        bVRAM.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding bPAL{ };
        bPAL.binding = 2; bPAL.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; bPAL.descriptorCount = 1;
        bPAL.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding bBG{};
        bBG.binding = 3;
        bBG.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bBG.descriptorCount = 1;
        bBG.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding bPALOBJ{};
        bPALOBJ.binding = 4; bPALOBJ.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bPALOBJ.descriptorCount = 1; bPALOBJ.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding bOAM{};
        bOAM.binding = 5; bOAM.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bOAM.descriptorCount = 1; bOAM.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding bWIN{};
        bWIN.binding = 6; bWIN.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bWIN.descriptorCount = 1; bWIN.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding bFX{};
        bFX.binding = 7; bFX.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bFX.descriptorCount = 1; bFX.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding bSCAN{};
        bSCAN.binding = 8; bSCAN.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bSCAN.descriptorCount = 1; bSCAN.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding bAFF{};
        bAFF.binding = 9; bAFF.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bAFF.descriptorCount = 1; bAFF.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding bOA{};
        bOA.binding = 10; bOA.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bOA.descriptorCount = 1; bOA.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;


        VkDescriptorSetLayoutBinding bindings[11] = { bOut, bVRAM, bPAL, bBG, bPALOBJ, bOAM, bWIN, bFX, bSCAN, bAFF, bOA };
        VkDescriptorSetLayoutCreateInfo dsli{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        dsli.bindingCount = 11;
        dsli.pBindings = bindings;

        VkDescriptorSetLayout dsl{};
        vkCheck(vkCreateDescriptorSetLayout(device, &dsli, nullptr, &dsl), "vkCreateDescriptorSetLayout");

        // 7) Pipeline layout (push constants)
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(PushConsts);

        VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &dsl;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcr;

        VkPipelineLayout pipelineLayout{};
        vkCheck(vkCreatePipelineLayout(device, &plci, nullptr, &pipelineLayout), "vkCreatePipelineLayout");

        // 8) Shader module
        auto spirv = readFile(SHADER_SPV_PATH);
        VkShaderModuleCreateInfo smci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        smci.codeSize = spirv.size();
        smci.pCode = reinterpret_cast<const uint32_t*>(spirv.data());

        VkShaderModule shader{};
        vkCheck(vkCreateShaderModule(device, &smci, nullptr, &shader), "vkCreateShaderModule");

        // 9) Compute pipeline
        VkPipelineShaderStageCreateInfo ssci{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        ssci.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        ssci.module = shader;
        ssci.pName = "main";

        VkComputePipelineCreateInfo cpci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cpci.stage = ssci;
        cpci.layout = pipelineLayout;

        VkPipeline pipeline{};
        vkCheck(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline), "vkCreateComputePipelines");

        // 10) Descriptor pool + set
        VkDescriptorPoolSize poolSizes[1] = { { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 11 } };
        VkDescriptorPoolCreateInfo dpci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = poolSizes;

        VkDescriptorPool pool{};
        vkCheck(vkCreateDescriptorPool(device, &dpci, nullptr, &pool), "vkCreateDescriptorPool");

        VkDescriptorSetAllocateInfo dsai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        dsai.descriptorPool = pool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &dsl;

        VkDescriptorSet dset{};
        vkCheck(vkAllocateDescriptorSets(device, &dsai, &dset), "vkAllocateDescriptorSets");

        VkDescriptorBufferInfo outInfo{ outBuf.buffer, 0, outBuf.size };
        VkDescriptorBufferInfo vramInfo{ vramBuf.buffer, 0, vramBuf.size };
        VkDescriptorBufferInfo palInfo{ palBuf.buffer,  0, palBuf.size };
        VkDescriptorBufferInfo bgInfo{ bgBuf.buffer,   0, bgBuf.size };
        VkDescriptorBufferInfo palObjInfo{ palObjBuf.buffer, 0, palObjBuf.size };
        VkDescriptorBufferInfo oamInfo{ oamBuf.buffer,    0, oamBuf.size };
        VkDescriptorBufferInfo winInfo{ winBuf.buffer, 0, winBuf.size };
        VkDescriptorBufferInfo fxInfo{ fxBuf.buffer,  0, fxBuf.size };
        VkDescriptorBufferInfo scanInfo{ scanBuf.buffer, 0, scanBuf.size };
        VkDescriptorBufferInfo affInfo{ affBuf.buffer, 0, affBuf.size };
        VkDescriptorBufferInfo oaInfo{ objAffBuf.buffer, 0, objAffBuf.size };

        VkWriteDescriptorSet writes[11]{};
        for (int i = 0; i < 11; ++i) writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

        writes[0].dstSet = dset; writes[0].dstBinding = 0; writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[0].pBufferInfo = &outInfo;

        writes[1].dstSet = dset; writes[1].dstBinding = 1; writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[1].pBufferInfo = &vramInfo;

        writes[2].dstSet = dset; writes[2].dstBinding = 2; writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[2].pBufferInfo = &palInfo;

        writes[3].dstSet = dset; writes[3].dstBinding = 3; writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[3].pBufferInfo = &bgInfo;

        writes[4].dstSet = dset; writes[4].dstBinding = 4; writes[4].descriptorCount = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[4].pBufferInfo = &palObjInfo;

        writes[5].dstSet = dset; writes[5].dstBinding = 5; writes[5].descriptorCount = 1;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[5].pBufferInfo = &oamInfo;

        writes[6].dstSet = dset; writes[6].dstBinding = 6; writes[6].descriptorCount = 1;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[6].pBufferInfo = &winInfo;

        writes[7].dstSet = dset; writes[7].dstBinding = 7; writes[7].descriptorCount = 1;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[7].pBufferInfo = &fxInfo;

        writes[8].dstSet = dset; writes[8].dstBinding = 8; writes[8].descriptorCount = 1;
        writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[8].pBufferInfo = &scanInfo;

        writes[9].dstSet = dset; writes[9].dstBinding = 9; writes[9].descriptorCount = 1;
        writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[9].pBufferInfo = &affInfo;

        writes[10].dstSet = dset; writes[10].dstBinding = 10; writes[10].descriptorCount = 1;
        writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[10].pBufferInfo = &oaInfo;

        vkUpdateDescriptorSets(device, 11, writes, 0, nullptr);

        // 11) Command pool/buffer
        VkCommandPoolCreateInfo cpci2{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        cpci2.queueFamilyIndex = queueFamilyIndex;
        VkCommandPool cmdPool{};
        vkCheck(vkCreateCommandPool(device, &cpci2, nullptr, &cmdPool), "vkCreateCommandPool");

        VkCommandBufferAllocateInfo cbai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cbai.commandPool = cmdPool;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        VkCommandBuffer cmd{};
        vkCheck(vkAllocateCommandBuffers(device, &cbai, &cmd), "vkAllocateCommandBuffers");

        // 12) Record
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkCheck(vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &dset, 0, nullptr);

        PushConsts pc{ FB_W, FB_H, mapWidth, mapHeight, objCharBase, 0u }; // 0=2D, 1=1D
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConsts), &pc);



        // 8x8 local size -> dispatch 30x20 groups for 240x160
        vkCmdDispatch(cmd, (FB_W + 7) / 8, (FB_H + 7) / 8, 1);

        // Barrier to ensure shader writes are visible to host read
        VkMemoryBarrier mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &mb, 0, nullptr, 0, nullptr);

        vkCheck(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

        // 13) Submit + wait
        VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkFence fence{};
        vkCheck(vkCreateFence(device, &fci, nullptr, &fence), "vkCreateFence");

        VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkCheck(vkQueueSubmit(queue, 1, &si, fence), "vkQueueSubmit");
        vkCheck(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");

        // 14) Read back and write PPM
        {
            std::vector<uint32_t> rgba(outPixels);
            void* p = outBuf.map();
            std::memcpy(rgba.data(), p, outPixels * sizeof(uint32_t));
            outBuf.unmap();

            std::ofstream ppm("hello_frame.ppm", std::ios::binary);
            ppm << "P6\n" << FB_W << " " << FB_H << "\n255\n";
            for (size_t i = 0; i < rgba.size(); ++i) {
                uint32_t v = rgba[i];
                unsigned char r = (unsigned char)((v >> 0) & 0xFF);
                unsigned char g = (unsigned char)((v >> 8) & 0xFF);
                unsigned char b = (unsigned char)((v >> 16) & 0xFF);
                ppm.put(r); ppm.put(g); ppm.put(b);
            }
            std::cout << "Wrote hello_frame.ppm\n";
        }

        // Cleanup
        vkDestroyFence(device, fence, nullptr);
        vkDestroyCommandPool(device, cmdPool, nullptr);
        vkDestroyDescriptorPool(device, pool, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyShaderModule(device, shader, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, dsl, nullptr);

        outBuf.destroy();
        vramBuf.destroy();
        palBuf.destroy();

        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 0;
    }
   
