#include "tcolormask.h"
#include "VCL2/vectorclass.h"

template <typename T, int subsamplingX, int subsamplingY>
void processSse2(void* __restrict pDstY_, const void* pSrcY_, const void* pSrcV_, const void* pSrcU_, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<T>>& colors, int tolerance, int halftolerance) noexcept
{
    const T* pSrcY = reinterpret_cast<const T*>(pSrcY_);
    const T* pSrcV = reinterpret_cast<const T*>(pSrcV_);
    const T* pSrcU = reinterpret_cast<const T*>(pSrcU_);
    T* __restrict pDstY = reinterpret_cast<T*>(pDstY_);

    for (int y = 0; y < height; ++y)
    {
        if constexpr (std::is_same_v<T, uint8_t>)
        {
            for (int x = 0; x < width; x += 16)
            {
                Vec16uc result_y = zero_si128();
                Vec16uc result_u = zero_si128();
                Vec16uc result_v = zero_si128();

                const auto srcY_v = Vec16uc().load(pSrcY + x);
                Vec16uc srcU_v, srcV_v;

                if constexpr (subsamplingX == 2)
                {
                    srcU_v = Vec16uc().loadl(pSrcU + x / subsamplingX);
                    srcU_v = blend16<0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23>(srcU_v, srcU_v);
                    srcV_v = Vec16uc().loadl(pSrcV + x / subsamplingX);
                    srcV_v = blend16<0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23>(srcV_v, srcV_v);
                }
                else
                {
                    srcU_v = Vec16uc().load(pSrcU + x);
                    srcV_v = Vec16uc().load(pSrcV + x);
                }

                for (auto& color : colors)
                {
                    const auto colorVector_y = Vec16uc(color.Y);
                    const auto colorVector_u = Vec16uc(color.U);
                    const auto colorVector_v = Vec16uc(color.V);
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
                    const auto diff_tolerance_min_y = max(diff_y, Vec16uc(tolerance));
                    const auto diff_tolerance_min_u = max(diff_u, Vec16uc(halftolerance));
                    const auto diff_tolerance_min_v = max(diff_v, Vec16uc(halftolerance));

                    const auto passed_y = select(diff_y >= diff_tolerance_min_y, zero_si128(), Vec16uc(255));
                    const auto passed_u = select(diff_u >= diff_tolerance_min_u, zero_si128(), Vec16uc(255));
                    const auto passed_v = select(diff_v >= diff_tolerance_min_v, zero_si128(), Vec16uc(255));

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
            for (int x = 0; x < width; x += 8)
            {
                Vec8us result_y = zero_si128();
                Vec8us result_u = zero_si128();
                Vec8us result_v = zero_si128();

                const auto srcY_v = Vec8us().load(pSrcY + x);
                Vec8us srcU_v, srcV_v;

                if constexpr (subsamplingX == 2)
                {
                    srcU_v = Vec8us().loadl(pSrcU + x / subsamplingX);
                    srcU_v = blend8<0, 8, 1, 9, 2, 10, 3, 11>(srcU_v, srcU_v);
                    srcV_v = Vec8us().loadl(pSrcV + x / subsamplingX);
                    srcV_v = blend8<0, 8, 1, 9, 2, 10, 3, 11>(srcV_v, srcV_v);
                }
                else
                {
                    srcU_v = Vec8us().load(pSrcU + x);
                    srcV_v = Vec8us().load(pSrcV + x);
                }

                for (auto& color : colors)
                {
                    const auto colorVector_y = Vec8us(color.Y);
                    const auto colorVector_u = Vec8us(color.U);
                    const auto colorVector_v = Vec8us(color.V);
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
                    const auto diff_tolerance_min_y = max(diff_y, Vec8us(tolerance));
                    const auto diff_tolerance_min_u = max(diff_u, Vec8us(halftolerance));
                    const auto diff_tolerance_min_v = max(diff_v, Vec8us(halftolerance));

                    const auto passed_y = select(diff_y >= diff_tolerance_min_y, zero_si128(), Vec8us(65535));
                    const auto passed_u = select(diff_u >= diff_tolerance_min_u, zero_si128(), Vec8us(65535));
                    const auto passed_v = select(diff_v >= diff_tolerance_min_v, zero_si128(), Vec8us(65535));

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

template void processSse2<uint8_t, 1, 1>(void* __restrict pDstY, const void* pSrcY, const void* pSrcV, const void* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<uint8_t>>& colors,
    int tolerance, int halftolerance) noexcept;
template void processSse2<uint8_t, 2, 2>(void* __restrict pDstY, const void* pSrcY, const void* pSrcV, const void* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<uint8_t>>& colors,
    int tolerance, int halftolerance) noexcept;
template void processSse2<uint8_t, 2, 1>(void* __restrict pDstY, const void* pSrcY, const void* pSrcV, const void* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<uint8_t>>& colors,
    int tolerance, int halftolerance) noexcept;

template void processSse2<uint16_t, 1, 1>(void* __restrict pDstY, const void* pSrcY, const void* pSrcV, const void* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<uint16_t>>& colors,
    int tolerance, int halftolerance) noexcept;
template void processSse2<uint16_t, 2, 2>(void* __restrict pDstY, const void* pSrcY, const void* pSrcV, const void* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<uint16_t>>& colors,
    int tolerance, int halftolerance) noexcept;
template void processSse2<uint16_t, 2, 1>(void* __restrict pDstY, const void* pSrcY, const void* pSrcV, const void* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<uint16_t>>& colors,
    int tolerance, int halftolerance) noexcept;
