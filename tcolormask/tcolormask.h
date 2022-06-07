#pragma once

#include <vector>

#include "avisynth.h"

struct YUVPixel
{
    uint8_t Y;
    uint8_t U;
    uint8_t V;
};

class TColorMask : public GenericVideoFilter
{
public:
    TColorMask(PClip child, std::vector<int> colors, int tolerance, bool bt601, bool grayscale, int lutthr, bool mt, int opt, IScriptEnvironment* env);
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override;

    int __stdcall SetCacheHints(int cachehints, int frame_range) override
    {
        return cachehints == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
    }

private:
    void buildLuts() noexcept;

    template <bool cs, bool border>
    void process(uint8_t* __restrict dstY_ptr, const uint8_t* srcY_ptr, const uint8_t* srcV_ptr, const uint8_t* srcU_ptr, int dst_pitch_y, int src_pitch_y, int src_pitch_uv, int width, int height) noexcept;
    void (TColorMask::* proc)(uint8_t* __restrict dstY_ptr, const uint8_t* srcY_ptr, const uint8_t* srcV_ptr, const uint8_t* srcU_ptr, int dst_pitch_y, int src_pitch_y, int src_pitch_uv, int width, int height) noexcept;

    void(*proc_lut)(uint8_t* __restrict pDstY, const uint8_t* pSrcY, const uint8_t* pSrcV, const uint8_t* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, uint8_t* lutY, uint8_t* lutU, uint8_t* lutV) noexcept;
    void(*p_)(uint8_t* __restrict pDstY, const uint8_t* pSrcY, const uint8_t* pSrcV, const uint8_t* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel>& colors,
        int vectorTolerance, int halfVectorTolerance) noexcept;

    std::vector<YUVPixel> colors_;
    int tolerance_;
    bool grayscale_;
    bool mt_;
    int subsamplingY_;
    int subsamplingX_;

    uint8_t lut_y[256];
    uint8_t lut_u[256];
    uint8_t lut_v[256];

    bool v8;
};

template<int subsamplingX, int subsamplingY>
void processSse2(uint8_t* __restrict pDstY, const uint8_t* pSrcY, const uint8_t* pSrcV, const uint8_t* pSrcU, int dstPitchY,
    int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel>& colors, int vectorTolerance, int halfVectorTolerance) noexcept;
template<int subsamplingX, int subsamplingY>
void processAvx2(uint8_t* __restrict pDstY, const uint8_t* pSrcY, const uint8_t* pSrcV, const uint8_t* pSrcU, int dstPitchY,
    int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel>& colors, int vectorTolerance, int halfVectorTolerance) noexcept;
