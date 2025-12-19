#include "shared.h"

VS_EXTERNAL_API(void)
VapourSynthPluginInit2(VSPlugin* plugin, const VSPLUGINAPI* vspapi) {
    vspapi->configPlugin("com.julek.plugin", "julek", "Julek filters", 4, VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("AGM", "clip:vnode;luma_scaling:float:opt;", "clip:vnode;", agmCreate, nullptr, plugin);
    vspapi->registerFunction("AutoGain", "clip:vnode;planes:int[]:opt;", "clip:vnode;", autogainCreate, nullptr, plugin);
    vspapi->registerFunction("Butteraugli", "reference:vnode;distorted:vnode;distmap:int:opt;heatmap:int:opt;intensity_target:float:opt;linput:int:opt;qnorm:float:opt;", "clip:vnode;", butteraugliCreate, nullptr, plugin);
    vspapi->registerFunction("ColorMap", "clip:vnode;type:int:opt;", "clip:vnode;", colormapCreate, nullptr, plugin);
    vspapi->registerFunction("RFS", "clip_a:vnode;clip_b:vnode;frames:int[];mismatch:int:opt;", "clip:vnode;", rfsCreate, nullptr, plugin);
    vspapi->registerFunction("SSIMULACRA", "reference:vnode;distorted:vnode;feature:int:opt;simple:int:opt;", "clip:vnode;", ssimulacraCreate, nullptr, plugin);
    vspapi->registerFunction("VisualizeDiffs", "clip_a:vnode;clip_b:vnode;auto_gain:int:opt;type:int:opt;", "clip:vnode;", visualizediffsCreate, nullptr, plugin);
}

static void* jpegxl_alloc(void* opaque, size_t size) {
    return malloc(size);
}

static void jpegxl_free(void* opaque, void* address) {
    free(address);
}

JxlMemoryManager* get_memory_manager() {
    static JxlMemoryManager mm = {
        nullptr,
        jpegxl_alloc,
        jpegxl_free};
    return &mm;
}

template <typename pixel_t, bool linput>
void fill_image(jxl::CodecInOut& ref, jxl::CodecInOut& dist, const VSFrame* src1, const VSFrame* src2, int width, int height, const VSAPI* vsapi) noexcept {
    auto tmp1_res = jxl::Image3F::Create(get_memory_manager(), width, height);
    auto tmp2_res = jxl::Image3F::Create(get_memory_manager(), width, height);
    if (!tmp1_res.ok() || !tmp2_res.ok()) return;
    jxl::Image3F tmp1 = std::move(tmp1_res).value_();
    jxl::Image3F tmp2 = std::move(tmp2_res).value_();

    float scale = 1.0f / std::numeric_limits<pixel_t>::max();

    for (int i = 0; i < 3; ++i) {
        auto srcp1{reinterpret_cast<const pixel_t*>(vsapi->getReadPtr(src1, i))};
        auto srcp2{reinterpret_cast<const pixel_t*>(vsapi->getReadPtr(src2, i))};
        const ptrdiff_t stride1 = vsapi->getStride(src1, i) / sizeof(pixel_t);
        const ptrdiff_t stride2 = vsapi->getStride(src2, i) / sizeof(pixel_t);

        for (int y = 0; y < height; ++y) {
            float* VS_RESTRICT row1 = tmp1.PlaneRow(i, y);
            float* VS_RESTRICT row2 = tmp2.PlaneRow(i, y);

            for (int x = 0; x < width; ++x) {
                row1[x] = srcp1[x] * scale;
                row2[x] = srcp2[x] * scale;
            }

            srcp1 += stride1;
            srcp2 += stride2;
        }
    }

    if (!ref.SetFromImage(std::move(tmp1), (linput) ? jxl::ColorEncoding::LinearSRGB(false) : jxl::ColorEncoding::SRGB(false)) ||
        !dist.SetFromImage(std::move(tmp2), (linput) ? jxl::ColorEncoding::LinearSRGB(false) : jxl::ColorEncoding::SRGB(false))) {
        return;
    }
}

template <bool linput>
void fill_imageF(jxl::CodecInOut& ref, jxl::CodecInOut& dist, const VSFrame* src1, const VSFrame* src2, int width, int height, const VSAPI* vsapi) noexcept {
    auto tmp1_res = jxl::Image3F::Create(get_memory_manager(), width, height);
    auto tmp2_res = jxl::Image3F::Create(get_memory_manager(), width, height);
    if (!tmp1_res.ok() || !tmp2_res.ok()) return;
    jxl::Image3F tmp1 = std::move(tmp1_res).value_();
    jxl::Image3F tmp2 = std::move(tmp2_res).value_();

    for (int i = 0; i < 3; ++i) {
        const float* srcp1{reinterpret_cast<const float*>(vsapi->getReadPtr(src1, i))};
        const float* srcp2{reinterpret_cast<const float*>(vsapi->getReadPtr(src2, i))};
        const ptrdiff_t stride1 = vsapi->getStride(src1, i) / sizeof(float);
        const ptrdiff_t stride2 = vsapi->getStride(src2, i) / sizeof(float);

        for (int y = 0; y < height; ++y) {
            memcpy(tmp1.PlaneRow(i, y), srcp1, width * sizeof(float));
            memcpy(tmp2.PlaneRow(i, y), srcp2, width * sizeof(float));

            srcp1 += stride1;
            srcp2 += stride2;
        }
    }

    if (!ref.SetFromImage(std::move(tmp1), (linput) ? jxl::ColorEncoding::LinearSRGB(false) : jxl::ColorEncoding::SRGB(false)) ||
        !dist.SetFromImage(std::move(tmp2), (linput) ? jxl::ColorEncoding::LinearSRGB(false) : jxl::ColorEncoding::SRGB(false))) {
        return;
    }
}

template void fill_image<uint8_t, true>(jxl::CodecInOut& ref, jxl::CodecInOut& dist, const VSFrame* src1, const VSFrame* src2, int width, int height, const VSAPI* vsapi) noexcept;
template void fill_image<uint16_t, true>(jxl::CodecInOut& ref, jxl::CodecInOut& dist, const VSFrame* src1, const VSFrame* src2, int width, int height, const VSAPI* vsapi) noexcept;

template void fill_image<uint8_t, false>(jxl::CodecInOut& ref, jxl::CodecInOut& dist, const VSFrame* src1, const VSFrame* src2, int width, int height, const VSAPI* vsapi) noexcept;
template void fill_image<uint16_t, false>(jxl::CodecInOut& ref, jxl::CodecInOut& dist, const VSFrame* src1, const VSFrame* src2, int width, int height, const VSAPI* vsapi) noexcept;

template void fill_imageF<true>(jxl::CodecInOut& ref, jxl::CodecInOut& dist, const VSFrame* src1, const VSFrame* src2, int width, int height, const VSAPI* vsapi) noexcept;
template void fill_imageF<false>(jxl::CodecInOut& ref, jxl::CodecInOut& dist, const VSFrame* src1, const VSFrame* src2, int width, int height, const VSAPI* vsapi) noexcept;