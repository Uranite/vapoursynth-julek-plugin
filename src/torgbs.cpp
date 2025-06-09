#include "shared.h"

// Thanks Julek!
// Convert input clip to RGBS (32-bit float) format
// If the input is already in RGB format with 8/16/32-bit depth, returns the input node
// Otherwise, uses Bicubic resizer to convert the input to RGBS
VSNode* toRGBS(VSNode* source, VSCore* core, const VSAPI* vsapi) {
    const VSVideoInfo* vi = vsapi->getVideoInfo(source);

    // Check if already in RGB format with 8/16/32-bit depth
    if (vi->format.colorFamily == cfRGB) {
        if ((vi->format.bitsPerSample == 8 && vi->format.sampleType == stInteger) ||
            (vi->format.bitsPerSample == 16 && vi->format.sampleType == stInteger) ||
            (vi->format.bitsPerSample == 32 && vi->format.sampleType == stFloat)) {
            return source;
        }
    }

    // Select matrix based on resolution (BT.709 for HD, BT.601 for SD)
    const int matrix = (vi->height > 650) ? 1 : 6;  // 1=BT.709, 6=BT.601
    
    VSMap* args = vsapi->createMap();
    vsapi->mapConsumeNode(args, "clip", source, maReplace);
    vsapi->mapSetInt(args, "matrix_in", matrix, maReplace);
    vsapi->mapSetInt(args, "format", pfRGBS, maReplace);

    // Use Bicubic resizer for the conversion
    VSPlugin* vsplugin = vsapi->getPluginByID(VSH_RESIZE_PLUGIN_ID, core);
    VSMap* ret = vsapi->invoke(vsplugin, "Bicubic", args);
    VSNode* out = vsapi->mapGetNode(ret, "clip", 0, nullptr);

    vsapi->freeMap(ret);
    vsapi->freeMap(args);

    return out;
}
