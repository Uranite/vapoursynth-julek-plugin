#include "shared.h"

struct BUTTERAUGLIData final {
    VSNode* node;
    VSNode* node2;
    const VSVideoInfo* vi;
    jxl::ButteraugliParams ba_params;
    double qnorm_val;
    bool distmap;
    bool heatmap;
    bool linput;

    void (*hmap)(VSFrame* dst, const jxl::ImageF& heatmap, int width, int height, const VSAPI* vsapi) noexcept;
    void (*fill)(jxl::CodecInOut& ref, jxl::CodecInOut& dist, const VSFrame* src1, const VSFrame* src2, int width, int height, const VSAPI* vsapi) noexcept;
};

template <typename pixel_t, int peak>
static void heatmap(VSFrame* dst, const jxl::ImageF& heatmap, int width, int height, const VSAPI* vsapi) noexcept {
    auto buff_res = jxl::CreateHeatMapImage(heatmap, jxl::ButteraugliFuzzyInverse(1.5), jxl::ButteraugliFuzzyInverse(0.5));
    if (!buff_res.ok()) return;
    jxl::Image3F buff = std::move(buff_res).value_();

    for (int i = 0; i < 3; i++) {
        auto dstp = reinterpret_cast<pixel_t*>(vsapi->getWritePtr(dst, i));
        const ptrdiff_t stride = vsapi->getStride(dst, i) / sizeof(pixel_t);
        for (int y = 0; y < height; y++) {
            const float* VS_RESTRICT row_in = buff.PlaneRow(i, y);
            pixel_t* VS_RESTRICT row_out = dstp + y * stride;

            for (int x = 0; x < width; x++) {
                // Scale and clamp
                float val = row_in[x] * peak;
                row_out[x] = static_cast<pixel_t>(val + 0.5f);  // Rounding
            }
        }
    }
}

static void heatmapF(VSFrame* dst, const jxl::ImageF& heatmap, int width, int height, const VSAPI* vsapi) noexcept {
    auto buff_res = jxl::CreateHeatMapImage(heatmap, jxl::ButteraugliFuzzyInverse(1.5), jxl::ButteraugliFuzzyInverse(0.5));
    if (!buff_res.ok()) return;  // TODO: handle error
    jxl::Image3F buff = std::move(buff_res).value_();

    for (int i = 0; i < 3; i++) {
        float* dstp{reinterpret_cast<float*>(vsapi->getWritePtr(dst, i))};
        const ptrdiff_t stride = vsapi->getStride(dst, i) / sizeof(float);
        for (int y = 0; y < height; y++) {
            memcpy(dstp, buff.ConstPlaneRow(i, y), width * sizeof(float));
            dstp += stride;
        }
    }
}

static void compute_norms(const jxl::ImageF& diff_map, double& norm_q, double& norm3, double& norm_inf, double q) {
    double sum_q = 0.0;    // Sum of q-powers for q-norm
    double sum3 = 0.0;     // Sum of cubes for L3-norm
    double max_val = 0.0;  // For L∞-norm (max)

    const int w = diff_map.xsize();
    const int h = diff_map.ysize();
    const int N = w * h;

    if (N == 0) {
        norm_q = norm3 = norm_inf = 0.0;
        return;
    }

    for (int y = 0; y < h; ++y) {
        const float* row = diff_map.Row(y);
        for (int x = 0; x < w; ++x) {
            const double val = std::abs(row[x]);

            sum_q += std::pow(val, q);
            sum3 += val * val * val;
            if (val > max_val) max_val = val;
        }
    }

    norm_q = std::pow(sum_q / N, 1.0 / q);
    norm3 = std::pow(sum3 / N, 1.0 / 3.0);
    norm_inf = max_val;  // Maximum absolute value (L∞-norm)
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

        jxl::CodecInOut ref{get_memory_manager()};
        jxl::CodecInOut dist{get_memory_manager()};
        jxl::ImageF diff_map;

        if (!ref.SetSize(width, height) || !dist.SetSize(width, height)) {
            vsapi->setFilterError("Butteraugli: Failed to set image size", frameCtx);
            vsapi->freeFrame(src);
            vsapi->freeFrame(src2);
            return nullptr;
        }

        d->fill(ref, dist, src, src2, width, height, vsapi);

        // Butteraugli expects linear RGB
        if (!d->linput) {
            if (!ref.Main().TransformTo(jxl::ColorEncoding::LinearSRGB(false), *JxlGetDefaultCms()) ||
                !dist.Main().TransformTo(jxl::ColorEncoding::LinearSRGB(false), *JxlGetDefaultCms())) {
                vsapi->setFilterError("Butteraugli: Failed to transform to Linear SRGB", frameCtx);
                vsapi->freeFrame(src);
                vsapi->freeFrame(src2);
                return nullptr;
            }
        }

        double score;
        if (!jxl::ButteraugliInterface(*ref.Main().color(), *dist.Main().color(), d->ba_params, diff_map, score)) {
            vsapi->setFilterError("Butteraugli: ButteraugliInterface failed", frameCtx);
            vsapi->freeFrame(src);
            vsapi->freeFrame(src2);
            return nullptr;
        }

        double norm_q, norm3, norm_inf;
        compute_norms(diff_map, norm_q, norm3, norm_inf, d->qnorm_val);

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
                memcpy(dstp, diff_map.Row(y), width * sizeof(float));
                dstp += dst_stride;
            }
            VSMap* dstProps = vsapi->getFramePropertiesRW(dst);

            vsapi->mapSetFloat(dstProps, "_BUTTERAUGLI_QNorm", norm_q, maReplace);
            vsapi->mapSetFloat(dstProps, "_BUTTERAUGLI_3Norm", norm3, maReplace);
            vsapi->mapSetFloat(dstProps, "_BUTTERAUGLI_INFNorm", norm_inf, maReplace);

            vsapi->freeFrame(src);
            vsapi->freeFrame(src2);
            return dst;
        } else if (d->heatmap) {
            VSFrame* dst = vsapi->newVideoFrame(vsapi->getVideoFrameFormat(src2), width, height, src2, core);
            d->hmap(dst, diff_map, width, height, vsapi);
            VSMap* dstProps = vsapi->getFramePropertiesRW(dst);

            vsapi->mapSetFloat(dstProps, "_BUTTERAUGLI_QNorm", norm_q, maReplace);
            vsapi->mapSetFloat(dstProps, "_BUTTERAUGLI_3Norm", norm3, maReplace);
            vsapi->mapSetFloat(dstProps, "_BUTTERAUGLI_INFNorm", norm_inf, maReplace);

            vsapi->freeFrame(src);
            vsapi->freeFrame(src2);
            return dst;
        } else {
            VSFrame* dst = vsapi->copyFrame(src2, core);
            VSMap* dstProps = vsapi->getFramePropertiesRW(dst);

            vsapi->mapSetFloat(dstProps, "_BUTTERAUGLI_QNorm", norm_q, maReplace);
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

    if (!d->node || !d->node2) {
        vsapi->mapSetError(out, "Butteraugli: Failed to convert input to RGBS");
        if (d->node) vsapi->freeNode(d->node);
        if (d->node2) vsapi->freeNode(d->node2);
        return;
    }
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
        intensity_target = 203.0f;

    d->ba_params.hf_asymmetry = 0.8f;
    d->ba_params.xmul = 1.0f;
    d->ba_params.intensity_target = intensity_target;

    if (intensity_target <= 0.0f) {
        vsapi->mapSetError(out, "Butteraugli: intensity_target must be greater than 0.0.");
        vsapi->freeNode(d->node);
        vsapi->freeNode(d->node2);
        return;
    }

    d->qnorm_val = vsapi->mapGetFloatSaturated(in, "qnorm", 0, &err);
    if (err)
        d->qnorm_val = 2.0;

    if (d->qnorm_val <= 0.0) {
        vsapi->mapSetError(out, "Butteraugli: qnorm must be greater than 0.0.");
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
            d->hmap = heatmap<uint8_t, 255>;
            break;
        case 2:
            d->fill = (d->linput) ? fill_image<uint16_t, jxl::Image3U, true> : fill_image<uint16_t, jxl::Image3U, false>;
            d->hmap = heatmap<uint16_t, 65535>;
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