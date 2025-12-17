#include "shared.h"

// Thanks Julek!
VSNode* toRGBS(VSNode* source, VSCore* core, const VSAPI* vsapi) {
    const VSVideoInfo* vi = vsapi->getVideoInfo(source);

    if (vi->format.colorFamily == cfRGB && vi->format.bitsPerSample == 32 && vi->format.sampleType == stFloat) {
        return source;
    }

    const int matrix = (vi->height > 650) ? 1 : 6;  // 1=BT.709, 6=BT.601

    VSMap* args = vsapi->createMap();
    vsapi->mapConsumeNode(args, "clip", source, maReplace);
    vsapi->mapSetInt(args, "matrix_in", matrix, maReplace);
    vsapi->mapSetInt(args, "format", pfRGBS, maReplace);

    VSPlugin* resizeplugin = vsapi->getPluginByID(VSH_RESIZE_PLUGIN_ID, core);
    VSMap* ret = vsapi->invoke(resizeplugin, "Bicubic", args);
    VSNode* out = vsapi->mapGetNode(ret, "clip", 0, nullptr);
    vsapi->freeMap(ret);
    vsapi->freeMap(args);

    return out;
}
