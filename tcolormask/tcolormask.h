#pragma once

#include <vector>

#include "avisynth.h"

template <typename T>
struct YUVPixel
{
    T Y;
    T U;
    T V;
};

template <typename T, bool grayscale, bool mt>
class TColorMask : public GenericVideoFilter
{
public:
    TColorMask(PClip child, std::vector<uint64_t> colors, int tolerance, bool bt601, int lutthr, bool onlyY, int opt, IScriptEnvironment* env);
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override;

    int __stdcall SetCacheHints(int cachehints, int frame_range) override
    {
        return cachehints == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
    }

private:
    void buildLuts() noexcept;

    template <bool cs, bool border>
    void process(T* __restrict dstY_ptr, const T* srcY_ptr, const T* srcV_ptr, const T* srcU_ptr, int dst_pitch_y, int src_pitch_y, int src_pitch_uv, int width, int height) noexcept;
    void (TColorMask::* proc)(T* __restrict dstY_ptr, const T* srcY_ptr, const T* srcV_ptr, const T* srcU_ptr, int dst_pitch_y, int src_pitch_y, int src_pitch_uv, int width, int height) noexcept;

    void(*proc_lut)(T* __restrict pDstY, const T* pSrcY, const T* pSrcV, const T* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, std::vector<T>& lutY, std::vector<T>& lutU, std::vector<T>& lutV) noexcept;
    void(*p_)(void* __restrict pDstY, const void* pSrcY, const void* pSrcV, const void* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<T>>& colors, int tolerance, int halftolerance) noexcept;

    std::vector<YUVPixel<T>> colors_;
    int tolerance_;
    int subsamplingY_;
    int subsamplingX_;

    std::vector<T> lut_y;
    std::vector<T> lut_u;
    std::vector<T> lut_v;

    bool v8;

    VideoInfo vi1;
};

template <typename T, int subsamplingX, int subsamplingY>
void processSse2(void* __restrict pDstY, const void* pSrcY, const void* pSrcV, const void* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<T>>& colors, int tolerance, int halftolerance) noexcept;
template <typename T, int subsamplingX, int subsamplingY>
void processAvx2(void* __restrict pDstY, const void* pSrcY, const void* pSrcV, const void* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<T>>& colors, int tolerance, int halftolerance) noexcept;
template <typename T, int subsamplingX, int subsamplingY>
void processAvx512(void* __restrict pDstY, const void* pSrcY, const void* pSrcV, const void* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<T>>& colors, int tolerance, int halftolerance) noexcept;
