#ifndef __XY_CLIPPER_PAINT_MACHINE_98D7A2E7_B2FA_44BC_9678_8B27CE8EB9DB_H__
#define __XY_CLIPPER_PAINT_MACHINE_98D7A2E7_B2FA_44BC_9678_8B27CE8EB9DB_H__

#include <boost/shared_ptr.hpp>

class CClipper;
typedef ::boost::shared_ptr<CClipper> SharedPtrCClipper;

struct GrayImage2;
typedef ::boost::shared_ptr<GrayImage2> SharedPtrGrayImage2;

class ClipperAlphaMaskCacheKey;

class CClipperPaintMachine
{
public:
    CClipperPaintMachine(const SharedPtrCClipper& clipper,
        const CPoint& offset = CPoint(0, 0))
        : m_clipper(clipper)
        , m_offset(offset)
        , m_hash_key(clipper, offset){ m_hash_key.UpdateHashValue(); }

    void Paint(SharedPtrGrayImage2* output);
    CRect CalcDirtyRect();
    const ClipperAlphaMaskCacheKey& GetHashKey();
private:
    SharedPtrCClipper m_clipper;
    CPoint m_offset;
    ClipperAlphaMaskCacheKey m_hash_key;
};

#endif // __XY_CLIPPER_PAINT_MACHINE_98D7A2E7_B2FA_44BC_9678_8B27CE8EB9DB_H__
