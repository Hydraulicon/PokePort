#include "agb_vk.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <cstring>
#include <stdexcept>
#include <fstream>
#include <iostream>

// ---------- Compile-time contract (from your original main.cpp) ----------
#ifndef SHADER_SPV_PATH
#define SHADER_SPV_PATH "compose_frame.comp.spv"
#endif

// Descriptor bindings: 0..10, exactly in shader order.
// 0: out, 1: vram, 2: palBG, 3: bgParams, 4: palOBJ, 5: oam,
// 6: win, 7: fx, 8: scan, 9: bgAff, 10: objAff  (matches your program).  :contentReference[oaicite:3]{index=3}

// Buffer sizes (bytes) — identical to your program’s allocations.  :contentReference[oaicite:4]{index=4}
static constexpr VkDeviceSize VRAM_BYTES = 96 * 1024;        // but stored as uint-per-byte
static constexpr VkDeviceSize PAL_BG_BYTES = 1024;             // uint-per-byte
static constexpr VkDeviceSize PAL_OBJ_BYTES = 512;              // uint-per-byte
static constexpr VkDeviceSize OAM_BYTES = 1024;             // uint-per-byte
static constexpr VkDeviceSize WIN_BYTES = 64;               // raw bytes
static constexpr VkDeviceSize FX_BYTES = 16;               // raw bytes (3 dwords padded)  :contentReference[oaicite:5]{index=5}
static constexpr VkDeviceSize SCAN_BYTES = 160 * 80;         // raw bytes (160 lines * ~80B) :contentReference[oaicite:6]{index=6}
static constexpr VkDeviceSize BG_PARAMS_U32 = 4 * 8;            // 32 u32’s
static constexpr VkDeviceSize BG_AFF_I32 = 4 * 6;            // 24 i32’s
static constexpr VkDeviceSize OBJ_AFF_I32 = 32 * 4;           // 128 i32’s

// The original sample fixed FB to 240x160 and allocated outBuf accordingly.  :contentReference[oaicite:7]{index=7}
static constexpr uint32_t DEFAULT_FB_W = 240;
static constexpr uint32_t DEFAULT_FB_H = 160;

// ---------- small local helpers (implementation-only) ----------
static void vkCheck(VkResult r, const char* what) {
    if (r != VK_SUCCESS) {
        std::cerr << "Vulkan error: " << what << " (" << int(r) << ")\n";
        std::terminate(); // same spirit as your prototype (fail fast).  :contentReference[oaicite:8]{index=8}
    }
}
static std::vector<char> readFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error(std::string("Cannot open file: ") + path);
    f.seekg(0, std::ios::end);
    size_t n = size_t(f.tellg());
    f.seekg(0);
    std::vector<char> buf(n);
    f.read(buf.data(), n);
    return buf;
}
static uint32_t findMemoryType(VkPhysicalDevice phys, uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("No suitable memory type.");
}
struct Buffer {
    VkDevice device{};
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    VkDeviceSize size{};

    void create(VkPhysicalDevice phys, VkDevice dev, VkDeviceSize sz,
        VkBufferUsageFlags usage, VkMemoryPropertyFlags props) {
        device = dev; size = sz;
        VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bi.size = sz; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCheck(vkCreateBuffer(dev, &bi, nullptr, &buffer), "vkCreateBuffer");
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(dev, buffer, &req);
        VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = findMemoryType(phys, req.memoryTypeBits, props);
        vkCheck(vkAllocateMemory(dev, &ai, nullptr, &memory), "vkAllocateMemory");
        vkCheck(vkBindBufferMemory(dev, buffer, memory, 0), "vkBindBufferMemory");
    }
    void* map() { void* p = nullptr; vkCheck(vkMapMemory(device, memory, 0, size, 0, &p), "vkMapMemory"); return p; }
    void  unmap() { vkUnmapMemory(device, memory); }
    void  destroy() {
        if (buffer) vkDestroyBuffer(device, buffer, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
        buffer = VK_NULL_HANDLE; memory = VK_NULL_HANDLE; device = VK_NULL_HANDLE; size = 0;
    }
};

// ---------- Opaque context (all Vulkan state lives here) ----------
struct AgbVkCtx {
    // Core
    VkInstance       instance{};
    VkPhysicalDevice phys{};
    uint32_t         qFamily{};
    VkDevice         dev{};
    VkQueue          queue{};

    // Buffers (11 SSBOs)
    Buffer outBuf, vramBuf, palBuf, bgBuf, palObjBuf, oamBuf,
        winBuf, fxBuf, scanBuf, affBuf, objAffBuf;

    // Descriptors/pipeline
    VkDescriptorSetLayout dsl{};
    VkPipelineLayout      pl{};
    VkShaderModule        shader{};
    VkPipeline            pipe{};
    VkDescriptorPool      pool{};
    VkDescriptorSet       dset{};

    // Commands/sync
    VkCommandPool   cmdPool{};
    VkCommandBuffer cmd{};
    VkFence         fence{};
};

// ---------- Public API implementation ----------
extern "C" {

AgbVkCtx* agbvk_create(void) {
    auto* c = new AgbVkCtx{};

    // 1) Instance  (matches your program)
    VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "agbvk";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "none";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    vkCheck(vkCreateInstance(&ici, nullptr, &c->instance), "vkCreateInstance");  // :contentReference[oaicite:9]{index=9}

    // 2) Physical device + compute queue family
    uint32_t pdCount = 0;
    vkEnumeratePhysicalDevices(c->instance, &pdCount, nullptr);
    if (pdCount == 0) { throw std::runtime_error("No Vulkan devices."); }      // :contentReference[oaicite:10]{index=10}
    std::vector<VkPhysicalDevice> pds(pdCount);
    vkEnumeratePhysicalDevices(c->instance, &pdCount, pds.data());
    for (auto pd : pds) {
        uint32_t n = 0; vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, nullptr);
        std::vector<VkQueueFamilyProperties> qfp(n);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, qfp.data());
        for (uint32_t i = 0; i < n; ++i) {
            if (qfp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                c->phys = pd; c->qFamily = i; break;
            }
        }
        if (c->phys) break;
    }
    if (!c->phys) throw std::runtime_error("No compute-capable queue.");       // :contentReference[oaicite:11]{index=11}

    // 3) Device + queue
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = c->qFamily; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    vkCheck(vkCreateDevice(c->phys, &dci, nullptr, &c->dev), "vkCreateDevice");
    vkGetDeviceQueue(c->dev, c->qFamily, 0, &c->queue);

    // 4) Buffers (allocations identical to your program)  :contentReference[oaicite:12]{index=12}
    const VkMemoryPropertyFlags HOST =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const VkBufferUsageFlags SSBO = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    // out framebuffer — initially sized for 240x160; see readback note below.
    c->outBuf.create(c->phys, c->dev, DEFAULT_FB_W * DEFAULT_FB_H * sizeof(uint32_t), SSBO, HOST);

    // uint-per-byte storages: vram, palBG, palOBJ, oam
    c->vramBuf.create(c->phys, c->dev, VRAM_BYTES * sizeof(uint32_t), SSBO, HOST);
    c->palBuf.create(c->phys, c->dev, PAL_BG_BYTES * sizeof(uint32_t), SSBO, HOST);
    c->palObjBuf.create(c->phys, c->dev, PAL_OBJ_BYTES * sizeof(uint32_t), SSBO, HOST);
    c->oamBuf.create(c->phys, c->dev, OAM_BYTES * sizeof(uint32_t), SSBO, HOST);

    // raw byte storages: win, fx, scan; typed: bgParams (u32), bgAff (i32), objAff (i32)
    c->winBuf.create(c->phys, c->dev, WIN_BYTES, SSBO, HOST);
    c->fxBuf.create(c->phys, c->dev, FX_BYTES, SSBO, HOST);
    c->scanBuf.create(c->phys, c->dev, SCAN_BYTES, SSBO, HOST);
    c->bgBuf.create(c->phys, c->dev, BG_PARAMS_U32 * sizeof(uint32_t), SSBO, HOST);
    c->affBuf.create(c->phys, c->dev, BG_AFF_I32 * sizeof(int32_t), SSBO, HOST);
    c->objAffBuf.create(c->phys, c->dev, OBJ_AFF_I32 * sizeof(int32_t), SSBO, HOST);

    // 5/6) Descriptor set layout (11 bindings), pipeline layout (push-consts)  :contentReference[oaicite:13]{index=13}
    VkDescriptorSetLayoutBinding binds[11]{};
    auto setB = [&](uint32_t idx) {
        binds[idx].binding = idx;
        binds[idx].descriptorCount = 1;
        binds[idx].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binds[idx].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        };
    for (uint32_t i = 0; i < 11; ++i) setB(i);

    VkDescriptorSetLayoutCreateInfo dsli{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dsli.bindingCount = 11; dsli.pBindings = binds;
    vkCheck(vkCreateDescriptorSetLayout(c->dev, &dsli, nullptr, &c->dsl), "vkCreateDescriptorSetLayout");

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(uint32_t) * 6; // fbW,fbH,mapW,mapH,objCharBase,objMapMode
    VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount = 1; plci.pSetLayouts = &c->dsl;
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    vkCheck(vkCreatePipelineLayout(c->dev, &plci, nullptr, &c->pl), "vkCreatePipelineLayout");

    // 7/8/9) Shader module and compute pipeline   :contentReference[oaicite:14]{index=14}
    auto spirv = readFile(SHADER_SPV_PATH);
    VkShaderModuleCreateInfo smci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smci.codeSize = spirv.size();
    smci.pCode = reinterpret_cast<const uint32_t*>(spirv.data());
    vkCheck(vkCreateShaderModule(c->dev, &smci, nullptr, &c->shader), "vkCreateShaderModule");

    VkPipelineShaderStageCreateInfo ssci{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    ssci.stage = VK_SHADER_STAGE_COMPUTE_BIT; ssci.module = c->shader; ssci.pName = "main";
    VkComputePipelineCreateInfo cpci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    cpci.stage = ssci; cpci.layout = c->pl;
    vkCheck(vkCreateComputePipelines(c->dev, VK_NULL_HANDLE, 1, &cpci, nullptr, &c->pipe),
        "vkCreateComputePipelines");

    // 10) Descriptor pool + set + writes  :contentReference[oaicite:15]{index=15}
    VkDescriptorPoolSize poolSizes[1] = { { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 11 } };
    VkDescriptorPoolCreateInfo dpci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpci.maxSets = 1; dpci.poolSizeCount = 1; dpci.pPoolSizes = poolSizes;
    vkCheck(vkCreateDescriptorPool(c->dev, &dpci, nullptr, &c->pool), "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo dsai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsai.descriptorPool = c->pool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &c->dsl;
    vkCheck(vkAllocateDescriptorSets(c->dev, &dsai, &c->dset), "vkAllocateDescriptorSets");

    VkDescriptorBufferInfo info[11] = {
        { c->outBuf.buffer,    0, c->outBuf.size },
        { c->vramBuf.buffer,   0, c->vramBuf.size },
        { c->palBuf.buffer,    0, c->palBuf.size },
        { c->bgBuf.buffer,     0, c->bgBuf.size },
        { c->palObjBuf.buffer, 0, c->palObjBuf.size },
        { c->oamBuf.buffer,    0, c->oamBuf.size },
        { c->winBuf.buffer,    0, c->winBuf.size },
        { c->fxBuf.buffer,     0, c->fxBuf.size },
        { c->scanBuf.buffer,   0, c->scanBuf.size },
        { c->affBuf.buffer,    0, c->affBuf.size },
        { c->objAffBuf.buffer, 0, c->objAffBuf.size },
    };
    VkWriteDescriptorSet writes[11]{};
    for (uint32_t i = 0; i < 11; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = c->dset;
        writes[i].dstBinding = i;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].descriptorCount = 1;
        writes[i].pBufferInfo = &info[i];
    }
    vkUpdateDescriptorSets(c->dev, 11, writes, 0, nullptr);

    // 11) Command pool/buffer + fence   :contentReference[oaicite:16]{index=16}
    VkCommandPoolCreateInfo cpci2{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpci2.queueFamilyIndex = c->qFamily;
    vkCheck(vkCreateCommandPool(c->dev, &cpci2, nullptr, &c->cmdPool), "vkCreateCommandPool");

    VkCommandBufferAllocateInfo cbai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbai.commandPool = c->cmdPool; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
    vkCheck(vkAllocateCommandBuffers(c->dev, &cbai, &c->cmd), "vkAllocateCommandBuffers");

    VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    vkCheck(vkCreateFence(c->dev, &fci, nullptr, &c->fence), "vkCreateFence");

    return c;
}

// ---- Upload helpers ----------------------------------------------------
static void write_bytes_as_u32(Buffer& buf, const void* srcBytes, size_t countBytes) {
    // SSBO is laid out as "uint-per-byte" (your program wrote each byte into a u32 slot).  :contentReference[oaicite:17]{index=17}
    auto* dst = static_cast<uint32_t*>(buf.map());
    const auto* src = static_cast<const uint8_t*>(srcBytes);
    for (size_t i = 0; i < countBytes; ++i) dst[i] = src[i];
    buf.unmap();
}
static void write_bytes(Buffer& buf, const void* srcBytes, size_t countBytes) {
    void* dst = buf.map();
    std::memcpy(dst, srcBytes, countBytes);
    buf.unmap();
}
static void write_u32(Buffer& buf, const uint32_t* srcU32, size_t countU32) {
    void* dst = buf.map();
    std::memcpy(dst, srcU32, countU32 * sizeof(uint32_t));
    buf.unmap();
}
static void write_i32(Buffer& buf, const int32_t* srcI32, size_t countI32) {
    void* dst = buf.map();
    std::memcpy(dst, srcI32, countI32 * sizeof(int32_t));
    buf.unmap();
}

void agbvk_upload_vram(AgbVkCtx* c, const void* bytes, size_t n) { write_bytes_as_u32(c->vramBuf, bytes, n); }
void agbvk_upload_pal_bg(AgbVkCtx* c, const void* bytes, size_t n) { write_bytes_as_u32(c->palBuf, bytes, n); }
void agbvk_upload_bg_params(AgbVkCtx* c, const uint32_t* u32, size_t n) { write_u32(c->bgBuf, u32, n); }
void agbvk_upload_pal_obj(AgbVkCtx* c, const void* bytes, size_t n) { write_bytes_as_u32(c->palObjBuf, bytes, n); }
void agbvk_upload_oam(AgbVkCtx* c, const void* bytes, size_t n) { write_bytes_as_u32(c->oamBuf, bytes, n); }
void agbvk_upload_win(AgbVkCtx* c, const void* bytes, size_t n) { write_bytes(c->winBuf, bytes, n); }
void agbvk_upload_fx(AgbVkCtx* c, const void* bytes, size_t n) { write_bytes(c->fxBuf, bytes, n); }
void agbvk_upload_scanline(AgbVkCtx* c, const void* bytes, size_t n) { write_bytes(c->scanBuf, bytes, n); }
void agbvk_upload_bg_aff(AgbVkCtx* c, const int32_t* i32, size_t n) { write_i32(c->affBuf, i32, n); }
void agbvk_upload_obj_aff(AgbVkCtx* c, const int32_t* i32, size_t n) { write_i32(c->objAffBuf, i32, n); }

// ---- Dispatch & readback -----------------------------------------------
void agbvk_dispatch_frame(AgbVkCtx* c,
    uint32_t fbW, uint32_t fbH,
    uint32_t mapW, uint32_t mapH,
    uint32_t objCharBase, uint32_t objMapMode)
{
    // Record fresh each call (simple, mirrors your single-shot recording).  :contentReference[oaicite:18]{index=18}
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkCheck(vkBeginCommandBuffer(c->cmd, &bi), "vkBeginCommandBuffer");

    vkCmdBindPipeline(c->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c->pipe);
    vkCmdBindDescriptorSets(c->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c->pl, 0, 1, &c->dset, 0, nullptr);

    // Push-constants layout matches your struct {fbW,fbH,mapW,mapH,objCharBase,objMapMode}. :contentReference[oaicite:19]{index=19}
    uint32_t pc[6] = { fbW, fbH, mapW, mapH, objCharBase, objMapMode };
    vkCmdPushConstants(c->cmd, c->pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);

    const uint32_t gx = (fbW + 7) / 8;
    const uint32_t gy = (fbH + 7) / 8;
    vkCmdDispatch(c->cmd, gx, gy, 1);                                           // :contentReference[oaicite:20]{index=20}

    // Ensure shader writes visible to host
    VkMemoryBarrier mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(c->cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &mb, 0, nullptr, 0, nullptr);

    vkCheck(vkEndCommandBuffer(c->cmd), "vkEndCommandBuffer");

    // Submit + wait (reuse fence)
    vkCheck(vkResetFences(c->dev, 1, &c->fence), "vkResetFences");
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &c->cmd;
    vkCheck(vkQueueSubmit(c->queue, 1, &si, c->fence), "vkQueueSubmit");
    vkCheck(vkWaitForFences(c->dev, 1, &c->fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
}

void agbvk_readback_rgba(AgbVkCtx* c, uint32_t* dstRGBA, size_t pixelCount) {
    // NOTE: outBuf was sized for 240x160 like your sample; callers should pass pixelCount=fbW*fbH (240*160).  :contentReference[oaicite:21]{index=21}
    const size_t bytes = pixelCount * sizeof(uint32_t);
    void* p = c->outBuf.map();
    std::memcpy(dstRGBA, p, bytes);
    c->outBuf.unmap();
}

void agbvk_destroy(AgbVkCtx* c) {
    if (!c) return;

    vkDestroyFence(c->dev, c->fence, nullptr);
    vkDestroyCommandPool(c->dev, c->cmdPool, nullptr);

    vkDestroyDescriptorPool(c->dev, c->pool, nullptr);
    vkDestroyPipeline(c->dev, c->pipe, nullptr);
    vkDestroyShaderModule(c->dev, c->shader, nullptr);
    vkDestroyPipelineLayout(c->dev, c->pl, nullptr);
    vkDestroyDescriptorSetLayout(c->dev, c->dsl, nullptr);

    c->outBuf.destroy();
    c->vramBuf.destroy();
    c->palBuf.destroy();
    c->bgBuf.destroy();
    c->palObjBuf.destroy();
    c->oamBuf.destroy();
    c->winBuf.destroy();
    c->fxBuf.destroy();
    c->scanBuf.destroy();
    c->affBuf.destroy();
    c->objAffBuf.destroy();

    vkDestroyDevice(c->dev, nullptr);
    vkDestroyInstance(c->instance, nullptr);
    delete c;
}

} // extern "C"
