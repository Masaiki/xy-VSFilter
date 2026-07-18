#include "stdafx.h"
#include "xy_clipper_paint_machine.h"
#include "RTS.h"
#include "Rasterizer.h"
#include "cache_manager.h"
#include "subpixel_position_controler.h"
#include "xy_malloc.h"

//////////////////////////////////////////////////////////////////////////
//
// CClipperPaintMachine
//

void CClipperPaintMachine::Paint(SharedPtrGrayImage2 *output)
{
    ASSERT(output);
    if (m_clipper!=NULL)
    {
        ClipperAlphaMaskMruCache * cache = CacheManager::GetClipperAlphaMaskMruCache();
        POSITION pos = cache->Lookup(m_hash_key);
        if( pos!=NULL )
        {
            *output = cache->GetAt(pos);
            cache->UpdateCache(pos);
            return;
        }

        ClipperAlphaMaskCacheKey base_key(m_clipper);
        base_key.UpdateHashValue();
        pos = cache->Lookup(base_key);
        if (pos != NULL)
        {
            *output = cache->GetAt(pos);
            cache->UpdateCache(pos);
        }
        else
        {
            (*output).reset(m_clipper->Paint());
            cache->UpdateCache(base_key, *output);
        }

        if (*output && m_offset != CPoint(0, 0))
        {
            SharedPtrGrayImage2 shifted(DEBUG_NEW GrayImage2(**output));
            if (!m_clipper->m_inverse)
            {
                shifted->left_top.Offset(m_offset);
            }
            else
            {
                const size_t byte_count = static_cast<size_t>(shifted->pitch) * shifted->size.cy;
                BYTE* shifted_data = reinterpret_cast<BYTE*>(xy_malloc(byte_count));
                if (!shifted_data)
                {
                    output->reset();
                    return;
                }
                shifted->data.reset(shifted_data, xy_free);
                memset(shifted_data, 0x40, byte_count);

                const GrayImage2& source = **output;
                for (int y = 0; y < shifted->size.cy; ++y)
                {
                    const int source_y = y - m_offset.y;
                    if (source_y < 0 || source_y >= source.size.cy)
                        continue;
                    BYTE* destination_row = shifted_data + y * shifted->pitch;
                    const BYTE* source_row = source.data.get() + source_y * source.pitch;
                    for (int x = 0; x < shifted->size.cx; ++x)
                    {
                        const int source_x = x - m_offset.x;
                        if (source_x >= 0 && source_x < source.size.cx)
                            destination_row[x] = source_row[source_x];
                    }
                }
            }
            *output = shifted;
            cache->UpdateCache(m_hash_key, *output);
        }
    }
}

CRect CClipperPaintMachine::CalcDirtyRect()
{
    return CRect(0,0,INT_MAX, INT_MAX);//fix me: not a decent state machine yet
}

const ClipperAlphaMaskCacheKey& CClipperPaintMachine::GetHashKey()
{
    return m_hash_key;
}
