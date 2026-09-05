/*
 *  Copyright (C) 2003-2006 Gabest
 *  http://www.gabest.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "stdafx.h"
#include <math.h>
#include <stdint.h>
#include <time.h>
#include "RTS.h"
#include "GdiTextRenderer.h"
#include "draw_item.h"
#include "cache_manager.h"
#include "subpixel_position_controler.h"
#include "xy_overlay_paint_machine.h"
#include "xy_clipper_paint_machine.h"
#include "../subpic/color_conv_table.h"

#if ENABLE_XY_LOG_TEXT_PARSER
#  define TRACE_PARSER(msg) XY_LOG_TRACE(msg)
#else
#  define TRACE_PARSER(msg)
#endif

#if ENABLE_XY_LOG_RENDERER_REQUEST2
#  define TRACE_RENDERER_REQUEST(msg) XY_LOG_TRACE(msg)
#  define TRACE_RENDERER_REQUEST_TIMING(msg) XY_AUTO_TIMING(msg)
#else
#  define TRACE_RENDERER_REQUEST(msg)
#  define TRACE_RENDERER_REQUEST_TIMING(msg)
#endif

const int MAX_SUB_PIXEL = 8;
const double MAX_SUB_PIXEL_F = 8.0;

// WARNING: this isn't very thread safe, use only one RTS a time.
static HDC g_hDC;
static int g_hDC_refcnt = 0;

class ScopedGraphicsMode
{
public:
    ScopedGraphicsMode(HDC dc, bool use_advanced_mode)
        : m_dc(dc)
        , m_previous_mode(0)
        , m_restore(false)
    {
        if (!use_advanced_mode) {
            return;
        }

        m_previous_mode = GetGraphicsMode(m_dc);
        if (m_previous_mode != 0 && m_previous_mode != GM_ADVANCED) {
            m_restore = SetGraphicsMode(m_dc, GM_ADVANCED) != 0;
        }
    }

    ~ScopedGraphicsMode()
    {
        if (m_restore) {
            SetGraphicsMode(m_dc, m_previous_mode);
        }
    }

private:
    HDC m_dc;
    int m_previous_mode;
    bool m_restore;
};

static long revcolor(long c)
{
    return ((c&0xff0000)>>16) + (c&0xff00) + ((c&0xff)<<16);
}

// Skip all leading whitespace
inline CStringW::PCXSTR SkipWhiteSpaceLeft(const CStringW& str)
{
    CStringW::PCXSTR psz = str.GetString();

    while( iswspace( *psz ) )
    {
        psz++;
    }
    return psz;
}

// Skip all trailing whitespace
inline CStringW::PCXSTR SkipWhiteSpaceRight(const CStringW& str)
{
    CStringW::PCXSTR psz = str.GetString();
    CStringW::PCXSTR pszLast = psz + str.GetLength() - 1;
    bool first_white = false;
    while( iswspace( *pszLast ) )
    {
        pszLast--;
        if(pszLast<psz)
            break;
    }
    return pszLast;
}

// Skip all leading whitespace
inline CStringW::PCXSTR SkipWhiteSpaceLeft(CStringW::PCXSTR start, CStringW::PCXSTR end)
{
    while( start!=end && iswspace( *start ) )
    {
        start++;
    }
    return start;
}

// Skip all trailing whitespace, first char must NOT be white space
inline CStringW::PCXSTR FastSkipWhiteSpaceRight(CStringW::PCXSTR start, CStringW::PCXSTR end)
{
    while( iswspace( *--end ) );
    return end+1;
}

inline CStringW::PCXSTR FindChar(CStringW::PCXSTR start, CStringW::PCXSTR end, WCHAR c)
{
    while( start!=end && *start!=c )
    {
        start++;
    }
    return start;
}

//////////////////////////////////////////////////////////////////////////////////////////////

// CMyFont

CMyFont::CMyFont(const STSStyleBase& style, double orientation)
{
    LOGFONT lf;
    ZeroMemory(&lf, sizeof(lf));
    lf <<= style;
    lf.lfHeight         = (LONG)(style.fontSize+0.5);
    lf.lfOutPrecision   = OUT_TT_PRECIS;
    lf.lfClipPrecision  = CLIP_DEFAULT_PRECIS;
    lf.lfQuality        = ANTIALIASED_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH|FF_DONTCARE;
    lf.lfOrientation    = static_cast<LONG>(orientation * 10.0);
    if(!CreateFontIndirect(&lf))
    {
        _tcscpy(lf.lfFaceName, _T("Arial"));
        VERIFY(CreateFontIndirect(&lf));
    }
    HFONT hOldFont = SelectFont(g_hDC, *this);
    TEXTMETRIC tm;
    GetTextMetrics(g_hDC, &tm);
    m_ascent  = ((tm.tmAscent  + 4) >> 3);
    m_descent = ((tm.tmDescent + 4) >> 3);
    SelectFont(g_hDC, hOldFont);
}

// CWord

CWord::CWord( const FwSTSStyle& style, const CStringW& str, int ktype, int kstart, int kend
    , double target_scale_x/*=1.0*/, double target_scale_y/*=1.0*/
    , bool round_to_whole_pixel_after_scale_to_target/*=false*/
    , const SharedPtrConstModStyleState& mod_style/*=SharedPtrConstModStyleState()*/
    , double mod_scale_x/*=1.0*/, double mod_scale_y/*=1.0*/
    , bool mod_compatibility_mode/*=false*/)
    : m_style(style), m_str(DEBUG_NEW CStringW(str))
    , m_mod_style(mod_style)
    , m_width(0), m_ascent(0), m_descent(0)
    , m_ktype(ktype), m_kstart(kstart), m_kend(kend)
    , m_fLineBreak(false), m_fWhiteSpaceChar(false)
    , m_target_scale_x(target_scale_x), m_target_scale_y(target_scale_y)
    , m_is_opaque_box(false)
    , m_mod_scale_x(mod_scale_x), m_mod_scale_y(mod_scale_y)
    , m_mod_compatibility_mode(mod_compatibility_mode)
    , m_round_to_whole_pixel_after_scale_to_target(round_to_whole_pixel_after_scale_to_target)
    //, m_pOpaqueBox(NULL)
{
    if(m_str.Get().IsEmpty())
    {
        m_fWhiteSpaceChar = m_fLineBreak = true;
    }
    m_width = 0;
}

CWord::CWord( const CWord& src):m_str(src.m_str)
{
    m_fWhiteSpaceChar                            = src.m_fWhiteSpaceChar;
    m_fLineBreak                                 = src.m_fLineBreak;
    m_style                                      = src.m_style;
    m_mod_style                                  = src.m_mod_style;
    m_pOpaqueBox                                 = src.m_pOpaqueBox;//allow since it is shared_ptr
    m_ktype                                      = src.m_ktype;
    m_kstart                                     = src.m_kstart;
    m_kend                                       = src.m_kend;
    m_width                                      = src.m_width;
    m_ascent                                     = src.m_ascent;
    m_descent                                    = src.m_descent;
    m_is_opaque_box                              = src.m_is_opaque_box;
    m_target_scale_x                             = src.m_target_scale_x;
    m_target_scale_y                             = src.m_target_scale_y;
    m_mod_scale_x                                = src.m_mod_scale_x;
    m_mod_scale_y                                = src.m_mod_scale_y;
    m_mod_compatibility_mode                     = src.m_mod_compatibility_mode;
    m_round_to_whole_pixel_after_scale_to_target = src.m_round_to_whole_pixel_after_scale_to_target;
}

CWord::~CWord()
{
    //if(m_pOpaqueBox) delete m_pOpaqueBox;
}

bool CWord::Append(const SharedPtrCWord& w)
{
    if (m_style != w->m_style ||
          m_mod_style.get() != w->m_mod_style.get() ||
          m_fLineBreak || w->m_fLineBreak || 
          w->m_kstart != w->m_kend || m_ktype != w->m_ktype)
          return(false);
    m_fWhiteSpaceChar = m_fWhiteSpaceChar && w->m_fWhiteSpaceChar;
    CStringW *str = DEBUG_NEW CStringW();//Fix me: anyway to avoid this flyweight update?
    ASSERT(str);
    *str  =    m_str.Get();
    *str += w->m_str.Get();
    m_str = XyFwStringW(str);
    m_width += w->m_width;
    return(true);
}

void CWord::PaintFromOverlay(const CPointCoor2& p, const CPointCoor2& trans_org2, OverlayKey &subpixel_variance_key, SharedPtrOverlay& overlay)
{
    if( SubpixelPositionControler::GetGlobalControler().UseBilinearShift() )
    {
        CPoint psub = SubpixelPositionControler::GetGlobalControler().GetSubpixel(p);
        if( (psub.x!=(p.x&SubpixelPositionControler::EIGHT_X_EIGHT_MASK) || 
             psub.y!=(p.y&SubpixelPositionControler::EIGHT_X_EIGHT_MASK)) )
        {
            overlay.reset(overlay->GetSubpixelVariance((p.x&SubpixelPositionControler::EIGHT_X_EIGHT_MASK) - psub.x, 
                (p.y&SubpixelPositionControler::EIGHT_X_EIGHT_MASK) - psub.y));
            OverlayMruCache* overlay_cache = CacheManager::GetSubpixelVarianceCache();
            overlay_cache->UpdateCache(subpixel_variance_key, overlay);
        }
    }
}

void CWord::PaintFromNoneBluredOverlay(SharedPtrOverlay raterize_result, const OverlayKey& overlay_key, SharedPtrOverlay* overlay)
{
    if( Rasterizer::IsItReallyBlur(m_style.get().fBlur, m_style.get().fGaussianBlur) )
    {
        overlay->reset(DEBUG_NEW Overlay());
        if (!Rasterizer::Blur(
            *raterize_result, 
            m_style.get().fBlur, 
            m_style.get().fGaussianBlur, 
            m_target_scale_x, 
            m_target_scale_y, 
            *overlay,
            m_mod_compatibility_mode))
        {
            *overlay = raterize_result;
        }
    }
    else
    {
        *overlay = raterize_result;
    }
    OverlayMruCache* overlay_cache = CacheManager::GetOverlayMruCache();
    overlay_cache->UpdateCache(overlay_key, *overlay);
}

bool CWord::PaintFromScanLineData2(const CPointCoor2& psub, const ScanLineData2& scan_line_data2, const OverlayKey& key, SharedPtrOverlay* overlay)
{
    SharedPtrOverlay raterize_result(DEBUG_NEW Overlay());
    if(!Rasterizer::Rasterize(scan_line_data2, psub.x, psub.y, raterize_result,
            m_mod_compatibility_mode)) 
    {     
        return false;
    }
    OverlayNoBlurMruCache* overlay_no_blur_cache = CacheManager::GetOverlayNoBlurMruCache();
    overlay_no_blur_cache->UpdateCache(key, raterize_result);
    PaintFromNoneBluredOverlay(raterize_result, key, overlay);
    return true;
}

bool CWord::PaintFromPathData(const CPointCoor2& psub, const CPointCoor2& trans_org, const PathData& path_data, const OverlayKey& key, SharedPtrOverlay* overlay )
{
    bool result = false;

    PathData *path_data2 = DEBUG_NEW PathData(path_data);//fix me: this copy operation can be saved if no transform is needed
    SharedPtrConstPathData shared_ptr_path_data2(path_data2);
    bool need_transform = NeedTransform();
    if(need_transform)
        Transform(path_data2, CPoint(trans_org.x*MAX_SUB_PIXEL, trans_org.y*MAX_SUB_PIXEL));

    CPoint left_top;
    CSize  size;
    path_data2->AlignLeftTop(&left_top, &size);

    int border_x = static_cast<int>(m_style.get().outlineWidthX*m_target_scale_x+0.5);//fix me: rounding err
    int border_y = static_cast<int>(m_style.get().outlineWidthY*m_target_scale_y+0.5);//fix me: rounding err
    int wide_border = border_x>border_y ? border_x:border_y;
    if (m_style.get().borderStyle==1)
    {
        border_x = border_y = 0;
    }

    OverlayNoOffsetMruCache* overlay_key_cache = CacheManager::GetOverlayNoOffsetMruCache();
    OverlayNoOffsetKey overlay_no_offset_key(shared_ptr_path_data2, psub.x, psub.y, border_x, border_y);
    overlay_no_offset_key.UpdateHashValue();
    POSITION pos_key = overlay_key_cache->Lookup(overlay_no_offset_key);
    POSITION pos = NULL;
        
    OverlayNoBlurMruCache* overlay_cache = CacheManager::GetOverlayNoBlurMruCache();
    if (pos_key!=NULL)
    {
        OverlayNoBlurKey overlay_key = overlay_key_cache->GetAt(pos_key);
        pos = overlay_cache->Lookup(overlay_key);
    }
    if (pos)
    {
        SharedPtrOverlay raterize_result( DEBUG_NEW Overlay() );
        *raterize_result = *overlay_cache->GetAt(pos);
        raterize_result->mOffsetX = left_top.x - psub.x - ((wide_border+7)&~7);
        raterize_result->mOffsetY = left_top.y - psub.y - ((wide_border+7)&~7);
        PaintFromNoneBluredOverlay(raterize_result, key, overlay);
        result = true;
        overlay_cache->UpdateCache(key, raterize_result);
    }
    else
    {
        ScanLineDataMruCache* scan_line_data_cache = CacheManager::GetScanLineDataMruCache();
        pos = scan_line_data_cache->Lookup(overlay_no_offset_key);
        SharedPtrConstScanLineData scan_line_data;
        if( pos != NULL )
        {
            scan_line_data = scan_line_data_cache->GetAt(pos);
            scan_line_data_cache->UpdateCache(pos);
        }
        else
        {
            ScanLineData *tmp = DEBUG_NEW ScanLineData();
            scan_line_data.reset(tmp);
            if(!tmp->ScanConvert(*path_data2, size))
            {
                return false;
            }
            scan_line_data_cache->UpdateCache(overlay_no_offset_key, scan_line_data);
        }
        ScanLineData2 *tmp = DEBUG_NEW ScanLineData2(left_top, scan_line_data);
        SharedPtrScanLineData2 scan_line_data2( tmp );
        if(m_style.get().borderStyle == 0 && (m_style.get().outlineWidthX+m_style.get().outlineWidthY > 0))
        {
            if(!tmp->CreateWidenedRegion(border_x, border_y, m_mod_compatibility_mode)) 
            {
                return false;
            }
        }
        ScanLineData2MruCache* scan_line_data2_cache = CacheManager::GetScanLineData2MruCache();
        scan_line_data2_cache->UpdateCache(key, scan_line_data2);
        result = PaintFromScanLineData2(psub, *tmp, key, overlay);
    }
    if (result)
    {
        if (pos_key!=NULL)
        {
            overlay_key_cache->UpdateCache(pos_key, key);
        }
        else
        {
            overlay_key_cache->UpdateCache(overlay_no_offset_key, key);
        }
    }
    return result;
}

bool CWord::PaintFromRawData( const CPointCoor2& psub, const CPointCoor2& trans_org, const OverlayKey& key, SharedPtrOverlay* overlay )
{
    PathDataMruCache* path_data_cache = CacheManager::GetPathDataMruCache();

    PathData *tmp=DEBUG_NEW PathData();
    SharedPtrPathData path_data(tmp);
    if(!CreatePath(tmp))
    {
        return false;
    }
    path_data_cache->UpdateCache(key, path_data);
    return PaintFromPathData(psub, trans_org, *tmp, key, overlay);
}

bool CWord::DoPaint(const CPointCoor2& psub, const CPointCoor2& trans_org, SharedPtrOverlay* overlay, const OverlayKey& key)
{
    bool result = true;
    OverlayNoBlurMruCache* overlay_no_blur_cache = CacheManager::GetOverlayNoBlurMruCache();
    POSITION pos = overlay_no_blur_cache->Lookup(key);

    if(pos!=NULL)
    {
        SharedPtrOverlay raterize_result = overlay_no_blur_cache->GetAt(pos);
        overlay_no_blur_cache->UpdateCache( pos );
        PaintFromNoneBluredOverlay(raterize_result, key, overlay);
    }  
    else
    {
        ScanLineData2MruCache* scan_line_data_cache = CacheManager::GetScanLineData2MruCache();
        pos = scan_line_data_cache->Lookup(key);
        if(pos!=NULL)
        {
            SharedPtrConstScanLineData2 scan_line_data = scan_line_data_cache->GetAt(pos);
            scan_line_data_cache->UpdateCache( pos );
            result = PaintFromScanLineData2(psub, *scan_line_data, key, overlay);
        }
        else
        {     
            PathDataMruCache* path_data_cache = CacheManager::GetPathDataMruCache();
            POSITION pos_path = path_data_cache->Lookup(key);
            if(pos_path!=NULL)    
            {
                SharedPtrConstPathData path_data = path_data_cache->GetAt(pos_path); //important! copy not ref
                path_data_cache->UpdateCache( pos_path );
                result = PaintFromPathData(psub, trans_org, *path_data, key, overlay);
            }
            else
            {
                result = PaintFromRawData(psub, trans_org, key, overlay);
            }
        }
    }
    return result;
}

bool CWord::NeedTransform()
{
    // VSFilterMod runs its SSE transform for every word, including the
    // identity/default case.  Keeping the identity path out of the transform
    // would preserve xy's geometry but produces different edge coverage in
    // MOD mode (especially after outline/blur).  Legacy mode retains the
    // existing predicate and therefore its cache/performance path.
    if (m_mod_compatibility_mode) {
        return true;
    }
    const bool mod_transform = m_mod_style
        && (m_mod_style->feature_mask & (MOD_FEATURE_Z | MOD_FEATURE_RANDOM | MOD_FEATURE_DISTORT));
    return mod_transform ||
           (fabs(m_style.get().fontScaleX - 100) > 0.000001) ||
           (fabs(m_style.get().fontScaleY - 100) > 0.000001) ||
           (fabs(m_style.get().fontAngleX) > 0.000001) ||
           (fabs(m_style.get().fontAngleY) > 0.000001) ||
           (fabs(m_style.get().fontAngleZ) > 0.000001) ||
           (fabs(m_style.get().fontShiftX) > 0.000001) ||
           (fabs(m_style.get().fontShiftY) > 0.000001) ||
           (fabs(m_target_scale_x-1.0) > 0.000001) ||
           (fabs(m_target_scale_y-1.0) > 0.000001);
}

//void CWord::Transform(PathData* path_data, const CPointCoor2& org)
//{
//    //// CPUID from VDub
//    //bool fSSE2 = !!(g_cpuid.m_flags & CCpuID::sse2);
//
//    //if(fSSE2) {	// SSE code
//    //	Transform_SSE2(path_data, org);
//    //} else		// C-code
//          Transform_C(path_data, org);
//}

void CWord::Transform(PathData* path_data, const CPointCoor2 &org )
{
    ASSERT(path_data);
    if (m_mod_compatibility_mode) {
        TransformMod(path_data, org);
        return;
    }
    const STSStyle& style = m_style.get();

    const double scalex = style.fontScaleX/100.0;
    const double scaley = style.fontScaleY/100.0;

    const double caz = cos((M_PI/180.0)*style.fontAngleZ);
    const double saz = sin((M_PI/180.0)*style.fontAngleZ);
    const double cax = cos((M_PI/180.0)*style.fontAngleX);
    const double sax = sin((M_PI/180.0)*style.fontAngleX);
    const double cay = cos((M_PI/180.0)*style.fontAngleY);
    const double say = sin((M_PI/180.0)*style.fontAngleY);

    double xxx[3][3];
    /******************
          targetScaleX            0    0
     S0 =            0 targetScaleY    0
                     0            0    1
    /******************
          20000     0    0
     A0 =     0 20000    0
              0     0    1
    /******************
          cay    0  say
     A1 =   0    1    0
          say    0 -cay
    /******************
            1    0    0
     A2 =   0  cax  sax
            0  sax -cax
    /******************
          caz  saz    0
     A3 =-saz  caz    0
            0    0    1
    /******************
          scalex            scalex*fontShiftX -org.x/targetScaleX
     A4 = scaley*fontShiftY scaley            -org.y/targetScaleY
          0                 0                  0
    /******************
              0     0      0
     B0 =     0     0      0
              0     0  20000
    /******************
     Formula:
       (x,y,z)' = (S0*A0*A1*A2*A3*A4 + B0) * (x y 1)'
       z = max(1000,z)
       x = x/z + org.x
       y = y/z + org.y
    *******************/

    //A3*A4
    ASSERT(m_target_scale_x!=0 && m_target_scale_y!=0);
    double tmp1 = -org.x/m_target_scale_x;
    double tmp2 = -org.y/m_target_scale_y;

    xxx[0][0] = caz*scalex + saz*scaley*style.fontShiftY;
    xxx[0][1] = caz*scalex*style.fontShiftX + saz*scaley;
    xxx[0][2] = caz*tmp1 + saz*tmp2;

    xxx[1][0] = -saz*scalex + caz*scaley*style.fontShiftY;
    xxx[1][1] = -saz*scalex*style.fontShiftX + caz*scaley;
    xxx[1][2] = -saz*tmp1 + caz*tmp2;

    xxx[2][0] = 
    xxx[2][0] = 
    xxx[2][0] = 0;
    
    //A2*A3*A4

    xxx[2][0] = sax*xxx[1][0];
    xxx[2][1] = sax*xxx[1][1];
    xxx[2][2] = sax*xxx[1][2];

    xxx[1][0] = cax*xxx[1][0];
    xxx[1][1] = cax*xxx[1][1];
    xxx[1][2] = cax*xxx[1][2];

    //A1*A2*A3*A4

    tmp1 = xxx[0][0];
    tmp2 = xxx[0][1];
    double tmp3 = xxx[0][2];
    xxx[0][0] = cay*tmp1 + say*xxx[2][0];
    xxx[0][1] = cay*tmp2 + say*xxx[2][1];
    xxx[0][2] = cay*tmp3 + say*xxx[2][2];

    xxx[2][0] = say*tmp1 - cay*xxx[2][0];
    xxx[2][1] = say*tmp2 - cay*xxx[2][1];
    xxx[2][2] = say*tmp3 - cay*xxx[2][2];

    //S0*A0*A1*A2*A3*A4

    tmp1 = 20000.0*m_target_scale_x;
    xxx[0][0] *= tmp1;
    xxx[0][1] *= tmp1;
    xxx[0][2] *= tmp1;

    tmp1 = 20000.0*m_target_scale_y;
    xxx[1][0] *= tmp1;
    xxx[1][1] *= tmp1;
    xxx[1][2] *= tmp1;

    //A0*A1*A2*A3*A4+B0

    xxx[2][2] += 20000.0;

    double scaled_org_x = org.x+0.5;
    double scaled_org_y = org.y+0.5;

    for (ptrdiff_t i = 0; i < path_data->mPathPoints; i++) {
        double x, y, z, xx;

        xx = path_data->mpPathPoints[i].x;
        y = path_data->mpPathPoints[i].y;

        z = xxx[2][0] * xx + xxx[2][1] * y + xxx[2][2];
        x = xxx[0][0] * xx + xxx[0][1] * y + xxx[0][2];
        y = xxx[1][0] * xx + xxx[1][1] * y + xxx[1][2];

        z = z > 1000.0 ? z : 1000.0;

        x = x / z;
        y = y / z;

        path_data->mpPathPoints[i].x = (long)(x + scaled_org_x);
        path_data->mpPathPoints[i].y = (long)(y + scaled_org_y);
        if (m_round_to_whole_pixel_after_scale_to_target && (m_target_scale_x!=1.0 || m_target_scale_y!=1.0))
        {
            path_data->mpPathPoints[i].x = (path_data->mpPathPoints[i].x + 32)&~63;
            path_data->mpPathPoints[i].y = (path_data->mpPathPoints[i].y + 32)&~63;//fix me: readability
        }
    }
}

void CWord::TransformMod(PathData* path_data, const CPointCoor2& org)
{
    ASSERT(path_data);
    const STSStyle& style = m_style.get();
    const ModStyleState empty_mod;
    const ModStyleState& mod = m_mod_style ? *m_mod_style : empty_mod;

    // VSFilterMod's hot path performs these calculations in SSE float
    // precision and uses its historical pi constant.  Keep the trigonometric
    // values as doubles until _mm_set1_ps performs the same conversion as the
    // upstream call to _mm_set_ps1(double).
    const double scalex_double = style.fontScaleX / 100.0;
    const double scaley_double = style.fontScaleY / 100.0;
    const double caz_double = cos((3.1415 / 180.0) * style.fontAngleZ);
    const double saz_double = sin((3.1415 / 180.0) * style.fontAngleZ);
    const double cax_double = cos((3.1415 / 180.0) * style.fontAngleX);
    const double sax_double = sin((3.1415 / 180.0) * style.fontAngleX);
    const double cay_double = cos((3.1415 / 180.0) * style.fontAngleY);
    const double say_double = sin((3.1415 / 180.0) * style.fontAngleY);
    const float scalex = static_cast<float>(m_is_opaque_box ? 1.0 : scalex_double);
    const float scaley = static_cast<float>(m_is_opaque_box ? 1.0 : scaley_double);
    const float caz = static_cast<float>(caz_double);
    const float saz = static_cast<float>(saz_double);
    const float cax = static_cast<float>(cax_double);
    const float sax = static_cast<float>(sax_double);
    const float cay = static_cast<float>(cay_double);
    const float say = static_cast<float>(say_double);

    // VSFilterMod's normal MOD path is an SSE2 four-point loop.  Keep the
    // same operation order for common ASS transforms; scalar MOD features
    // below intentionally retain their separate random/distort handling.
    if (!(mod.feature_mask & (MOD_FEATURE_Z | MOD_FEATURE_RANDOM | MOD_FEATURE_DISTORT))) {
        const __m128 xshift = _mm_set1_ps(static_cast<float>(style.fontShiftX));
        const __m128 yshift = _mm_set1_ps(static_cast<float>(style.fontShiftY));
        const __m128 xscale = _mm_set1_ps(scalex);
        const __m128 yscale = _mm_set1_ps(scaley);
        const __m128 source_org_x = _mm_set1_ps(
            static_cast<float>(org.x / m_target_scale_x));
        const __m128 source_org_y = _mm_set1_ps(
            static_cast<float>(org.y / m_target_scale_y));
        const __m128 render_org_x = _mm_set1_ps(static_cast<float>(org.x));
        const __m128 render_org_y = _mm_set1_ps(static_cast<float>(org.y));
        const __m128 caz_vec = _mm_set1_ps(static_cast<float>(caz_double));
        const __m128 saz_vec = _mm_set1_ps(static_cast<float>(saz_double));
        const __m128 cax_vec = _mm_set1_ps(static_cast<float>(cax_double));
        const __m128 sax_vec = _mm_set1_ps(static_cast<float>(sax_double));
        const __m128 cay_vec = _mm_set1_ps(static_cast<float>(cay_double));
        const __m128 say_vec = _mm_set1_ps(static_cast<float>(say_double));
        const __m128 xzoomf = _mm_set1_ps(static_cast<float>(m_mod_scale_x * 20000.0));
        const __m128 yzoomf = _mm_set1_ps(static_cast<float>(m_mod_scale_y * 20000.0));
        const __m128 min_focal = _mm_set1_ps(1000.0f);
        const __m128 half = _mm_set1_ps(0.5f);

        for (int base = 0; base < path_data->mPathPoints; base += 4) {
            const int count = min(4, path_data->mPathPoints - base);
            const float x0 = static_cast<float>(path_data->mpPathPoints[base].x);
            const float y0 = static_cast<float>(path_data->mpPathPoints[base].y);
            const float x1 = count > 1
                ? static_cast<float>(path_data->mpPathPoints[base + 1].x) : 0.0f;
            const float y1 = count > 1
                ? static_cast<float>(path_data->mpPathPoints[base + 1].y) : 0.0f;
            const float x2 = count > 2
                ? static_cast<float>(path_data->mpPathPoints[base + 2].x) : 0.0f;
            const float y2 = count > 2
                ? static_cast<float>(path_data->mpPathPoints[base + 2].y) : 0.0f;
            const float x3 = count > 3
                ? static_cast<float>(path_data->mpPathPoints[base + 3].x) : 0.0f;
            const float y3 = count > 3
                ? static_cast<float>(path_data->mpPathPoints[base + 3].y) : 0.0f;

            // _mm_set_ps mirrors VSFilterMod's lane order: point 0 is lane 3
            // and the results are written back in reverse lane order.
            __m128 point_x = _mm_set_ps(x0, x1, x2, x3);
            __m128 point_y = _mm_set_ps(y0, y1, y2, y3);

            __m128 tmp_x;
            if (style.fontShiftX != 0) {
                tmp_x = _mm_mul_ps(xshift, point_y);
                tmp_x = _mm_add_ps(tmp_x, point_x);
            } else {
                tmp_x = point_x;
            }
            tmp_x = _mm_mul_ps(tmp_x, xscale);
            tmp_x = _mm_sub_ps(tmp_x, source_org_x);

            __m128 tmp_y;
            if (style.fontShiftY != 0) {
                tmp_y = _mm_mul_ps(yshift, point_x);
                tmp_y = _mm_add_ps(tmp_y, point_y);
            } else {
                tmp_y = point_y;
            }
            tmp_y = _mm_mul_ps(tmp_y, yscale);
            tmp_y = _mm_sub_ps(tmp_y, source_org_y);

            __m128 xx = _mm_mul_ps(tmp_x, caz_vec);
            __m128 yy = _mm_mul_ps(tmp_y, saz_vec);
            point_x = _mm_add_ps(xx, yy);
            xx = _mm_mul_ps(tmp_x, saz_vec);
            yy = _mm_mul_ps(tmp_y, caz_vec);
            point_y = _mm_sub_ps(yy, xx);
            __m128 point_z = _mm_set1_ps(static_cast<float>(mod.z));

            __m128 zz = _mm_mul_ps(point_z, sax_vec);
            yy = _mm_mul_ps(point_y, cax_vec);
            tmp_y = point_y;
            point_y = _mm_add_ps(yy, zz);
            zz = _mm_mul_ps(point_z, cax_vec);
            yy = _mm_mul_ps(tmp_y, sax_vec);
            point_z = _mm_sub_ps(yy, zz);

            xx = _mm_mul_ps(point_x, cay_vec);
            zz = _mm_mul_ps(point_z, say_vec);
            tmp_x = point_x;
            point_x = _mm_add_ps(xx, zz);
            xx = _mm_mul_ps(tmp_x, say_vec);
            zz = _mm_mul_ps(point_z, cay_vec);
            point_z = _mm_sub_ps(xx, zz);

            __m128 denominator = _mm_add_ps(point_z, xzoomf);
            xx = _mm_mul_ps(point_x, xzoomf);
            point_x = _mm_div_ps(xx, _mm_max_ps(denominator, min_focal));
            denominator = _mm_add_ps(point_z, yzoomf);
            yy = _mm_mul_ps(point_y, yzoomf);
            point_y = _mm_div_ps(yy, _mm_max_ps(denominator, min_focal));

            point_x = _mm_add_ps(point_x, render_org_x);
            point_y = _mm_add_ps(point_y, render_org_y);
            point_x = _mm_add_ps(point_x, half);
            point_y = _mm_add_ps(point_y, half);

            float output_x[4];
            float output_y[4];
            _mm_storeu_ps(output_x, point_x);
            _mm_storeu_ps(output_y, point_y);
            for (int k = 0; k < count; ++k) {
                path_data->mpPathPoints[base + k].x = static_cast<LONG>(output_x[3 - k]);
                path_data->mpPathPoints[base + k].y = static_cast<LONG>(output_y[3 - k]);
                if (m_round_to_whole_pixel_after_scale_to_target
                        && (m_target_scale_x != 1.0 || m_target_scale_y != 1.0)) {
                    path_data->mpPathPoints[base + k].x =
                        (path_data->mpPathPoints[base + k].x + 32) & ~63;
                    path_data->mpPathPoints[base + k].y =
                        (path_data->mpPathPoints[base + k].y + 32) & ~63;
                }
            }
        }
        return;
    }

    LONG min_x = LONG_MAX;
    LONG min_y = LONG_MAX;
    LONG max_x = LONG_MIN;
    LONG max_y = LONG_MIN;
    if (mod.feature_mask & MOD_FEATURE_DISTORT) {
        for (ptrdiff_t i = 0; i < path_data->mPathPoints; ++i) {
            min_x = min(min_x, path_data->mpPathPoints[i].x);
            min_y = min(min_y, path_data->mpPathPoints[i].y);
            max_x = max(max_x, path_data->mpPathPoints[i].x);
            max_y = max(max_y, path_data->mpPathPoints[i].y);
        }
    }
    const float width = max_x > min_x ? static_cast<float>(max_x - min_x) : 0.0f;
    const float height = max_y > min_y ? static_cast<float>(max_y - min_y) : 0.0f;
    const float inverse_width = width > 0
        ? _mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(width))) : 0.0f;
    const float inverse_height = height > 0
        ? _mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(height))) : 0.0f;
    ModRandomGenerator random(static_cast<uint32_t>(mod.random_seed));
    float random_x[4] = {};
    float random_y[4] = {};
    float random_z[4] = {};
    const auto random_offset = [&random](double amplitude) -> float {
        const double scaled = amplitude * 100.0;
        const int range = static_cast<int>(scaled * 2.0 + 1.0);
        return scaled > 0.0 && range > 0
            ? static_cast<float>(scaled - random.Next() % range) * 0.01f
            : 0.0f;
    };

    for (ptrdiff_t i = 0; i < path_data->mPathPoints; ++i) {
        float x = static_cast<float>(path_data->mpPathPoints[i].x);
        float y = static_cast<float>(path_data->mpPathPoints[i].y);
        float z = static_cast<float>(mod.z);

        if ((mod.feature_mask & MOD_FEATURE_DISTORT) && width > 0 && height > 0) {
            const float u = (x - static_cast<float>(min_x)) * inverse_width;
            const float v = (y - static_cast<float>(min_y)) * inverse_height;
            const float distort_x0 = static_cast<float>(mod.distort_x[0]);
            const float distort_x2 = static_cast<float>(mod.distort_x[2]);
            float distort_x213 = static_cast<float>(mod.distort_x[1]);
            distort_x213 -= distort_x0;
            distort_x213 -= distort_x2;
            const float distort_y0 = static_cast<float>(mod.distort_y[0]);
            const float distort_y2 = static_cast<float>(mod.distort_y[2]);
            float distort_y213 = static_cast<float>(mod.distort_y[1]);
            distort_y213 -= distort_y0;
            distort_y213 -= distort_y2;
            x = ((distort_x213 * u * v + distort_x2 * v + distort_x0 * u)
                * width) + static_cast<float>(min_x);
            y = ((distort_y213 * u * v + distort_y2 * v + distort_y0 * u)
                * height) + static_cast<float>(min_y);
        }

        if (mod.feature_mask & MOD_FEATURE_RANDOM) {
            const int batch_index = static_cast<int>(i & 3);
            if (batch_index == 0) {
                for (int k = 0; k < 4; ++k) {
                    random_x[k] = random_offset(mod.random_x);
                    random_y[k] = random_offset(mod.random_y);
                    random_z[k] = random_offset(mod.random_z);
                }
            }
            const int source_index = 3 - batch_index;
            x += random_x[source_index];
            y += random_y[source_index];
            z += random_z[source_index];
        }

        const float original_x = x;
        x = scalex * (x + static_cast<float>(style.fontShiftX) * y)
            - static_cast<float>(org.x / m_target_scale_x);
        y = scaley * (y + static_cast<float>(style.fontShiftY) * original_x)
            - static_cast<float>(org.y / m_target_scale_y);

        float xx = x * caz + y * saz;
        float yy = y * caz - x * saz;
        float zz = z;

        x = xx;
        y = yy * cax + zz * sax;
        z = yy * sax - zz * cax;

        xx = x * cay + z * say;
        yy = y;
        zz = x * say - z * cay;

        // VSFilterMod uses the script-to-render scale as the focal length.
        // Keeping this separate from m_target_scale is important when the
        // video is rendered at a different size than PlayRes.
        const float xzoomf = static_cast<float>(m_mod_scale_x * 20000.0);
        const float yzoomf = static_cast<float>(m_mod_scale_y * 20000.0);
        const float projected_x = xx * xzoomf / max(zz + xzoomf, 1000.0f);
        const float projected_y = yy * yzoomf / max(zz + yzoomf, 1000.0f);

        path_data->mpPathPoints[i].x = static_cast<LONG>(
            projected_x + static_cast<float>(org.x) + 0.5f);
        path_data->mpPathPoints[i].y = static_cast<LONG>(
            projected_y + static_cast<float>(org.y) + 0.5f);
        if (m_round_to_whole_pixel_after_scale_to_target
                && (m_target_scale_x != 1.0 || m_target_scale_y != 1.0)) {
            path_data->mpPathPoints[i].x = (path_data->mpPathPoints[i].x + 32) & ~63;
            path_data->mpPathPoints[i].y = (path_data->mpPathPoints[i].y + 32) & ~63;
        }
    }
}

bool CWord::CreateOpaqueBox()
{
    if(m_pOpaqueBox) return(true);
    STSStyle style      = m_style.get();
    style.borderStyle   = 0;
    style.outlineWidthX = style.outlineWidthY = 0;
    style.colors[0]     = m_style.get().colors[2];
    style.alpha[0]      = m_style.get().alpha[2];
    int w               = (int)(m_style.get().outlineWidthX + 0.5);
    int h               = (int)(m_style.get().outlineWidthY + 0.5);
    CStringW str;
    str.Format(L"m %d %d l %d %d %d %d %d %d",
               -w,                   -h,
        m_width+w,                   -h,
        m_width+w, m_ascent+m_descent+h,
               -w, m_ascent+m_descent+h);
    m_pOpaqueBox.reset( DEBUG_NEW CPolygon(FwSTSStyle(style), str, 0, 0, 0, 1.0/MAX_SUB_PIXEL, 1.0/MAX_SUB_PIXEL, 0,
        m_target_scale_x, m_target_scale_y, false, m_mod_style,
        m_mod_compatibility_mode) );
    if (m_pOpaqueBox) {
        m_pOpaqueBox->m_is_opaque_box = true;
    }
    return(!!m_pOpaqueBox);
}

bool CWord::operator==( const CWord& rhs ) const
{
    return (         this ==&rhs) || (
        m_str.GetId()     == rhs.m_str.GetId()     &&
        m_fWhiteSpaceChar == rhs.m_fWhiteSpaceChar &&
        m_fLineBreak      == rhs.m_fLineBreak      &&
        m_style           == rhs.m_style           && //fix me:?
        ((m_mod_style.get() == rhs.m_mod_style.get()) ||
         (m_mod_style && rhs.m_mod_style && *m_mod_style == *rhs.m_mod_style)) &&
        m_ktype           == rhs.m_ktype           &&
        m_kstart          == rhs.m_kstart          &&
        m_kend            == rhs.m_kend            &&
        m_width           == rhs.m_width           &&
        m_ascent          == rhs.m_ascent          &&
        m_descent         == rhs.m_descent         &&
        m_target_scale_x  == rhs.m_target_scale_x  &&
        m_target_scale_y  == rhs.m_target_scale_y &&
        m_mod_scale_x     == rhs.m_mod_scale_x     &&
        m_mod_scale_y     == rhs.m_mod_scale_y &&
        m_is_opaque_box   == rhs.m_is_opaque_box &&
        m_mod_compatibility_mode == rhs.m_mod_compatibility_mode);
    //m_pOpaqueBox
}


// CText

CText::CText( const FwSTSStyle& style, const CStringW& str, int ktype, int kstart, int kend
    , double target_scale_x/*=1.0*/, double target_scale_y/*=1.0*/
    , TextRendererMode text_renderer_mode/*=TEXT_RENDERER_LEGACY_GDI*/
    , const SharedPtrConstModStyleState& mod_style/*=SharedPtrConstModStyleState()*/
    , double mod_scale_x/*=1.0*/, double mod_scale_y/*=1.0*/
    , bool mod_compatibility_mode/*=false*/ )
    : CWord(style, str, ktype, kstart, kend, target_scale_x, target_scale_y, false, mod_style,
        mod_scale_x, mod_scale_y, mod_compatibility_mode)
    , m_text_renderer_mode(NormalizeTextRendererMode(text_renderer_mode))
{
    if(m_str.Get() == L" ")
    {
        m_fWhiteSpaceChar = true;
    }
    SharedPtrTextInfo text_info;
    TextInfoCacheKey text_info_key;
    text_info_key.m_str_id = m_str.GetId();
    text_info_key.m_style  = m_style;
    text_info_key.m_text_renderer_mode = m_text_renderer_mode;
    text_info_key.m_font_orientation = m_mod_style
        && (m_mod_style->feature_mask & MOD_FEATURE_SYMBOL_ROTATION)
        ? m_mod_style->font_orientation : 0;
    text_info_key.UpdateHashValue();
    TextInfoMruCache* text_info_cache = CacheManager::GetTextInfoCache();
    POSITION pos = text_info_cache->Lookup(text_info_key);
    if(pos==NULL)
    {
        TextInfo* tmp=DEBUG_NEW TextInfo();
        GetTextInfo(tmp, m_style, m_str.Get(), m_text_renderer_mode, text_info_key.m_font_orientation);
        text_info.reset(tmp);
        text_info_cache->UpdateCache(text_info_key, text_info);
    }
    else
    {
        text_info = text_info_cache->GetAt(pos);
        text_info_cache->UpdateCache( pos );
    }
    this->m_ascent  = text_info->m_ascent;
    this->m_descent = text_info->m_descent;
    this->m_width   = text_info->m_width;
}

CText::CText( const CText& src )
    : CWord(src)
    , m_text_renderer_mode(src.m_text_renderer_mode)
{
    m_width = src.m_width;
}

SharedPtrCWord CText::Copy()
{
    SharedPtrCWord result(DEBUG_NEW CText(*this));
    return result;
}

bool CText::Append(const SharedPtrCWord& w)
{
    boost::shared_ptr<CText> p = boost::dynamic_pointer_cast<CText>(w);
    return (p && m_text_renderer_mode == p->m_text_renderer_mode && CWord::Append(w));
}

bool CText::CreatePath(PathData* path_data)
{
    bool succeeded = false;
    const bool has_orientation = m_mod_style
        && (m_mod_style->feature_mask & MOD_FEATURE_SYMBOL_ROTATION);
    ScopedGraphicsMode graphics_mode(g_hDC, has_orientation);
    FwCMyFont font(m_style);
    boost::shared_ptr<CMyFont> oriented_font;
    const CMyFont* selected_font = &font.get();
    if (has_orientation) {
        oriented_font.reset(DEBUG_NEW CMyFont(m_style.get(), m_mod_style->font_orientation));
        selected_font = oriented_font.get();
    }
    HFONT hOldFont = SelectFont(g_hDC, *selected_font);
    ASSERT(hOldFont);

    int width = 0;
    const CStringW& str = m_str.Get();
    if(m_style.get().fontSpacing || (long)GetVersion() < 0)
    {
        bool bFirstPath = true;
        for(LPCWSTR s = str; *s; s++)
        {
            CSize extent;
            GdiTextRenderer text_renderer(g_hDC, s, 1, m_text_renderer_mode);
            if(!text_renderer.GetExtent(&extent)) {SelectFont(g_hDC, hOldFont); ASSERT(0); return(false);}
            path_data->PartialBeginPath(g_hDC, bFirstPath);
            bFirstPath = false;
            if(!text_renderer.Draw(0, 0)) {SelectFont(g_hDC, hOldFont); ASSERT(0); return(false);}
            path_data->PartialEndPath(g_hDC, width, 0);
            width += extent.cx + (int)m_style.get().fontSpacing;
        }
    }
    else
    {
        CSize extent;
        GdiTextRenderer text_renderer(g_hDC, str, str.GetLength(), m_text_renderer_mode);
        succeeded = text_renderer.GetExtent(&extent);
        if(!succeeded)
        {
            SelectFont(g_hDC, hOldFont); ASSERT(0); return(false);
        }
        succeeded = path_data->BeginPath(g_hDC);
        if(!succeeded)
        {
            SelectFont(g_hDC, hOldFont); ASSERT(0); return(false);
        }
        succeeded = text_renderer.Draw(0, 0);
        if(!succeeded)
        {
            SelectFont(g_hDC, hOldFont); ASSERT(0); return(false);
        }
        succeeded = path_data->EndPath(g_hDC);
        if(!succeeded)
        {
            SelectFont(g_hDC, hOldFont); ASSERT(0); return(false);
        }
    }
    ASSERT(SelectFont(g_hDC, hOldFont));
    return(true);
}

void CText::GetTextInfo(TextInfo *output, const FwSTSStyle& style, const CStringW& str,
                        TextRendererMode text_renderer_mode, double font_orientation)
{
    ScopedGraphicsMode graphics_mode(g_hDC, font_orientation != 0);
    FwCMyFont font(style);
    boost::shared_ptr<CMyFont> oriented_font;
    const CMyFont* selected_font = &font.get();
    if (font_orientation != 0) {
        oriented_font.reset(DEBUG_NEW CMyFont(style.get(), font_orientation));
        selected_font = oriented_font.get();
    }
    output->m_ascent = (int)(style.get().fontScaleY/100*selected_font->m_ascent);
    output->m_descent = (int)(style.get().fontScaleY/100*selected_font->m_descent);

    HFONT hOldFont = SelectFont(g_hDC, *selected_font);
    const double orientation_radians = font_orientation * M_PI / 180.0;
    if(style.get().fontSpacing || (long)GetVersion() < 0)
    {
        bool bFirstPath = true;
        for(LPCWSTR s = str; *s; s++)
        {
            CSize extent;
            GdiTextRenderer text_renderer(g_hDC, s, 1, text_renderer_mode);
            if(!text_renderer.GetExtent(&extent)) {SelectFont(g_hDC, hOldFont); ASSERT(0); return;}
            output->m_width += static_cast<int>(
                extent.cx * fabs(cos(orientation_radians))
                + extent.cy * fabs(sin(orientation_radians))
                + style.get().fontSpacing);
        }
        //          m_width -= (int)m_style.get().fontSpacing; // TODO: subtract only at the end of the line
    }
    else
    {
        CSize extent;
        GdiTextRenderer text_renderer(g_hDC, str, str.GetLength(), text_renderer_mode);
        if(!text_renderer.GetExtent(&extent)) {SelectFont(g_hDC, hOldFont); ASSERT(0); return;}
        output->m_width += static_cast<int>(
            extent.cx * fabs(cos(orientation_radians))
            + extent.cy * fabs(sin(orientation_radians)));
    }
    output->m_width = (int)(style.get().fontScaleX/100*output->m_width + 4) >> 3;
    SelectFont(g_hDC, hOldFont);
}

// CPolygon

CPolygon::CPolygon( const FwSTSStyle& style, const CStringW& str, int ktype, int kstart, int kend 
    , double scalex, double scaley, int baseline 
    , double target_scale_x/*=1.0*/, double target_scale_y/*=1.0*/
    , bool round_to_whole_pixel_after_scale_to_target/*=false*/
    , const SharedPtrConstModStyleState& mod_style/*=SharedPtrConstModStyleState()*/
    , bool mod_compatibility_mode/*=false*/)
    : CWord(style, str, ktype, kstart, kend, target_scale_x, target_scale_y,
        round_to_whole_pixel_after_scale_to_target, mod_style, scalex, scaley,
        mod_compatibility_mode)
    , m_scalex(scalex), m_scaley(scaley), m_baseline(baseline)
{
    ParseStr();
}

CPolygon::CPolygon(CPolygon& src) : CWord(src)
{
    m_scalex           = src.m_scalex;
    m_scaley           = src.m_scaley;
    m_baseline         = src.m_baseline;
    m_width            = src.m_width;
    m_ascent           = src.m_ascent;
    m_descent          = src.m_descent;
    m_pathTypesOrg.Copy (src.m_pathTypesOrg );
    m_pathPointsOrg.Copy(src.m_pathPointsOrg);
}

CPolygon::~CPolygon()
{
}

SharedPtrCWord CPolygon::Copy()
{
    SharedPtrCWord result(DEBUG_NEW CPolygon(*this));
    return result;
}

bool CPolygon::Append(const SharedPtrCWord& w)
{
    // TODO
    return(false);
}

bool CPolygon::Get6BitFixedPoint(CStringW& str, LONG& ret)
{
    LPWSTR s = (LPWSTR)(LPCWSTR)str, e = s;
    ret = wcstod(str, &e) * 64;
    str.Delete(0,e-s); 
    TRACE_PARSER(ret);//fix me: use a specific logger for it
    return(e > s);
}

bool CPolygon::GetPOINT(CStringW& str, POINT& ret)
{
    return(Get6BitFixedPoint(str, ret.x) && Get6BitFixedPoint(str, ret.y));
}

bool CPolygon::ParseStr()
{
    if(m_pathTypesOrg.GetCount() > 0) return(true);
    CPoint p;
    int j, lastsplinestart = -1, firstmoveto = -1, lastmoveto = -1;
    CStringW str = m_str.Get();
    str.SpanIncluding(L"mnlbspc 0123456789");
    str.Replace(L"m", L"*m");
    str.Replace(L"n", L"*n");
    str.Replace(L"l", L"*l");
    str.Replace(L"b", L"*b");
    str.Replace(L"s", L"*s");
    str.Replace(L"p", L"*p");
    str.Replace(L"c", L"*c");
    int k = 0;
    for(CStringW s = str.Tokenize(L"*", k); !s.IsEmpty(); s = str.Tokenize(L"*", k))
    {
        WCHAR c = s[0];
        s.TrimLeft(L"mnlbspc ");
        switch(c)
        {
        case 'm':
            lastmoveto = m_pathTypesOrg.GetCount();
            if(firstmoveto == -1) 
                firstmoveto = lastmoveto;
            while(GetPOINT(s, p)) {
                m_pathTypesOrg.Add(PT_MOVETO); 
                m_pathPointsOrg.Add(p);
            }
            break;
        case 'n':
            while(GetPOINT(s, p)) {
                m_pathTypesOrg.Add(PT_MOVETONC);
                m_pathPointsOrg.Add(p);
            }
            break;
        case 'l':
            if (m_pathPointsOrg.GetCount() < 1) {
                break;
            }
            while(GetPOINT(s, p)) {
                m_pathTypesOrg.Add(PT_LINETO);
                m_pathPointsOrg.Add(p);
            }
            break;
        case 'b':
            j = m_pathTypesOrg.GetCount();
            if (j < 1) {
                break;
            }
            while(GetPOINT(s, p)) {
                m_pathTypesOrg.Add(PT_BEZIERTO);
                m_pathPointsOrg.Add(p);
                j++;
            }
            j = m_pathTypesOrg.GetCount() - ((m_pathTypesOrg.GetCount()-j)%3);
            m_pathTypesOrg.SetCount(j);
            m_pathPointsOrg.SetCount(j);
            break;
        case 's':
            if (m_pathPointsOrg.GetCount() < 1) {
                break;
            }
            {
                j = lastsplinestart = m_pathTypesOrg.GetCount();
                int i = 3;
                while(i-- && GetPOINT(s, p)) {
                    m_pathTypesOrg.Add(PT_BSPLINETO);
                    m_pathPointsOrg.Add(p);
                    j++;
                }
                if(m_pathTypesOrg.GetCount()-lastsplinestart < 3) {
                    m_pathTypesOrg.SetCount(lastsplinestart);
                    m_pathPointsOrg.SetCount(lastsplinestart);
                    lastsplinestart = -1;
                }
            }
            // no break here
        case 'p':
            if (m_pathPointsOrg.GetCount() < 3) {
                break;
            }
            while(GetPOINT(s, p)) {
                m_pathTypesOrg.Add(PT_BSPLINEPATCHTO);
                m_pathPointsOrg.Add(p);
            }
            break;
        case 'c':
            if(lastsplinestart > 0)
            {
                m_pathTypesOrg.Add(PT_BSPLINEPATCHTO);
                m_pathTypesOrg.Add(PT_BSPLINEPATCHTO);
                m_pathTypesOrg.Add(PT_BSPLINEPATCHTO);
                p = m_pathPointsOrg[lastsplinestart-1]; // we need p for temp storage, because operator [] will return a reference to CPoint and Add() may reallocate its internal buffer (this is true for MFC 7.0 but not for 6.0, hehe)
                m_pathPointsOrg.Add(p);
                p = m_pathPointsOrg[lastsplinestart];
                m_pathPointsOrg.Add(p);
                p = m_pathPointsOrg[lastsplinestart+1];
                m_pathPointsOrg.Add(p);
                lastsplinestart = -1;
            }
            break;
        default:
            break;
        }
    }
    if(lastmoveto == -1 || firstmoveto > 0)
    {
        m_pathTypesOrg.RemoveAll();
        m_pathPointsOrg.RemoveAll();
        return(false);
    }
    int minx = INT_MAX, miny = INT_MAX, maxx = -INT_MAX, maxy = -INT_MAX;
    for(size_t i = 0; i < m_pathTypesOrg.GetCount(); i++)
    {
        m_pathPointsOrg[i].x = (int)(m_scalex * m_pathPointsOrg[i].x);
        m_pathPointsOrg[i].y = (int)(m_scaley * m_pathPointsOrg[i].y);
        if(minx > m_pathPointsOrg[i].x) minx = m_pathPointsOrg[i].x;
        if(miny > m_pathPointsOrg[i].y) miny = m_pathPointsOrg[i].y;
        if(maxx < m_pathPointsOrg[i].x) maxx = m_pathPointsOrg[i].x;
        if(maxy < m_pathPointsOrg[i].y) maxy = m_pathPointsOrg[i].y;
    }
    m_width      = max(maxx - minx, 0);
    m_ascent     = max(maxy - miny, 0);
    int baseline = (int)(64 * m_scaley * m_baseline);
    m_descent    = baseline;
    m_ascent    -= baseline;
    if (m_mod_compatibility_mode) {
        m_width      = ((int)(m_style.get().fontScaleX * m_width / 100.0) + 4) >> 3;
        m_ascent     = ((int)(m_style.get().fontScaleY * m_ascent / 100.0) + 4) >> 3;
        m_descent    = ((int)(m_style.get().fontScaleY * m_descent / 100.0) + 4) >> 3;
    } else {
        m_width      = ((int)(m_style.get().fontScaleX/100 * m_width  ) + 4) >> 3;
        m_ascent     = ((int)(m_style.get().fontScaleY/100 * m_ascent ) + 4) >> 3;
        m_descent    = ((int)(m_style.get().fontScaleY/100 * m_descent) + 4) >> 3;
    }
    return(true);
}

bool CPolygon::CreatePath(PathData* path_data)
{
    int len = m_pathTypesOrg.GetCount();
    if(len == 0) return(false);
    if(path_data->mPathPoints != len)
    {
        BYTE* pNewPathTypes = (BYTE*)realloc(path_data->mpPathTypes, len * sizeof(BYTE));
        if (!pNewPathTypes)
        {
            TRACE_PARSER("Overflow!");
            return false;
        }
        path_data->mpPathTypes = pNewPathTypes;

        POINT* pNewPathPoints = (POINT*)realloc(path_data->mpPathPoints, len*sizeof(POINT));
        if (!pNewPathPoints)
        {
            TRACE_PARSER("Overflow!");
            return false;
        }
        path_data->mpPathPoints = pNewPathPoints;
        path_data->mPathPoints  = len;
    }
    memcpy(path_data->mpPathTypes, m_pathTypesOrg.GetData(), len*sizeof(BYTE));
    memcpy(path_data->mpPathPoints, m_pathPointsOrg.GetData(), len*sizeof(POINT));
    return(true);
}

// CClipper

CClipper::CClipper(CStringW str, CSizeCoor2 size, double scalex, double scaley, bool inverse
    , double target_scale_x/*=1.0*/, double target_scale_y/*=1.0*/)
    : m_polygon( DEBUG_NEW CPolygon(FwSTSStyle(), str, 0, 0, 0, scalex, scaley, 0, target_scale_x, target_scale_y, true) )
    , m_size(size), m_inverse(inverse)
    , m_effectType(-1), m_painted(false)
{
}

CClipper::~CClipper()
{
}

GrayImage2* CClipper::PaintSimpleClipper()
{
    GrayImage2* result = NULL;
    if(m_size.cx < 0 || m_size.cy < 0)
        return result;

    SharedPtrOverlay overlay;
    CWordPaintMachine::PaintBody( m_polygon, CPoint(0, 0), CPoint(0, 0), &overlay );
    int w =  overlay->mOverlayWidth, 
        h =  overlay->mOverlayHeight;
    int x = (overlay->mOffsetX+4)>>3,
        y = (overlay->mOffsetY+4)>>3;
    result = DEBUG_NEW GrayImage2();
    if( !result )
        return result;
    result->data  = overlay->mBody;
    result->pitch = overlay->mOverlayPitch;
    result->size.SetSize(w, h);
    result->left_top.SetPoint(x, y);
    return result;
}

GrayImage2* CClipper::PaintBaseClipper()
{
    GrayImage2* result = NULL;
    //m_pAlphaMask = NULL;
    if (m_size.cx <= 0 || m_size.cy <= 0) {
        return result;
    }

    const size_t alphaMaskSize = size_t(m_size.cx) * m_size.cy;

    SharedPtrOverlay overlay;

    CWordPaintMachine::PaintBody( m_polygon, CPoint(0, 0), CPoint(0, 0), &overlay );
    int w =  overlay->mOverlayWidth,
        h =  overlay->mOverlayHeight;
    int x = (overlay->mOffsetX+4)>>3,
        y = (overlay->mOffsetY+4)>>3;
    int xo = 0, yo = 0;

    if(x < 0) {xo = -x; w -= -x; x = 0;}
    if(y < 0) {yo = -y; h -= -y; y = 0;}
    if(x+w > m_size.cx) w = m_size.cx-x;
    if(y+h > m_size.cy) h = m_size.cy-y;

    if(w <= 0 || h <= 0) return result;

    result = DEBUG_NEW GrayImage2();

    if( !result ) {
        return result;
    }

    result->data.reset( reinterpret_cast<BYTE*>(xy_malloc(alphaMaskSize)), xy_free );
    result->pitch = m_size.cx;
    result->size  = m_size;
    result->left_top.SetPoint(0, 0);

    BYTE * result_data = result->data.get();

    if(!result_data)
    {
        delete result;
        return NULL;
    }

    memset(result_data, (m_inverse ? 0x40 : 0), alphaMaskSize);

    const BYTE* src = overlay->mBody.get() + (overlay->mOverlayPitch * yo + xo);
    BYTE* dst = result_data + m_size.cx * y + x;

    if (m_inverse) {
          for (ptrdiff_t i = 0; i < h; ++i) {
            for (ptrdiff_t wt = 0; wt < w; ++wt) {
                dst[wt] = 0x40 - src[wt];
            }

            src += overlay->mOverlayPitch;
            dst += m_size.cx;
          }
    } else {
       for (ptrdiff_t i = 0; i < h; ++i) {
            memcpy(dst, src, w * sizeof(BYTE));
            src += overlay->mOverlayPitch;
            dst += m_size.cx;
       }
    }
    return result;
}

GrayImage2* CClipper::PaintBannerClipper()
{
    ASSERT(m_polygon);

    int width = static_cast<int>(m_effect.param[2] * m_polygon->m_target_scale_x);//fix me: rounding err
    int w = m_size.cx, 
        h = m_size.cy;

    GrayImage2* result = PaintBaseClipper();
    if(!result)
        return result;

    int da = (64<<8)/width;
    BYTE* am = result->data.get();
    for(int j = 0; j < h; j++, am += w)
    {
        int a = 0;
        int k = min(width, w);
        for(int i = 0; i < k; i++, a += da)
            am[i] = (am[i]*a)>>14;
        a = 0x40<<8;
        k = w-width;
        if(k < 0) {a -= -k*da; k = 0;}
        for(int i = k; i < w; i++, a -= da)
            am[i] = (am[i]*a)>>14;
    }
    return result;
}

GrayImage2* CClipper::PaintScrollClipper()
{
    ASSERT(m_polygon);

    int height = static_cast<int>(m_effect.param[4] * m_polygon->m_target_scale_y);//fix me: rounding err
    int w = m_size.cx,
        h = m_size.cy;

    GrayImage2* result = PaintBaseClipper();
    if(!result)
        return result;

    BYTE* data = result->data.get();

    int da = (64<<8)/height;
    int a = 0;
    int k = (static_cast<int>(m_effect.param[0] * m_polygon->m_target_scale_y)>>3);//fix me: rounding err
    int l = k+height;
    if(k < 0) {a += -k*da; k = 0;}
    if(l > h) {l = h;}
    if(k < h)
    {
        BYTE* am = &data[k*w];
        memset(data, 0, am - data);
        for(int j = k; j < l; j++, a += da)
        {
            for(int i = 0; i < w; i++, am++)
                *am = ((*am)*a)>>14;
        }
    }
    da = -(64<<8)/height;
    a = 0x40<<8;
    l = (static_cast<int>(m_effect.param[1] * m_polygon->m_target_scale_y)>>3);//fix me: rounding err
    k = l-height;
    if(k < 0) {a += -k*da; k = 0;}
    if(l > h) {l = h;}
    if(k < h)
    {
        BYTE* am = &data[k*w];
        int j = k;
        for(; j < l; j++, a += da)
        {
            for(int i = 0; i < w; i++, am++)
                *am = ((*am)*a)>>14;
        }
        memset(am, 0, (h-j)*w);
    }
    return result;
}

GrayImage2* CClipper::Paint()
{
    GrayImage2* result = NULL;
    switch(m_effectType)
    {
    case -1:
        if (!m_inverse)
        {
            result = PaintSimpleClipper();
        }
        else
        {
            result = PaintBaseClipper();
        }
        break;
    case EF_BANNER:
        result = PaintBannerClipper();
        break;
    case EF_SCROLL:
        result = PaintScrollClipper();
        break;
    }
    return result;
}

void CClipper::SetEffect( const Effect& effect, int effectType )
{
    m_effectType = effectType;
    m_effect     = effect;
}

SharedPtrGrayImage2 CClipper::GetAlphaMask( const SharedPtrCClipper& clipper )
{
    SharedPtrGrayImage2 result;
    CClipperPaintMachine paint_machine(clipper);
    paint_machine.Paint(&result);
    return result;
}

// CLine

CLine::~CLine()
{
    //POSITION pos = GetHeadPosition();
    //while(pos) delete GetNext(pos);
}

void CLine::Compact()
{
    POSITION pos = GetHeadPosition();
    while(pos)
    {
        SharedPtrCWord w = GetNext(pos);
        if(!w->m_fWhiteSpaceChar) break;
        m_width -= w->m_width;
//        delete w;
        RemoveHead();
    }
    pos = GetTailPosition();
    while(pos)
    {
        SharedPtrCWord w = GetPrev(pos);
        if(!w->m_fWhiteSpaceChar) break;
        m_width -= w->m_width;
//        delete w;
        RemoveTail();
    }
    if(IsEmpty()) return;
    CLine l;
    l.AddTailList(this);
    RemoveAll();
    SharedPtrCWord last;
    pos = l.GetHeadPosition();
    while(pos)
    {
        SharedPtrCWord w = l.GetNext(pos);
        if(!last || !last->Append(w))
            AddTail(last = w->Copy());
    }
    m_ascent = m_descent = m_borderX = m_borderY = 0;
    pos = GetHeadPosition();
    while(pos)
    {
        SharedPtrCWord w = GetNext(pos);
        if(m_ascent  < w->m_ascent)  m_ascent  = w->m_ascent;
        if(m_descent < w->m_descent) m_descent = w->m_descent;
        if(m_borderX < w->m_style.get().outlineWidthX) m_borderX = (int)(w->m_style.get().outlineWidthX+0.5);
        if(m_borderY < w->m_style.get().outlineWidthY) m_borderY = (int)(w->m_style.get().outlineWidthY+0.5);
    }
}

CRectCoor2 CLine::PaintAll( CompositeDrawItemList* output, const CRectCoor2& clipRect, 
    const CPointCoor2& margin,
    const SharedPtrCClipperPaintMachine &clipper, CPoint p, const CPoint& org, const int time,
    const int alpha, REFERENCE_TIME rt )
{
    CRectCoor2 bbox(0, 0, 0, 0);
    POSITION pos = GetHeadPosition();
    POSITION outputPos = output->GetHeadPosition();
    while(pos)
    {
        SharedPtrCWord w = GetNext(pos);
        CompositeDrawItem& outputItem = output->GetNext(outputPos);
        if(w->m_fLineBreak) return(bbox); // should not happen since this class is just a line of text without any breaks
        CPointCoor2 shadowPos, outlinePos, bodyPos, org_coor2;
        CPoint mod_offset(0, 0);
        int mod_vertical_spacing = 0;
        if (w->m_mod_style) {
            if (w->m_mod_style->feature_mask & MOD_FEATURE_JITTER) {
                mod_offset = w->m_mod_style->jitter.GetOffset(rt);
            }
            if (w->m_mod_style->feature_mask & MOD_FEATURE_VERTICAL_SPACING) {
                mod_vertical_spacing = static_cast<int>(w->m_mod_style->vertical_spacing);
            }
        }

        double shadowPos_x = p.x + mod_offset.x + w->m_style.get().shadowDepthX;
        double shadowPos_y = p.y + mod_offset.y - mod_vertical_spacing
            + w->m_style.get().shadowDepthY + m_ascent - w->m_ascent;
        outlinePos = CPoint(p.x + mod_offset.x,
            p.y + mod_offset.y - mod_vertical_spacing + m_ascent - w->m_ascent);
        bodyPos = outlinePos;

        if (w->m_mod_compatibility_mode) {
            outlinePos.x = w->m_target_scale_x * outlinePos.x + margin.x;
            outlinePos.y = w->m_target_scale_y * outlinePos.y + margin.y;
            shadowPos.x = static_cast<int>(outlinePos.x)
                + static_cast<int>(w->m_target_scale_x * w->m_style.get().shadowDepthX + 0.5);
            shadowPos.y = static_cast<int>(outlinePos.y)
                + static_cast<int>(w->m_target_scale_y * w->m_style.get().shadowDepthY + 0.5);
        } else {
            shadowPos.x = static_cast<int>(w->m_target_scale_x * shadowPos_x + 0.5) + margin.x;
            shadowPos.y = static_cast<int>(w->m_target_scale_y * shadowPos_y + 0.5) + margin.y;
            outlinePos.x = w->m_target_scale_x * outlinePos.x + margin.x;
            outlinePos.y = w->m_target_scale_y * outlinePos.y + margin.y;
        }
        bodyPos.x     =                  w->m_target_scale_x * bodyPos.x          + margin.x;
        bodyPos.y     =                  w->m_target_scale_y * bodyPos.y          + margin.y;
        org_coor2.x   =                  w->m_target_scale_x * org.x              + margin.x;//fix me: move it out of this loop
        org_coor2.y   =                  w->m_target_scale_y * org.y              + margin.y;

        bool hasShadow  =   w->m_style.get().shadowDepthX != 0 || w->m_style.get().shadowDepthY != 0;
        bool hasOutline = ((w->m_style.get().outlineWidthX*w->m_target_scale_x+0.5>=1.0) ||
                           (w->m_style.get().outlineWidthY*w->m_target_scale_y+0.5>=1.0)) && 
                          !(w->m_ktype == 2 && time < w->m_kstart);
        bool hasBody = true;

        SharedPtrOverlayPaintMachine shadow_pm, outline_pm, body_pm;
        CWordPaintMachine::CreatePaintMachines(w, shadowPos, outlinePos, bodyPos, org_coor2,
            hasShadow  ? &shadow_pm  : NULL, 
            hasOutline ? &outline_pm : NULL, 
            hasBody    ? &body_pm    : NULL);

        //shadow
        if(hasShadow)
        {
            //NOTE: Calculation of shadow's alpha value is different from outline's and body's in the way they're rounded.
            // Shadow is rounded to invisible while outline and body is rounded to visible.
            // Should we change it to be consist with one rounding polocy?
            DWORD a = 0xff - w->m_style.get().alpha[3];
            if(alpha > 0) a = MulDiv(a, 0xff - alpha, 0xff);
            COLORREF shadow = revcolor(w->m_style.get().colors[3]) | (a<<24);

            DWORD sw[6] = {shadow, -1};
            sw[0] = XySubRenderFrameCreater::GetDefaultCreater()->TransColor(sw[0]);
            const DWORD mod_solid[2] = {sw[0], 0};
            const SharedPtrConstModPaintSource mod_paint =
                w->m_mod_style && HasVariableModPaint(*w->m_mod_style, 3)
                ? CreateModPaintSource(*w->m_mod_style, 3, -1, mod_solid, alpha)
                : SharedPtrConstModPaintSource();
            if(w->m_style.get().borderStyle == 0)
            {
                outputItem.shadow.reset( 
                    DrawItem::CreateDrawItem(shadow_pm, clipRect, clipper, shadowPos.x, shadowPos.y, sw,
                    w->m_ktype > 0 || w->m_style.get().alpha[0] < 0xff,
                    hasOutline, mod_paint, w->m_mod_compatibility_mode)
                    );
            }
            else if(w->m_style.get().borderStyle == 1)
            {
                outputItem.shadow.reset( 
                    DrawItem::CreateDrawItem( shadow_pm, clipRect, clipper, shadowPos.x, shadowPos.y, sw,
                    true, false, mod_paint, w->m_mod_compatibility_mode)
                    );
            }
        }
        //outline
        if(hasOutline)
        {
            DWORD aoutline = w->m_style.get().alpha[2];
            if(alpha > 0) aoutline += MulDiv(alpha, 0xff - w->m_style.get().alpha[2], 0xff);
            COLORREF outline = revcolor(w->m_style.get().colors[2]) | ((0xff-aoutline)<<24);
            DWORD sw[6] = {outline, -1};
            sw[0] = XySubRenderFrameCreater::GetDefaultCreater()->TransColor(sw[0]);
            const DWORD mod_solid[2] = {sw[0], 0};
            const SharedPtrConstModPaintSource mod_paint =
                w->m_mod_style && HasVariableModPaint(*w->m_mod_style, 2)
                ? CreateModPaintSource(*w->m_mod_style, 2, -1, mod_solid, alpha)
                : SharedPtrConstModPaintSource();
            const bool body_has_gradient_alpha =
                w->m_mod_style && w->m_mod_style->body_gradient_alpha;
            if(w->m_style.get().borderStyle == 0)
            {
                outputItem.outline.reset( 
                    DrawItem::CreateDrawItem(outline_pm, clipRect, clipper, outlinePos.x, outlinePos.y, sw, 
                    !w->m_style.get().alpha[0] && !w->m_style.get().alpha[1]
                        && !alpha && !body_has_gradient_alpha,
                    true, mod_paint, w->m_mod_compatibility_mode)
                    );
            }
            else if(w->m_style.get().borderStyle == 1)
            {
                outputItem.outline.reset( 
                    DrawItem::CreateDrawItem(outline_pm, clipRect, clipper, outlinePos.x, outlinePos.y, sw,
                    true, false, mod_paint, w->m_mod_compatibility_mode)
                    );
            }
        }
        //body
        if(hasBody)
        {
            // colors
            DWORD aprimary   = w->m_style.get().alpha[0];
            DWORD asecondary = w->m_style.get().alpha[1];
            if(alpha > 0) aprimary   += MulDiv(alpha, 0xff - w->m_style.get().alpha[0], 0xff),
                          asecondary += MulDiv(alpha, 0xff - w->m_style.get().alpha[1], 0xff);
            COLORREF primary   = revcolor(w->m_style.get().colors[0]) | ((0xff-aprimary  )<<24);
            COLORREF secondary = revcolor(w->m_style.get().colors[1]) | ((0xff-asecondary)<<24);
            DWORD sw[6] = {primary, 0, secondary};
            int mod_primary_layer = 0;
            int mod_secondary_layer = 1;
            // karaoke
            double t;
            if(w->m_ktype == 0 || w->m_ktype == 2)
            {
                t = time < w->m_kstart ? 0 : 1;
            }
            else if(w->m_ktype == 1)
            {
                if(time < w->m_kstart) t = 0;
                else if(time < w->m_kend)
                {
                    t = 1.0 * (time - w->m_kstart) / (w->m_kend - w->m_kstart);
                    double angle = fmod(w->m_style.get().fontAngleZ, 360.0);
                    if(angle > 90 && angle < 270)
                    {
                        t = 1-t;
                        COLORREF tmp = sw[0];
                        sw[0] = sw[2];
                        sw[2] = tmp;
                    }
                }
                else t = 1.0;
            }
            if(t >= 1)
            {
                sw[1] = 0xffffffff;
            }
            sw[3] = (int)( (w->m_style.get().outlineWidthX + t*w->m_width)*w->m_target_scale_x ) >> 3;
            sw[4] = sw[2];
            sw[5] = 0x00ffffff;
            sw[0] = XySubRenderFrameCreater::GetDefaultCreater()->TransColor(sw[0]);
            sw[2] = XySubRenderFrameCreater::GetDefaultCreater()->TransColor(sw[2]);
            sw[4] = XySubRenderFrameCreater::GetDefaultCreater()->TransColor(sw[4]);
            const DWORD mod_solid[2] = {sw[0], sw[2]};
            const SharedPtrConstModPaintSource mod_paint =
                w->m_mod_style
                    && HasVariableModPaint(*w->m_mod_style,
                        mod_primary_layer, mod_secondary_layer)
                ? CreateModPaintSource(*w->m_mod_style,
                    mod_primary_layer, mod_secondary_layer, mod_solid, alpha)
                : SharedPtrConstModPaintSource();
            outputItem.body.reset( 
                DrawItem::CreateDrawItem(body_pm, clipRect, clipper,
                    bodyPos.x, bodyPos.y, sw,
                    true, false, mod_paint, w->m_mod_compatibility_mode)
                );
        }
        bbox |= CompositeDrawItem::GetDirtyRect(outputItem);
        p.x += w->m_width;
    }
    return(bbox);
}

void CLine::AddWord2Tail( SharedPtrCWord words )
{
    __super::AddTail(words);
}

bool CLine::IsEmpty()
{
    return __super::IsEmpty();
}

int CLine::GetWordCount()
{
    return GetCount();
}

// CSubtitle

CSubtitle::CSubtitle()
{
    memset(m_effects, 0, sizeof(Effect*)*EF_NUMBEROFEFFECTS);
    m_clipInverse = false;
    m_scalex = m_scaley = 1;
    m_fAnimated2 = false;

    m_target_scale_x = m_target_scale_y = 1.0;
    m_hard_position_level = -1;
}

CSubtitle::~CSubtitle()
{
    Empty();
}

void CSubtitle::Empty()
{
    POSITION pos = GetHeadPosition();
    while(pos) delete GetNext(pos);
//    pos = m_words.GetHeadPosition();
//    while(pos) delete m_words.GetNext(pos);
    for(int i = 0; i < EF_NUMBEROFEFFECTS; i++) {if(m_effects[i]) delete m_effects[i];}
    memset(m_effects, 0, sizeof(Effect*)*EF_NUMBEROFEFFECTS);
}

int CSubtitle::GetFullWidth()
{
    int width = 0;
    POSITION pos = m_words.GetHeadPosition();
    while(pos) width += m_words.GetNext(pos)->m_width;
    return(width);
}

int CSubtitle::GetFullLineWidth(POSITION pos)
{
    int width = 0;
    while(pos)
    {
        SharedPtrCWord w = m_words.GetNext(pos);
        if(w->m_fLineBreak) break;
        width += w->m_width;
    }
    return(width);
}

int CSubtitle::GetWrapWidth(POSITION pos, int maxwidth)
{
    if(m_wrapStyle == 0 || m_wrapStyle == 3)
    {
        if(maxwidth > 0)
        {
            int fullwidth = GetFullLineWidth(pos);
            int minwidth = fullwidth / ((abs(fullwidth) / maxwidth) + 1);
            int width = 0, wordwidth = 0;
            while(pos && width < minwidth)
            {
                SharedPtrCWord w = m_words.GetNext(pos);
                wordwidth = w->m_width;
                if(abs(width + wordwidth) < abs(maxwidth)) width += wordwidth;
            }
            if (m_wrapStyle == 3 && width < fullwidth && fullwidth - width + wordwidth < maxwidth) {
                width -= wordwidth;
            }
            maxwidth = width;
        }
    }
    else if(m_wrapStyle == 1)
    {
//      maxwidth = maxwidth;
    }
    else if(m_wrapStyle == 2)
    {
        maxwidth = INT_MAX;
    }
    return(maxwidth);
}

CLine* CSubtitle::GetNextLine(POSITION& pos, int maxwidth)
{
    if(pos == NULL) return(NULL);
    CLine* ret = DEBUG_NEW CLine();
    if(!ret) return(NULL);
    ret->m_width = ret->m_ascent = ret->m_descent = ret->m_borderX = ret->m_borderY = 0;
    maxwidth = GetWrapWidth(pos, maxwidth);
    bool fEmptyLine = true;
    while(pos)
    {
        SharedPtrCWord w = m_words.GetNext(pos);
        if(ret->m_ascent < w->m_ascent) ret->m_ascent = w->m_ascent;
        if(ret->m_descent < w->m_descent) ret->m_descent = w->m_descent;
        if(ret->m_borderX < w->m_style.get().outlineWidthX) ret->m_borderX = (int)(w->m_style.get().outlineWidthX+0.5);
        if(ret->m_borderY < w->m_style.get().outlineWidthY) ret->m_borderY = (int)(w->m_style.get().outlineWidthY+0.5);
        if(w->m_fLineBreak)
        {
            if(fEmptyLine) {ret->m_ascent /= 2; ret->m_descent /= 2; ret->m_borderX = ret->m_borderY = 0;}
            ret->Compact();
            return(ret);
        }
        fEmptyLine = false;
        bool fWSC = w->m_fWhiteSpaceChar;
        int width = w->m_width;
        POSITION pos2 = pos;
        while(pos2)
        {
            if(m_words.GetAt(pos2)->m_fWhiteSpaceChar != fWSC
                    || m_words.GetAt(pos2)->m_fLineBreak) break;
            SharedPtrCWord w2 = m_words.GetNext(pos2);
            width += w2->m_width;
        }
        if((ret->m_width += width) <= maxwidth || ret->IsEmpty())
        {
            ret->AddWord2Tail(w);
            while(pos != pos2)
            {
                ret->AddWord2Tail(m_words.GetNext(pos));
            }
            pos = pos2;
        }
        else
        {
            if(pos) m_words.GetPrev(pos);
            else pos = m_words.GetTailPosition();
            ret->m_width -= width;
            break;
        }
    }
    ret->Compact();
    return(ret);
}

void CSubtitle::CreateClippers( CSize size1, const CSizeCoor2& size_scale_to )
{
    size1.cx >>= 3;
    size1.cy >>= 3;
    if(m_effects[EF_BANNER] && m_effects[EF_BANNER]->param[2])
    {
        int w = size1.cx, h = size1.cy;
        if(!m_pClipper)
        {
            CStringW str;
            str.Format(L"m %d %d l %d %d %d %d %d %d", 0, 0, w, 0, w, h, 0, h);
            m_pClipper.reset( DEBUG_NEW CClipper(str, size_scale_to, 1, 1, false, m_target_scale_x, m_target_scale_y) );
            if(!m_pClipper) return;
        }
        m_pClipper->SetEffect( *m_effects[EF_BANNER], EF_BANNER );
    }
    else if(m_effects[EF_SCROLL] && m_effects[EF_SCROLL]->param[4])
    {
        int height = m_effects[EF_SCROLL]->param[4];
        int w = size1.cx, h = size1.cy;
        if(!m_pClipper)
        {
            CStringW str;
            str.Format(L"m %d %d l %d %d %d %d %d %d", 0, 0, w, 0, w, h, 0, h);
            m_pClipper.reset( DEBUG_NEW CClipper(str, size_scale_to, 1, 1, false, m_target_scale_x, m_target_scale_y) ); 
            if(!m_pClipper) return;
        }
        m_pClipper->SetEffect(*m_effects[EF_SCROLL], EF_SCROLL);
    }
}

void CSubtitle::MakeLines(CSize size, CRect marginRect)
{
    CSize spaceNeeded(0, 0);
    bool fFirstLine = true;
    m_topborder = m_bottomborder = 0;
    CLine* l = NULL;
    POSITION pos = m_words.GetHeadPosition();
    while(pos)
    {
        l = GetNextLine(pos, size.cx - marginRect.left - marginRect.right);
        if(!l) break;
        if(fFirstLine) {m_topborder = l->m_borderY; fFirstLine = false;}
        spaceNeeded.cx = max(l->m_width+l->m_borderX, spaceNeeded.cx);
        spaceNeeded.cy += l->m_ascent + l->m_descent;
        AddTail(l);
    }
    if(l) m_bottomborder = l->m_borderY;
    m_rect = CRect(
                 CPoint((m_scrAlignment%3) == 1 ? marginRect.left
                        : (m_scrAlignment%3) == 2 ? (marginRect.left + (size.cx - marginRect.right) - spaceNeeded.cx + 1) / 2
                        : (size.cx - marginRect.right - spaceNeeded.cx),
                        m_scrAlignment <= 3 ? (size.cy - marginRect.bottom - spaceNeeded.cy)
                        : m_scrAlignment <= 6 ? (marginRect.top + (size.cy - marginRect.bottom) - spaceNeeded.cy + 1) / 2
                        : marginRect.top),
                 spaceNeeded);
}

POSITION CSubtitle::GetHeadLinePosition()
{
    return __super::GetHeadPosition();
}

CLine* CSubtitle::GetNextLine( POSITION& pos )
{
    return __super::GetNext(pos);
}

// CScreenLayoutAllocator

void CScreenLayoutAllocator::Empty()
{
    m_subrects.RemoveAll();
}

void CScreenLayoutAllocator::AdvanceToSegment(int segment, const CAtlArray<int>& sa)
{
    TRACE_RENDERER_REQUEST("Begin AdvanceToSegment. m_subrects.size:"
        <<m_subrects.GetCount()<<" sa.size:"<<sa.GetCount());
    POSITION pos = m_subrects.GetHeadPosition();
    while(pos)
    {
        POSITION prev = pos;
        SubRect& sr = m_subrects.GetNext(pos);
        bool fFound = false;
        if(abs(sr.segment - segment) <= 1) // using abs() makes it possible to play the subs backwards, too :)
        {
            for(size_t i = 0; i < sa.GetCount() && !fFound; i++)
            {
                if(sa[i] == sr.entry)
                {
                    sr.segment = segment;
                    fFound = true;
                }
            }
        }
        if(!fFound) m_subrects.RemoveAt(prev);
    }
}

CRect CScreenLayoutAllocator::AllocRect(CSubtitle* s, int segment, int entry, int layer, int collisions)
{
    // TODO: handle collisions == 1 (reversed collisions)
    POSITION pos = m_subrects.GetHeadPosition();
    while(pos)
    {
        SubRect& sr = m_subrects.GetNext(pos);
        if(sr.segment == segment && sr.entry == entry)
        {
            return(sr.r + CRect(0, -s->m_topborder, 0, -s->m_bottomborder));
        }
    }
    CRect r = s->m_rect + CRect(0, s->m_topborder, 0, s->m_bottomborder);
    bool fSearchDown = s->m_scrAlignment > 3;
    bool fOK;
    do
    {
        fOK = true;
        pos = m_subrects.GetHeadPosition();
        while(pos)
        {
            SubRect& sr = m_subrects.GetNext(pos);
            if(layer == sr.layer && !(r & sr.r).IsRectEmpty())
            {
                if(fSearchDown)
                {
                    r.bottom = sr.r.bottom + r.Height();
                    r.top    = sr.r.bottom;
                }
                else
                {
                    r.top    = sr.r.top - r.Height();
                    r.bottom = sr.r.top;
                }
                fOK = false;
            }
        }
    }
    while(!fOK);
    SubRect sr;
    sr.r       = r;
    sr.segment = segment;
    sr.entry   = entry;
    sr.layer   = layer;
    m_subrects.AddTail(sr);
    return(sr.r + CRect(0, -s->m_topborder, 0, -s->m_bottomborder));
}

// CRenderedTextSubtitle

CAtlMap<CStringW, CRenderedTextSubtitle::AssCmdType, CStringElementTraits<CStringW>> CRenderedTextSubtitle::m_cmdMap;

std::size_t CRenderedTextSubtitle::s_max_cache_size = SIZE_MAX;

std::size_t CRenderedTextSubtitle::SetMaxCacheSize( std::size_t max_cache_size )
{
    s_max_cache_size = max_cache_size;
    XY_LOG_INFO("MAX_CACHE_SIZE: "<<s_max_cache_size);
    return s_max_cache_size;
}

CAtlArray<AssCmdPosLevel> CRenderedTextSubtitle::m_cmd_pos_level;

CRenderedTextSubtitle::CRenderedTextSubtitle(CCritSec* pLock)
    : CSubPicProviderImpl(pLock)
    , m_target_scale_x(1.0), m_target_scale_y(1.0)
    , m_direct_render_target(NULL)
{
    if( m_cmdMap.IsEmpty() )
    {
        InitCmdMap();
    }
    m_size = CSize(0, 0);
    if(g_hDC_refcnt == 0)
    {
        g_hDC = CreateCompatibleDC(NULL);
        SetBkMode(g_hDC, TRANSPARENT);
        SetTextColor(g_hDC, 0xffffff);
        SetMapMode(g_hDC, MM_TEXT);
    }
    g_hDC_refcnt++;
    m_movable = true;
}

CRenderedTextSubtitle::~CRenderedTextSubtitle()
{
    Deinit();
    g_hDC_refcnt--;
    if(g_hDC_refcnt == 0) DeleteDC(g_hDC);
}

void CRenderedTextSubtitle::InitCmdMap()
{
    if( m_cmdMap.IsEmpty() )
    {
        m_cmdMap.SetAt(L"1c",        CMD_1c   );
        m_cmdMap.SetAt(L"2c",        CMD_2c   );
        m_cmdMap.SetAt(L"3c",        CMD_3c   );
        m_cmdMap.SetAt(L"4c",        CMD_4c   );
        m_cmdMap.SetAt(L"1a",        CMD_1a   );
        m_cmdMap.SetAt(L"2a",        CMD_2a   );
        m_cmdMap.SetAt(L"3a",        CMD_3a   );
        m_cmdMap.SetAt(L"4a",        CMD_4a   );
        m_cmdMap.SetAt(L"alpha",     CMD_alpha);
        m_cmdMap.SetAt(L"an",        CMD_an   );
        m_cmdMap.SetAt(L"a",         CMD_a    );
        m_cmdMap.SetAt(L"blur",      CMD_blur );
        m_cmdMap.SetAt(L"bord",      CMD_bord );
        m_cmdMap.SetAt(L"be",        CMD_be   );
        m_cmdMap.SetAt(L"b",         CMD_b    );
        m_cmdMap.SetAt(L"clip",      CMD_clip );
        m_cmdMap.SetAt(L"iclip",     CMD_iclip);
        m_cmdMap.SetAt(L"c",         CMD_c    );
        m_cmdMap.SetAt(L"fade",      CMD_fade );
        m_cmdMap.SetAt(L"fad",       CMD_fad  );
        m_cmdMap.SetAt(L"fax",       CMD_fax  );
        m_cmdMap.SetAt(L"fay",       CMD_fay  );
        m_cmdMap.SetAt(L"fe",        CMD_fe   );
        m_cmdMap.SetAt(L"fn",        CMD_fn   );
        m_cmdMap.SetAt(L"frx",       CMD_frx  );
        m_cmdMap.SetAt(L"fry",       CMD_fry  );
        m_cmdMap.SetAt(L"frz",       CMD_frz  );
        m_cmdMap.SetAt(L"fr",        CMD_fr   );
        m_cmdMap.SetAt(L"fscx",      CMD_fscx );
        m_cmdMap.SetAt(L"fscy",      CMD_fscy );
        m_cmdMap.SetAt(L"fsc",       CMD_fsc  );
        m_cmdMap.SetAt(L"fsp",       CMD_fsp  );
        m_cmdMap.SetAt(L"fs",        CMD_fs   );
        m_cmdMap.SetAt(L"i",         CMD_i    );
        m_cmdMap.SetAt(L"kt",        CMD_kt   );
        m_cmdMap.SetAt(L"kf",        CMD_kf   );
        m_cmdMap.SetAt(L"K",         CMD_K    );
        m_cmdMap.SetAt(L"ko",        CMD_ko   );
        m_cmdMap.SetAt(L"k",         CMD_k    );
        m_cmdMap.SetAt(L"move",      CMD_move );
        m_cmdMap.SetAt(L"org",       CMD_org  );
        m_cmdMap.SetAt(L"pbo",       CMD_pbo  );
        m_cmdMap.SetAt(L"pos",       CMD_pos  );
        m_cmdMap.SetAt(L"p",         CMD_p    );
        m_cmdMap.SetAt(L"q",         CMD_q    );
        m_cmdMap.SetAt(L"r",         CMD_r    );
        m_cmdMap.SetAt(L"shad",      CMD_shad );
        m_cmdMap.SetAt(L"s",         CMD_s    );
        m_cmdMap.SetAt(L"t",         CMD_t    );
        m_cmdMap.SetAt(L"u",         CMD_u    );
        m_cmdMap.SetAt(L"xbord",     CMD_xbord);
        m_cmdMap.SetAt(L"xshad",     CMD_xshad);
        m_cmdMap.SetAt(L"ybord",     CMD_ybord);
        m_cmdMap.SetAt(L"yshad",     CMD_yshad);
        // VSFilterMod-derived commands. Keep them in the shared lexical cache;
        // compatibility mode is checked only when applying their semantics.
        // See NOTICE-VSFilterMod.md for provenance and licensing.
        m_cmdMap.SetAt(L"1img",      CMD_1img);
        m_cmdMap.SetAt(L"2img",      CMD_2img);
        m_cmdMap.SetAt(L"3img",      CMD_3img);
        m_cmdMap.SetAt(L"4img",      CMD_4img);
        m_cmdMap.SetAt(L"1vc",       CMD_1vc);
        m_cmdMap.SetAt(L"2vc",       CMD_2vc);
        m_cmdMap.SetAt(L"3vc",       CMD_3vc);
        m_cmdMap.SetAt(L"4vc",       CMD_4vc);
        m_cmdMap.SetAt(L"1va",       CMD_1va);
        m_cmdMap.SetAt(L"2va",       CMD_2va);
        m_cmdMap.SetAt(L"3va",       CMD_3va);
        m_cmdMap.SetAt(L"4va",       CMD_4va);
        m_cmdMap.SetAt(L"distort",   CMD_distort);
        m_cmdMap.SetAt(L"frs",       CMD_frs);
        m_cmdMap.SetAt(L"fsvp",      CMD_fsvp);
        m_cmdMap.SetAt(L"jitter",    CMD_jitter);
        m_cmdMap.SetAt(L"mover",     CMD_mover);
        m_cmdMap.SetAt(L"moves3",    CMD_moves3);
        m_cmdMap.SetAt(L"moves4",    CMD_moves4);
        m_cmdMap.SetAt(L"movevc",    CMD_movevc);
        m_cmdMap.SetAt(L"rnd",       CMD_rnd);
        m_cmdMap.SetAt(L"rndx",      CMD_rndx);
        m_cmdMap.SetAt(L"rndy",      CMD_rndy);
        m_cmdMap.SetAt(L"rndz",      CMD_rndz);
        m_cmdMap.SetAt(L"rnds",      CMD_rnds);
        m_cmdMap.SetAt(L"z",         CMD_z);
    }
    m_cmd_pos_level.SetCount(CMD_COUNT+1);
    m_cmd_pos_level[CMD_1c   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_2c   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_3c   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_4c   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_1a   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_2a   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_3a   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_4a   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_alpha] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_an   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_a    ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_blur ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_bord ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_be   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_b    ] = POS_LVL_NONE;

    m_cmd_pos_level[CMD_clip ] = POS_LVL_HARD;
    m_cmd_pos_level[CMD_iclip] = POS_LVL_HARD;

    m_cmd_pos_level[CMD_c    ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_fade ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_fad  ] = POS_LVL_NONE;

    m_cmd_pos_level[CMD_fax  ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fay  ] = POS_LVL_SOFT;

    m_cmd_pos_level[CMD_fe   ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_fn   ] = POS_LVL_NONE;

    m_cmd_pos_level[CMD_frx  ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fry  ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_frz  ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fr   ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fscx ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fscy ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fsc  ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fsp  ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fs   ] = POS_LVL_SOFT;

    m_cmd_pos_level[CMD_i    ] = POS_LVL_NONE;

    m_cmd_pos_level[CMD_kt   ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_kf   ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_K    ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_ko   ] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_k    ] = POS_LVL_SOFT;

    m_cmd_pos_level[CMD_move ] = POS_LVL_HARD;
    m_cmd_pos_level[CMD_org  ] = POS_LVL_HARD;
    m_cmd_pos_level[CMD_pbo  ] = POS_LVL_HARD;
    m_cmd_pos_level[CMD_pos  ] = POS_LVL_HARD;
    m_cmd_pos_level[CMD_p    ] = POS_LVL_HARD;

    m_cmd_pos_level[CMD_q    ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_r    ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_shad ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_s    ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_t    ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_u    ] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_xbord] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_xshad] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_ybord] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_yshad] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_1img] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_2img] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_3img] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_4img] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_1vc] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_2vc] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_3vc] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_4vc] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_1va] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_2va] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_3va] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_4va] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_distort] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_frs] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_fsvp] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_jitter] = POS_LVL_NONE;
    m_cmd_pos_level[CMD_mover] = POS_LVL_HARD;
    m_cmd_pos_level[CMD_moves3] = POS_LVL_HARD;
    m_cmd_pos_level[CMD_moves4] = POS_LVL_HARD;
    m_cmd_pos_level[CMD_movevc] = POS_LVL_HARD;
    m_cmd_pos_level[CMD_rnd] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_rndx] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_rndy] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_rndz] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_rnds] = POS_LVL_SOFT;
    m_cmd_pos_level[CMD_z] = POS_LVL_SOFT;

    m_cmd_pos_level[CMD_COUNT] = POS_LVL_NONE;
}

void CRenderedTextSubtitle::Copy(CSimpleTextSubtitle& sts)
{
    __super::Copy(sts);
    m_size = CSize(0, 0);
    if(CRenderedTextSubtitle* pRTS = dynamic_cast<CRenderedTextSubtitle*>(&sts))
    {
        m_size = pRTS->m_size;
        m_ass_context.ApplyRenderOptions(pRTS->m_ass_context.m_options, nullptr);
    }
}

void CRenderedTextSubtitle::Empty()
{
    Deinit();
    __super::Empty();
}

void CRenderedTextSubtitle::OnChanged()
{
    __super::OnChanged();
    POSITION pos = m_subtitleCacheEntry.GetHeadPosition();
    while(pos)
    {
        int i = m_subtitleCacheEntry.GetNext(pos);
        delete m_subtitleCache.GetAt(i);
        m_subtitleCache.SetAt(i, NULL);
    }
    m_subtitleCacheEntry.RemoveAll();
    m_subtitleCache.ReleaseMemory(m_entries.GetCount());

    m_sla.Empty();
}


bool CRenderedTextSubtitle::Init( const CRectCoor2& video_rect, const CRectCoor2& subtitle_target_rect,
    const SIZE& original_video_size )
{
    XY_LOG_INFO(_T(""));
    Deinit();
    m_video_rect = CRect(video_rect.left*MAX_SUB_PIXEL, 
                         video_rect.top*MAX_SUB_PIXEL, 
                         video_rect.right*MAX_SUB_PIXEL, 
                         video_rect.bottom*MAX_SUB_PIXEL);
    m_subtitle_target_rect = CRect(subtitle_target_rect.left*MAX_SUB_PIXEL, subtitle_target_rect.top*MAX_SUB_PIXEL, 
        subtitle_target_rect.right*MAX_SUB_PIXEL, subtitle_target_rect.bottom*MAX_SUB_PIXEL);
    m_size = CSize(original_video_size.cx*MAX_SUB_PIXEL, original_video_size.cy*MAX_SUB_PIXEL);

    ASSERT(original_video_size.cx!=0 && original_video_size.cy!=0);

    m_target_scale_x = video_rect.Width()  * 1.0 / original_video_size.cx;
    m_target_scale_y = video_rect.Height() * 1.0 / original_video_size.cy;

    return(true);
}

void CRenderedTextSubtitle::Deinit()
{
    Deinit(true, true);
}

void CRenderedTextSubtitle::Deinit(bool clear_ass_tag_cache, bool reset_geometry)
{
    POSITION pos = m_subtitleCacheEntry.GetHeadPosition();
    while(pos)
    {
        int i = m_subtitleCacheEntry.GetNext(pos);
        delete m_subtitleCache.GetAt(i);
    }
    m_subtitleCache.ReleaseMemory();
    m_subtitleCacheEntry.RemoveAll();
    m_sla.Empty();

    if (reset_geometry) {
        m_video_rect.SetRectEmpty();
        m_subtitle_target_rect.SetRectEmpty();
        m_size = CSize(0, 0);
        m_target_scale_x = m_target_scale_y = 1.0;
    }

    CacheManager::GetBitmapMruCache()->RemoveAll();

    CacheManager::GetClipperAlphaMaskMruCache()->RemoveAll();
    CacheManager::GetTextInfoCache           ()->RemoveAll();
    if (clear_ass_tag_cache) {
        CacheManager::GetAssTagListMruCache()->RemoveAll();
    }

    CacheManager::GetScanLineDataMruCache   ()->RemoveAll();
    CacheManager::GetOverlayNoOffsetMruCache()->RemoveAll();

    CacheManager::GetSubpixelVarianceCache()->RemoveAll();
    CacheManager::GetOverlayMruCache      ()->RemoveAll();
    CacheManager::GetOverlayNoBlurMruCache()->RemoveAll();
    CacheManager::GetScanLineData2MruCache()->RemoveAll();
    CacheManager::GetPathDataMruCache     ()->RemoveAll();

    //The ids generated by XyFlyWeight is ever increasing.
    //That is good for ids generated after re-init won't conflict with ids generated before re-init.
    XyFwGroupedDrawItemsHashKey::GetCacher()->RemoveAll();
}

void CRenderedTextSubtitle::SetTextRendererMode(TextRendererMode mode)
{
    mode = NormalizeTextRendererMode(mode);
    if (m_text_renderer_mode != mode) {
        m_text_renderer_mode = mode;
        Deinit();
    }
}

void CRenderedTextSubtitle::SetVsFilterCompatibilityMode(VsFilterCompatibilityMode mode)
{
    mode = NormalizeVsFilterCompatibilityMode(mode);
    if (m_vsfilter_compatibility_mode != mode) {
        m_vsfilter_compatibility_mode = mode;
        m_mod_image_cache.ResetDecoded();
        Deinit(false, false);
    }
}

// Map the VSFilter default style to an ASS_Style for libass' selective style
// override. Field conventions follow detect_style_changes() in xy_sub_filter.cpp.
static void StsStyleToAssStyle(const STSStyle &sts, CStringA &font_name, ASS_Style *style)
{
    memset(style, 0, sizeof(*style));
    font_name = UTF16To8(sts.fontName.GetString());
    style->Name = const_cast<char *>("Default");
    style->FontName = const_cast<char *>(font_name.GetString());
    style->FontSize = sts.fontSize;
    style->PrimaryColour = (sts.alpha[0] << 24) | sts.colors[0];
    style->SecondaryColour = (sts.alpha[1] << 24) | sts.colors[1];
    style->OutlineColour = (sts.alpha[2] << 24) | sts.colors[2];
    style->BackColour = (sts.alpha[3] << 24) | sts.colors[3];
    style->Bold = sts.fontWeight >= FW_BOLD ? -1 : 0;
    style->Italic = sts.fItalic ? -1 : 0;
    style->Underline = sts.fUnderline ? -1 : 0;
    style->StrikeOut = sts.fStrikeOut ? -1 : 0;
    style->ScaleX = sts.fontScaleX / 100.0;
    style->ScaleY = sts.fontScaleY / 100.0;
    style->Spacing = sts.fontSpacing;
    style->Angle = sts.fontAngleZ;
    style->BorderStyle = sts.borderStyle ? 3 : 1;
    style->Outline = sts.outlineWidthX;
    style->Shadow = sts.shadowDepthX;
    int Alignment = ((sts.scrAlignment - 1) % 3) + 1;
    if (sts.scrAlignment <= 3) {
        Alignment |= VALIGN_SUB;
    } else if (sts.scrAlignment <= 6) {
        Alignment |= VALIGN_CENTER;
    } else {
        Alignment |= VALIGN_TOP;
    }
    style->Alignment = Alignment;
    const CRect &margin = sts.marginRect.get();
    style->MarginL = margin.left;
    style->MarginR = margin.right;
    style->MarginV = margin.bottom;
    style->Encoding = sts.charSet;
}

void CRenderedTextSubtitle::SetLibassRenderOptions(const LibassRenderOptions &options)
{
    STSStyle default_style;
    if (GetDefaultStyle(default_style)) {
        CStringA font_name;
        ASS_Style style;
        StsStyleToAssStyle(default_style, font_name, &style);
        m_ass_context.ApplyRenderOptions(options, &style);
    } else {
        m_ass_context.ApplyRenderOptions(options, nullptr);
    }
}

void CRenderedTextSubtitle::ParseEffect(CSubtitle* sub, const CStringW& str)
{
    CStringW::PCXSTR str_start = str.GetString();
    CStringW::PCXSTR str_end   = str.GetLength() + str_start;
    str_start = SkipWhiteSpaceLeft(str_start, str_end);

    if(!sub || *str_start==0)
        return;

    str_end = FastSkipWhiteSpaceRight(str_start, str_end);

    const WCHAR* s = FindChar(str_start, str_end, L';');
    if(*s==L';') {
        s++;
    }
    
    const CStringW effect(str_start, s-str_start);
    if(!effect.CompareNoCase( L"Banner;" ) )
    {
        int delay, lefttoright = 0, fadeawaywidth = 0;
        if(swscanf(s, L"%d;%d;%d", &delay, &lefttoright, &fadeawaywidth) < 1) return;
        Effect* e = DEBUG_NEW Effect;
        if(!e) return;
        sub->m_effects[e->type = EF_BANNER] = e;
        e->param[0] = (int)(max(1.0*delay/sub->m_scalex, 1));
        e->param[1] = lefttoright;
        e->param[2] = (int)(sub->m_scalex*fadeawaywidth);
        sub->m_wrapStyle = 2;

        sub->m_hard_position_level = sub->m_hard_position_level > POS_LVL_NONE ? 
                                     sub->m_hard_position_level : POS_LVL_NONE;
    }
    else if(!effect.CompareNoCase(L"Scroll up;") || !effect.CompareNoCase(L"Scroll down;"))
    {
        int top, bottom, delay, fadeawayheight = 0;
        if(swscanf(s, L"%d;%d;%d;%d", &top, &bottom, &delay, &fadeawayheight) < 3) return;
        if(top > bottom) {int tmp = top; top = bottom; bottom = tmp;}
        Effect* e = DEBUG_NEW Effect;
        if(!e) return;
        sub->m_effects[e->type = EF_SCROLL] = e;
        e->param[0] = (int)(sub->m_scaley*top*MAX_SUB_PIXEL);
        e->param[1] = (int)(sub->m_scaley*bottom*MAX_SUB_PIXEL);
        e->param[2] = (int)(max(1.0*delay/sub->m_scaley, 1));
        e->param[3] = (effect.GetLength() == 12);
        e->param[4] = (int)(sub->m_scaley*fadeawayheight);

        sub->m_hard_position_level = POS_LVL_HARD;
    }
}

void CRenderedTextSubtitle::ParseString(CSubtitle* sub, CStringW str, const FwSTSStyle& style,
    const SharedPtrConstModStyleState& mod_style)
{
    if(!sub) return;
    str.Replace(L"\\N", L"\n");
    str.Replace(L"\\n", (sub->m_wrapStyle < 2 || sub->m_wrapStyle == 3) ? L" " : L"\n");
    str.Replace(L"\\h", L"\x00A0");    
    for(int ite = 0, j = 0, len = str.GetLength(); j <= len; j++)
    {
        WCHAR c = str[j];
        if(c != L'\n' && c != L' ' && c != 0)
            continue;
        if(ite < j)
        {
            if(PCWord tmp_ptr = DEBUG_NEW CText(style, str.Mid(ite, j-ite), m_ktype, m_kstart, m_kend
                , m_target_scale_x, m_target_scale_y, m_text_renderer_mode, mod_style,
                  sub->m_scalex, sub->m_scaley, IsVsFilterModMode()))
            {
                SharedPtrCWord w(tmp_ptr);
                sub->m_words.AddTail(w);
            }
            else
            {
                ///TODO: overflow handling
            }
            m_kstart = m_kend;
        }
        if(c == L'\n')
        {
            if(PCWord tmp_ptr = DEBUG_NEW CText(style, CStringW(), m_ktype, m_kstart, m_kend
                , m_target_scale_x, m_target_scale_y, m_text_renderer_mode, mod_style,
                  sub->m_scalex, sub->m_scaley, IsVsFilterModMode()))
            {
                SharedPtrCWord w(tmp_ptr);
                sub->m_words.AddTail(w);
            }
            else
            {
                ///TODO: overflow handling
            }
            m_kstart = m_kend;
        }
        else if(c == L' ')
        {
            if(PCWord tmp_ptr = DEBUG_NEW CText(style, CStringW(c), m_ktype, m_kstart, m_kend
                , m_target_scale_x, m_target_scale_y, m_text_renderer_mode, mod_style,
                  sub->m_scalex, sub->m_scaley, IsVsFilterModMode()))
            {
                SharedPtrCWord w(tmp_ptr);
                sub->m_words.AddTail(w);
            }
            else
            {
                ///TODO: overflow handling
            }
            m_kstart = m_kend;
        }
        ite = j+1;
    }
    return;
}

void CRenderedTextSubtitle::ParsePolygon(CSubtitle* sub, const CStringW& str, const FwSTSStyle& style,
    const SharedPtrConstModStyleState& mod_style)
{
    if (!sub || !str.GetLength() || !m_nPolygon) return;

    if (PCWord tmp_ptr = DEBUG_NEW CPolygon(style, str, m_ktype, m_kstart, m_kend, sub->m_scalex/(1<<(m_nPolygon-1))
        , sub->m_scaley/(1<<(m_nPolygon-1)), m_polygonBaselineOffset
        , m_target_scale_x, m_target_scale_y, false, mod_style, IsVsFilterModMode()))
    {
        SharedPtrCWord w(tmp_ptr);
        ///Todo: fix me
        //if( PCWord w_cache = m_wordCache.lookup(*w) )
        //{
        //    sub->m_words.AddTail(w_cache);
        //    delete w;
        //}
        //else
        {
            sub->m_words.AddTail(w);
        }
        m_kstart = m_kend;
    }
}

bool CRenderedTextSubtitle::ParseSSATag( AssTagList *assTags, const CStringW& str )
{
    if (!assTags) return(false);
    int nTags = 0, nUnrecognizedTags = 0;
    for(int i = 0, j; (j = str.Find(L'\\', i)) >= 0; i = j)
    {
        POSITION pos   = assTags->AddTail();
        AssTag& assTag = assTags->GetAt(pos);
        assTag.cmdType = CMD_COUNT;

        j++;
        CStringW::PCXSTR str_start = str.GetString() + j;
        CStringW::PCXSTR pc = str_start;
        while( iswspace(*pc) ) 
        {
            pc++;
        }
        j += pc-str_start;
        str_start = pc;
        while( *pc && *pc != L'(' && *pc != L'\\' )
        {
            pc++;
        }
        j += pc-str_start;
        if( pc-str_start>0 )
        {
            while( iswspace(*--pc) );
            pc++;
        }

        const CStringW cmd(str_start, pc-str_start);
        if(cmd.IsEmpty()) continue;

        CAtlArray<CStringW>& params = assTag.strParams;
        const bool has_bracket = (str[j] == L'(');
        if(has_bracket)
        {
            j++;
            CStringW::PCXSTR str_start = str.GetString() + j;
            CStringW::PCXSTR pc = str_start;
            while( iswspace(*pc) ) 
            {
                pc++;
            }
            j += pc-str_start;
            str_start = pc;
            while( *pc && *pc != L')' )
            {
                pc++;
            }
            j += pc-str_start;
            if (pc-str_start>0)
            {
                while( iswspace(*--pc) );
                pc++;
            }

            CStringW::PCXSTR param_start = str_start;
            CStringW::PCXSTR param_end = pc;
            while( param_start<param_end )
            {
                param_start = SkipWhiteSpaceLeft(param_start, param_end);

                CStringW::PCXSTR newstart = FindChar(param_start, param_end, L',');
                CStringW::PCXSTR newend = FindChar(param_start, param_end, L'\\');
                if(newstart < newend)
                {
                    if(newstart > param_start)
                    {
                        newend = FastSkipWhiteSpaceRight(param_start, newstart);
                        CStringW s(param_start, newend - param_start);
                        if(!s.IsEmpty()) params.Add(s);
                    }
                    param_start = newstart + 1;
                }
                else if(param_start<param_end)
                {
                    CStringW s(param_start, param_end - param_start);

                    params.Add(s);
                    param_start = param_end;
                }
            }
        }
        const size_t bracket_param_count = params.GetCount();

        AssCmdType cmd_type = CMD_COUNT;
        int cmd_length = min(MAX_CMD_LENGTH, cmd.GetLength());
        for( ;cmd_length>=MIN_CMD_LENGTH;cmd_length-- )
        {
            if( m_cmdMap.Lookup(cmd.Left(cmd_length), cmd_type) )
                break;
        }
        if(cmd_length<MIN_CMD_LENGTH)
            cmd_type = CMD_COUNT;
        switch( cmd_type )
        {
        case CMD_fax  :
        case CMD_fay  :
        case CMD_fe   :
        case CMD_fn   :
        case CMD_frx  :
        case CMD_fry  :
        case CMD_frz  :
        case CMD_fr   :
        case CMD_fscx :
        case CMD_fscy :
        case CMD_fsc  :
        case CMD_fsp  :
        case CMD_fs   :
        case CMD_i    :
        case CMD_kt   :
        case CMD_kf   :
        case CMD_K    :
        case CMD_ko   :
        case CMD_k    :
        case CMD_pbo  :
        case CMD_p    :
        case CMD_q    :
        case CMD_r    :
        case CMD_shad :
        case CMD_s    :
        case CMD_an   :
        case CMD_a    :
        case CMD_blur :
        case CMD_bord :
        case CMD_be   :
        case CMD_b    :
        case CMD_u    :
        case CMD_xbord:
        case CMD_xshad:
        case CMD_ybord:
        case CMD_yshad:
        case CMD_frs:
        case CMD_fsvp:
        case CMD_rnd:
        case CMD_rndx:
        case CMD_rndy:
        case CMD_rndz:
        case CMD_rnds:
        case CMD_z:
            //        default:
            params.Add(cmd.Mid(cmd_length));
            break;
        case CMD_c    :
        case CMD_1c   :
        case CMD_2c   :
        case CMD_3c   :
        case CMD_4c   :
        case CMD_1a   :
        case CMD_2a   :
        case CMD_3a   :
        case CMD_4a   :
        case CMD_alpha:
            params.Add(cmd.Mid(cmd_length).Trim(L"&H"));
            break;
        case CMD_clip :
        case CMD_iclip:
        case CMD_fade :
        case CMD_fad  :
        case CMD_move :
        case CMD_org  :
        case CMD_pos  :
        case CMD_1img :
        case CMD_2img :
        case CMD_3img :
        case CMD_4img :
        case CMD_1vc  :
        case CMD_2vc  :
        case CMD_3vc  :
        case CMD_4vc  :
        case CMD_1va  :
        case CMD_2va  :
        case CMD_3va  :
        case CMD_4va  :
        case CMD_distort:
        case CMD_jitter:
        case CMD_mover:
        case CMD_moves3:
        case CMD_moves4:
        case CMD_movevc:
            break;
        case CMD_t:
            if (!params.IsEmpty() && params.GetCount()<=4)
                ParseSSATag(&assTag.embeded, params[params.GetCount()-1]);
            break;
        case CMD_COUNT:
            nUnrecognizedTags++;
            break;
        }

        // Default (non-MOD) mode falls back to the longest shorter prefix that
        // maps to a legacy command, mirroring how a renderer without MOD support
        // would lex the tag (e.g. \rnd20 -> \r with param "nd20"). Bracket params
        // stay in strParams; inline params are re-sliced from cmd at legacy_len.
        assTag.legacyCmdType = CMD_COUNT;
        if (cmd_type >= CMD_1img && cmd_type < CMD_COUNT) {
            for (int legacy_len = cmd_length - 1; legacy_len >= MIN_CMD_LENGTH; legacy_len--) {
                AssCmdType legacy_type;
                if (m_cmdMap.Lookup(cmd.Left(legacy_len), legacy_type) && legacy_type < CMD_1img) {
                    assTag.legacyCmdType = legacy_type;
                    if (!has_bracket || bracket_param_count == 0) {
                        assTag.legacyParam = cmd.Mid(legacy_len);
                    }
                    break;
                }
            }
        }

        assTag.cmdType = cmd_type;

        nTags++;
    }
    return(true);
}

bool CRenderedTextSubtitle::ParseSSATag( CSubtitle* sub, const AssTagList& assTags, STSStyle& style,
    const STSStyle& org, SharedPtrModStyleState& mod_style, bool fAnimate /*= false*/ )
{
    if(!sub) return(false);
    
    POSITION pos = assTags.GetHeadPosition();
    while(pos)
    {
        const AssTag& assTag = assTags.GetNext(pos);
        AssCmdType cmd_type = assTag.cmdType;
        const CAtlArray<CStringW>& params = assTag.strParams;

        const bool mod_only_command = cmd_type >= CMD_1img && cmd_type < CMD_COUNT;
        const bool use_legacy_fallback = mod_only_command && !IsVsFilterModMode()
            && assTag.legacyCmdType != CMD_COUNT;
        if (mod_only_command && !IsVsFilterModMode()) {
            if (!use_legacy_fallback) {
                continue;
            }
            cmd_type = assTag.legacyCmdType;
        }
        const bool use_legacy_param = use_legacy_fallback && !assTag.legacyParam.IsEmpty();

        sub->m_hard_position_level = sub->m_hard_position_level > m_cmd_pos_level[cmd_type] ?
                                     sub->m_hard_position_level : m_cmd_pos_level[cmd_type];
        // TODO: call ParseStyleModifier(cmd, params, ..) and move the rest there
        const CStringW& p = use_legacy_param
            ? assTag.legacyParam
            : params.GetCount() > 0 ? params[0] : CStringW("");
        switch ( cmd_type )
        {
        case CMD_1c:
        case CMD_2c:
        case CMD_3c:
        case CMD_4c:
            {
                const int i = 
                      cmd_type==CMD_1c ? 0 :
                      cmd_type==CMD_2c ? 1 :
                      cmd_type==CMD_3c ? 2 :
                    /*cmd_type==CMD_4c ?*/ 3;
                DWORD c = wcstol(p, NULL, 16);
                style.colors[i] = !p.IsEmpty()
                    ? (((int)CalcAnimation(c&0x0000ff, style.colors[i]&0x0000ff, fAnimate))&0x0000ff
                      |((int)CalcAnimation(c&0x00ff00, style.colors[i]&0x00ff00, fAnimate))&0x00ff00
                      |((int)CalcAnimation(c&0xff0000, style.colors[i]&0xff0000, fAnimate))&0xff0000)
                    : org.colors[i];
                if (IsVsFilterModMode() && mod_style) {
                    ModStyleState& mod = EnsureWritableModStyleState(mod_style);
                    ModPaintLayerState& layer = mod.paint[i];
                    if (!fAnimate) {
                        layer.mode = MOD_PAINT_SOLID;
                        layer.image.reset();
                        layer.image_id.Empty();
                    } else if (layer.mode == MOD_PAINT_GRADIENT) {
                        for (int corner = 0; corner < 4; ++corner) {
                            DWORD& color = layer.colors[corner];
                            color = !p.IsEmpty()
                                ? (((int)CalcAnimation(c&0x0000ff, color&0x0000ff, true))&0x0000ff
                                  |((int)CalcAnimation(c&0x00ff00, color&0x00ff00, true))&0x00ff00
                                  |((int)CalcAnimation(c&0xff0000, color&0xff0000, true))&0xff0000)
                                : org.colors[i];
                        }
                    }
                    mod.feature_mask &= ~(MOD_FEATURE_GRADIENT | MOD_FEATURE_IMAGE);
                    for (const auto& paint : mod.paint) {
                        if (paint.mode == MOD_PAINT_GRADIENT) mod.feature_mask |= MOD_FEATURE_GRADIENT;
                        if (paint.mode == MOD_PAINT_IMAGE && paint.image) mod.feature_mask |= MOD_FEATURE_IMAGE;
                    }
                }
                break;
            }
        case CMD_1a :
        case CMD_2a :
        case CMD_3a :
        case CMD_4a :
            {
                const int i = 
                      cmd_type==CMD_1a ? 0 : 
                      cmd_type==CMD_2a ? 1 :
                      cmd_type==CMD_3a ? 2 :
                    /*cmd_type==CMD_4a ?*/ 3;
                style.alpha[i] = !p.IsEmpty()
                    ? (BYTE)CalcAnimation(wcstol(p, NULL, 16), style.alpha[i], fAnimate)
                    : org.alpha[i];
                if (IsVsFilterModMode() && mod_style) {
                    ModStyleState& mod = EnsureWritableModStyleState(mod_style);
                    ModPaintLayerState& layer = mod.paint[i];
                    if (layer.mode == MOD_PAINT_GRADIENT) {
                        const BYTE alpha = !p.IsEmpty() ? static_cast<BYTE>(wcstol(p, NULL, 16)) : org.alpha[i];
                        for (int corner = 0; corner < 4; ++corner) {
                            layer.alpha[corner] = static_cast<BYTE>(
                                CalcAnimation(alpha, layer.alpha[corner], fAnimate));
                        }
                    }
                }
                break;
            }
        case CMD_1img:
        case CMD_2img:
        case CMD_3img:
        case CMD_4img:
            {
                const int i =
                      cmd_type == CMD_1img ? 0
                    : cmd_type == CMD_2img ? 1
                    : cmd_type == CMD_3img ? 2 : 3;
                if (!params.IsEmpty() && !params[0].IsEmpty()) {
                    const SharedPtrConstModImageResource image =
                        m_mod_image_cache.Resolve(params[0], m_path, m_mod_resource_path);
                    if (!image) {
                        break;
                    }
                    ModStyleState& mod = EnsureWritableModStyleState(mod_style);
                    ModPaintLayerState& layer = mod.paint[i];
                    layer.mode = MOD_PAINT_IMAGE;
                    layer.image_id = params[0];
                    layer.image = image;
                    if (params.GetCount() >= 3) {
                        layer.image_x_offset = static_cast<int>(CalcAnimation(
                            wcstod(params[1], NULL), layer.image_x_offset, fAnimate));
                        layer.image_y_offset = static_cast<int>(CalcAnimation(
                            wcstod(params[2], NULL), layer.image_y_offset, fAnimate));
                    }
                    mod.feature_mask |= MOD_FEATURE_IMAGE;
                    if (fAnimate && params.GetCount() >= 3) {
                        sub->m_fAnimated = true;
                        sub->m_fAnimated2 = true;
                    }
                    mod.feature_mask &= ~(MOD_FEATURE_GRADIENT | MOD_FEATURE_IMAGE);
                    for (const auto& paint : mod.paint) {
                        if (paint.mode == MOD_PAINT_GRADIENT) mod.feature_mask |= MOD_FEATURE_GRADIENT;
                        if (paint.mode == MOD_PAINT_IMAGE && paint.image) mod.feature_mask |= MOD_FEATURE_IMAGE;
                    }
                }
                break;
            }
        case CMD_1vc:
        case CMD_2vc:
        case CMD_3vc:
        case CMD_4vc:
            {
                const int i =
                      cmd_type == CMD_1vc ? 0
                    : cmd_type == CMD_2vc ? 1
                    : cmd_type == CMD_3vc ? 2 : 3;
                if (params.GetCount() >= 4) {
                    ModStyleState& mod = EnsureWritableModStyleState(mod_style);
                    ModPaintLayerState& layer = mod.paint[i];
                    if (layer.mode != MOD_PAINT_GRADIENT) {
                        for (int corner = 0; corner < 4; ++corner) {
                            layer.colors[corner] = style.colors[i];
                            layer.alpha[corner] = style.alpha[i];
                        }
                        layer.image.reset();
                        layer.image_id.Empty();
                    }
                    layer.mode = MOD_PAINT_GRADIENT;
                    for (int corner = 0; corner < 4; ++corner) {
                        CStringW color_param(params[corner]);
                        color_param.Trim(L"&H");
                        const DWORD color = wcstol(color_param, NULL, 16) & 0xffffff;
                        DWORD& current = layer.colors[corner];
                        current =
                            (((int)CalcAnimation(color&0x0000ff, current&0x0000ff, fAnimate))&0x0000ff)
                          | (((int)CalcAnimation(color&0x00ff00, current&0x00ff00, fAnimate))&0x00ff00)
                          | (((int)CalcAnimation(color&0xff0000, current&0xff0000, fAnimate))&0xff0000);
                    }
                    mod.feature_mask |= MOD_FEATURE_GRADIENT;
                }
                break;
            }
        case CMD_1va:
        case CMD_2va:
        case CMD_3va:
        case CMD_4va:
            {
                const int i =
                      cmd_type == CMD_1va ? 0
                    : cmd_type == CMD_2va ? 1
                    : cmd_type == CMD_3va ? 2 : 3;
                if (params.GetCount() >= 4) {
                    ModStyleState& mod = EnsureWritableModStyleState(mod_style);
                    ModPaintLayerState& layer = mod.paint[i];
                    if (layer.mode != MOD_PAINT_GRADIENT) {
                        for (int corner = 0; corner < 4; ++corner) {
                            layer.colors[corner] = style.colors[i];
                            layer.alpha[corner] = style.alpha[i];
                        }
                        layer.image.reset();
                        layer.image_id.Empty();
                    }
                    layer.mode = MOD_PAINT_GRADIENT;
                    for (int corner = 0; corner < 4; ++corner) {
                        CStringW alpha_param(params[corner]);
                        alpha_param.Trim(L"&H");
                        const int alpha = wcstol(alpha_param, NULL, 16) & 0xff;
                        layer.alpha[corner] = static_cast<BYTE>(
                            CalcAnimation(alpha, layer.alpha[corner], fAnimate));
                    }
                    mod.feature_mask |= MOD_FEATURE_GRADIENT;
                    if (i <= 1) {
                        mod.body_gradient_alpha = true;
                    }
                }
                break;
            }
        case CMD_alpha:
            {
                for(int i = 0; i < 4; i++)
                {
                    style.alpha[i] = !p.IsEmpty()
                                     ? (BYTE)CalcAnimation(wcstol(p, NULL, 16), style.alpha[i], fAnimate)
                                     : org.alpha[i];
                    if (IsVsFilterModMode() && mod_style) {
                        ModStyleState& mod = EnsureWritableModStyleState(mod_style);
                        ModPaintLayerState& layer = mod.paint[i];
                        if (layer.mode == MOD_PAINT_GRADIENT) {
                            const BYTE alpha = !p.IsEmpty() ? static_cast<BYTE>(wcstol(p, NULL, 16)) : org.alpha[i];
                            for (int corner = 0; corner < 4; ++corner) {
                                layer.alpha[corner] = static_cast<BYTE>(
                                    CalcAnimation(alpha, layer.alpha[corner], fAnimate));
                            }
                        }
                    }
                }
                break;
            }
        case CMD_an:
            {
                int n = wcstol(p, NULL, 10);
                if (sub->m_scrAlignment < 0)
                    sub->m_scrAlignment = (n > 0 && n < 10) ? n : org.scrAlignment;
                break;
            }
        case CMD_a:
            {
                int n = wcstol(p, NULL, 10);
                if (sub->m_scrAlignment < 0)
                    sub->m_scrAlignment = (n > 0 && n < 12) ? ((((n-1)&3)+1)+((n&4)?6:0)+((n&8)?3:0)) : org.scrAlignment;
                break;
            }
        case CMD_blur:
            {
                double n = CalcAnimation(wcstod(p, NULL), style.fGaussianBlur, fAnimate);
                style.fGaussianBlur = !p.IsEmpty()
                                      ? (n < 0 ? 0 : n)
                                          : org.fGaussianBlur;
                break;
            }
        case CMD_bord:
            {
                double dst = wcstod(p, NULL);
                double nx = CalcAnimation(dst, style.outlineWidthX, fAnimate);
                style.outlineWidthX = !p.IsEmpty()
                                      ? (nx < 0 ? 0 : nx)
                                          : org.outlineWidthX;
                double ny = CalcAnimation(dst, style.outlineWidthY, fAnimate);
                style.outlineWidthY = !p.IsEmpty()
                                      ? (ny < 0 ? 0 : ny)
                                          : org.outlineWidthY;
                break;
            }
        case CMD_be:
            {
                double d = CalcAnimation(wcstod(p, NULL), style.fBlur, fAnimate);
                style.fBlur = !p.IsEmpty()
                              ? d
                              : org.fBlur;
                break;
            }
        case CMD_b:
            {
                int n = wcstol(p, NULL, 10);
                style.fontWeight = !p.IsEmpty()
                                   ? (n == 0 ? FW_NORMAL : n == 1 ? FW_BOLD : n >= 100 ? n : org.fontWeight)
                                       : org.fontWeight;
                break;
            }
        case CMD_clip:
        case CMD_iclip:
            {
                bool invert = (cmd_type == CMD_iclip);
                if(params.GetCount() == 1 && !sub->m_pClipper)
                {
                    sub->m_pClipper.reset( DEBUG_NEW CClipper(params[0],
                        CSize(m_video_rect.Width()>>3, m_video_rect.Height()>>3),
                        sub->m_scalex, sub->m_scaley, invert, m_target_scale_x, m_target_scale_y) );
                }
                else if(params.GetCount() == 2 && !sub->m_pClipper)
                {
                    int scale = max(wcstol(p, NULL, 10), 1);
                    sub->m_pClipper.reset( DEBUG_NEW CClipper(params[1],
                        CSize(m_video_rect.Width()>>3, m_video_rect.Height()>>3),
                        sub->m_scalex/(1<<(scale-1)), sub->m_scaley/(1<<(scale-1)), invert, m_target_scale_x, m_target_scale_y) );
                }
                else if(params.GetCount() == 4)
                {
                    CRect r;
                    sub->m_clipInverse = invert;
                    r.SetRect(
                        wcstod(params[0], NULL)+0.5,
                        wcstod(params[1], NULL)+0.5,
                        wcstod(params[2], NULL)+0.5,
                        wcstod(params[3], NULL)+0.5);
                    sub->m_clip.SetRect(
                        (int)CalcAnimation(sub->m_scalex*r.left  , sub->m_clip.left  , fAnimate),
                        (int)CalcAnimation(sub->m_scaley*r.top   , sub->m_clip.top   , fAnimate),
                        (int)CalcAnimation(sub->m_scalex*r.right , sub->m_clip.right , fAnimate),
                        (int)CalcAnimation(sub->m_scaley*r.bottom, sub->m_clip.bottom, fAnimate));
                }
                break;
            }
        case CMD_c:
            {
                DWORD c = wcstol(p, NULL, 16);
                style.colors[0] = !p.IsEmpty()
                                  ? (((int)CalcAnimation(c&0x0000ff, style.colors[0]&0x0000ff, fAnimate))&0x0000ff
                                    |((int)CalcAnimation(c&0x00ff00, style.colors[0]&0x00ff00, fAnimate))&0x00ff00
                                    |((int)CalcAnimation(c&0xff0000, style.colors[0]&0xff0000, fAnimate))&0xff0000)
                                  : org.colors[0];
                if (IsVsFilterModMode() && mod_style) {
                    ModStyleState& mod = EnsureWritableModStyleState(mod_style);
                    ModPaintLayerState& layer = mod.paint[0];
                    if (!fAnimate) {
                        layer.mode = MOD_PAINT_SOLID;
                        layer.image.reset();
                        layer.image_id.Empty();
                    } else if (layer.mode == MOD_PAINT_GRADIENT) {
                        for (int corner = 0; corner < 4; ++corner) {
                            DWORD& color = layer.colors[corner];
                            color = !p.IsEmpty()
                                ? (((int)CalcAnimation(c&0x0000ff, color&0x0000ff, true))&0x0000ff
                                  |((int)CalcAnimation(c&0x00ff00, color&0x00ff00, true))&0x00ff00
                                  |((int)CalcAnimation(c&0xff0000, color&0xff0000, true))&0xff0000)
                                : org.colors[0];
                        }
                    }
                    mod.feature_mask &= ~(MOD_FEATURE_GRADIENT | MOD_FEATURE_IMAGE);
                    for (const auto& paint : mod.paint) {
                        if (paint.mode == MOD_PAINT_GRADIENT) mod.feature_mask |= MOD_FEATURE_GRADIENT;
                        if (paint.mode == MOD_PAINT_IMAGE && paint.image) mod.feature_mask |= MOD_FEATURE_IMAGE;
                    }
                }
                break;
            }
        case CMD_distort:
            {
                if (params.GetCount() >= 6) {
                    ModStyleState& mod = EnsureWritableModStyleState(mod_style);
                    for (int point = 0; point < 3; ++point) {
                        mod.distort_x[point] = CalcAnimation(
                            wcstod(params[point * 2], NULL), mod.distort_x[point], fAnimate);
                        mod.distort_y[point] = CalcAnimation(
                            wcstod(params[point * 2 + 1], NULL), mod.distort_y[point], fAnimate);
                    }
                    mod.feature_mask |= MOD_FEATURE_DISTORT;
                }
                break;
            }
        case CMD_fade:
        case CMD_fad:
            {
                sub->m_fAnimated2 = true;
                if(params.GetCount() == 7 && !sub->m_effects[EF_FADE])// {\fade(a1=param[0], a2=param[1], a3=param[2], t1=t[0], t2=t[1], t3=t[2], t4=t[3])
                {
                    if(Effect* e = DEBUG_NEW Effect)
                    {
                        for(int i = 0; i < 3; i++)
                            e->param[i] = wcstol(params[i], NULL, 10);
                        for(int i = 0; i < 4; i++)
                            e->t[i] = wcstol(params[3+i], NULL, 10);
                        sub->m_effects[EF_FADE] = e;
                    }
                }
                else if(params.GetCount() == 2 && !sub->m_effects[EF_FADE]) // {\fad(t1=t[1], t2=t[2])
                {
                    if(Effect* e = DEBUG_NEW Effect)
                    {
                        e->param[0] = e->param[2] = 0xff;
                        e->param[1] = 0x00;
                        for(int i = 1; i < 3; i++)
                            e->t[i] = wcstol(params[i-1], NULL, 10);
                        e->t[0] = e->t[3] = -1; // will be substituted with "start" and "end"
                        sub->m_effects[EF_FADE] = e;
                    }
                }
                break;
            }
        case CMD_fax:
            {
                style.fontShiftX = !p.IsEmpty()
                                   ? CalcAnimation(wcstod(p, NULL), style.fontShiftX, fAnimate)
                                   : org.fontShiftX;
                break;
            }
        case CMD_fay:
            {
                style.fontShiftY = !p.IsEmpty()
                                   ? CalcAnimation(wcstod(p, NULL), style.fontShiftY, fAnimate)
                                   : org.fontShiftY;
                break;
            }
        case CMD_fe:
            {
                int n = wcstol(p, NULL, 10);
                style.charSet = !p.IsEmpty()
                                ? n
                                : org.charSet;
                break;
            }
        case CMD_fn:
            {
                if(!p.IsEmpty() && p != L'0')
                    style.fontName = CString(p).Trim();
                else
                    style.fontName = org.fontName;
                break;
            }
        case CMD_frs:
            {
                ModStyleState& mod = EnsureWritableModStyleState(mod_style);
                mod.font_orientation = !p.IsEmpty()
                    ? CalcAnimation(wcstod(p, NULL), mod.font_orientation, fAnimate)
                    : 0;
                if (mod.font_orientation != 0) {
                    mod.feature_mask |= MOD_FEATURE_SYMBOL_ROTATION;
                } else {
                    mod.feature_mask &= ~MOD_FEATURE_SYMBOL_ROTATION;
                }
                break;
            }
        case CMD_frx:
            {
                style.fontAngleX = !p.IsEmpty()
                                   ? CalcAnimation(wcstod(p, NULL), style.fontAngleX, fAnimate)
                                   : org.fontAngleX;
                break;
            }
        case CMD_fry:
            {
                style.fontAngleY = !p.IsEmpty()
                                   ? CalcAnimation(wcstod(p, NULL), style.fontAngleY, fAnimate)
                                   : org.fontAngleY;
                break;
            }
        case CMD_frz:
        case CMD_fr:
            {
                style.fontAngleZ = !p.IsEmpty()
                                   ? CalcAnimation(wcstod(p, NULL), style.fontAngleZ, fAnimate)
                                   : org.fontAngleZ;
                break;
            }
        case CMD_fscx:
            {
                const double target = IsVsFilterModMode()
                    ? static_cast<double>(wcstol(p, NULL, 10))
                    : wcstod(p, NULL);
                double n = CalcAnimation(target, style.fontScaleX, fAnimate);
                style.fontScaleX = !p.IsEmpty()
                                   ? ((n < 0) ? 0 : n)
                                       : org.fontScaleX;
                break;
            }
        case CMD_fscy:
            {
                const double target = IsVsFilterModMode()
                    ? static_cast<double>(wcstol(p, NULL, 10))
                    : wcstod(p, NULL);
                double n = CalcAnimation(target, style.fontScaleY, fAnimate);
                style.fontScaleY = !p.IsEmpty()
                                   ? ((n < 0) ? 0 : n)
                                       : org.fontScaleY;
                break;
            }
        case CMD_fsc:
            {
                if (IsVsFilterModMode() && !p.IsEmpty()) {
                    const double dst = wcstod(p, NULL);
                    style.fontScaleX = max(0.0, CalcAnimation(dst, style.fontScaleX, fAnimate));
                    style.fontScaleY = max(0.0, CalcAnimation(dst, style.fontScaleY, fAnimate));
                } else {
                    style.fontScaleX = org.fontScaleX;
                    style.fontScaleY = org.fontScaleY;
                }
                break;
            }
        case CMD_fsp:
            {
                style.fontSpacing = !p.IsEmpty()
                                    ? CalcAnimation(wcstod(p, NULL), style.fontSpacing, fAnimate)
                                    : org.fontSpacing;
                break;
            }
        case CMD_fsvp:
            {
                ModStyleState& mod = EnsureWritableModStyleState(mod_style);
                mod.vertical_spacing = !p.IsEmpty()
                    ? CalcAnimation(wcstod(p, NULL) * MAX_SUB_PIXEL, mod.vertical_spacing, fAnimate)
                    : 0;
                if (mod.vertical_spacing != 0) {
                    mod.feature_mask |= MOD_FEATURE_VERTICAL_SPACING;
                } else {
                    mod.feature_mask &= ~MOD_FEATURE_VERTICAL_SPACING;
                }
                break;
            }
        case CMD_fs:
            {
                if(!p.IsEmpty())
                {
                    if(p[0] == L'-' || p[0] == L'+')
                    {
                        double n = CalcAnimation(style.fontSize + style.fontSize*wcstod(p, NULL)/10, style.fontSize, fAnimate);
                        style.fontSize = (n > 0) ? n : org.fontSize;
                    }
                    else
                    {
                        double n = CalcAnimation(wcstod(p, NULL), style.fontSize, fAnimate);
                        style.fontSize = (n > 0) ? n : org.fontSize;
                    }
                }
                else
                {
                    style.fontSize = org.fontSize;
                }
                break;
            }
        case CMD_i:
            {
                int n = wcstol(p, NULL, 10);
                style.fItalic = !p.IsEmpty()
                                ? (n == 0 ? false : n == 1 ? true : org.fItalic)
                                    : org.fItalic;
                break;
            }
        case CMD_jitter:
            {
                if (params.GetCount() >= 4) {
                    ModStyleState& mod = EnsureWritableModStyleState(mod_style);
                    mod.jitter.left = static_cast<int>(CalcAnimation(
                        abs(wcstol(params[0], NULL, 10)) * MAX_SUB_PIXEL, mod.jitter.left, fAnimate));
                    mod.jitter.right = static_cast<int>(CalcAnimation(
                        abs(wcstol(params[1], NULL, 10)) * MAX_SUB_PIXEL, mod.jitter.right, fAnimate));
                    mod.jitter.up = static_cast<int>(CalcAnimation(
                        abs(wcstol(params[2], NULL, 10)) * MAX_SUB_PIXEL, mod.jitter.up, fAnimate));
                    mod.jitter.down = static_cast<int>(CalcAnimation(
                        abs(wcstol(params[3], NULL, 10)) * MAX_SUB_PIXEL, mod.jitter.down, fAnimate));
                    if (params.GetCount() >= 5) {
                        mod.jitter.period_100ns = max(1, static_cast<int>(CalcAnimation(
                            wcstol(params[4], NULL, 10) * 10000.0, mod.jitter.period_100ns, fAnimate)));
                    }
                    if (params.GetCount() >= 6) {
                        mod.jitter.seed = wcstol(params[5], NULL, 10);
                    }
                    mod.feature_mask |= MOD_FEATURE_JITTER;
                    sub->m_fAnimated = true;
                    sub->m_fAnimated2 = true;
                }
                break;
            }
        case CMD_kt:
            {
                m_kstart = !p.IsEmpty()
                           ? wcstod(p, NULL)*10
                           : 0;
                m_kend = m_kstart;
                sub->m_fAnimated2 = true;//fix me: define m_fAnimated m_fAnimated2 strictly
                break;
            }
        case CMD_kf:
        case CMD_K:
            {
                m_ktype = 1;
                m_kstart = m_kend;
                m_kend += !p.IsEmpty()
                          ? wcstod(p, NULL)*10
                          : 1000;
                sub->m_fAnimated2 = true;//fix me: define m_fAnimated m_fAnimated2 strictly
                break;
            }
        case CMD_ko:
            {
                m_ktype = 2;
                m_kstart = m_kend;
                m_kend += !p.IsEmpty()
                          ? wcstod(p, NULL)*10
                          : 1000;
                sub->m_fAnimated2 = true;//fix me: define m_fAnimated m_fAnimated2 strictly
                break;
            }
        case CMD_k:
            {
                m_ktype = 0;
                m_kstart = m_kend;
                m_kend += !p.IsEmpty()
                          ? wcstod(p, NULL)*10
                          : 1000;
                sub->m_fAnimated2 = true;//fix me: define m_fAnimated m_fAnimated2 strictly
                break;
            }
        case CMD_mover:
            {
                if ((params.GetCount() == 8 || params.GetCount() == 10)
                        && !sub->m_effects[EF_MOVE]
                        && (!sub->m_mod_effects || !(sub->m_mod_effects->feature_mask & MOD_EFFECT_MOVE))) {
                    ModEffectState& effect = EnsureWritableModEffectState(sub->m_mod_effects);
                    effect.feature_mask |= MOD_EFFECT_MOVE;
                    effect.move_type = MOD_MOVE_RADIAL;
                    effect.move_points[0].SetPoint(
                        static_cast<int>(sub->m_scalex * wcstod(params[0], NULL) * MAX_SUB_PIXEL),
                        static_cast<int>(sub->m_scaley * wcstod(params[1], NULL) * MAX_SUB_PIXEL));
                    effect.move_points[1].SetPoint(
                        static_cast<int>(sub->m_scalex * wcstod(params[2], NULL) * MAX_SUB_PIXEL),
                        static_cast<int>(sub->m_scaley * wcstod(params[3], NULL) * MAX_SUB_PIXEL));
                    effect.move_angle[0] =
                        static_cast<double>(
                            static_cast<int>(wcstod(params[4], NULL) * 10000.0)
                            * 3.141592654f / 1800000);
                    effect.move_angle[1] =
                        static_cast<double>(
                            static_cast<int>(wcstod(params[5], NULL) * 10000.0)
                            * 3.141592654f / 1800000);
                    effect.move_radius.SetPoint(
                        static_cast<int>(sub->m_scalex * wcstod(params[6], NULL) * MAX_SUB_PIXEL),
                        static_cast<int>(sub->m_scaley * wcstod(params[7], NULL) * MAX_SUB_PIXEL));
                    if (params.GetCount() == 10) {
                        effect.move_t[0] = wcstol(params[8], NULL, 10);
                        effect.move_t[1] = wcstol(params[9], NULL, 10);
                    }
                    sub->m_fAnimated = true;
                    sub->m_fAnimated2 = true;
                }
                break;
            }
        case CMD_moves3:
            {
                if ((params.GetCount() == 6 || params.GetCount() == 8)
                        && !sub->m_effects[EF_MOVE]
                        && (!sub->m_mod_effects || !(sub->m_mod_effects->feature_mask & MOD_EFFECT_MOVE))) {
                    ModEffectState& effect = EnsureWritableModEffectState(sub->m_mod_effects);
                    effect.feature_mask |= MOD_EFFECT_MOVE;
                    effect.move_type = MOD_MOVE_QUADRATIC;
                    for (int point = 0; point < 3; ++point) {
                        effect.move_points[point].SetPoint(
                            static_cast<int>(sub->m_scalex * wcstod(params[point * 2], NULL) * MAX_SUB_PIXEL),
                            static_cast<int>(sub->m_scaley * wcstod(params[point * 2 + 1], NULL) * MAX_SUB_PIXEL));
                    }
                    if (params.GetCount() == 8) {
                        effect.move_t[0] = wcstol(params[6], NULL, 10);
                        effect.move_t[1] = wcstol(params[7], NULL, 10);
                    }
                    sub->m_fAnimated = true;
                    sub->m_fAnimated2 = true;
                }
                break;
            }
        case CMD_moves4:
            {
                if ((params.GetCount() == 8 || params.GetCount() == 10)
                        && !sub->m_effects[EF_MOVE]
                        && (!sub->m_mod_effects || !(sub->m_mod_effects->feature_mask & MOD_EFFECT_MOVE))) {
                    ModEffectState& effect = EnsureWritableModEffectState(sub->m_mod_effects);
                    effect.feature_mask |= MOD_EFFECT_MOVE;
                    effect.move_type = MOD_MOVE_CUBIC;
                    for (int point = 0; point < 4; ++point) {
                        effect.move_points[point].SetPoint(
                            static_cast<int>(sub->m_scalex * wcstod(params[point * 2], NULL) * MAX_SUB_PIXEL),
                            static_cast<int>(sub->m_scaley * wcstod(params[point * 2 + 1], NULL) * MAX_SUB_PIXEL));
                    }
                    if (params.GetCount() == 10) {
                        effect.move_t[0] = wcstol(params[8], NULL, 10);
                        effect.move_t[1] = wcstol(params[9], NULL, 10);
                    }
                    sub->m_fAnimated = true;
                    sub->m_fAnimated2 = true;
                }
                break;
            }
        case CMD_movevc:
            {
                if ((params.GetCount() == 2 || params.GetCount() == 4 || params.GetCount() == 6)
                        && (!sub->m_mod_effects || !(sub->m_mod_effects->feature_mask & MOD_EFFECT_MOVING_CLIP))) {
                    ModEffectState& effect = EnsureWritableModEffectState(sub->m_mod_effects);
                    effect.feature_mask |= MOD_EFFECT_MOVING_CLIP;
                    effect.clip_points[0].SetPoint(
                        static_cast<int>(sub->m_scalex * wcstod(params[0], NULL)),
                        static_cast<int>(sub->m_scaley * wcstod(params[1], NULL)));
                    effect.clip_points[1] = effect.clip_points[0];
                    if (params.GetCount() >= 4) {
                        effect.clip_points[1].SetPoint(
                            static_cast<int>(sub->m_scalex * wcstod(params[2], NULL)),
                            static_cast<int>(sub->m_scaley * wcstod(params[3], NULL)));
                    }
                    if (params.GetCount() == 6) {
                        effect.clip_t[0] = static_cast<int>(
                            sub->m_scalex * wcstod(params[4], NULL));
                        effect.clip_t[1] = static_cast<int>(
                            sub->m_scaley * wcstod(params[5], NULL));
                    }
                    sub->m_fAnimated = true;
                    sub->m_fAnimated2 = true;
                }
                break;
            }
        case CMD_move: // {\move(x1=param[0], y1=param[1], x2=param[2], y2=param[3][, t1=t[0], t2=t[1]])}
            {
                if((params.GetCount() == 4 || params.GetCount() == 6) && !sub->m_effects[EF_MOVE])
                {
                    if(Effect* e = DEBUG_NEW Effect)
                    {
                        e->param[0] = (int)(sub->m_scalex*wcstod(params[0], NULL)*MAX_SUB_PIXEL);
                        e->param[1] = (int)(sub->m_scaley*wcstod(params[1], NULL)*MAX_SUB_PIXEL);
                        e->param[2] = (int)(sub->m_scalex*wcstod(params[2], NULL)*MAX_SUB_PIXEL);
                        e->param[3] = (int)(sub->m_scaley*wcstod(params[3], NULL)*MAX_SUB_PIXEL);
                        e->t[0] = e->t[1] = -1;
                        if(params.GetCount() == 6)
                        {
                            for(int i = 0; i < 2; i++)
                                e->t[i] = wcstol(params[4+i], NULL, 10);
                        }
                        sub->m_effects[EF_MOVE] = e;
                        sub->m_fAnimated2 = true;
                    }
                }
                break;
            }
        case CMD_org: // {\org(x=param[0], y=param[1])}
            {
                if(params.GetCount() == 2 && !sub->m_effects[EF_ORG])
                {
                    if(Effect* e = DEBUG_NEW Effect)
                    {
                        e->param[0] = (int)(sub->m_scalex*wcstod(params[0], NULL)*MAX_SUB_PIXEL);
                        e->param[1] = (int)(sub->m_scaley*wcstod(params[1], NULL)*MAX_SUB_PIXEL);
                        sub->m_effects[EF_ORG] = e;
                    }
                }
                else if (IsVsFilterModMode()
                        && (params.GetCount() == 4 || params.GetCount() == 6)
                        && (!sub->m_mod_effects || !(sub->m_mod_effects->feature_mask & MOD_EFFECT_MOVING_ORG))) {
                    ModEffectState& effect = EnsureWritableModEffectState(sub->m_mod_effects);
                    effect.feature_mask |= MOD_EFFECT_MOVING_ORG;
                    effect.org_points[0].SetPoint(
                        static_cast<int>(sub->m_scalex * wcstod(params[0], NULL) * MAX_SUB_PIXEL),
                        static_cast<int>(sub->m_scaley * wcstod(params[1], NULL) * MAX_SUB_PIXEL));
                    effect.org_points[1].SetPoint(
                        static_cast<int>(sub->m_scalex * wcstod(params[2], NULL) * MAX_SUB_PIXEL),
                        static_cast<int>(sub->m_scaley * wcstod(params[3], NULL) * MAX_SUB_PIXEL));
                    if (params.GetCount() == 6) {
                        effect.org_t[0] = static_cast<int>(
                            sub->m_scalex * wcstod(params[4], NULL) * MAX_SUB_PIXEL);
                        effect.org_t[1] = static_cast<int>(
                            sub->m_scaley * wcstod(params[5], NULL) * MAX_SUB_PIXEL);
                    }
                    sub->m_fAnimated = true;
                    sub->m_fAnimated2 = true;
                }
                break;
            }
        case CMD_pbo:
            {
                m_polygonBaselineOffset = wcstol(p, NULL, 10);
                break;
            }
        case CMD_pos:
            {
                if((params.GetCount() == 2 || (IsVsFilterModMode() && params.GetCount() == 3))
                        && !sub->m_effects[EF_MOVE])
                {
                    if(Effect* e = DEBUG_NEW Effect)
                    {
                        e->param[0] = e->param[2] = (int)(sub->m_scalex*wcstod(params[0], NULL)*MAX_SUB_PIXEL);
                        e->param[1] = e->param[3] = (int)(sub->m_scaley*wcstod(params[1], NULL)*MAX_SUB_PIXEL);
                        e->t[0] = e->t[1] = 0;
                        sub->m_effects[EF_MOVE] = e;
                    }
                    if (params.GetCount() == 3) {
                        ModStyleState& mod = EnsureWritableModStyleState(mod_style);
                        mod.z = wcstod(params[2], NULL) * 80.0;
                        if (mod.z != 0) mod.feature_mask |= MOD_FEATURE_Z;
                    }
                }
                break;
            }
        case CMD_p:
            {
                int n = wcstol(p, NULL, 10);
                m_nPolygon = (n <= 0 ? 0 : n);
                break;
            }
        case CMD_q:
            {
                int n = wcstol(p, NULL, 10);
                sub->m_wrapStyle = !p.IsEmpty() && (0 <= n && n <= 3)
                                   ? n
                                   : m_defaultWrapStyle;
                break;
            }
        case CMD_rnds:
            {
                ModStyleState& mod = EnsureWritableModStyleState(mod_style);
                mod.random_seed = !p.IsEmpty()
                    ? static_cast<int>(CalcAnimation(wcstol(p, NULL, 16), mod.random_seed, fAnimate))
                    : 0;
                if (mod.random_x != 0 || mod.random_y != 0 || mod.random_z != 0) {
                    mod.feature_mask |= MOD_FEATURE_RANDOM;
                }
                break;
            }
        case CMD_rndx:
        case CMD_rndy:
        case CMD_rndz:
        case CMD_rnd:
            {
                ModStyleState& mod = EnsureWritableModStyleState(mod_style);
                const double value = !p.IsEmpty() ? wcstod(p, NULL) * MAX_SUB_PIXEL : 0;
                if (cmd_type == CMD_rnd || cmd_type == CMD_rndx) {
                    // VSFilterMod's MOD_RANDOM fields are integers. Preserve
                    // that truncation after animated tags so the random
                    // sequence and resulting offsets use the same amplitude.
                    mod.random_x = static_cast<int>(CalcAnimation(value, mod.random_x, fAnimate));
                }
                if (cmd_type == CMD_rnd || cmd_type == CMD_rndy) {
                    mod.random_y = static_cast<int>(CalcAnimation(value, mod.random_y, fAnimate));
                }
                if (cmd_type == CMD_rnd || cmd_type == CMD_rndz) {
                    mod.random_z = static_cast<int>(CalcAnimation(value, mod.random_z, fAnimate));
                }
                if (mod.random_x != 0 || mod.random_y != 0 || mod.random_z != 0) {
                    mod.feature_mask |= MOD_FEATURE_RANDOM;
                } else {
                    mod.feature_mask &= ~MOD_FEATURE_RANDOM;
                }
                break;
            }
        case CMD_r:
            {
                STSStyle* val;
                style = (!p.IsEmpty() && m_styles.Lookup(p, val) && val) ? *val : org;
                if (!IsVsFilterModMode() && style.fontSpacing < 0) {
                    style.fontSpacing = 0;
                }
                mod_style.reset();
                break;
            }
        case CMD_shad:
            {
                double dst = wcstod(p, NULL);
                double nx = CalcAnimation(dst, style.shadowDepthX, fAnimate);
                style.shadowDepthX = !p.IsEmpty()
                                     ? (nx < 0 ? 0 : nx)
                                         : org.shadowDepthX;
                double ny = CalcAnimation(dst, style.shadowDepthY, fAnimate);
                style.shadowDepthY = !p.IsEmpty()
                                     ? (ny < 0 ? 0 : ny)
                                         : org.shadowDepthY;
                break;
            }
        case CMD_s:
            {
                int n = wcstol(p, NULL, 10);
                style.fStrikeOut = !p.IsEmpty()
                                   ? (n == 0 ? false : n == 1 ? true : org.fStrikeOut)
                                       : org.fStrikeOut;
                break;
            }
        case CMD_t: // \t([<t1>,<t2>,][<accel>,]<style modifiers>)
            {
                CStringW param;
                m_animStart = m_animEnd = 0;
                m_animAccel = 1;
                if(params.GetCount() == 1)
                {
                    param = params[0];
                }
                else if(params.GetCount() == 2)
                {
                    m_animAccel = wcstod(params[0], NULL);
                    param = params[1];
                }
                else if(params.GetCount() == 3)
                {
                    m_animStart = (int)wcstod(params[0], NULL);
                    m_animEnd = (int)wcstod(params[1], NULL);
                    param = params[2];
                }
                else if(params.GetCount() == 4)
                {
                    m_animStart = wcstol(params[0], NULL, 10);
                    m_animEnd = wcstol(params[1], NULL, 10);
                    m_animAccel = wcstod(params[2], NULL);
                    param = params[3];
                }
                ParseSSATag(sub, assTag.embeded, style, org, mod_style, true);
                sub->m_fAnimated = true;
                sub->m_fAnimated2 = true;
                break;
            }
        case CMD_u:
            {
                int n = wcstol(p, NULL, 10);
                style.fUnderline = !p.IsEmpty()
                                   ? (n == 0 ? false : n == 1 ? true : org.fUnderline)
                                       : org.fUnderline;
                break;
            }
        case CMD_xbord:
            {
                double dst = wcstod(p, NULL);
                double nx = CalcAnimation(dst, style.outlineWidthX, fAnimate);
                style.outlineWidthX = !p.IsEmpty()
                                      ? (nx < 0 ? 0 : nx)
                                          : org.outlineWidthX;
                break;
            }
        case CMD_xshad:
            {
                double dst = wcstod(p, NULL);
                double nx = CalcAnimation(dst, style.shadowDepthX, fAnimate);
                style.shadowDepthX = !p.IsEmpty()
                                     ? nx
                                     : org.shadowDepthX;
                break;
            }
        case CMD_ybord:
            {
                double dst = wcstod(p, NULL);
                double ny = CalcAnimation(dst, style.outlineWidthY, fAnimate);
                style.outlineWidthY = !p.IsEmpty()
                                      ? (ny < 0 ? 0 : ny)
                                          : org.outlineWidthY;
                break;
            }
        case CMD_yshad:
            {
                double dst = wcstod(p, NULL);
                double ny = CalcAnimation(dst, style.shadowDepthY, fAnimate);
                style.shadowDepthY = !p.IsEmpty()
                                     ? ny
                                     : org.shadowDepthY;
                break;
            }
        case CMD_z:
            {
                ModStyleState& mod = EnsureWritableModStyleState(mod_style);
                mod.z = !p.IsEmpty()
                    ? CalcAnimation(wcstod(p, NULL) * 80.0, mod.z, fAnimate)
                    : 0;
                if (mod.z != 0) {
                    mod.feature_mask |= MOD_FEATURE_Z;
                } else {
                    mod.feature_mask &= ~MOD_FEATURE_Z;
                }
                break;
            }
        default:
            break;
        }
    }
    return(true);
}

bool CRenderedTextSubtitle::ParseSSATag(CSubtitle* sub, const CStringW& str, STSStyle& style,
    const STSStyle& org, SharedPtrModStyleState& mod_style, bool fAnimate)
{
    if(!sub) return(false);   

    SharedPtrConstAssTagList assTags;
    AssTagListMruCache *ass_tag_cache = CacheManager::GetAssTagListMruCache();
    POSITION pos = ass_tag_cache->Lookup(str);
    if (pos==NULL)
    {
        AssTagList *tmp = DEBUG_NEW AssTagList();
        ParseSSATag(tmp, str);
        assTags.reset(tmp);
        ass_tag_cache->UpdateCache(str, assTags);
    }
    else
    {
        assTags = ass_tag_cache->GetAt(pos);
        ass_tag_cache->UpdateCache( pos );
    }
    return ParseSSATag(sub, *assTags, style, org, mod_style, fAnimate);
}

bool CRenderedTextSubtitle::ParseHtmlTag(CSubtitle* sub, CStringW str, STSStyle& style, STSStyle& org)
{
    if(str.Find(L"!--") == 0)
        return(true);
    bool fClosing = str[0] == L'/';
    str.Trim(L" /");
    int i = str.Find(L' ');
    if(i < 0) i = str.GetLength();
    CStringW tag = str.Left(i).MakeLower();
    str = str.Mid(i).Trim();
    CAtlArray<CStringW> attribs, params;
    while((i = str.Find(L'=')) > 0)
    {
        attribs.Add(str.Left(i).Trim().MakeLower());
        str = str.Mid(i+1);
        for(i = 0; _istspace(str[i]); i++);
        str = str.Mid(i);
        if(str[0] == L'\"') {str = str.Mid(1); i = str.Find(L'\"');}
        else i = str.Find(L' ');
        if(i < 0) i = str.GetLength();
        params.Add(str.Left(i).Trim().MakeLower());
        str = str.Mid(i+1);
    }
    if(tag == L"text")
        ;
    else if(tag == L"b" || tag == L"strong")
        style.fontWeight = !fClosing ? FW_BOLD : org.fontWeight;
    else if(tag == L"i" || tag == L"em")
        style.fItalic = !fClosing ? true : org.fItalic;
    else if(tag == L"u")
        style.fUnderline = !fClosing ? true : org.fUnderline;
    else if(tag == L"s" || tag == L"strike" || tag == L"del")
        style.fStrikeOut = !fClosing ? true : org.fStrikeOut;
    else if(tag == L"font")
    {
        if(!fClosing)
        {
            for(size_t i = 0; i < attribs.GetCount(); i++)
            {
                if(params[i].IsEmpty()) continue;
                int nColor = -1;
                if(attribs[i] == L"face")
                {
                    style.fontName = params[i];
                }
                else if(attribs[i] == L"size")
                {
                    if(params[i][0] == L'+')
                        style.fontSize += wcstol(params[i], NULL, 10);
                    else if(params[i][0] == L'-')
                        style.fontSize -= wcstol(params[i], NULL, 10);
                    else
                        style.fontSize = wcstol(params[i], NULL, 10);
                }
                else if(attribs[i] == L"color")
                {
                    nColor = 0;
                }
                else if(attribs[i] == L"outline-color")
                {
                    nColor = 2;
                }
                else if(attribs[i] == L"outline-level")
                {
                    style.outlineWidthX = style.outlineWidthY = wcstol(params[i], NULL, 10);
                }
                else if(attribs[i] == L"shadow-color")
                {
                    nColor = 3;
                }
                else if(attribs[i] == L"shadow-level")
                {
                    style.shadowDepthX = style.shadowDepthY = wcstol(params[i], NULL, 10);
                }
                if(nColor >= 0 && nColor < 4)
                {
                    CString key = params[i].TrimLeft(L'#');
                    DWORD val;
                    if(g_colors.Lookup(key, val))
                        style.colors[nColor] = val;
                    else if((style.colors[nColor] = _tcstol(key, NULL, 16)) == 0)
                        style.colors[nColor] = 0x00ffffff;  // default is white
                    style.colors[nColor] = ((style.colors[nColor]>>16)&0xff)|((style.colors[nColor]&0xff)<<16)|(style.colors[nColor]&0x00ff00);
                }
            }
        }
        else
        {
            style.fontName = org.fontName;
            style.fontSize = org.fontSize;
            memcpy(style.colors, org.colors, sizeof(style.colors));
        }
    }
    else if(tag == L"k" && attribs.GetCount() == 1 && attribs[0] == L"t")
    {
        m_ktype = 1;
        m_kstart = m_kend;
        m_kend += wcstol(params[0], NULL, 10);
    }
    else
        return(false);
    return(true);
}

double CRenderedTextSubtitle::CalcAnimation(double dst, double src, bool fAnimate)
{
    int s = m_animStart ? m_animStart : 0;
    int e = m_animEnd ? m_animEnd : m_delay;
    if(fabs(dst-src) >= 0.0001 && fAnimate)
    {
        if(m_time < s) dst = src;
        else if(s <= m_time && m_time < e)
        {
            double t = pow(1.0 * (m_time - s) / (e - s), m_animAccel);
            dst = (1 - t) * src + t * dst;
        }
//      else dst = dst;
    }
    return(dst);
}

bool CRenderedTextSubtitle::IsVsFilterModMode() const
{
    return m_render_backend == SUBTITLE_RENDER_BACKEND_VSFILTER
        && m_vsfilter_compatibility_mode == VSFILTER_COMPATIBILITY_MOD;
}

CSubtitle* CRenderedTextSubtitle::GetSubtitle(int entry)
{
    CSubtitle* sub = m_subtitleCache.GetAt(entry);
    if (sub)
    {
        return sub;
    }
    sub = DEBUG_NEW CSubtitle();
    if(!sub) return(NULL);
    CStringW str = GetStrW(entry, true);
    STSStyle stss, orgstss;
    SharedPtrModStyleState mod_style;
    GetStyle(entry, &stss);
    if (!IsVsFilterModMode() && stss.fontSpacing < 0) {
        stss.fontSpacing = 0;
    }
    if (stss.fontScaleX == stss.fontScaleY && m_dPARCompensation != 1.0)
    {
        switch(m_ePARCompensationType)
        {
        case EPCTUpscale:
            if (m_dPARCompensation < 1.0)
                stss.fontScaleY /= m_dPARCompensation;
            else
                stss.fontScaleX *= m_dPARCompensation;
            break;
        case EPCTDownscale:
            if (m_dPARCompensation < 1.0)
                stss.fontScaleX *= m_dPARCompensation;
            else
                stss.fontScaleY /= m_dPARCompensation;
            break;
        case EPCTAccurateSize:
            stss.fontScaleX *= m_dPARCompensation;
            break;
        }
    }
    orgstss = stss;
    sub->m_clip.SetRect(0, 0, m_size.cx>>3, m_size.cy>>3);
    sub->m_scrAlignment = -stss.scrAlignment;
    sub->m_wrapStyle    = m_defaultWrapStyle;
    sub->m_fAnimated    = false;
    sub->m_relativeTo   = stss.relativeTo;
    sub->m_scalex       = m_dstScreenSize.cx > 0 ? 1.0 * m_size.cx / (m_dstScreenSize.cx*MAX_SUB_PIXEL) : 1.0;
    sub->m_scaley       = m_dstScreenSize.cy > 0 ? 1.0 * m_size.cy / (m_dstScreenSize.cy*MAX_SUB_PIXEL) : 1.0;

    sub->m_target_scale_x = m_target_scale_x;
    sub->m_target_scale_y = m_target_scale_y;

    m_animStart             =
    m_animEnd               = 0;
    m_animAccel             = 1;
    m_ktype                 = 
    m_kstart                = 
    m_kend                  = 0;
    m_nPolygon              = 0;
    m_polygonBaselineOffset = 0;
    ParseEffect(sub, m_entries.GetAt(entry).effect);
    while(!str.IsEmpty())
    {
        bool fParsed = false;
        int i;
        if(str[0] == L'{' && (i = str.Find(L'}')) > 0)
        {
            fParsed = ParseSSATag(sub, str.Mid(1, i-1), stss, orgstss, mod_style);
            if(fParsed)
                str = str.Mid(i+1);
        }
        else if(str[0] == L'<' && (i = str.Find(L'>')) > 0)
        {
            fParsed = ParseHtmlTag(sub, str.Mid(1, i-1), stss, orgstss);
            if(fParsed)
                str = str.Mid(i+1);
        }
        if(fParsed)
        {
            i = str.FindOneOf(L"{<");
            if(i < 0) i = str.GetLength();
            if(i == 0) continue;
        }
        else
        {
            i = str.Mid(1).FindOneOf(L"{<");
            if(i < 0) i = str.GetLength()-1;
            i++;
        }
        STSStyle tmp       = stss;
        tmp.fontSpacing   *=                 sub->m_scalex * 64;
        tmp.fontSize      *=                 sub->m_scaley * 64;
        tmp.outlineWidthX *= (m_fScaledBAS ? sub->m_scalex : 1) * MAX_SUB_PIXEL;
        tmp.outlineWidthY *= (m_fScaledBAS ? sub->m_scaley : 1) * MAX_SUB_PIXEL;
        tmp.shadowDepthX  *= (m_fScaledBAS ? sub->m_scalex : 1) * MAX_SUB_PIXEL;
        tmp.shadowDepthY  *= (m_fScaledBAS ? sub->m_scaley : 1) * MAX_SUB_PIXEL;
        FwSTSStyle fw_tmp(tmp);
        const SharedPtrConstModStyleState frozen_mod_style = FreezeModStyleState(mod_style);
        if(m_nPolygon)
        {
            ParsePolygon(sub, str.Left(i), fw_tmp, frozen_mod_style);
        }
        else
        {
            ParseString(sub, str.Left(i), fw_tmp, frozen_mod_style);
        }
        str = str.Mid(i);
    }
    if( sub->m_effects[EF_BANNER] || sub->m_effects[EF_SCROLL] )
        sub->m_fAnimated2 = true;
    // just a "work-around" solution... in most cases nobody will want to use \org together with moving but without rotating the subs
    if(sub->m_effects[EF_ORG] && (sub->m_effects[EF_MOVE] || sub->m_effects[EF_BANNER] || sub->m_effects[EF_SCROLL]))
        sub->m_fAnimated = true;
    sub->m_scrAlignment = abs(sub->m_scrAlignment);
    STSEntry stse = m_entries.GetAt(entry);
    CRect marginRect = stse.marginRect;
    if(marginRect.left   == 0) marginRect.left   = orgstss.marginRect.get().left;
    if(marginRect.top    == 0) marginRect.top    = orgstss.marginRect.get().top;
    if(marginRect.right  == 0) marginRect.right  = orgstss.marginRect.get().right;
    if(marginRect.bottom == 0) marginRect.bottom = orgstss.marginRect.get().bottom;
    marginRect.left   = (int)(sub->m_scalex*marginRect.left  *MAX_SUB_PIXEL);
    marginRect.top    = (int)(sub->m_scaley*marginRect.top   *MAX_SUB_PIXEL);
    marginRect.right  = (int)(sub->m_scalex*marginRect.right *MAX_SUB_PIXEL);
    marginRect.bottom = (int)(sub->m_scaley*marginRect.bottom*MAX_SUB_PIXEL);

    sub->CreateClippers(m_size, m_video_rect.Size());
    sub->MakeLines(m_size, marginRect);
    if (!sub->m_fAnimated)
    {
        if (!m_subtitleCache.SetAt(entry, sub))
        {
            XY_LOG_FATAL("Out of Memory!");
            delete sub;
            return NULL;
        }
        m_subtitleCacheEntry.AddTail(entry);
    }
    return(sub);
}

void CRenderedTextSubtitle::ClearUnCachedSubtitle( CSubtitle2List& sub2List )
{
    POSITION pos = sub2List.GetHeadPosition();
    while(pos)
    {
        CSubtitle2 & sub2 = sub2List.GetNext(pos);
        if (sub2.s->m_fAnimated)
        {
            delete sub2.s;
        }
    }
}

void CRenderedTextSubtitle::ShrinkCache()
{
    OverlayNoBlurMruCache *cache1 = CacheManager::GetOverlayNoBlurMruCache();
    OverlayMruCache       *cache2 = CacheManager::GetOverlayMruCache();
    BitmapMruCache        *cache3 = CacheManager::GetBitmapMruCache();
    while (g_xy_malloc_used_size > s_max_cache_size && 
        (cache1->GetCurItemNum()>0 || cache2->GetCurItemNum()>0 || cache3->GetCurItemNum()>0))
    {
        cache1->RemoveTail();
        cache2->RemoveTail();
        cache3->RemoveTail();
    }
}

//

STDMETHODIMP CRenderedTextSubtitle::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    CheckPointer(ppv, E_POINTER);
    *ppv = NULL;
    return
        QI(IPersist)
        QI(ISubStream)
        QI(ISubPicProviderEx2)
        QI(ISubPicProvider)
        QI(ISubPicProviderEx)
        __super::NonDelegatingQueryInterface(riid, ppv);
}

// ISubPicProvider

STDMETHODIMP_(POSITION) CRenderedTextSubtitle::GetStartPosition(REFERENCE_TIME rt, double fps)
{
    m_fps = fps;
    if ((m_render_backend == SUBTITLE_RENDER_BACKEND_LIBASS && m_ass_context.m_assloaded) || 
        (m_render_backend == SUBTITLE_RENDER_BACKEND_CSRI && m_csri_context.m_csri_loaded)) {
        // POSITION is pointer-sized. If it cannot hold a full REFERENCE_TIME,
        // store milliseconds instead. Add one so zero remains the NULL sentinel.
#if UINTPTR_MAX < INT64_MAX
        return (POSITION)(DWORD_PTR)(rt / 10000i64 + 1);
#else
        return (POSITION)(DWORD_PTR)(rt + 1);
#endif
    }

    int iSegment;
    rt /= 10000i64;
    const STSSegment *stss = SearchSubs((int)rt, fps, &iSegment, NULL);
    if(stss==NULL) {
        TRACE_PARSER("No subtitle at "<<XY_LOG_VAR_2_STR(rt));
        return NULL;
    }
    return (POSITION)(iSegment + 1);
}

STDMETHODIMP_(POSITION) CRenderedTextSubtitle::GetNext(POSITION pos)
{
    if ((m_render_backend == SUBTITLE_RENDER_BACKEND_LIBASS && m_ass_context.m_assloaded) || 
        (m_render_backend == SUBTITLE_RENDER_BACKEND_CSRI && m_csri_context.m_csri_loaded)) {
        return NULL;
    }
    int iSegment = (int)pos;
    const STSSegment *stss = GetSegment(iSegment);
    while(stss && stss->subs.GetCount() == 0) {
        iSegment++;
        stss = GetSegment(iSegment);
    }
    return(stss ? (POSITION)(iSegment+1) : NULL);
}

STDMETHODIMP_(REFERENCE_TIME) CRenderedTextSubtitle::GetStart(POSITION pos, double fps)
{
    if ((m_render_backend == SUBTITLE_RENDER_BACKEND_LIBASS && m_ass_context.m_assloaded) || 
        (m_render_backend == SUBTITLE_RENDER_BACKEND_CSRI && m_csri_context.m_csri_loaded)) {
#if UINTPTR_MAX < INT64_MAX
        return ((REFERENCE_TIME)(DWORD_PTR)pos - 1) * 10000i64;
#else
        return (REFERENCE_TIME)(DWORD_PTR)pos - 1;
#endif
    }
    return(10000i64 * TranslateSegmentStart((int)pos-1, fps));
}

STDMETHODIMP_(REFERENCE_TIME) CRenderedTextSubtitle::GetStop(POSITION pos, double fps)
{
    if ((m_render_backend == SUBTITLE_RENDER_BACKEND_LIBASS && m_ass_context.m_assloaded) || 
        (m_render_backend == SUBTITLE_RENDER_BACKEND_CSRI && m_csri_context.m_csri_loaded)) {
#if UINTPTR_MAX < INT64_MAX
        return ((REFERENCE_TIME)(DWORD_PTR)pos - 1) * 10000i64 + 10000i64;
#else
        return (REFERENCE_TIME)(DWORD_PTR)pos;
#endif
    }
    return(10000i64 * TranslateSegmentEnd((int)pos-1, fps));
}

//@start, @stop: -1 if segment not found; @stop may < @start if subIndex exceed uppper bound
STDMETHODIMP_(VOID) CRenderedTextSubtitle::GetStartStop(POSITION pos, double fps, /*out*/REFERENCE_TIME &start, /*out*/REFERENCE_TIME &stop)
{
    if ((m_render_backend == SUBTITLE_RENDER_BACKEND_LIBASS && m_ass_context.m_assloaded) || 
        (m_render_backend == SUBTITLE_RENDER_BACKEND_CSRI && m_csri_context.m_csri_loaded)) {
#if UINTPTR_MAX < INT64_MAX
        start = ((REFERENCE_TIME)(DWORD_PTR)pos - 1) * 10000i64;
        stop = start + 10000i64;
#else
        start = (REFERENCE_TIME)(DWORD_PTR)pos - 1;
        stop = start + 1;
#endif
        return;
    }
    int iSegment = (int)pos-1;
    int tempStart, tempEnd;
    TranslateSegmentStartEnd(iSegment, fps, tempStart, tempEnd);
    start = tempStart;
    stop = tempEnd;
}

STDMETHODIMP_(bool) CRenderedTextSubtitle::IsAnimated(POSITION pos)
{
    if ((m_render_backend == SUBTITLE_RENDER_BACKEND_LIBASS && m_ass_context.m_assloaded) || 
        (m_render_backend == SUBTITLE_RENDER_BACKEND_CSRI && m_csri_context.m_csri_loaded)) {
        return true;
    }
    unsigned int iSegment = (int)pos-1;
    if(iSegment<m_segments.GetCount())
        return m_segments[iSegment].animated;
    else
        return false;
    //return(true);
}

struct LSub {int idx, layer, readorder;};

static int lscomp(const void* ls1, const void* ls2)
{
    int ret = ((LSub*)ls1)->layer - ((LSub*)ls2)->layer;
    if(!ret) ret = ((LSub*)ls1)->readorder - ((LSub*)ls2)->readorder;
    return(ret);
}

HRESULT CRenderedTextSubtitle::ParseScript(REFERENCE_TIME rt, double fps, CSubtitle2List *outputSub2List )
{
    TRACE_RENDERER_REQUEST("Begin search subtitle segment");
    //fix me: check input and log error
    int t = (int)(rt / 10000);
    int segment;
    //const
    STSSegment* stss = SearchSubs2(t, fps, &segment);
    if(!stss) return S_FALSE;
    // clear any cached subs that has been passed
    {
        TRACE_RENDERER_REQUEST("Begin clear parsed subtitle cache. m_subtitleCache.size:"<<m_subtitleCacheEntry.GetCount());
        POSITION pos = m_subtitleCacheEntry.GetHeadPosition();
        while(pos)
        {
            POSITION pos_old = pos;
            int key = m_subtitleCacheEntry.GetNext(pos);
            STSEntry& stse = m_entries.GetAt(key);
            if(stse.end <= t)
            {
                delete m_subtitleCache.GetAt(key);
                m_subtitleCache.SetAt(key, NULL);
                m_subtitleCacheEntry.RemoveAt(pos_old);
            }
        }
    }
    m_sla.AdvanceToSegment(segment, stss->subs);
    TRACE_RENDERER_REQUEST("Begin copy LSub. subs.size:"<<stss->subs.GetCount());
    CAtlArray<LSub> subs;
    for(int i = 0, j = stss->subs.GetCount(); i < j; i++)
    {
        LSub ls;
        ls.idx       = stss->subs[i];
        ls.layer     = m_entries.GetAt(stss->subs[i]).layer;
        ls.readorder = m_entries.GetAt(stss->subs[i]).readorder;
        subs.Add(ls);
    }
    TRACE_RENDERER_REQUEST("Begin sort LSub.");
    qsort(subs.GetData(), subs.GetCount(), sizeof(LSub), lscomp);
    TRACE_RENDERER_REQUEST("Begin parse subs.");
    for(int i = 0, j = subs.GetCount(); i < j; i++)
    {
        int entry = subs[i].idx;
        STSEntry stse = m_entries.GetAt(entry);
        {
            int start = TranslateStart(entry, fps);
            m_time    = t - start;
            m_delay   = TranslateEnd(entry, fps) - start;
        }
        CSubtitle* s = GetSubtitle(entry);
        if(!s) continue;
        stss->animated |= s->m_fAnimated2;
        CRect clipRect  = s->m_clip & CRect(0,0, (m_size.cx>>3), (m_size.cy>>3));
        CRect r         = s->m_rect;
        CSize spaceNeeded = r.Size();
        // apply the effects
        bool fPosOverride = false, fOrgOverride = false;
        int alpha = 0x00;
        CPoint org2;
        CPoint mod_clip_offset(0, 0);
        bool has_mod_clip_offset = false;
        for(int k = 0; k < EF_NUMBEROFEFFECTS; k++)
        {
            if(!s->m_effects[k]) continue;
            switch(k)
            {
            case EF_MOVE: // {\move(x1=param[0], y1=param[1], x2=param[2], y2=param[3], t1=t[0], t2=t[1])}
                {
                    CPoint p;
                    CPoint p1(s->m_effects[k]->param[0], s->m_effects[k]->param[1]);
                    CPoint p2(s->m_effects[k]->param[2], s->m_effects[k]->param[3]);
                    int t1 = s->m_effects[k]->t[0];
                    int t2 = s->m_effects[k]->t[1];
                    if(t2 < t1)            {int t = t1; t1 = t2; t2 = t;      }
                    if(t1 <= 0 && t2 <= 0) {            t1 = 0;  t2 = m_delay;}
                    if(m_time <= t1)  p = p1;
                    else if(p1 == p2) p = p1;
                    else if(t1 < m_time && m_time < t2)
                    {
                        double t = 1.0*(m_time-t1)/(t2-t1);
                        p.x = (int)((1-t)*p1.x + t*p2.x);
                        p.y = (int)((1-t)*p1.y + t*p2.y);
                    }
                    else p = p2;
                    int x = (s->m_scrAlignment%3) == 1 ? p.x : 
                            (s->m_scrAlignment%3) == 0 ? p.x - spaceNeeded.cx :
                                                         p.x - (spaceNeeded.cx+1)/2;
                    int y =  s->m_scrAlignment <= 3    ? p.y - spaceNeeded.cy : 
                             s->m_scrAlignment <= 6    ? p.y - (spaceNeeded.cy+1)/2 :
                                                         p.y;
                    r = CRect(CPoint(x,y), spaceNeeded);
                    fPosOverride = true;
                }
                break;
            case EF_ORG: // {\org(x=param[0], y=param[1])}
                {
                    org2 = CPoint(s->m_effects[k]->param[0], s->m_effects[k]->param[1]);
                    fOrgOverride = true;
                }
                break;
            case EF_FADE: // {\fade(a1=param[0], a2=param[1], a3=param[2], t1=t[0], t2=t[1], t3=t[2], t4=t[3]) or {\fad(t1=t[1], t2=t[2])
                {
                    int t1 = s->m_effects[k]->t[0];
                    int t2 = s->m_effects[k]->t[1];
                    int t3 = s->m_effects[k]->t[2];
                    int t4 = s->m_effects[k]->t[3];
                    if(t1 == -1 && t4 == -1) {t1 = 0; t3 = m_delay-t3; t4 = m_delay;}
                    if(m_time < t1) alpha = s->m_effects[k]->param[0];
                    else if(m_time >= t1 && m_time < t2)
                    {
                        double t = 1.0 * (m_time - t1) / (t2 - t1);
                        alpha = (int)(s->m_effects[k]->param[0]*(1-t) + s->m_effects[k]->param[1]*t);
                    }
                    else if(m_time >= t2 && m_time < t3) alpha = s->m_effects[k]->param[1];
                    else if(m_time >= t3 && m_time < t4)
                    {
                        double t = 1.0 * (m_time - t3) / (t4 - t3);
                        alpha = (int)(s->m_effects[k]->param[1]*(1-t) + s->m_effects[k]->param[2]*t);
                    }
                    else if(m_time >= t4) alpha = s->m_effects[k]->param[2];
                }
                break;
            case EF_BANNER: // Banner;delay=param[0][;leftoright=param[1];fadeawaywidth=param[2]]
                {
                    int left = 0,
                        right = m_size.cx;
                    r.left = !!s->m_effects[k]->param[1] 
                        ? (left  /*marginRect.left*/ - spaceNeeded.cx) + (int)(m_time*MAX_SUB_PIXEL_F/s->m_effects[k]->param[0])
                        : (right /*marginRect.right*/)                 - (int)(m_time*MAX_SUB_PIXEL_F/s->m_effects[k]->param[0]);
                    r.right = r.left + spaceNeeded.cx;
                    clipRect &= CRect(left>>3, clipRect.top, right>>3, clipRect.bottom);
                    fPosOverride = true;
                }
                break;
            case EF_SCROLL: // Scroll up/down(toptobottom=param[3]);top=param[0];bottom=param[1];delay=param[2][;fadeawayheight=param[4]]
                {
                    r.top = !!s->m_effects[k]->param[3]
                        ? s->m_effects[k]->param[0] + (int)(m_time*MAX_SUB_PIXEL_F/s->m_effects[k]->param[2]) - spaceNeeded.cy
                        : s->m_effects[k]->param[1] - (int)(m_time*MAX_SUB_PIXEL_F/s->m_effects[k]->param[2]);
                    r.bottom = r.top + spaceNeeded.cy;
                    CRect cr(0, (s->m_effects[k]->param[0] + 4) >> 3, (m_size.cx>>3), (s->m_effects[k]->param[1] + 4) >> 3);
                    clipRect &= cr;
                    fPosOverride = true;
                }
                break;
            default:
                break;
            }
        }
        if (s->m_mod_effects) {
            const ModEffectState& effect = *s->m_mod_effects;
            if (effect.feature_mask & MOD_EFFECT_MOVE) {
                int t1 = effect.move_t[0];
                int t2 = effect.move_t[1];
                if (t2 < t1) std::swap(t1, t2);
                if (t1 <= 0 && t2 <= 0) {
                    t1 = 0;
                    t2 = m_delay;
                }
                CPoint point;
                switch (effect.move_type) {
                case MOD_MOVE_RADIAL:
                    {
                        const CPoint first(
                            static_cast<int>(effect.move_points[0].x
                                + cos(effect.move_angle[0]) * effect.move_radius.x),
                            static_cast<int>(effect.move_points[0].y
                                - sin(effect.move_angle[0]) * effect.move_radius.x));
                        const CPoint second(
                            static_cast<int>(effect.move_points[1].x
                                + cos(effect.move_angle[1]) * effect.move_radius.y),
                            static_cast<int>(effect.move_points[1].y
                                - sin(effect.move_angle[1]) * effect.move_radius.y));
                        if (m_time <= t1) {
                            point = first;
                        } else if (t1 < m_time && m_time < t2) {
                            const double progress =
                                static_cast<double>(m_time - t1) / (t2 - t1);
                            const double angle = (1.0 - progress) * effect.move_angle[0]
                                + progress * effect.move_angle[1];
                            const double radius = (1.0 - progress) * effect.move_radius.x
                                + progress * effect.move_radius.y;
                            point.x = static_cast<int>(
                                (1.0 - progress) * effect.move_points[0].x
                                + progress * effect.move_points[1].x);
                            point.y = static_cast<int>(
                                (1.0 - progress) * effect.move_points[0].y
                                + progress * effect.move_points[1].y);
                            point.x += static_cast<int>(cos(angle) * radius);
                            point.y -= static_cast<int>(sin(angle) * radius);
                        } else {
                            point = second;
                        }
                    }
                    break;
                case MOD_MOVE_QUADRATIC:
                    {
                        if (m_time <= t1 || effect.move_points[0] == effect.move_points[1]) {
                            point = effect.move_points[0];
                        } else if (t1 < m_time && m_time < t2) {
                            const double progress =
                                static_cast<double>(m_time - t1) / (t2 - t1);
                            const double one_minus = 1.0 - progress;
                            point.x = static_cast<int>(
                                one_minus * one_minus * effect.move_points[0].x
                                + 2.0 * progress * one_minus * effect.move_points[1].x
                                + progress * progress * effect.move_points[2].x);
                            point.y = static_cast<int>(
                                one_minus * one_minus * effect.move_points[0].y
                                + 2.0 * progress * one_minus * effect.move_points[1].y
                                + progress * progress * effect.move_points[2].y);
                        } else {
                            point = effect.move_points[2];
                        }
                    }
                    break;
                case MOD_MOVE_CUBIC:
                    {
                        if (m_time <= t1 || effect.move_points[0] == effect.move_points[1]) {
                            point = effect.move_points[0];
                        } else if (t1 < m_time && m_time < t2) {
                            const double progress =
                                static_cast<double>(m_time - t1) / (t2 - t1);
                            const double one_minus = 1.0 - progress;
                            point.x = static_cast<int>(
                                one_minus * one_minus * one_minus * effect.move_points[0].x
                                + 3.0 * progress * one_minus * one_minus
                                    * effect.move_points[1].x
                                + 3.0 * progress * progress * one_minus
                                    * effect.move_points[2].x
                                + progress * progress * progress * effect.move_points[3].x);
                            point.y = static_cast<int>(
                                one_minus * one_minus * one_minus * effect.move_points[0].y
                                + 3.0 * progress * one_minus * one_minus
                                    * effect.move_points[1].y
                                + 3.0 * progress * progress * one_minus
                                    * effect.move_points[2].y
                                + progress * progress * progress * effect.move_points[3].y);
                        } else {
                            point = effect.move_points[3];
                        }
                    }
                    break;
                default:
                    point = effect.move_points[0];
                    break;
                }
                const int x = (s->m_scrAlignment%3) == 1 ? point.x
                    : (s->m_scrAlignment%3) == 0 ? point.x - spaceNeeded.cx
                    : point.x - (spaceNeeded.cx+1)/2;
                const int y = s->m_scrAlignment <= 3 ? point.y - spaceNeeded.cy
                    : s->m_scrAlignment <= 6 ? point.y - (spaceNeeded.cy+1)/2 : point.y;
                r = CRect(CPoint(x, y), spaceNeeded);
                fPosOverride = true;
            }
            if (effect.feature_mask & MOD_EFFECT_MOVING_ORG) {
                int t1 = effect.org_t[0];
                int t2 = effect.org_t[1];
                if (t2 < t1) std::swap(t1, t2);
                if (t1 <= 0 && t2 <= 0) {
                    t1 = 0;
                    t2 = m_delay;
                }
                if (m_time <= t1 || t1 == t2) {
                    org2 = effect.org_points[0];
                } else if (t1 < m_time && m_time < t2) {
                    const double progress =
                        static_cast<double>(m_time - t1) / (t2 - t1);
                    org2.x = static_cast<int>(
                        (1.0 - progress) * effect.org_points[0].x
                        + progress * effect.org_points[1].x);
                    org2.y = static_cast<int>(
                        (1.0 - progress) * effect.org_points[0].y
                        + progress * effect.org_points[1].y);
                } else {
                    org2 = effect.org_points[1];
                }
                fOrgOverride = true;
            }
            if (effect.feature_mask & MOD_EFFECT_MOVING_CLIP) {
                int t1 = effect.clip_t[0];
                int t2 = effect.clip_t[1];
                if (t2 < t1) std::swap(t1, t2);
                if (t1 <= 0 && t2 <= 0) {
                    t1 = 0;
                    t2 = m_delay;
                }
                if (m_time <= t1 || t1 == t2) {
                    mod_clip_offset = effect.clip_points[0];
                } else if (t1 < m_time && m_time < t2) {
                    const double progress =
                        static_cast<double>(m_time - t1) / (t2 - t1);
                    mod_clip_offset.x = static_cast<int>(
                        (1.0 - progress) * effect.clip_points[0].x
                        + progress * effect.clip_points[1].x);
                    mod_clip_offset.y = static_cast<int>(
                        (1.0 - progress) * effect.clip_points[0].y
                        + progress * effect.clip_points[1].y);
                } else {
                    mod_clip_offset = effect.clip_points[1];
                }
                has_mod_clip_offset = true;
            }
        }
        if(!fPosOverride && !fOrgOverride && !s->m_fAnimated)
            r = m_sla.AllocRect(s, segment, entry, stse.layer, m_collisions);
        CPoint org;
        org.x = (s->m_scrAlignment%3) == 1 ? r.left   : 
                (s->m_scrAlignment%3) == 2 ? r.CenterPoint().x 
                                           : r.right;
        org.y =  s->m_scrAlignment <= 3    ? r.bottom : 
                 s->m_scrAlignment <= 6    ? r.CenterPoint().y 
                                           : r.top;
        if(!fOrgOverride) org2 = org;
        CPoint p2(0, r.top);
        // Rectangles for inverse clip

        CRectCoor2 clipRect_coor2;
        clipRect_coor2.left   = clipRect.left   * m_target_scale_x;
        clipRect_coor2.right  = clipRect.right  * m_target_scale_x;
        clipRect_coor2.top    = clipRect.top    * m_target_scale_y;
        clipRect_coor2.bottom = clipRect.bottom * m_target_scale_y;
        CSubtitle2& sub2 = outputSub2List->GetAt(outputSub2List->AddTail(
            CSubtitle2(s, clipRect_coor2, org, org2, p2, alpha, m_time, rt,
                mod_clip_offset, has_mod_clip_offset) ));
    }

    return (subs.GetCount()) ? S_OK : S_FALSE;
}

STDMETHODIMP CRenderedTextSubtitle::RenderEx(SubPicDesc& spd, REFERENCE_TIME rt, double fps, CAtlList<CRectCoor2>& rectList)
{
    CSize output_size = CSize(spd.w,spd.h);
    CRectCoor2 video_rect = CRect(0,0,spd.w,spd.h);
    rectList.RemoveAll();
    if (spd.vidrect.left!=0 || spd.vidrect.top!=0 || spd.vidrect.right!=spd.w || spd.vidrect.bottom!=spd.h)
    {
        XY_LOG_WARN("Video rectangle is different from window. But support for relative to video rectangle has been removed.");
    }

    CComPtr<IXySubRenderFrame> sub_render_frame;
    const bool direct_mod_rgb32 = IsVsFilterModMode() && spd.type == MSP_RGB32;
    if (direct_mod_rgb32) {
        m_direct_render_target = &spd;
    }
    HRESULT hr = RenderEx(&sub_render_frame, spd.type, video_rect, video_rect, output_size, rt, fps);
    if (direct_mod_rgb32) {
        m_direct_render_target = NULL;
        rectList.AddTail(CRectCoor2(0, 0, spd.w, spd.h));
        return hr;
    }
    if (SUCCEEDED(hr) && sub_render_frame)
    {
        int count = 0;
        hr = sub_render_frame->GetBitmapCount(&count);
        if(FAILED(hr))
        {
            return hr;
        }
        int color_space;
        hr = sub_render_frame->GetXyColorSpace(&color_space);
        if(FAILED(hr))
        {
            return hr;
        }
        for (int i=0;i<count;i++)
        {
            POINT pos;
            SIZE size;
            LPCVOID pixels;
            int pitch;
            hr = sub_render_frame->GetBitmap(i, NULL, &pos, &size, &pixels, &pitch );
            if(FAILED(hr))
            {
                return hr;
            }
            rectList.AddTail(CRect(pos, size));
            if (color_space==XY_CS_AYUV_PLANAR)
            {
                XyPlannerFormatExtra plans;
                hr = sub_render_frame->GetBitmapExtra(i, &plans);
                if(FAILED(hr))
                {
                    return hr;
                }
                XyBitmap::AlphaBltPlannar(spd, pos, size, plans, pitch);
            }
            else
            {
                XyBitmap::AlphaBltPack(spd, pos, size, pixels, pitch);
            }
        }
    }
    return (!rectList.IsEmpty()) ? S_OK : S_FALSE;
}

#define div_255_fast_v2(x) (((x) + 1 + (((x) + 1) >> 8)) >> 8)
static __forceinline void ayuv_planar_mix_c(
    BYTE *dst_a,
    BYTE *dst_y,
    BYTE *dst_u,
    BYTE *dst_v,
    const BYTE *alpha,
    int w,
    DWORD ayuv)
{
    const uint8_t colorA = (ayuv >> 24) & 0xFF;
    const uint8_t colorY = (ayuv >> 16) & 0xFF;
    const uint8_t colorU = (ayuv >> 8) & 0xFF;
    const uint8_t colorV = ayuv & 0xFF;

    for (int x = 0; x < w; ++x)
    {
        uint8_t &destA = *(dst_a + x), &destY = *(dst_y + x), &destU = *(dst_u + x), &destV = *(dst_v + x);

        uint8_t srcA = div_255_fast_v2(alpha[x] * colorA);
        uint8_t compA = ~srcA;

        destA = (srcA + div_255_fast_v2((destA ^ 0xFF) * compA)) ^ 0xFF;
        destY = div_255_fast_v2(colorY * srcA + destY * compA);
        destU = div_255_fast_v2(colorU * srcA + destU * compA);
        destV = div_255_fast_v2(colorV * srcA + destV * compA);
    }
}
#undef div_255_fast_v2

#include <emmintrin.h>

static __forceinline __m128i ayuv_component_mix_sse2(
    const __m128i &dst_bytes,
    const __m128i &srcA_lo,
    const __m128i &srcA_hi,
    const __m128i &compA_lo,
    const __m128i &compA_hi,
    const __m128i &color_component16)
{
    const __m128i zero = _mm_setzero_si128();
    const __m128i rounding32 = _mm_set1_epi32(0x80);

    __m128i dst_lo = _mm_unpacklo_epi8(dst_bytes, zero);
    __m128i dst_hi = _mm_unpackhi_epi8(dst_bytes, zero);

    __m128i src_mul_lo = _mm_mullo_epi16(color_component16, srcA_lo);
    __m128i src_mul_hi = _mm_mullo_epi16(color_component16, srcA_hi);

    __m128i dst_mul_lo = _mm_mullo_epi16(dst_lo, compA_lo);
    __m128i dst_mul_hi = _mm_mullo_epi16(dst_hi, compA_hi);

    __m128i sum0_lo = _mm_add_epi32(_mm_unpacklo_epi16(src_mul_lo, zero), _mm_unpacklo_epi16(dst_mul_lo, zero));
    __m128i sum0_hi = _mm_add_epi32(_mm_unpackhi_epi16(src_mul_lo, zero), _mm_unpackhi_epi16(dst_mul_lo, zero));
    __m128i sum1_lo = _mm_add_epi32(_mm_unpacklo_epi16(src_mul_hi, zero), _mm_unpacklo_epi16(dst_mul_hi, zero));
    __m128i sum1_hi = _mm_add_epi32(_mm_unpackhi_epi16(src_mul_hi, zero), _mm_unpackhi_epi16(dst_mul_hi, zero));

    sum0_lo = _mm_add_epi32(sum0_lo, rounding32);
    sum0_hi = _mm_add_epi32(sum0_hi, rounding32);
    sum1_lo = _mm_add_epi32(sum1_lo, rounding32);
    sum1_hi = _mm_add_epi32(sum1_hi, rounding32);

    __m128i lo0 = _mm_srli_epi32(sum0_lo, 8);
    __m128i lo1 = _mm_srli_epi32(sum0_hi, 8);
    __m128i hi0 = _mm_srli_epi32(sum1_lo, 8);
    __m128i hi1 = _mm_srli_epi32(sum1_hi, 8);

    __m128i res_lo = _mm_packs_epi32(lo0, lo1);
    __m128i res_hi = _mm_packs_epi32(hi0, hi1);
    return _mm_packus_epi16(res_lo, res_hi);
}

static __forceinline void ayuv_planar_mix_sse2(
    BYTE *dst_a,
    BYTE *dst_y,
    BYTE *dst_u,
    BYTE *dst_v,
    const BYTE *alpha,
    int w,
    DWORD ayuv)
{
    ASSERT((w & 15) == 0);

    const uint8_t colorA = (ayuv >> 24) & 0xFF;
    const uint8_t colorY = (ayuv >> 16) & 0xFF;
    const uint8_t colorU = (ayuv >> 8) & 0xFF;
    const uint8_t colorV = ayuv & 0xFF;

    const __m128i zero = _mm_setzero_si128();
    const __m128i ff16 = _mm_set1_epi16(0xFF);
    const __m128i full = _mm_set1_epi16(0x100);
    const __m128i ones16 = _mm_set1_epi16(0x1);
    const __m128i round16 = _mm_set1_epi16(0x80);
    const __m128i colorA16 = _mm_set1_epi16(colorA);
    const __m128i colorY16 = _mm_set1_epi16(colorY);
    const __m128i colorU16 = _mm_set1_epi16(colorU);
    const __m128i colorV16 = _mm_set1_epi16(colorV);

    for (int x = 0; x < w; x += 16)
    {
        __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i *>(alpha + x));
        __m128i a_lo = _mm_unpacklo_epi8(a, zero);
        __m128i a_hi = _mm_unpackhi_epi8(a, zero);

        __m128i srcA_lo = _mm_mullo_epi16(_mm_add_epi16(a_lo, ones16), colorA16);
        __m128i srcA_hi = _mm_mullo_epi16(_mm_add_epi16(a_hi, ones16), colorA16);
        srcA_lo = _mm_srli_epi16(srcA_lo, 8);
        srcA_hi = _mm_srli_epi16(srcA_hi, 8);

        __m128i compA_lo = _mm_sub_epi16(full, srcA_lo);
        __m128i compA_hi = _mm_sub_epi16(full, srcA_hi);

        __m128i dstAlpha = _mm_loadu_si128(reinterpret_cast<const __m128i *>(dst_a + x));
        __m128i dstAlpha_lo = _mm_unpacklo_epi8(dstAlpha, zero);
        __m128i dstAlpha_hi = _mm_unpackhi_epi8(dstAlpha, zero);

        __m128i dstAlpha_inv_lo = _mm_sub_epi16(ff16, dstAlpha_lo);
        __m128i dstAlpha_inv_hi = _mm_sub_epi16(ff16, dstAlpha_hi);

        __m128i dstBlend_lo = _mm_mullo_epi16(dstAlpha_inv_lo, compA_lo);
        __m128i dstBlend_hi = _mm_mullo_epi16(dstAlpha_inv_hi, compA_hi);
        dstBlend_lo = _mm_srli_epi16(_mm_add_epi16(dstBlend_lo, round16), 8);
        dstBlend_hi = _mm_srli_epi16(_mm_add_epi16(dstBlend_hi, round16), 8);

        __m128i newAlpha_lo = _mm_min_epi16(_mm_add_epi16(srcA_lo, dstBlend_lo), ff16);
        __m128i newAlpha_hi = _mm_min_epi16(_mm_add_epi16(srcA_hi, dstBlend_hi), ff16);

        __m128i storedAlpha = _mm_packus_epi16(
            _mm_sub_epi16(ff16, newAlpha_lo),
            _mm_sub_epi16(ff16, newAlpha_hi));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst_a + x), storedAlpha);

        __m128i dstY_vec = _mm_loadu_si128(reinterpret_cast<const __m128i *>(dst_y + x));
        __m128i dstU_vec = _mm_loadu_si128(reinterpret_cast<const __m128i *>(dst_u + x));
        __m128i dstV_vec = _mm_loadu_si128(reinterpret_cast<const __m128i *>(dst_v + x));

        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst_y + x),
            ayuv_component_mix_sse2(dstY_vec, srcA_lo, srcA_hi, compA_lo, compA_hi, colorY16));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst_u + x),
            ayuv_component_mix_sse2(dstU_vec, srcA_lo, srcA_hi, compA_lo, compA_hi, colorU16));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst_v + x),
            ayuv_component_mix_sse2(dstV_vec, srcA_lo, srcA_hi, compA_lo, compA_hi, colorV16));
    }
}

static __forceinline void pixmix_sse2(DWORD *dst, DWORD color, DWORD alpha)
{
    alpha = ((alpha + 1) * (color >> 24)) >> 8;
    color &= 0xffffff;
    __m128i zero = _mm_setzero_si128();
    __m128i a = _mm_set1_epi32(((alpha + 1) << 16) | (0x100 - alpha));
    __m128i d = _mm_unpacklo_epi8(_mm_cvtsi32_si128(*dst), zero);
    __m128i s = _mm_unpacklo_epi8(_mm_cvtsi32_si128(color), zero);
    __m128i r = _mm_unpacklo_epi16(d, s);
    r = _mm_madd_epi16(r, a);
    r = _mm_srli_epi32(r, 8);
    r = _mm_packs_epi32(r, r);
    r = _mm_packus_epi16(r, r);
    *dst = (DWORD)_mm_cvtsi128_si32(r) + (alpha << 24);
}

static __forceinline __m128i packed_pix_mix_sse2(const __m128i &dst,
    const __m128i &c_r, const __m128i &c_g, const __m128i &c_b, const __m128i &a)
{
    __m128i d_a, d_r, d_g, d_b;

    d_a = _mm_srli_epi32(dst, 24);

    d_r = _mm_slli_epi32(dst, 8);
    d_r = _mm_srli_epi32(d_r, 24);

    d_g = _mm_slli_epi32(dst, 16);
    d_g = _mm_srli_epi32(d_g, 24);

    d_b = _mm_slli_epi32(dst, 24);
    d_b = _mm_srli_epi32(d_b, 24);

    //d_a = _mm_or_si128(d_a, c_a);
    d_r = _mm_or_si128(d_r, c_r);
    d_g = _mm_or_si128(d_g, c_g);
    d_b = _mm_or_si128(d_b, c_b);

    d_a = _mm_mullo_epi16(d_a, a);
    d_r = _mm_madd_epi16(d_r, a);
    d_g = _mm_madd_epi16(d_g, a);
    d_b = _mm_madd_epi16(d_b, a);

    d_a = _mm_srli_epi32(d_a, 8);
    d_r = _mm_srli_epi32(d_r, 8);
    d_g = _mm_srli_epi32(d_g, 8);
    d_b = _mm_srli_epi32(d_b, 8);

    __m128i ones = _mm_set1_epi32(0x1);
    __m128i a_sub_one = _mm_srli_epi32(a, 16);
    a_sub_one = _mm_sub_epi32(a_sub_one, ones);
    d_a = _mm_add_epi32(d_a, a_sub_one);

    d_a = _mm_slli_epi32(d_a, 24);
    d_r = _mm_slli_epi32(d_r, 16);
    d_g = _mm_slli_epi32(d_g, 8);

    d_b = _mm_or_si128(d_b, d_g);
    d_b = _mm_or_si128(d_b, d_r);
    return _mm_or_si128(d_b, d_a);
}

static __forceinline void packed_pix_mix_sse2(BYTE *dst, const BYTE *alpha, int w, DWORD color)
{
    __m128i c_r = _mm_set1_epi32((color & 0xFF0000));
    __m128i c_g = _mm_set1_epi32((color & 0xFF00) << 8);
    __m128i c_b = _mm_set1_epi32((color & 0xFF) << 16);
    __m128i c_a = _mm_set1_epi16((color & 0xFF000000) >> 24);

    __m128i zero = _mm_setzero_si128();

    __m128i ones = _mm_set1_epi16(0x1);

    const BYTE *alpha_end0 = alpha + (w & ~15);
    const BYTE *alpha_end = alpha + w;
    for (; alpha < alpha_end0; alpha += 16, dst += 16 * 4)
    {
        __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i *>(alpha));
        __m128i d1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(dst));
        __m128i d2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(dst + 16));
        __m128i d3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(dst + 32));
        __m128i d4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(dst + 48));

        __m128i ra = _mm_setzero_si128();
        __m128i a1 = _mm_unpacklo_epi8(a, zero);
        a1 = _mm_add_epi16(a1, ones);
        a1 = _mm_mullo_epi16(a1, c_a);
        a1 = _mm_srli_epi16(a1, 8);

        __m128i a2 = _mm_unpackhi_epi8(a, zero);
        a2 = _mm_add_epi16(a2, ones);
        a2 = _mm_mullo_epi16(a2, c_a);
        a2 = _mm_srli_epi16(a2, 8);

        a = _mm_packus_epi16(a1, a2);

        ra = _mm_cmpeq_epi32(ra, ra);
        ra = _mm_xor_si128(ra, a);
        a1 = _mm_unpacklo_epi8(ra, a);
        a2 = _mm_unpackhi_epi8(a1, zero);
        a1 = _mm_unpacklo_epi8(a1, zero);
        a1 = _mm_add_epi16(a1, ones);
        a2 = _mm_add_epi16(a2, ones);

        __m128i a3 = _mm_unpackhi_epi8(ra, a);
        __m128i a4 = _mm_unpackhi_epi8(a3, zero);
        a3 = _mm_unpacklo_epi8(a3, zero);
        a3 = _mm_add_epi16(a3, ones);
        a4 = _mm_add_epi16(a4, ones);

        d1 = packed_pix_mix_sse2(d1, c_r, c_g, c_b, a1);
        d2 = packed_pix_mix_sse2(d2, c_r, c_g, c_b, a2);
        d3 = packed_pix_mix_sse2(d3, c_r, c_g, c_b, a3);
        d4 = packed_pix_mix_sse2(d4, c_r, c_g, c_b, a4);

        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst), d1);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + 16), d2);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + 32), d3);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + 48), d4);
    }
    DWORD *dst_w = reinterpret_cast<DWORD *>(dst);
    for (; alpha < alpha_end; alpha++, dst_w++)
    {
        pixmix_sse2(dst_w, color, *alpha);
    }
}

STDMETHODIMP CRenderedTextSubtitle::RenderEx( IXySubRenderFrame**subRenderFrame, int spd_type,
    const RECT& video_rect, const RECT& subtitle_target_rect,
    const SIZE& original_video_size,
    REFERENCE_TIME rt, double fps )
{
    TRACE_RENDERER_REQUEST("Begin RenderEx rt"<<rt);
    if (!subRenderFrame)
    {
        return S_FALSE;
    }
    *subRenderFrame = NULL;
    CRect cvideo_rect = video_rect;

    cvideo_rect &= subtitle_target_rect;
    if (cvideo_rect!=video_rect)
    {
        XY_LOG_WARN("NOT supported yet!");
        return E_NOTIMPL;
    }

    if (cvideo_rect.left!=0 || cvideo_rect.top!=0)
    {
        XY_LOG_WARN("FIXME: supported with hack");
        cvideo_rect.MoveToXY(0,0);
    }

    XyColorSpace color_space = XY_CS_ARGB;
    switch(spd_type)
    {
    case MSP_AYUV_PLANAR:
        color_space = XY_CS_AYUV_PLANAR;
        break;
    case MSP_XY_AUYV:
        color_space = XY_CS_AUYV;
        break;
    case MSP_AYUV:
        color_space = XY_CS_AYUV;
        break;
    case MSP_RGBA_F:
        color_space = XY_CS_ARGB_F;
        break;
    default:
        color_space = XY_CS_ARGB;
        break;
    }

    XySubRenderFrameCreater *render_frame_creater = XySubRenderFrameCreater::GetDefaultCreater();
    render_frame_creater->SetColorSpace(color_space);

    if( m_video_rect != CRect(cvideo_rect.left*MAX_SUB_PIXEL,
                              cvideo_rect.top*MAX_SUB_PIXEL,
                              cvideo_rect.right*MAX_SUB_PIXEL,
                              cvideo_rect.bottom*MAX_SUB_PIXEL)
        || m_subtitle_target_rect != CRect(subtitle_target_rect.left*MAX_SUB_PIXEL,
                                           subtitle_target_rect.top*MAX_SUB_PIXEL,
                                           subtitle_target_rect.right*MAX_SUB_PIXEL,
                                           subtitle_target_rect.bottom*MAX_SUB_PIXEL)
        || m_size != CSize(original_video_size.cx*MAX_SUB_PIXEL, original_video_size.cy*MAX_SUB_PIXEL) )
    {
        if (!Init(cvideo_rect, subtitle_target_rect, original_video_size))
        {
            XY_LOG_FATAL("Failed to Init.");
            return E_FAIL;
        }

        render_frame_creater->SetOutputRect(cvideo_rect);
        render_frame_creater->SetClipRect(subtitle_target_rect);
    }

	if (m_render_backend == SUBTITLE_RENDER_BACKEND_CSRI && m_csri_context.m_csri_loaded && (color_space == XY_CS_ARGB || color_space == XY_CS_ARGB_F)) {
		csri_fmt fmt = {
			CSRI_F_BGRA,
			subtitle_target_rect.right - subtitle_target_rect.left,
			subtitle_target_rect.bottom - subtitle_target_rect.top,
		};
		if (m_csri_context.m_loader->csri_request_fmt(m_csri_context.m_inst.get(), &fmt) != 0) {
			XY_LOG_ERROR("CSRI renderer rejected BGRA format " << fmt.width << "x" << fmt.height);
			return E_FAIL;
		}
		auto xy_sub_render_frame = XySubRenderFrameCreater::GetDefaultCreater()->NewXySubRenderFrame(1);
		XyBitmap *tmp = XySubRenderFrameCreater::GetDefaultCreater()->CreateBitmap(subtitle_target_rect);
		xy_sub_render_frame->m_bitmaps.GetAt(0).reset(tmp);
		xy_sub_render_frame->m_bitmap_ids.GetAt(0) = rt;
		// CSRI_F_BGRA uses straight alpha, while a new XyBitmap starts with
		// VSFilter's inverted alpha. Present a transparent BGRA frame to CSRI.
		memset(tmp->bits, 0, tmp->pitch * tmp->h);
		csri_frame frame = {
			fmt.pixfmt,
			{
				tmp->plans[0],
				nullptr,
				nullptr,
				nullptr,
			},
			{
				tmp->pitch,
				0,
				0,
				0,
			},
		};
		m_csri_context.m_loader->csri_render(m_csri_context.m_inst.get(), &frame, rt / 1e7);

		// CSRI returns straight BGRA, but IXySubRenderFrame requires
		// premultiplied RGB. XY_CS_ARGB stores inverted alpha; ARGB_F does not.
		for (int y = 0; y < tmp->h; ++y) {
			BYTE *pixel = tmp->plans[0] + y * tmp->pitch;
			for (int x = 0; x < tmp->w; ++x, pixel += 4) {
				const unsigned alpha = pixel[3];
				pixel[0] = (BYTE)((pixel[0] * alpha + 127) / 255);
				pixel[1] = (BYTE)((pixel[1] * alpha + 127) / 255);
				pixel[2] = (BYTE)((pixel[2] * alpha + 127) / 255);
				if (color_space == XY_CS_ARGB) {
					pixel[3] = (BYTE)(255 - alpha);
				}
			}
		}
		(*subRenderFrame = xy_sub_render_frame)->AddRef();
		return S_OK;
	}

    if (m_render_backend == SUBTITLE_RENDER_BACKEND_LIBASS && m_ass_context.m_assloaded) {
        if (!m_ass_context.m_assfontloaded) {
            m_ass_context.LoadASSFont(m_pPin, m_pGraph);
            m_ass_context.m_assfontloaded = true;
        }

        ass_set_storage_size(m_ass_context.m_renderer.get(), original_video_size.cx, original_video_size.cy);
        ass_set_frame_size(m_ass_context.m_renderer.get(), subtitle_target_rect.right-subtitle_target_rect.left, subtitle_target_rect.bottom-subtitle_target_rect.top);

        int changed = 0;
        ASS_Image *img = ass_render_frame(m_ass_context.m_renderer.get(), m_ass_context.m_track.get(), rt / 10000, &changed);

        if (!changed && m_last_frame) {
            (*subRenderFrame = m_last_frame)->AddRef();
            return S_OK;
        }

        if (!img) {
            XySubRenderFrame* sub_render_frame = render_frame_creater->NewXySubRenderFrame(0);
            m_last_frame = sub_render_frame;
            (*subRenderFrame = sub_render_frame)->AddRef();

            return S_OK;
        }

        RECT clip_rect = {};
        for (auto i = img; i != nullptr; i = i->next)
        {
            RECT rect1 = clip_rect;
            RECT rect2 = { i->dst_x, i->dst_y, i->dst_x + i->w, i->dst_y + i->h };
            UnionRect(&clip_rect, &rect1, &rect2);
        }

        auto rect_width = clip_rect.right - clip_rect.left;
        auto rect_height = clip_rect.bottom - clip_rect.top;
        switch (color_space)
        {
        case XY_CS_AYUV_PLANAR:
        case XY_CS_AYUV:
        case XY_CS_AUYV:
            if (clip_rect.left & 1) {
                --clip_rect.left;
                ++rect_width;
            }
            if (clip_rect.top & 1) {
                --clip_rect.top;
                ++rect_height;
            }
            break;
        case XY_CS_ARGB_F:
        case XY_CS_ARGB:
            break;
        }
        clip_rect = RECT{ clip_rect.left, clip_rect.top, clip_rect.left + (rect_width + (rect_width & 1)),  clip_rect.top + (rect_height + (rect_height & 1)) };

        XySubRenderFrameCreater *render_frame_creater = XySubRenderFrameCreater::GetDefaultCreater();
        XySubRenderFrame *sub_render_frame = render_frame_creater->NewXySubRenderFrame(1);
        XyBitmap *tmp = XySubRenderFrameCreater::GetDefaultCreater()->CreateBitmap(clip_rect);
        sub_render_frame->m_bitmaps.GetAt(0).reset(tmp);
        sub_render_frame->m_bitmap_ids.GetAt(0) = rt;

        switch (color_space)
        {
        case XY_CS_ARGB_F:
            XyBitmap::FlipAlphaValue(tmp->bits, tmp->w, tmp->h, tmp->pitch);
            for (auto i = img; i != nullptr; i = i->next) {
                uint32_t argb = (i->color << 24) ^ (i->color >> 8) ^ 0xFF000000;
                argb = render_frame_creater->TransColor(argb);
                for (int y = 0; y < i->h; ++y)
                {
                    auto dst = reinterpret_cast<uint8_t *>(tmp->plans[0] + (i->dst_y + y - clip_rect.top) * tmp->pitch + (i->dst_x - clip_rect.left)*4);
                    auto alpha = i->bitmap + y * i->stride;
                    packed_pix_mix_sse2(dst, alpha, i->w, argb);
                }
            }
            break;
        case XY_CS_AYUV_PLANAR:
            for (auto i = img; i != nullptr; i = i->next) {
                uint32_t argb = (i->color << 24) ^ (i->color >> 8) ^ 0xFF000000;
                uint32_t ayuv = render_frame_creater->TransColor(argb);
                for (int y = 0; y < i->h; ++y)
                {
                    int rowOffset = (i->dst_y + y - clip_rect.top) * tmp->pitch + (i->dst_x - clip_rect.left);
                    BYTE *dstA = tmp->plans[0] + rowOffset;
                    BYTE *dstY = tmp->plans[1] + rowOffset;
                    BYTE *dstU = tmp->plans[2] + rowOffset;
                    BYTE *dstV = tmp->plans[3] + rowOffset;
                    const BYTE *alpha = i->bitmap + y * i->stride;

                    int w0 = i->w & ~15;
                    ayuv_planar_mix_sse2(dstA, dstY, dstU, dstV, alpha, w0, ayuv);
                    ayuv_planar_mix_c(dstA+w0, dstY+w0, dstU+w0, dstV+w0, alpha+w0, i->w-w0, ayuv);
                }
            }
            break;
        case XY_CS_ARGB:
            XyBitmap::FlipAlphaValue(tmp->bits, tmp->w, tmp->h, tmp->pitch);
            for (auto i = img; i != nullptr; i = i->next) {
                uint32_t argb = (i->color << 24) ^ (i->color >> 8) ^ 0xFF000000;
                argb = render_frame_creater->TransColor(argb);
                for (int y = 0; y < i->h; ++y)
                {
                    auto dst = reinterpret_cast<uint8_t *>(tmp->plans[0] + (i->dst_y + y - clip_rect.top) * tmp->pitch + (i->dst_x - clip_rect.left) * 4);
                    auto alpha = i->bitmap + y * i->stride;
                    packed_pix_mix_sse2(dst, alpha, i->w, argb);
                }
            }
            XyBitmap::FlipAlphaValue(tmp->bits, tmp->w, tmp->h, tmp->pitch);
            break;
        case XY_CS_AYUV:
            XyBitmap::FlipAlphaValue(tmp->bits, tmp->w, tmp->h, tmp->pitch);
            for (auto i = img; i != nullptr; i = i->next) {
                uint32_t argb = (i->color << 24) ^ (i->color >> 8) ^ 0xFF000000;
                uint32_t ayuv = render_frame_creater->TransColor(argb);
                for (int y = 0; y < i->h; ++y)
                {
                    auto dst = reinterpret_cast<uint8_t*>(tmp->plans[0] + (i->dst_y + y - clip_rect.top) * tmp->pitch + (i->dst_x - clip_rect.left) * 4);
                    auto alpha = i->bitmap + y * i->stride;
                    packed_pix_mix_sse2(dst, alpha, i->w, ayuv);
                }
            }
            XyBitmap::FlipAlphaValue(tmp->bits, tmp->w, tmp->h, tmp->pitch);
            break;
        case XY_CS_AUYV:
            XyBitmap::FlipAlphaValue(tmp->bits, tmp->w, tmp->h, tmp->pitch);
            for (auto i = img; i != nullptr; i = i->next) {
                uint32_t argb = (i->color << 24) ^ (i->color >> 8) ^ 0xFF000000;
                uint32_t auyv = render_frame_creater->TransColor(argb);
                for (int y = 0; y < i->h; ++y)
                {
                    auto dst = reinterpret_cast<uint8_t*>(tmp->plans[0] + (i->dst_y + y - clip_rect.top) * tmp->pitch + (i->dst_x - clip_rect.left) * 4);
                    auto alpha = i->bitmap + y * i->stride;
                    packed_pix_mix_sse2(dst, alpha, i->w, auyv);
                }
            }
            XyBitmap::FlipAlphaValue(tmp->bits, tmp->w, tmp->h, tmp->pitch);
            break;
        }
        m_last_frame = sub_render_frame;
        (*subRenderFrame = sub_render_frame)->AddRef();

        return S_OK;
    }

    TRACE_RENDERER_REQUEST("Begin ParseScript");
    CSubtitle2List sub2List;
    HRESULT hr = ParseScript(rt, fps, &sub2List);
    if(hr!=S_OK)
    {
        return hr;
    }

    if (!m_simple)
    {
        bool newMovable = true;
        POSITION pos=sub2List.GetHeadPosition();
        while ( pos!=NULL )
        {
            const CSubtitle2& sub2 = sub2List.GetNext(pos);
            if (sub2.s->m_hard_position_level > POS_LVL_NONE)
            {
              newMovable = false;
              break;
            }
        }
        m_movable = newMovable;
    }

    TRACE_RENDERER_REQUEST("Begin build draw item tree");
    CRectCoor2 margin_rect(
        m_video_rect.left - m_subtitle_target_rect.left,
        m_video_rect.top  - m_subtitle_target_rect.top,
        m_subtitle_target_rect.right - m_video_rect.right,
        m_subtitle_target_rect.bottom - m_video_rect.bottom);
    CompositeDrawItemListList compDrawItemListList;
    DoRender(m_video_rect.Size(), m_video_rect.TopLeft(), margin_rect, sub2List, &compDrawItemListList);
    ClearUnCachedSubtitle(sub2List);

    TRACE_RENDERER_REQUEST("Begin Draw");
    if (m_direct_render_target) {
        CompositeDrawItem::DrawDirect(*m_direct_render_target, compDrawItemListList);
        ShrinkCache();
        TRACE_RENDERER_REQUEST("Finished direct MOD draw");
        return hr;
    }
    XySubRenderFrame *sub_render_frame;
    CompositeDrawItem::Draw(&sub_render_frame, compDrawItemListList);
    sub_render_frame->MoveTo(video_rect.left, video_rect.top);
    (*subRenderFrame = sub_render_frame)->AddRef();

    ShrinkCache();

    TRACE_RENDERER_REQUEST("Finished");
    return hr;
}

void CRenderedTextSubtitle::DoRender( const SIZECoor2& output_size, 
    const POINTCoor2& video_org,
    const RECTCoor2& margin_rect, 
    const CSubtitle2List& sub2List, 
    CompositeDrawItemListList *compDrawItemListList /*output*/)
{
    //check input and log error
    POSITION pos=sub2List.GetHeadPosition();
    while ( pos!=NULL )
    {
        const CSubtitle2& sub2 = sub2List.GetNext(pos);
        CompositeDrawItemList& compDrawItemList = compDrawItemListList->GetAt(compDrawItemListList->AddTail());
        RenderOneSubtitle(output_size, video_org, margin_rect, sub2, &compDrawItemList);
    }
}

void CRenderedTextSubtitle::RenderOneSubtitle( const SIZECoor2& output_size, 
    const POINTCoor2& video_org, const RECTCoor2& margin_rect, 
    const CSubtitle2& sub2, 
    CompositeDrawItemList* compDrawItemList /*output*/)
{
    CSubtitle   * s        = sub2.s;
    CRect         clipRect = sub2.clipRect;
    const CPoint& org      = sub2.org;
    const CPoint& org2     = sub2.org2;
    const CPoint& p2       = sub2.p;
    int           alpha    = sub2.alpha;
    int           time     = sub2.time;
    if(!s) return;

    CPointCoor2 margin;
    if (s->m_hard_position_level > POS_LVL_NONE)
    {
        margin = video_org;
    }
    else
    {
        //set margin to move subtitles to black bars
        switch(s->m_scrAlignment%3)
        {
        case 1: margin.x = video_org.x - margin_rect.left;  break; //move to left
        case 2: margin.x = video_org.x;                     break; //do not move so that it aligns with the middle of the video
        case 3: margin.x = video_org.x + margin_rect.right; break; //move to right
        }
        switch((s->m_scrAlignment-1)/3)
        {
        case 0: margin.y = video_org.y - margin_rect.top;    break; //move to top
        case 1: margin.y = video_org.y;                      break; //do not move so that it aligns with the middle of the video
        case 2: margin.y = video_org.y + margin_rect.bottom; break;//move to bottom
        }
        ASSERT(clipRect.Width()*MAX_SUB_PIXEL==output_size.cx && clipRect.Height()*MAX_SUB_PIXEL==output_size.cy);
        clipRect.SetRect(0,0, 
            (output_size.cx + video_org.x+margin_rect.left+margin_rect.right)>>3,
            (output_size.cy + video_org.y+margin_rect.top +margin_rect.bottom)>>3);
    }

    CPoint clipper_offset(0, 0);
    if (sub2.has_clip_offset) {
        clipper_offset.x = static_cast<int>(sub2.clip_offset.x * s->m_target_scale_x);
        clipper_offset.y = static_cast<int>(sub2.clip_offset.y * s->m_target_scale_y);
    }
    SharedPtrCClipperPaintMachine clipper(
        DEBUG_NEW CClipperPaintMachine(s->m_pClipper, clipper_offset) );

    CRect iclipRect[4];
    iclipRect[0] = CRect(0             , 0              , output_size.cx>>3, clipRect.top     );
    iclipRect[1] = CRect(0             , clipRect.top   , clipRect.left    , clipRect.bottom  );
    iclipRect[2] = CRect(clipRect.right, clipRect.top   , output_size.cx>>3, clipRect.bottom  );
    iclipRect[3] = CRect(0             , clipRect.bottom, output_size.cx>>3, output_size.cy>>3);
    CRect bbox2(0,0,0,0);
    POSITION pos = s->GetHeadLinePosition();
    CPoint p = p2;
    while(pos)
    {
        CLine* l = s->GetNextLine(pos);
        p.x = (s->m_scrAlignment%3) == 1 ? org.x :
              (s->m_scrAlignment%3) == 0 ? org.x -  l->m_width :
                                           org.x - (l->m_width/2);

        CompositeDrawItemList tmpCompDrawItemList;
        if (s->m_clipInverse)
        {
            CompositeDrawItemList tmp1,tmp2,tmp3,tmp4;
            for (int i=0;i<l->GetWordCount();i++)
            {
                tmp1.AddTail();
                tmp2.AddTail();
                tmp3.AddTail();
                tmp4.AddTail();
            }
            bbox2 |= l->PaintAll(&tmp1, iclipRect[0], margin, clipper, p, org2, time, alpha, sub2.rt);
            bbox2 |= l->PaintAll(&tmp2, iclipRect[1], margin, clipper, p, org2, time, alpha, sub2.rt);
            bbox2 |= l->PaintAll(&tmp3, iclipRect[2], margin, clipper, p, org2, time, alpha, sub2.rt);
            bbox2 |= l->PaintAll(&tmp4, iclipRect[3], margin, clipper, p, org2, time, alpha, sub2.rt);
            tmpCompDrawItemList.AddTailList(&tmp1);
            tmpCompDrawItemList.AddTailList(&tmp2);
            tmpCompDrawItemList.AddTailList(&tmp3);
            tmpCompDrawItemList.AddTailList(&tmp4);
        }
        else
        {
            for (int i=0;i<l->GetWordCount();i++)
            {
                tmpCompDrawItemList.AddTail();
            }
            bbox2 |= l->PaintAll(&tmpCompDrawItemList, clipRect, margin, clipper, p, org2, time, alpha, sub2.rt);
        }
        compDrawItemList->AddTailList(&tmpCompDrawItemList);
        p.y += l->m_ascent + l->m_descent;
    }
}

STDMETHODIMP CRenderedTextSubtitle::Render(SubPicDesc& spd, REFERENCE_TIME rt, double fps, RECTCoor2& bbox)
{
    CAtlList<CRect> rectList;
    HRESULT result = RenderEx(spd, rt, fps, rectList);
    POSITION pos = rectList.GetHeadPosition();
    CRect bbox2(0,0,0,0);
    while(pos!=NULL)
    {
        bbox2 |= rectList.GetNext(pos);
    }
    bbox = bbox2;
    return result;
}

// IPersist

STDMETHODIMP CRenderedTextSubtitle::GetClassID(CLSID* pClassID)
{
    return pClassID ? *pClassID = __uuidof(this), S_OK : E_POINTER;
}

// ISubStream

STDMETHODIMP_(int) CRenderedTextSubtitle::GetStreamCount()
{
    return(1);
}

STDMETHODIMP CRenderedTextSubtitle::GetStreamInfo(int iStream, WCHAR** ppName, LCID* pLCID)
{
    if(iStream != 0) return E_INVALIDARG;
    if(ppName)
    {
        *ppName = (WCHAR*)CoTaskMemAlloc((m_name.GetLength()+1)*sizeof(WCHAR));
        if(!(*ppName))
            return E_OUTOFMEMORY;
        wcscpy(*ppName, CStringW(m_name));
    }
    if(pLCID)
    {
        *pLCID = 0; // TODO
    }
    return S_OK;
}

STDMETHODIMP_(int) CRenderedTextSubtitle::GetStream()
{
    return(0);
}

STDMETHODIMP CRenderedTextSubtitle::SetStream(int iStream)
{
    return iStream == 0 ? S_OK : E_FAIL;
}

STDMETHODIMP CRenderedTextSubtitle::Reload()
{
    CFileStatus s;
    if(!CFile::GetStatus(m_path, s)) return E_FAIL;
    return !m_path.IsEmpty() && Open(m_path, DEFAULT_CHARSET) ? S_OK : E_FAIL;
}

STDMETHODIMP_(bool) CRenderedTextSubtitle::IsColorTypeSupported( int type )
{
    return type==MSP_AYUV_PLANAR ||
           type==MSP_AYUV ||
           type==MSP_XY_AUYV ||
           type==MSP_RGBA;
}

STDMETHODIMP_(bool) CRenderedTextSubtitle::IsMovable()
{
    return m_movable;
}

STDMETHODIMP_(bool) CRenderedTextSubtitle::IsSimple()
{
    return m_simple;
}

STDMETHODIMP CRenderedTextSubtitle::Lock()
{
    return CSubPicProviderImpl::Lock();
}

STDMETHODIMP CRenderedTextSubtitle::Unlock()
{
    return CSubPicProviderImpl::Unlock();
}
