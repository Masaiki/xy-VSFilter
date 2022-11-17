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
#include <emmintrin.h>

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

SubFrame::SubFrame(RECT rect, ULONGLONG id, ASS_Image* image)
    : CUnknown("", nullptr)
    , m_rect(rect)
    , m_id(id)
    , m_pixelsRect{}
{
    Flatten(image);
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
    *count = (m_pixels ? 1 : 0);
    return S_OK;
}

STDMETHODIMP SubFrame::GetBitmap(int index, ULONGLONG* id, POINT* position, SIZE* size, LPCVOID* pixels, int* pitch)
{
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

    return S_OK;
}

static __forceinline void pixmix_sse2(DWORD *dst, DWORD color, DWORD alpha)
{
    alpha = ((alpha+1) * (color>>24)) >> 8;
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

        __m128i ra;
#ifdef _DEBUG
        ra = _mm_setzero_si128();
#endif // _DEBUG
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

void SubFrame::Flatten(ASS_Image *image)
{
    if (image)
    {
        for (auto i = image; i != nullptr; i = i->next)
        {
            RECT rect1 = m_pixelsRect;
            RECT rect2 = { i->dst_x, i->dst_y, i->dst_x + i->w, i->dst_y + i->h };
            UnionRect(&m_pixelsRect, &rect1, &rect2);
        }

        const POINT pixelsPoint = GetRectPos(m_pixelsRect);
        const SIZE pixelsSize = GetRectSize(m_pixelsRect);
        m_pixels = std::make_unique<uint32_t[]>(pixelsSize.cx * pixelsSize.cy);

        for (auto i = image; i != nullptr; i = i->next)
        {
            for (int y = 0; y < i->h; ++y)
            {
                auto dst = reinterpret_cast<uint8_t *>(m_pixels.get() + (i->dst_y + y - pixelsPoint.y) * pixelsSize.cx + (i->dst_x - pixelsPoint.x));
                auto alpha = i->bitmap + y * i->stride;
                packed_pix_mix_sse2(dst, alpha, i->w, (i->color >> 8) | (~(i->color) << 24));
            }
        }
    }
}
