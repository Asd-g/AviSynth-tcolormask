#include <algorithm>
#include <future>
#include <limits>
#include <regex>

#include "tcolormask.h"
#include "VCL2/instrset.h"


static AVS_FORCEINLINE int depfree_round(float d)
{
    return static_cast<int>(d + 0.5f);
}

template <typename T, int subsamplingX, int subsamplingY>
static void processLut(T* __restrict pDstY, const T* pSrcY, const T* pSrcV, const T* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, std::vector<T>& lutY, std::vector<T>& lutU, std::vector<T>& lutV) noexcept
{
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
            pDstY[x] = lutY[pSrcY[x]] & lutU[pSrcU[x / subsamplingX]] & lutV[pSrcV[x / subsamplingX]];


        pSrcY += srcPitchY;

        if (y % subsamplingY == (subsamplingY - 1))
        {
            pSrcU += srcPitchUV;
            pSrcV += srcPitchUV;
        }

        pDstY += dstPitchY;
    }
}

template <typename T, int subsamplingX, int subsamplingY>
void processC(void* __restrict pDstY_, const void* pSrcY_, const void* pSrcV_, const void* pSrcU_, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel<T>>& colors, int tolerance, int halftolerance) noexcept
{
    const T* pSrcY = reinterpret_cast<const T*>(pSrcY_);
    const T* pSrcV = reinterpret_cast<const T*>(pSrcV_);
    const T* pSrcU = reinterpret_cast<const T*>(pSrcU_);
    T* __restrict pDstY = reinterpret_cast<T*>(pDstY_);

    constexpr int peak = std::numeric_limits<T>::max();

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            T result_y = 0;
            T result_u = 0;
            T result_v = 0;

            const T srcY_v = pSrcY[x];
            T srcU_v, srcV_v;

            if constexpr (subsamplingX == 2)
            {
                srcU_v = pSrcU[x / subsamplingX];
                srcV_v = pSrcV[x / subsamplingX];
            }
            else
            {
                srcU_v = pSrcU[x];
                srcV_v = pSrcV[x];
            }

            for (auto& color : colors)
            {
                /* absolute difference */
                const int diff_y = std::abs(srcY_v - color.Y);
                const int diff_u = std::abs(srcU_v - color.U);
                const int diff_v = std::abs(srcV_v - color.V);
                /* comparing to tolerance */
                const int diff_tolerance_min_y = std::max(diff_y, tolerance);
                const int diff_tolerance_min_u = std::max(diff_u, halftolerance);
                const int diff_tolerance_min_v = std::max(diff_v, halftolerance);

                const int passed_y = (diff_y >= diff_tolerance_min_y) ? 0 : peak;
                const int passed_u = (diff_u >= diff_tolerance_min_u) ? 0 : peak;
                const int passed_v = (diff_v >= diff_tolerance_min_v) ? 0 : peak;

                result_y = result_y | passed_y;
                result_u = result_u | passed_u;
                result_v = result_v | passed_v;
            }

            result_y = result_y & result_u;
            pDstY[x] = result_y & result_v;
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

template <typename T, bool grayscale, bool mt>
TColorMask<T, grayscale, mt>::TColorMask(PClip child, std::vector<uint64_t> colors, int tolerance, bool bt601, int lutthr, bool onlyY, int opt, IScriptEnvironment* env)
    : GenericVideoFilter(child), tolerance_(tolerance), v8(true)
{
    const int peak = (sizeof(T) == 1) ? 255 : 65535;
    if (tolerance_ == -1)
        tolerance_ = (sizeof(T) == 1) ? 10 : 2570;
    if (tolerance_ < 0 || tolerance_ > peak)
        env->ThrowError("tcolormask: tolerance must be between 0..%s", std::to_string(peak).c_str());

    if (opt < -1 || opt > 2)
        env->ThrowError("tcolormask: opt must be between -1..2.");

    const int iset{ instrset_detect() };
    if (opt == 1 && iset < 2)
        env->ThrowError("tcolormask: opt=1 requires SSE2.");

    if (vi.Is444())
    {
        subsamplingY_ = 1;
        subsamplingX_ = 1;
        proc_lut = processLut<T, 1, 1>;

        if ((opt == -1 && iset >= 8) || opt == 2)
            p_ = processAvx2<T, 1, 1>;
        else if ((opt == -1 && iset >= 2) || opt == 1)
            p_ = processSse2<T, 1, 1>;
        else
            p_ = processC<T, 1, 1>;
    }
    else if (vi.Is420())
    {
        subsamplingY_ = 2;
        subsamplingX_ = 2;

        if (mt && (vi.height / 2) % 2 != 0)
            env->ThrowError("tcolormask: chroma height must be mod2 for mt=true!");

        proc_lut = processLut<T, 2, 2>;

        if ((opt == -1 && iset >= 8) || opt == 2)
            p_ = processAvx2<T, 2, 2>;
        else if ((opt == -1 && iset >= 2) || opt == 1)
            p_ = processSse2<T, 2, 2>;
        else
            p_ = processC<T, 2, 2>;
    }
    else if (vi.Is422())
    {
        subsamplingY_ = 1;
        subsamplingX_ = 2;
        proc_lut = processLut<T, 2, 1>;

        if ((opt == -1 && iset >= 8) || opt == 2)
            p_ = processAvx2<T, 2, 1>;
        else if ((opt == -1 && iset >= 2) || opt == 1)
            p_ = processSse2<T, 2, 1>;
        else
            p_ = processC<T, 2, 1>;
    }
    else
        env->ThrowError("tcolormask: only YUV420, YUV422 and YUV444 are supported!");

    const float kR = bt601 ? 0.299f : 0.2126f;
    const float kB = bt601 ? 0.114f : 0.0722f;

    colors_.reserve(colors.size());

    if constexpr (std::is_same_v<T, uint8_t>)
    {
        for (auto color : colors)
        {
            YUVPixel<T> p;
            memset(&p, 0, sizeof(p));
            const float r = static_cast<float>((color & 0xFF0000) >> 16) / 255.0f;
            const float g = static_cast<float>((color & 0xFF00) >> 8) / 255.0f;
            const float b = static_cast<float>(color & 0xFF) / 255.0f;

            const float y = kR * r + (1 - kR - kB) * g + kB * b;
            p.U = 128 + depfree_round(112.0f * (b - y) / (1 - kB));
            p.V = 128 + depfree_round(112.0f * (r - y) / (1 - kR));
            p.Y = 16 + depfree_round(219.0f * y);

            colors_.emplace_back(p);
        }
    }
    else
    {
        for (auto color : colors)
        {
            YUVPixel<T> p;
            memset(&p, 0, sizeof(p));
            const float r = static_cast<float>((color & 0xFFFF00000000) >> 32) / 65535.0f;
            const float g = static_cast<float>((color & 0xFFFF0000) >> 16) / 65535.0f;
            const float b = static_cast<float>(color & 0xFFFF) / 65535.0f;

            const float y = kR * r + (1 - kR - kB) * g + kB * b;
            p.U = 32768 + depfree_round(28672.0f * (b - y) / (1 - kB));
            p.V = 32768 + depfree_round(28672.0f * (r - y) / (1 - kR));
            p.Y = 4096 + depfree_round(56064.0f * y);

            colors_.emplace_back(p);
        }
    }

    if (colors_.size() > lutthr)
        proc = (vi.width % 16) ? &TColorMask::process<true, true> : &TColorMask::process<true, false>;
    else
        proc = (vi.width % 16) ? &TColorMask::process<false, true> : &TColorMask::process<false, false>;

    if (((vi.width % 16) != 0) || (colors_.size() > lutthr))
        buildLuts();

    try { env->CheckVersion(8); }
    catch (const AvisynthError&) { v8 = false; };

    vi1 = vi;

    if (onlyY)
        vi.pixel_type = (sizeof(T) == 1) ? VideoInfo::CS_Y8 : VideoInfo::CS_Y16;
}

template <typename T, bool grayscale, bool mt>
void TColorMask<T, grayscale, mt>::buildLuts() noexcept
{
    constexpr int peak = std::numeric_limits<T>::max();
    const int range_max = peak + 1;
    lut_y.reserve(range_max);
    lut_u.reserve(range_max);
    lut_v.reserve(range_max);

    for (int i = 0; i < range_max; ++i)
    {
        T val_y = 0;
        T val_u = 0;
        T val_v = 0;

        for (auto& color : colors_)
        {
            val_y |= ((abs(i - color.Y) < tolerance_) ? peak : 0);
            val_u |= ((abs(i - color.U) < (tolerance_ / 2)) ? peak : 0);
            val_v |= ((abs(i - color.V) < (tolerance_ / 2)) ? peak : 0);
        }

        lut_y.emplace_back(val_y);
        lut_u.emplace_back(val_u);
        lut_v.emplace_back(val_v);
    }
}

template <typename T, bool grayscale, bool mt>
PVideoFrame TColorMask<T, grayscale, mt>::GetFrame(int n, IScriptEnvironment* env)
{
    PVideoFrame src = child->GetFrame(n, env);
    PVideoFrame dst = (v8) ? env->NewVideoFrameP(vi1, &src) : env->NewVideoFrame(vi1);

    const int width = src->GetRowSize(PLANAR_Y) / sizeof(T);
    const int height = src->GetHeight(PLANAR_Y);

    const T* srcY_ptr = reinterpret_cast<const T*>(src->GetReadPtr(PLANAR_Y));
    const T* srcU_ptr = reinterpret_cast<const T*>(src->GetReadPtr(PLANAR_U));
    const T* srcV_ptr = reinterpret_cast<const T*>(src->GetReadPtr(PLANAR_V));
    const int src_pitch_y = src->GetPitch(PLANAR_Y) / sizeof(T);
    const int src_pitch_uv = src->GetPitch(PLANAR_U) / sizeof(T);

    T* __restrict dstY_ptr = reinterpret_cast<T*>(dst->GetWritePtr(PLANAR_Y));
    const int dst_pitch_y = dst->GetPitch(PLANAR_Y) / sizeof(T);

    if constexpr (mt)
    {
        //async seems to be threadpool'ed on windows, creating threads is less efficient
        auto thread2 = std::async(std::launch::async, [=] {
            (this->*proc)(dstY_ptr,
                srcY_ptr,
                srcV_ptr,
                srcU_ptr,
                dst_pitch_y,
                src_pitch_y,
                src_pitch_uv,
                width,
                height / 2);
            });
        (this->*proc)(dstY_ptr + (dst_pitch_y * height / 2),
            srcY_ptr + (src_pitch_y * height / 2),
            srcV_ptr + (src_pitch_uv * height / (2 * subsamplingY_)),
            srcU_ptr + (src_pitch_uv * height / (2 * subsamplingY_)),
            dst_pitch_y,
            src_pitch_y,
            src_pitch_uv,
            width,
            height / 2);
        thread2.wait();
    }
    else
        (this->*proc)(dstY_ptr, srcY_ptr, srcV_ptr, srcU_ptr, dst_pitch_y, src_pitch_y, src_pitch_uv, width, height);

    if constexpr (grayscale)
    {
        memset(dst->GetWritePtr(PLANAR_U), 128, dst->GetPitch(PLANAR_U) * dst->GetHeight(PLANAR_U));
        memset(dst->GetWritePtr(PLANAR_V), 128, dst->GetPitch(PLANAR_V) * dst->GetHeight(PLANAR_V));
    }

    return dst;
}

template <typename T, bool grayscale, bool mt>
template <bool cs, bool border>
void TColorMask<T, grayscale, mt>::process(T* __restrict dstY_ptr, const T* srcY_ptr, const T* srcV_ptr, const T* srcU_ptr, int dst_pitch_y, int src_pitch_y, int src_pitch_uv, int width, int height) noexcept
{
    if constexpr (cs)
    {
        proc_lut(dstY_ptr, srcY_ptr, srcV_ptr, srcU_ptr, dst_pitch_y, src_pitch_y, src_pitch_uv, width, height, lut_y, lut_u, lut_v);
        return;
    }

    p_(dstY_ptr, srcY_ptr, srcV_ptr, srcU_ptr, dst_pitch_y, src_pitch_y, src_pitch_uv, width - border, height, colors_, tolerance_, tolerance_ / 2);

    if constexpr (border != 0)
    {
        proc_lut(dstY_ptr + width - border,
            srcY_ptr + width - border,
            srcV_ptr + (width - border) / subsamplingX_,
            srcU_ptr + (width - border) / subsamplingX_,
            dst_pitch_y, src_pitch_y, src_pitch_uv, border, height, lut_y, lut_u, lut_v);
    }
}

static uint64_t avisynthStringToInt(const std::string& str) noexcept
{
    if (str[0] == '$')
    {
        auto substr = str.substr(1, str.length());
        return strtoll(substr.c_str(), 0, 16);
    }

    return strtoll(str.c_str(), 0, 10);
}

AVSValue __cdecl CreateTColorMask(AVSValue args, void*, IScriptEnvironment* env)
{
    enum { CLIP, COLORS, TOLERANCE, BT601, GRAYSCALE, LUTTHR, MT, ONLYy, OPT };

    PClip clip = args[CLIP].AsClip();
    const int bits = clip->GetVideoInfo().BitsPerComponent();

    if (bits != 8 && bits != 16)
        env->ThrowError("tcolormask: only 8 and 16-bit supported.");
    if (!clip->GetVideoInfo().IsPlanar())
        env->ThrowError("tcolormask: only planar format supported.");

    std::string str = args[COLORS].AsString("");

    std::regex e("(/\\*.*\\*/|//.*$)");   //comments
    str = std::regex_replace(str, e, "");

    std::vector<uint64_t> colors;
    std::regex rgx("(\\$?[\\da-fA-F]+)");

    std::sregex_token_iterator iter(str.cbegin(), str.cend(), rgx, 1);
    std::sregex_token_iterator end;
    for (; iter != end; ++iter)
    {
        if (bits == 16 && iter->str().size() < 12)
            env->ThrowError("tcolormask: wrong hex color for 16-bit.");

        try {
            colors.emplace_back(avisynthStringToInt(iter->str()));
        }
        catch (...) {
            env->ThrowError("tcolormask: parsing error.");
        }
    }

    const bool grayscale = args[GRAYSCALE].AsBool(false);
    const bool mt = args[MT].AsBool(false);

    if (bits == 8)
    {
        if (!grayscale)
        {
            if (!mt)
                return new TColorMask<uint8_t, false, false>(clip, colors, args[TOLERANCE].AsInt(-1), args[BT601].AsBool(false), args[LUTTHR].AsInt(9), args[ONLYy].AsBool(false), args[OPT].AsInt(-1), env);
            else
                return new TColorMask<uint8_t, false, true>(clip, colors, args[TOLERANCE].AsInt(-1), args[BT601].AsBool(false), args[LUTTHR].AsInt(9), args[ONLYy].AsBool(false), args[OPT].AsInt(-1), env);
        }
        else
        {
            if (!mt)
                return new TColorMask<uint8_t, true, false>(clip, colors, args[TOLERANCE].AsInt(-1), args[BT601].AsBool(false), args[LUTTHR].AsInt(9), args[ONLYy].AsBool(false), args[OPT].AsInt(-1), env);
            else
                return new TColorMask<uint8_t, true, true>(clip, colors, args[TOLERANCE].AsInt(-1), args[BT601].AsBool(false), args[LUTTHR].AsInt(9), args[ONLYy].AsBool(false), args[OPT].AsInt(-1), env);
        }
    }
    else
    {
        if (!grayscale)
        {
            if (!mt)
                return new TColorMask<uint16_t, false, false>(clip, colors, args[TOLERANCE].AsInt(-1), args[BT601].AsBool(false), args[LUTTHR].AsInt(9), args[ONLYy].AsBool(false), args[OPT].AsInt(-1), env);
            else
                return new TColorMask<uint16_t, false, true>(clip, colors, args[TOLERANCE].AsInt(-1), args[BT601].AsBool(false), args[LUTTHR].AsInt(9), args[ONLYy].AsBool(false), args[OPT].AsInt(-1), env);
        }
        else
        {
            if (!mt)
                return new TColorMask<uint16_t, true, false>(clip, colors, args[TOLERANCE].AsInt(-1), args[BT601].AsBool(false), args[LUTTHR].AsInt(9), args[ONLYy].AsBool(false), args[OPT].AsInt(-1), env);
            else
                return new TColorMask<uint16_t, true, true>(clip, colors, args[TOLERANCE].AsInt(-1), args[BT601].AsBool(false), args[LUTTHR].AsInt(9), args[ONLYy].AsBool(false), args[OPT].AsInt(-1), env);
        }
    }
}

const AVS_Linkage* AVS_linkage = nullptr;

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment * env, const AVS_Linkage* const vectors) {
    AVS_linkage = vectors;

    env->AddFunction("tcolormask", "c[colors]s[tolerance]i[bt601]b[gray]b[lutthr]i[mt]b[onlyY]b[opt]i", CreateTColorMask, 0);
    return "Why are you looking at this?";
}
