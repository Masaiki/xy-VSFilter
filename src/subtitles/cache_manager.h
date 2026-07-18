/************************************************************************/
/* author: xy                                                           */
/* date: 20110926                                                       */
/************************************************************************/
#ifndef __CACHE_MANAGER_H_310C134F_844C_4590_A4D2_AD30165AF10A__
#define __CACHE_MANAGER_H_310C134F_844C_4590_A4D2_AD30165AF10A__

#include "RTS.h"
#include "mru_cache.h"
#include "flyweight_base_types.h"

template<class CahcheKey>
class XyCacheKeyTraits:public CElementTraits<CahcheKey>
{    
public:
    static inline ULONG Hash(const CahcheKey& key)
    {
        return key.GetHashValue();
    }
    static inline bool CompareElements(
        const CahcheKey& element1,
        const CahcheKey& element2)
    {
        return ( (element1==element2)!=0 );
    }
};

class TextInfoCacheKey
{
public:
    TextInfoCacheKey()
        : m_str_id(0)
        , m_text_renderer_mode(TEXT_RENDERER_LEGACY_GDI)
        , m_font_orientation(0)
        , m_hash_value(0)
    {
    }

    XyFwStringW::IdType m_str_id;
    FwSTSStyle m_style;
    TextRendererMode m_text_renderer_mode;
    double m_font_orientation;

    ULONG m_hash_value;
public:
    bool operator==(const TextInfoCacheKey& key)const;

    ULONG UpdateHashValue();
    inline ULONG GetHashValue()const
    {
        return m_hash_value;
    }
};

class PathDataCacheKey
{
public:
    PathDataCacheKey(const CWord& word):m_style(word.m_style)
    {
        const CPolygon *tmp = dynamic_cast<const CPolygon *>(&word);
        if (tmp)
        {
            m_scalex = tmp->m_scalex;
            m_scaley = tmp->m_scaley;
        }
        else
        {
            m_scalex = 0;
            m_scaley = 0;
        }
        const CText* text = dynamic_cast<const CText*>(&word);
        m_text_renderer_mode = text ? text->m_text_renderer_mode : TEXT_RENDERER_LEGACY_GDI;
        m_mod_hash = word.m_mod_style ? word.m_mod_style->path_hash : 0;
        m_mod_compatibility_mode = word.m_mod_compatibility_mode;
        if (m_mod_compatibility_mode) {
            m_mod_scale_x = word.m_mod_scale_x;
            m_mod_scale_y = word.m_mod_scale_y;
            m_is_opaque_box = word.m_is_opaque_box;
        } else {
            m_mod_scale_x = 0;
            m_mod_scale_y = 0;
            m_is_opaque_box = false;
        }
        m_str_id = word.m_str.GetId();
    }
    PathDataCacheKey(const PathDataCacheKey& key)
        :m_str_id(key.m_str_id)
        ,m_scalex(key.m_scalex)
        ,m_scaley(key.m_scaley)
        ,m_style(key.m_style)
        ,m_text_renderer_mode(key.m_text_renderer_mode)
        ,m_mod_hash(key.m_mod_hash)
        ,m_mod_compatibility_mode(key.m_mod_compatibility_mode)
        ,m_mod_scale_x(key.m_mod_scale_x)
        ,m_mod_scale_y(key.m_mod_scale_y)
        ,m_is_opaque_box(key.m_is_opaque_box)
        ,m_hash_value(key.m_hash_value){}
    bool operator==(const PathDataCacheKey& key)const
    {
        return m_str_id==key.m_str_id 
            && fabs(m_scalex-key.m_scalex)<0.000001
            && fabs(m_scaley-key.m_scaley)<0.000001
            && m_text_renderer_mode==key.m_text_renderer_mode
            && m_mod_hash==key.m_mod_hash
            && m_mod_compatibility_mode==key.m_mod_compatibility_mode
            && m_mod_scale_x==key.m_mod_scale_x
            && m_mod_scale_y==key.m_mod_scale_y
            && m_is_opaque_box==key.m_is_opaque_box
            && ( m_style==key.m_style || CompareSTSStyle(m_style, key.m_style) );
    }
    bool operator==(const CWord& key)const
    {
        return operator==(PathDataCacheKey(key));
    }

    static bool CompareSTSStyle(const STSStyle& lhs, const STSStyle& rhs);
    
    ULONG UpdateHashValue();
    inline ULONG GetHashValue()const
    {
        return m_hash_value;
    }
public:
    ULONG m_hash_value;
protected:
    double m_scalex, m_scaley;//for CPolygon 
    XyFwStringW::IdType m_str_id;
    FwSTSStyle m_style;
    TextRendererMode m_text_renderer_mode;
    size_t m_mod_hash;
    bool m_mod_compatibility_mode;
    double m_mod_scale_x, m_mod_scale_y;
    bool m_is_opaque_box;
};

class ScanLineData2CacheKey: public PathDataCacheKey
{
public:
    ScanLineData2CacheKey(const CWord& word, const POINT& org):PathDataCacheKey(word),m_org(org) { }
    ScanLineData2CacheKey(const ScanLineData2CacheKey& key)
        :PathDataCacheKey(key)
        ,m_org(key.m_org)
        ,m_hash_value(key.m_hash_value) { }

    bool operator==(const ScanLineData2CacheKey& key)const;

    ULONG UpdateHashValue();
    inline ULONG GetHashValue()const
    {
        return m_hash_value;
    }
public:
    ULONG m_hash_value;
    POINT m_org;
};

class OverlayNoBlurKey: public ScanLineData2CacheKey
{
public:
    OverlayNoBlurKey(const CWord& word, const POINT& p, const POINT& org):ScanLineData2CacheKey(word,org),m_p(p) {}
    OverlayNoBlurKey(const OverlayNoBlurKey& key):ScanLineData2CacheKey(key),m_p(key.m_p),m_hash_value(key.m_hash_value) {}

    bool operator==(const OverlayNoBlurKey& key)const;

    ULONG UpdateHashValue();
    inline ULONG GetHashValue()const
    {
        return m_hash_value;
    }
public:
    ULONG m_hash_value;
    POINT m_p;    
};

class OverlayKey: public OverlayNoBlurKey
{
public:
    OverlayKey(const CWord& word, const POINT& p, const POINT& org):OverlayNoBlurKey(word, p, org) {}
    OverlayKey(const OverlayKey& key):OverlayNoBlurKey(key), m_hash_value(key.m_hash_value) {}

    bool operator==(const OverlayKey& key)const;

    ULONG UpdateHashValue();
    inline ULONG GetHashValue()const
    {
        return m_hash_value;
    }
public:
    ULONG m_hash_value;
};

class ScanLineDataCacheKey
{
public:
    ScanLineDataCacheKey(const SharedPtrConstPathData& path_data):m_path_data(path_data){}
    bool operator==(const ScanLineDataCacheKey& key)const;

    ULONG UpdateHashValue();
    inline ULONG GetHashValue()const
    {
        return m_hash_value;
    }
public:
    ULONG m_hash_value;
    SharedPtrConstPathData m_path_data;
};

class OverlayNoOffsetKey:public ScanLineDataCacheKey
{
public:
    OverlayNoOffsetKey(const SharedPtrConstPathData& path_data, int xsub, int ysub, int border_x, int border_y)
        : ScanLineDataCacheKey(path_data)
        , m_border( border_x+(border_y<<16) )
        , m_rasterize_sub( xsub+(ysub<<16) ){}
    bool operator==(const OverlayNoOffsetKey& key)const;

    ULONG UpdateHashValue();
    inline ULONG GetHashValue()const
    {
        return m_hash_value;
    }
public:
    ULONG m_hash_value;

    int m_border;
    int m_rasterize_sub;
};

class ClipperAlphaMaskCacheKey
{
public:
    ClipperAlphaMaskCacheKey(const SharedPtrCClipper& clipper,
        const CPoint& offset = CPoint(0, 0))
        : m_clipper(clipper), m_offset(offset) {}
    bool operator==(const ClipperAlphaMaskCacheKey& key)const;

    ULONG UpdateHashValue();
    inline ULONG GetHashValue()const
    {
        return m_hash_value;
    }
public:
    ULONG m_hash_value;

    SharedPtrCClipper m_clipper;
    CPoint m_offset;
};

struct DrawItem;
class DrawItemHashKey
{
public:
    DrawItemHashKey(const DrawItem& draw_item);
    bool operator==(const DrawItemHashKey& hash_key) const;

    ULONG UpdateHashValue();
    inline ULONG GetHashValue()const
    {
        return m_hash_value;
    }
public:
    ULONG m_hash_value;
private:
    typedef ::boost::shared_ptr<OverlayKey> SharedOverlayKey;
    SharedOverlayKey m_overlay_key;
    ClipperAlphaMaskCacheKey m_clipper_key;
    CRect m_clip_rect;
    int m_xsub;
    int m_ysub;
    DWORD m_switchpts[6];
    bool m_fBody;
    bool m_fBorder;
    bool m_vsfilter_mod_compatibility;
    SharedPtrConstModPaintSource m_mod_paint_source;
};

class GroupedDrawItemsHashKey
{
public:
    bool operator==(const GroupedDrawItemsHashKey& key) const;

    ULONG UpdateHashValue();
    inline ULONG GetHashValue()const
    {
        return m_hash_value;
    }
public:
    ULONG m_hash_value;
private:
    typedef ::boost::shared_ptr<DrawItemHashKey> PKey;
    typedef CAtlArray<PKey> Keys;
    typedef ::boost::shared_ptr<Keys> PKeys;

    PKeys m_key;
    CRect m_clip_rect;

    friend struct GroupedDrawItems;
};

class PathDataTraits:public CElementTraits<PathData>
{
public:
    static ULONG Hash(const PathData& key);
};

class ClipperTraits:public CElementTraits<CClipper>
{
public:
    static ULONG Hash(const CClipper& key);
};

typedef EnhancedXyMru<
    TextInfoCacheKey, 
    CText::SharedPtrTextInfo, 
    XyCacheKeyTraits<TextInfoCacheKey>
> TextInfoMruCache;

typedef EnhancedXyMru<
    CStringW, 
    CRenderedTextSubtitle::SharedPtrConstAssTagList, 
    CStringElementTraits<CStringW>
> AssTagListMruCache;

typedef EnhancedXyMru<PathDataCacheKey, SharedPtrConstPathData, XyCacheKeyTraits<PathDataCacheKey>> PathDataMruCache;

typedef EnhancedXyMru<ScanLineData2CacheKey, SharedPtrConstScanLineData2, XyCacheKeyTraits<ScanLineData2CacheKey>> ScanLineData2MruCache;

typedef EnhancedXyMru<OverlayNoBlurKey, SharedPtrOverlay, XyCacheKeyTraits<OverlayNoBlurKey>> OverlayNoBlurMruCache;

typedef EnhancedXyMru<OverlayKey, SharedPtrOverlay, XyCacheKeyTraits<OverlayKey>> OverlayMruCache;

typedef EnhancedXyMru<ScanLineDataCacheKey, SharedPtrConstScanLineData, XyCacheKeyTraits<ScanLineDataCacheKey>> ScanLineDataMruCache;

typedef EnhancedXyMru<OverlayNoOffsetKey, OverlayNoBlurKey, XyCacheKeyTraits<OverlayNoOffsetKey>> OverlayNoOffsetMruCache;

typedef EnhancedXyMru<ClipperAlphaMaskCacheKey, SharedPtrGrayImage2, XyCacheKeyTraits<ClipperAlphaMaskCacheKey>> ClipperAlphaMaskMruCache;

class XyBitmap;
typedef ::boost::shared_ptr<XyBitmap> SharedPtrXyBitmap;
typedef EnhancedXyMru<std::size_t, SharedPtrXyBitmap> BitmapMruCache;

class CacheManager
{
public:
    static const int BITMAP_MRU_CACHE_ITEM_NUM = 64;//for test only 
    static const int CLIPPER_MRU_CACHE_ITEM_NUM = 48;

    static const int TEXT_INFO_CACHE_ITEM_NUM = 2048;
    static const int ASS_TAG_LIST_CACHE_ITEM_NUM = 2048;
    static const int SUBPIXEL_VARIANCE_CACHE_ITEM_NUM = 2048;
    static const int OVERLAY_CACHE_ITEM_NUM = 2048;

    static const int OVERLAY_NO_BLUR_CACHE_ITEM_NUM = 256;
    static const int SCAN_LINE_DATA_CACHE_ITEM_NUM = 512;
    static const int PATH_CACHE_ITEM_NUM = 768;
    static const int WORD_CACHE_ITEM_NUM = 512;

    static BitmapMruCache* GetBitmapMruCache();

    static ClipperAlphaMaskMruCache* GetClipperAlphaMaskMruCache();
    static TextInfoMruCache* GetTextInfoCache();
    static AssTagListMruCache* GetAssTagListMruCache();

    static ScanLineDataMruCache* GetScanLineDataMruCache();
    static OverlayNoOffsetMruCache* GetOverlayNoOffsetMruCache();

    static OverlayMruCache* GetSubpixelVarianceCache();
    static OverlayMruCache* GetOverlayMruCache();
    static OverlayNoBlurMruCache* GetOverlayNoBlurMruCache();
    static ScanLineData2MruCache* GetScanLineData2MruCache();
    static PathDataMruCache* GetPathDataMruCache();
};


typedef XyFlyWeight<GroupedDrawItemsHashKey, CacheManager::BITMAP_MRU_CACHE_ITEM_NUM, XyCacheKeyTraits<GroupedDrawItemsHashKey>> XyFwGroupedDrawItemsHashKey;

#endif // end of __CACHE_MANAGER_H_310C134F_844C_4590_A4D2_AD30165AF10A__

