/*
 *   Copyright(C) 2016-2017 Blitzker
 *
 *   This program is free software : you can redistribute it and / or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.If not, see <http://www.gnu.org/licenses/>.
 */

#include "stdafx.h"
#include "SubFrame.h"
#include "../subpic/color_conv_table.h"
#include <ppl.h>

namespace
{
    inline POINT GetRectPos(RECT rect)
    {
        return {rect.left, rect.top};
    }

    inline SIZE GetRectSize(RECT rect)
    {
        return {rect.right - rect.left, rect.bottom - rect.top};
    }
}

SubFrame::SubFrame(RECT rect, ULONGLONG id, ASS_Image* image, XyColorSpace xy_color_space)
    : CUnknown("", nullptr)
    , m_rect(rect)
    , m_id(id)
    , m_pixelsRect{}
    , m_xy_color_space(xy_color_space)
{
    switch (xy_color_space) {
    case XY_CS_AYUV_PLANAR:
        Flatten2AYUV(image);
        break;
    case XY_CS_ARGB:
    case XY_CS_ARGB_F:
        Flatten2ARGB(image);
        break;
    }
}

STDMETHODIMP SubFrame::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    if (riid == __uuidof(ISubRenderFrame))
        return GetInterface(static_cast<ISubRenderFrame*>(this), ppv);

    return __super::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP SubFrame::GetOutputRect(RECT* outputRect)
{
    CheckPointer(outputRect, E_POINTER);
    *outputRect = m_rect;
    return S_OK;
}

STDMETHODIMP SubFrame::GetClipRect(RECT* clipRect)
{
    CheckPointer(clipRect, E_POINTER);
    *clipRect = m_rect;
    return S_OK;
}

STDMETHODIMP SubFrame::GetBitmapCount(int* count)
{
    CheckPointer(count, E_POINTER);
    if (m_xy_color_space == XY_CS_ARGB || m_xy_color_space == XY_CS_ARGB_F) {
        *count = (m_pixels ? 1 : 0);
    }
    else if (m_xy_color_space == XY_CS_AYUV_PLANAR) {
        *count = m_bitmaps.GetCount();
    }
    return S_OK;
}

STDMETHODIMP SubFrame::GetBitmap(int index, ULONGLONG* id, POINT* position, SIZE* size, LPCVOID* pixels, int* pitch)
{
    if (m_xy_color_space == XY_CS_ARGB || m_xy_color_space == XY_CS_ARGB_F) {
        if (index != 0) return E_INVALIDARG;

        if (!id && !position && !size && !pixels && !pitch)
            return S_FALSE;

        if (id)
            *id = m_id;
        if (position)
            *position = GetRectPos(m_pixelsRect);
        if (size)
            *size = GetRectSize(m_pixelsRect);
        if (pixels)
            *pixels = m_pixels.get();
        if (pitch)
            *pitch = size->cx * 4;
    }
    else if (m_xy_color_space == XY_CS_AYUV_PLANAR) {
        if (index < 0 || index >= (int)m_bitmaps.GetCount())
        {
            return E_INVALIDARG;
        }
        if (id)
        {
            *id = m_bitmap_ids.GetAt(index);
        }
        const XyBitmap& bitmap = *(m_bitmaps.GetAt(index));
        if (position)
        {
            position->x = bitmap.x;
            position->y = bitmap.y;
        }
        if (size)
        {
            size->cx = bitmap.w;
            size->cy = bitmap.h;
        }
        if (pixels)
        {
            *pixels = bitmap.plans[0];
        }
        if (pitch)
        {
            *pitch = bitmap.pitch;
        }
    }
    else return E_FAIL;
    return S_OK;
}

void SubFrame::Flatten2ARGB(ASS_Image* image)
{
    if (image)
    {
        for (auto i = image; i != nullptr; i = i->next)
        {
            RECT rect1 = m_pixelsRect;
            RECT rect2 = {i->dst_x, i->dst_y, i->dst_x + i->w, i->dst_y + i->h};
            UnionRect(&m_pixelsRect, &rect1, &rect2);
        }

        const POINT pixelsPoint = GetRectPos(m_pixelsRect);
        const SIZE pixelsSize = GetRectSize(m_pixelsRect);
        m_pixels = std::make_unique<uint32_t[]>(pixelsSize.cx * pixelsSize.cy);
        std::fill(m_pixels.get(), m_pixels.get() + pixelsSize.cx * pixelsSize.cy, 0xff000000);

        for (auto i = image; i != nullptr; i = i->next)
        {
            concurrency::parallel_for(0, i->h, [&](int y)
            {
                for (int x = 0; x < i->w; ++x)
                {
                    uint32_t& dest = m_pixels[(i->dst_y + y - pixelsPoint.y) * pixelsSize.cx +
                                              (i->dst_x + x - pixelsPoint.x)];

                    uint32_t destA = (dest & 0xff000000) >> 24;

                    uint32_t srcA = i->bitmap[y * i->stride + x] * (0xff - (i->color & 0x000000ff));
                    srcA >>= 8;

                    uint32_t compA = 0xff - srcA;

                    uint32_t outA = 0xff - (srcA + (((0xff-destA) * compA) >> 8));

                    uint32_t outR = ((i->color & 0xff000000) >> 8) * srcA + (dest & 0x00ff0000) * compA;
                    outR >>= 8;

                    uint32_t outG = ((i->color & 0x00ff0000) >> 8) * srcA + (dest & 0x0000ff00) * compA;
                    outG >>= 8;

                    uint32_t outB = ((i->color & 0x0000ff00) >> 8) * srcA + (dest & 0x000000ff) * compA;
                    outB >>= 8;

                    dest = (outA << 24) + (outR & 0x00ff0000) + (outG & 0x0000ff00) + (outB & 0x000000ff);
                }
            }, concurrency::static_partitioner());
        }
    }
}

void SubFrame::Flatten2AYUV(ASS_Image* image)
{
    if (image)
    {
        for (auto i = image; i != nullptr; i = i->next)
        {
            int h2 = (i->h + 1) & (~1);
            CRect target_rect{ i->dst_x,i->dst_y,i->dst_x + i->w,i->dst_y + h2 };
            SharedBitmap tmp = boost::shared_ptr<XyBitmap>(XyBitmap::CreateBitmap(target_rect, XyBitmap::PLANNA));
            uint32_t srcRGB = (i->color >> 8) & 0x00ffffff;
            uint32_t partial_srcA = 0xff - (i->color & 0x000000ff);
            concurrency::parallel_for(0, i->h, [&](int y)
                {
                    for (int x = 0; x < i->w; ++x)
                    {
                        uint32_t srcA = i->bitmap[y * i->stride + x] * partial_srcA;
                        uint32_t AYUV = ColorConvTable::Argb2Ayuv(((srcA << 16) & 0xff000000) | srcRGB);
                        tmp->plans[0][y * tmp->pitch + x] = (AYUV >> 24) & 0x000000ff;
                        tmp->plans[1][y * tmp->pitch + x] = (AYUV >> 16) & 0x000000ff;
                        tmp->plans[2][y * tmp->pitch + x] = (AYUV >> 8) & 0x000000ff;
                        tmp->plans[3][y * tmp->pitch + x] = (AYUV) & 0x000000ff;
                    }
                }, concurrency::static_partitioner());
            m_bitmaps.Add(tmp);
            m_bitmap_ids.Add((uint64_t)tmp.get());
        }
    }
}

STDMETHODIMP SubFrame::GetXyColorSpace(int* xyColorSpace)
{
    if (!xyColorSpace)
    {
        return E_POINTER;
    }
    *xyColorSpace = m_xy_color_space;
    return S_OK;
}

STDMETHODIMP SubFrame::GetBitmapExtra(int index, LPVOID extra_info)
{
    if (index < 0 || index >= (int)m_bitmaps.GetCount())
    {
        return E_INVALIDARG;
    }
    if (extra_info && m_xy_color_space == XY_CS_AYUV_PLANAR)
    {
        const XyBitmap& bitmap = *(m_bitmaps.GetAt(index));
        XyPlannerFormatExtra* output = reinterpret_cast<XyPlannerFormatExtra*>(extra_info);

        output->plans[0] = bitmap.plans[0];
        output->plans[1] = bitmap.plans[1];
        output->plans[2] = bitmap.plans[2];
        output->plans[3] = bitmap.plans[3];
    }
    return S_OK;
}
