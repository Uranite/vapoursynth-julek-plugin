#include <cmath>

#include "shared.h"

struct BUTTERAUGLIData final {
    VSNode* node;
    VSNode* node2;
    const VSVideoInfo* vi;
    jxl::ButteraugliParams ba_params;
    bool distmap;
    bool heatmap;
    bool linput;

    void (*hmap)(VSFrame* dst, const jxl::ImageF& heatmap, int width, int height, const ptrdiff_t stride, const VSAPI* vsapi) noexcept;
    void (*fill)(jxl::CodecInOut& ref, jxl::CodecInOut& dist, const VSFrame* src1, const VSFrame* src2, int width, int height, const ptrdiff_t stride, const VSAPI* vsapi) noexcept;
};

template <typename pixel_t, typename jxl_t, bool linput>
extern void fill_image(jxl::CodecInOut& ref, jxl::CodecInOut& dist, const VSFrame* src1, const VSFrame* src2, int width, int height, const ptrdiff_t stride, const VSAPI* vsapi) noexcept;
template <bool linput>
extern void fill_imageF(jxl::CodecInOut& ref, jxl::CodecInOut& dist, const VSFrame* src1, const VSFrame* src2, int width, int height, const ptrdiff_t stride, const VSAPI* vsapi) noexcept;

template <typename pixel_t, typename jxl_t, int peak>
static void heatmap(VSFrame* dst, const jxl::ImageF& heatmap, int width, int height, const ptrdiff_t stride, const VSAPI* vsapi) noexcept {
    jxl::Image3F buff = jxl::CreateHeatMapImage(heatmap, jxl::ButteraugliFuzzyInverse(1.5), jxl::ButteraugliFuzzyInverse(0.5));
    jxl_t tmp(width, height);

    for (int i = 0; i < 3; i++) {
        jxl::ImageConvert(buff.Plane(i), peak, &tmp.Plane(i));
        auto dstp{reinterpret_cast<pixel_t*>(vsapi->getWritePtr(dst, i))};
        for (int y = 0; y < height; y++) {
            memcpy(dstp, tmp.ConstPlaneRow(i, y), width * sizeof(pixel_t));
            dstp += stride;
        }
    }
}

static void heatmapF(VSFrame* dst, const jxl::ImageF& heatmap, int width, int height, const ptrdiff_t stride, const VSAPI* vsapi) noexcept {
    jxl::Image3F buff = jxl::CreateHeatMapImage(heatmap, jxl::ButteraugliFuzzyInverse(1.5), jxl::ButteraugliFuzzyInverse(0.5));

    for (int i = 0; i < 3; i++) {
        float* dstp{reinterpret_cast<float*>(vsapi->getWritePtr(dst, i))};
        for (int y = 0; y < height; y++) {
            memcpy(dstp, buff.ConstPlaneRow(i, y), width * sizeof(float));
            dstp += stride;
        }
    }
}

static void compute_norms(const jxl::ImageF& diff_map, float& norm2, float& norm3, float& norm_inf) {
    float sum2 = 0.0f;     // Sum of squares for L2-norm
    float sum3 = 0.0f;     // Sum of cubes for L3-norm
    float max_val = 0.0f;  // For L∞-norm (max)

    const int w = diff_map.xsize();
    const int h = diff_map.ysize();
    const int N = w * h;

    if (N == 0) {
        norm2 = norm3 = norm_inf = 0.0f;
        return;
    }

    for (int y = 0; y < h; ++y) {
        const float* row = diff_map.Row(y);
        for (int x = 0; x < w; ++x) {
            const float val = std::abs(row[x]);
            const float val2 = val * val;

            sum2 += val2;
            sum3 += val2 * val;
            max_val = std::max(max_val, val);
        }
    }

    norm2 = std::sqrt(sum2 / N);              // Root-mean-square (L2-norm)
    norm3 = std::pow(sum3 / N, 1.0f / 3.0f);  // Cubic root of mean of cubes (L3-norm)
    norm_inf = max_val;                       // Maximum absolute value (L∞-norm)
}

static const VSFrame* VS_CC butteraugliGetFrame(int n, int activationReason, void* instanceData, void** frameData, VSFrameContext* frameCtx, VSCore* core, const VSAPI* vsapi) {
    auto d{static_cast<BUTTERAUGLIData*>(instanceData)};

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        vsapi->requestFrameFilter(n, d->node2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame* src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFrame* src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);

        int width = vsapi->getFrameWidth(src2, 0);
        int height = vsapi->getFrameHeight(src2, 0);
        const ptrdiff_t stride = vsapi->getStride(src2, 0) / d->vi->format.bytesPerSample;

        jxl::CodecInOut ref;
        jxl::CodecInOut dist;
        jxl::ImageF diff_map;

        ref.SetSize(width, height);
        dist.SetSize(width, height);

        if (d->linput) {
            ref.metadata.m.color_encoding = jxl::ColorEncoding::LinearSRGB(false);
            dist.metadata.m.color_encoding = jxl::ColorEncoding::LinearSRGB(false);
        } else {
            ref.metadata.m.color_encoding = jxl::ColorEncoding::SRGB(false);
            dist.metadata.m.color_encoding = jxl::ColorEncoding::SRGB(false);
        }

        d->fill(ref, dist, src, src2, width, height, stride, vsapi);

        jxl::ButteraugliDistance(ref.Main(), dist.Main(), d->ba_params, jxl::GetJxlCms(), &diff_map, nullptr);

        float norm2, norm3, norm_inf;
        compute_norms(diff_map, norm2, norm3, norm_inf);

        if (d->distmap) {
            VSVideoFormat fmt;
            if (!vsapi->queryVideoFormat(&fmt, cfGray, stFloat, 32, 0, 0, core)) {
                vsapi->setFilterError("Butteraugli: Failed to create grayscale float format", frameCtx);
                vsapi->freeFrame(src);
                vsapi->freeFrame(src2);
                return nullptr;
            }
            VSFrame* dst = vsapi->newVideoFrame(&fmt, width, height, nullptr, core);
            float* dstp = reinterpret_cast<float*>(vsapi->getWritePtr(dst, 0));
            const ptrdiff_t dst_stride = vsapi->getStride(dst, 0) / sizeof(float);

            for (int y = 0; y < height; y++) {
                const float* row = diff_map.Row(y);
                for (int x = 0; x < width; x++) {
                    dstp[y * dst_stride + x] = row[x];
                }
            }
            VSMap* dstProps = vsapi->getFramePropertiesRW(dst);

            vsapi->mapSetFloat(dstProps, "_BUTTERAUGLI_2Norm", norm2, maReplace);
            vsapi->mapSetFloat(dstProps, "_BUTTERAUGLI_3Norm", norm3, maReplace);
            vsapi->mapSetFloat(dstProps, "_BUTTERAUGLI_INFNorm", norm_inf, maReplace);

            vsapi->freeFrame(src);
            vsapi->freeFrame(src2);
            return dst;
        } else if (d->heatmap) {
            VSFrame* dst = vsapi->newVideoFrame(vsapi->getVideoFrameFormat(src2), width, height, src2, core);
            d->hmap(dst, diff_map, width, height, stride, vsapi);
            VSMap* dstProps = vsapi->getFramePropertiesRW(dst);

            vsapi->mapSetFloat(dstProps, "_BUTTERAUGLI_2Norm", norm2, maReplace);
            vsapi->mapSetFloat(dstProps, "_BUTTERAUGLI_3Norm", norm3, maReplace);
            vsapi->mapSetFloat(dstProps, "_BUTTERAUGLI_INFNorm", norm_inf, maReplace);

            vsapi->freeFrame(src);
            vsapi->freeFrame(src2);
            return dst;
        } else {
            VSFrame* dst = vsapi->copyFrame(src2, core);
            VSMap* dstProps = vsapi->getFramePropertiesRW(dst);

            vsapi->mapSetFloat(dstProps, "_BUTTERAUGLI_2Norm", norm2, maReplace);
            vsapi->mapSetFloat(dstProps, "_BUTTERAUGLI_3Norm", norm3, maReplace);
            vsapi->mapSetFloat(dstProps, "_BUTTERAUGLI_INFNorm", norm_inf, maReplace);

            vsapi->freeFrame(src);
            vsapi->freeFrame(src2);
            return dst;
        }
    }
    return nullptr;
}

static void VS_CC butteraugliFree(void* instanceData, VSCore* core, const VSAPI* vsapi) {
    auto d{reinterpret_cast<BUTTERAUGLIData*>(instanceData)};

    vsapi->freeNode(d->node);
    vsapi->freeNode(d->node2);
    delete d;
}

void VS_CC butteraugliCreate(const VSMap* in, VSMap* out, void* userData, VSCore* core, const VSAPI* vsapi) {
    auto d{std::make_unique<BUTTERAUGLIData>()};
    int err{0};

    VSNode* node = vsapi->mapGetNode(in, "reference", 0, nullptr);
    VSNode* node2 = vsapi->mapGetNode(in, "distorted", 0, nullptr);

    d->node = toRGBS(node, core, vsapi);
    d->node2 = toRGBS(node2, core, vsapi);

    vsapi->freeNode(node);
    vsapi->freeNode(node2);
    d->vi = vsapi->getVideoInfo(d->node);

    d->heatmap = !!vsapi->mapGetInt(in, "heatmap", 0, &err);
    if (err)
        d->heatmap = false;

    d->distmap = !!vsapi->mapGetInt(in, "distmap", 0, &err);
    if (err)
        d->distmap = false;

    if (d->heatmap && d->distmap) {
        vsapi->mapSetError(out, "Butteraugli: 'heatmap' and 'distmap' cannot both be enabled at the same time.");
        vsapi->freeNode(d->node);
        vsapi->freeNode(d->node2);
        return;
    }

    d->linput = !!vsapi->mapGetInt(in, "linput", 0, &err);
    if (err)
        d->linput = false;

    float intensity_target;
    intensity_target = vsapi->mapGetFloatSaturated(in, "intensity_target", 0, &err);
    if (err)
        intensity_target = 80.0f;

    d->ba_params.hf_asymmetry = 1.0f;
    d->ba_params.xmul = 1.0f;
    d->ba_params.intensity_target = intensity_target;

    if (intensity_target <= 0.0f) {
        vsapi->mapSetError(out, "Butteraugli: intensity_target must be greater than 0.0.");
        vsapi->freeNode(d->node);
        vsapi->freeNode(d->node2);
        return;
    }

    if (d->vi->format.colorFamily != cfRGB) {
        vsapi->mapSetError(out, "Butteraugli: the clip must be in RGB format.");
        vsapi->freeNode(d->node);
        vsapi->freeNode(d->node2);
        return;
    }

    int bits = d->vi->format.bitsPerSample;
    if (bits != 8 && bits != 16 && bits != 32) {
        vsapi->mapSetError(out, "Butteraugli: the clip bit depth must be 8, 16, or 32.");
        vsapi->freeNode(d->node);
        vsapi->freeNode(d->node2);
        return;
    }

    if (!vsh::isSameVideoInfo(vsapi->getVideoInfo(d->node2), d->vi)) {
        vsapi->mapSetError(out, "Butteraugli: both clips must have the same format and dimensions.");
        vsapi->freeNode(d->node);
        vsapi->freeNode(d->node2);
        return;
    }

    switch (d->vi->format.bytesPerSample) {
        case 1:
            d->fill = (d->linput) ? fill_image<uint8_t, jxl::Image3B, true> : fill_image<uint8_t, jxl::Image3B, false>;
            d->hmap = heatmap<uint8_t, jxl::Image3B, 255>;
            break;
        case 2:
            d->fill = (d->linput) ? fill_image<uint16_t, jxl::Image3U, true> : fill_image<uint16_t, jxl::Image3U, false>;
            d->hmap = heatmap<uint16_t, jxl::Image3U, 65535>;
            break;
        case 4:
            d->fill = (d->linput) ? fill_imageF<true> : fill_imageF<false>;
            d->hmap = heatmapF;
            break;
    }

    VSVideoInfo vi_out = *d->vi;
    if (d->distmap) {
        VSVideoFormat fmt;
        if (!vsapi->queryVideoFormat(&fmt, cfGray, stFloat, 32, 0, 0, core)) {
            vsapi->mapSetError(out, "Butteraugli: Failed to create grayscale float format");
            vsapi->freeNode(d->node);
            vsapi->freeNode(d->node2);
            return;
        }
        vi_out.format = fmt;
    }

    VSFilterDependency deps[]{{d->node, rpGeneral}, {d->node2, rpGeneral}};
    vsapi->createVideoFilter(out, "Butteraugli", &vi_out, butteraugliGetFrame, butteraugliFree, fmParallel, deps, 2, d.get(), core);
    d.release();
}