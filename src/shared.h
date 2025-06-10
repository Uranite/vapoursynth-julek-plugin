#pragma once

#include <VSHelper4.h>
#include <VapourSynth4.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "config.h"
#include "lib/extras/codec.h"
#include "lib/jxl/color_management.h"
#include "lib/jxl/enc_butteraugli_comparator.h"
#include "lib/jxl/enc_color_management.h"
#include "tools/ssimulacra.h"
#include "tools/ssimulacra2.h"

#ifdef PLUGIN_X86
#include "vectorclass.h"
#include "vectormath_exp.h"
#endif

extern void VS_CC agmCreate(const VSMap* in, VSMap* out, void* userData, VSCore* core, const VSAPI* vsapi);
extern void VS_CC autogainCreate(const VSMap* in, VSMap* out, void* userData, VSCore* core, const VSAPI* vsapi);
extern void VS_CC butteraugliCreate(const VSMap* in, VSMap* out, void* userData, VSCore* core, const VSAPI* vsapi);
extern void VS_CC colormapCreate(const VSMap* in, VSMap* out, void* userData, VSCore* core, const VSAPI* vsapi);
extern void VS_CC rfsCreate(const VSMap* in, VSMap* out, void* userData, VSCore* core, const VSAPI* vsapi);
extern void VS_CC ssimulacraCreate(const VSMap* in, VSMap* out, void* userData, VSCore* core, const VSAPI* vsapi);
extern void VS_CC visualizediffsCreate(const VSMap* in, VSMap* out, void* userData, VSCore* core, const VSAPI* vsapi);

// RGBS conversion helper function
extern VSNode* toRGBS(VSNode* source, VSCore* core, const VSAPI* vsapi);

#ifdef _MSC_VER
#define FORCE_INLINE inline __forceinline
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#endif

struct AGMData final {
    VSNode* node;
    const VSVideoInfo* vi;
    float luma_scaling;
    float float_range[256];
    int shift, peak;
    void (*process)(const VSFrame* src, VSFrame* dst, float& avg, const AGMData* const VS_RESTRICT d, const VSAPI* vsapi) noexcept;
};