#include "tcolormask.h"
#include "VCL2/vectorclass.h"

template <typename T, int subsamplingX, int subsamplingY>
void processAvx512(void* __restrict pDstY_, const void* pSrcY_, const void* pSrcV_, const void* pSrcU_, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<T>>& colors, int tolerance, int halftolerance) noexcept
{
    const T* pSrcY = reinterpret_cast<const T*>(pSrcY_);
    const T* pSrcV = reinterpret_cast<const T*>(pSrcV_);
    const T* pSrcU = reinterpret_cast<const T*>(pSrcU_);
    T* __restrict pDstY = reinterpret_cast<T*>(pDstY_);

    for (int y = 0; y < height; ++y)
    {
        if constexpr (std::is_same_v<T, uint8_t>)
        {
            for (int x = 0; x < width; x += 64)
            {
                Vec64uc result_y = zero_si512();
                Vec64uc result_u = zero_si512();
                Vec64uc result_v = zero_si512();

                const auto srcY_v = Vec64uc().load(pSrcY + x);
                Vec64uc srcU_v, srcV_v;

                if constexpr (subsamplingX == 2)
                {
                    srcU_v = Vec64uc().load(pSrcU + x / subsamplingX);
                    srcU_v = blend64<0, 64, 1, 65, 2, 66, 3, 67, 4, 68, 5, 69, 6, 70, 7, 71, 8, 72, 9, 73, 10, 74, 11, 75, 12, 76, 13, 77, 14, 78, 15, 79,
                        16, 80, 17, 81, 18, 82, 19, 83, 20, 84, 21, 85, 22, 86, 23, 87, 24, 88, 25, 89, 26, 90, 27, 91, 28, 92, 29, 93, 30, 94, 31, 95>(srcU_v, srcU_v);
                    srcV_v = Vec64uc().load(pSrcV + x / subsamplingX);
                    srcV_v = blend64<0, 64, 1, 65, 2, 66, 3, 67, 4, 68, 5, 69, 6, 70, 7, 71, 8, 72, 9, 73, 10, 74, 11, 75, 12, 76, 13, 77, 14, 78, 15, 79,
                        16, 80, 17, 81, 18, 82, 19, 83, 20, 84, 21, 85, 22, 86, 23, 87, 24, 88, 25, 89, 26, 90, 27, 91, 28, 92, 29, 93, 30, 94, 31, 95>(srcV_v, srcV_v);
                }
                else
                {
                    srcU_v = Vec64uc().load(pSrcU + x);
                    srcV_v = Vec64uc().load(pSrcV + x);
                }

                for (auto& color : colors)
                {
                    const auto colorVector_y = Vec64uc(color.Y);
                    const auto colorVector_u = Vec64uc(color.U);
                    const auto colorVector_v = Vec64uc(color.V);
                    /* absolute difference */
                    const auto maximum_y = max(srcY_v, colorVector_y);
                    const auto maximum_u = max(srcU_v, colorVector_u);
                    const auto maximum_v = max(srcV_v, colorVector_v);

                    const auto minimum_y = min(srcY_v, colorVector_y);
                    const auto minimum_u = min(srcU_v, colorVector_u);
                    const auto minimum_v = min(srcV_v, colorVector_v);

                    const auto diff_y = maximum_y - minimum_y;
                    const auto diff_u = maximum_u - minimum_u;
                    const auto diff_v = maximum_v - minimum_v;
                    /* comparing to tolerance */
                    const auto diff_tolerance_min_y = max(diff_y, Vec64uc(tolerance));
                    const auto diff_tolerance_min_u = max(diff_u, Vec64uc(halftolerance));
                    const auto diff_tolerance_min_v = max(diff_v, Vec64uc(halftolerance));

                    const auto passed_y = select(diff_y >= diff_tolerance_min_y, zero_si512(), Vec64uc(255));
                    const auto passed_u = select(diff_u >= diff_tolerance_min_u, zero_si512(), Vec64uc(255));
                    const auto passed_v = select(diff_v >= diff_tolerance_min_v, zero_si512(), Vec64uc(255));

                    result_y = result_y | passed_y;
                    result_u = result_u | passed_u;
                    result_v = result_v | passed_v;
                }

                result_y = result_y & result_u;
                result_y = result_y & result_v;
                result_y.store(pDstY + x);
            }
        }
        else
        {
            for (int x = 0; x < width; x += 32)
            {
                Vec32us result_y = zero_si512();
                Vec32us result_u = zero_si512();
                Vec32us result_v = zero_si512();

                const auto srcY_v = Vec32us().load(pSrcY + x);
                Vec32us srcU_v, srcV_v;

                if constexpr (subsamplingX == 2)
                {
                    srcU_v = Vec32us().load(pSrcU + x / subsamplingX);
                    srcU_v = blend32<0, 32, 1, 33, 2, 34, 3, 35, 4, 36, 5, 37, 6, 38, 7, 39, 8, 40, 9, 41, 10, 42, 11, 43, 12, 44, 13, 45, 14, 46, 15, 47>(srcU_v, srcU_v);
                    srcV_v = Vec32us().load(pSrcV + x / subsamplingX);
                    srcV_v = blend32<0, 32, 1, 33, 2, 34, 3, 35, 4, 36, 5, 37, 6, 38, 7, 39, 8, 40, 9, 41, 10, 42, 11, 43, 12, 44, 13, 45, 14, 46, 15, 47>(srcV_v, srcV_v);
                }
                else
                {
                    srcU_v = Vec32us().load(pSrcU + x);
                    srcV_v = Vec32us().load(pSrcV + x);
                }

                for (auto& color : colors)
                {
                    const auto colorVector_y = Vec32us(color.Y);
                    const auto colorVector_u = Vec32us(color.U);
                    const auto colorVector_v = Vec32us(color.V);
                    /* absolute difference */
                    const auto maximum_y = max(srcY_v, colorVector_y);
                    const auto maximum_u = max(srcU_v, colorVector_u);
                    const auto maximum_v = max(srcV_v, colorVector_v);

                    const auto minimum_y = min(srcY_v, colorVector_y);
                    const auto minimum_u = min(srcU_v, colorVector_u);
                    const auto minimum_v = min(srcV_v, colorVector_v);

                    const auto diff_y = (maximum_y - minimum_y);
                    const auto diff_u = (maximum_u - minimum_u);
                    const auto diff_v = (maximum_v - minimum_v);
                    /* comparing to tolerance */
                    const auto diff_tolerance_min_y = max(diff_y, Vec32us(tolerance));
                    const auto diff_tolerance_min_u = max(diff_u, Vec32us(halftolerance));
                    const auto diff_tolerance_min_v = max(diff_v, Vec32us(halftolerance));

                    const auto passed_y = select(diff_y >= diff_tolerance_min_y, zero_si512(), Vec32us(65535));
                    const auto passed_u = select(diff_u >= diff_tolerance_min_u, zero_si512(), Vec32us(65535));
                    const auto passed_v = select(diff_v >= diff_tolerance_min_v, zero_si512(), Vec32us(65535));

                    result_y = result_y | passed_y;
                    result_u = result_u | passed_u;
                    result_v = result_v | passed_v;
                }

                result_y = result_y & result_u;
                result_y = result_y & result_v;
                result_y.store(pDstY + x);
            }
        }

        pSrcY += srcPitchY;

        if (y % subsamplingY == (subsamplingY - 1))
        {
            pSrcU += srcPitchUV;
            pSrcV += srcPitchUV;
        }

        pDstY += dstPitchY;
    }
}

template void processAvx512<uint8_t, 1, 1>(void* __restrict pDstY, const void* pSrcY, const void* pSrcV, const void* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<uint8_t>>& colors,
    int tolerance, int halftolerance) noexcept;
template void processAvx512<uint8_t, 2, 2>(void* __restrict pDstY, const void* pSrcY, const void* pSrcV, const void* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<uint8_t>>& colors,
    int tolerance, int halftolerance) noexcept;
template void processAvx512<uint8_t, 2, 1>(void* __restrict pDstY, const void* pSrcY, const void* pSrcV, const void* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<uint8_t>>& colors,
    int tolerance, int halftolerance) noexcept;

template void processAvx512<uint16_t, 1, 1>(void* __restrict pDstY, const void* pSrcY, const void* pSrcV, const void* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<uint16_t>>& colors,
    int tolerance, int halftolerance) noexcept;
template void processAvx512<uint16_t, 2, 2>(void* __restrict pDstY, const void* pSrcY, const void* pSrcV, const void* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<uint16_t>>& colors,
    int tolerance, int halftolerance) noexcept;
template void processAvx512<uint16_t, 2, 1>(void* __restrict pDstY, const void* pSrcY, const void* pSrcV, const void* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<uint16_t>>& colors,
    int tolerance, int halftolerance) noexcept;
