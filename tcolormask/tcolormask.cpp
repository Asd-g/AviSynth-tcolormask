#include <algorithm>
#include <future>
#include <regex>

#include "tcolormask.h"
#include "VCL2/instrset.h"


static AVS_FORCEINLINE int depfree_round(float d)
{
    return static_cast<int>(d + 0.5f);
}

template<int subsamplingX, int subsamplingY>
static void processLut(uint8_t* __restrict pDstY, const uint8_t* pSrcY, const uint8_t* pSrcV, const uint8_t* pSrcU, int dstPitchY, int srcPitchY, int srcPitchUV, int width, int height, uint8_t* lutY, uint8_t* lutU, uint8_t* lutV) noexcept
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

template<int subsamplingX, int subsamplingY>
void processC(uint8_t* __restrict pDstY, const uint8_t* pSrcY, const uint8_t* pSrcV, const uint8_t* pSrcU, int dstPitchY,
    int srcPitchY, int srcPitchUV, int width, int height, const std::vector<YUVPixel>& colors, int tolerance, int haltolerance) noexcept
{
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            uint8_t result_y = 0;
            uint8_t result_u = 0;
            uint8_t result_v = 0;

            const uint8_t srcY_v = pSrcY[x];
            uint8_t srcU_v, srcV_v;

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
                const int diff_tolerance_min_u = std::max(diff_u, haltolerance);
                const int diff_tolerance_min_v = std::max(diff_v, haltolerance);

                const int passed_y = (diff_y >= diff_tolerance_min_y) ? 0 : 255;
                const int passed_u = (diff_u >= diff_tolerance_min_u) ? 0 : 255;
                const int passed_v = (diff_v >= diff_tolerance_min_v) ? 0 : 255;

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

TColorMask::TColorMask(PClip child, std::vector<int> colors, int tolerance, bool bt601, bool grayscale, int lutthr, bool mt, int opt, IScriptEnvironment* env)
    : GenericVideoFilter(child), tolerance_(tolerance), grayscale_(grayscale), mt_(mt), lut_y{ 0 }, lut_u{ 0 }, lut_v{ 0 }, v8(true)
{
    if (opt < -1 || opt > 2)
        env->ThrowError("FCBI: opt must be between -1..2.");

    const int iset{ instrset_detect() };
    if (opt == 1 && iset < 2)
        env->ThrowError("FCBI: opt=1 requires SSE2.");

    if (vi.IsYV24())
    {
        subsamplingY_ = 1;
        subsamplingX_ = 1;
        proc_lut = processLut<1, 1>;

        if ((opt == -1 && iset >= 8) || opt == 2)
            p_ = processAvx2<1, 1>;
        else if ((opt == -1 && iset >= 2) || opt == 1)
            p_ = processSse2<1, 1>;
        else
            p_ = processC<1, 1>;
    }
    else if (vi.IsYV12())
    {
        subsamplingY_ = 2;
        subsamplingX_ = 2;

        if (mt_ && (vi.height / 2) % 2 != 0)
            env->ThrowError("Chroma height must be mod2 for mt=true!");

        proc_lut = processLut<2, 2>;

        if ((opt == -1 && iset >= 8) || opt == 2)
            p_ = processAvx2<2, 2>;
        else if ((opt == -1 && iset >= 2) || opt == 1)
            p_ = processSse2<2, 2>;
        else
            p_ = processC<2, 2>;
    }
    else if (vi.IsYV16())
    {
        subsamplingY_ = 1;
        subsamplingX_ = 2;
        proc_lut = processLut<2, 1>;

        if ((opt == -1 && iset >= 8) || opt == 2)
            p_ = processAvx2<2, 1>;
        else if ((opt == -1 && iset >= 2) || opt == 1)
            p_ = processSse2<2, 1>;
        else
            p_ = processC<2, 1>;
    }
    else
        env->ThrowError("Only YV12, YV16 and YV24 are supported!");

    const float kR = bt601 ? 0.299f : 0.2126f;
    const float kB = bt601 ? 0.114f : 0.0722f;

    colors_.reserve(colors.size());

    for (auto color : colors)
    {
        YUVPixel p;
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

    if (colors_.size() > lutthr)
        proc = (vi.width % 16) ? &TColorMask::process<true, true> : &TColorMask::process<true, false>;
    else
        proc = (vi.width % 16) ? &TColorMask::process<false, true> : &TColorMask::process<false, false>;

    if (((vi.width % 16) != 0) || (colors_.size() > lutthr))
        buildLuts();

    try { env->CheckVersion(8); }
    catch (const AvisynthError&) { v8 = false; };
}

void TColorMask::buildLuts() noexcept
{
    for (int i = 0; i < 256; ++i)
    {
        uint8_t val_y = 0;
        uint8_t val_u = 0;
        uint8_t val_v = 0;

        for (auto& color : colors_)
        {
            val_y |= ((abs(i - color.Y) < tolerance_) ? 255 : 0);
            val_u |= ((abs(i - color.U) < (tolerance_ / 2)) ? 255 : 0);
            val_v |= ((abs(i - color.V) < (tolerance_ / 2)) ? 255 : 0);
        }

        lut_y[i] = val_y;
        lut_u[i] = val_u;
        lut_v[i] = val_v;
    }
}

PVideoFrame TColorMask::GetFrame(int n, IScriptEnvironment* env)
{
    PVideoFrame src = child->GetFrame(n, env);
    PVideoFrame dst = (v8) ? env->NewVideoFrameP(vi, &src) : env->NewVideoFrame(vi);

    const int width = src->GetRowSize(PLANAR_Y);
    const int height = src->GetHeight(PLANAR_Y);

    const uint8_t* srcY_ptr = src->GetReadPtr(PLANAR_Y);
    const uint8_t* srcU_ptr = src->GetReadPtr(PLANAR_U);
    const uint8_t* srcV_ptr = src->GetReadPtr(PLANAR_V);
    const int src_pitch_y = src->GetPitch(PLANAR_Y);
    const int src_pitch_uv = src->GetPitch(PLANAR_U);

    uint8_t* __restrict dstY_ptr = dst->GetWritePtr(PLANAR_Y);
    const int dst_pitch_y = dst->GetPitch(PLANAR_Y);

    if (mt_)
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

    if (grayscale_)
    {
        memset(dst->GetWritePtr(PLANAR_U), 128, dst->GetPitch(PLANAR_U) * dst->GetHeight(PLANAR_U));
        memset(dst->GetWritePtr(PLANAR_V), 128, dst->GetPitch(PLANAR_V) * dst->GetHeight(PLANAR_V));
    }

    return dst;
}

template <bool cs, bool border>
void TColorMask::process(uint8_t* __restrict dstY_ptr, const uint8_t* srcY_ptr, const uint8_t* srcV_ptr, const uint8_t* srcU_ptr, int dst_pitch_y, int src_pitch_y, int src_pitch_uv, int width, int height) noexcept
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

static int avisynthStringToInt(const std::string& str) noexcept
{
    if (str[0] == '$')
    {
        auto substr = str.substr(1, str.length());
        return strtol(substr.c_str(), 0, 16);
    }

    return strtol(str.c_str(), 0, 10);
}

AVSValue __cdecl CreateTColorMask(AVSValue args, void*, IScriptEnvironment* env)
{
    enum { CLIP, COLORS, TOLERANCE, BT601, GRAYSCALE, LUTTHR, MT, OPT };

    std::string str = args[COLORS].AsString("");

    std::regex e("(/\\*.*\\*/|//.*$)");   //comments
    str = std::regex_replace(str, e, "");

    std::vector<int> colors;
    std::regex rgx("(\\$?[\\da-fA-F]+)");

    std::sregex_token_iterator iter(str.cbegin(), str.cend(), rgx, 1);
    std::sregex_token_iterator end;
    for (; iter != end; ++iter)
    {
        try {
            colors.emplace_back(avisynthStringToInt(iter->str()));
        }
        catch (...) {
            env->ThrowError("Parsing error");
        }
    }

    return new TColorMask(args[CLIP].AsClip(), colors, args[TOLERANCE].AsInt(10), args[BT601].AsBool(false),
        args[GRAYSCALE].AsBool(false), args[LUTTHR].AsInt(9), args[MT].AsBool(false), args[OPT].AsInt(-1), env);
}

const AVS_Linkage* AVS_linkage = nullptr;

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment * env, const AVS_Linkage* const vectors) {
    AVS_linkage = vectors;

    env->AddFunction("tcolormaska", "c[colors]s[tolerance]i[bt601]b[gray]b[lutthr]i[mt]b[opt]i", CreateTColorMask, 0);
    return "Why are you looking at this?";
}
