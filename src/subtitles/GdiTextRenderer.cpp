#include "stdafx.h"
#include "GdiTextRenderer.h"

#include <algorithm>
#include <vector>

#include <usp10.h>

GdiTextRenderer::GdiTextRenderer(HDC hdc, LPCWSTR text, int length, TextRendererMode mode)
    : m_hdc(hdc)
    , m_text(text)
    , m_length(length)
    , m_analysis(NULL)
{
    mode = NormalizeTextRendererMode(mode);
    if (m_length <= 0 || mode == TEXT_RENDERER_LEGACY_GDI ||
            (mode == TEXT_RENDERER_AUTO_FALLBACK && !NeedsFallback())) {
        return;
    }

    SCRIPT_STRING_ANALYSIS analysis = NULL;
    const int glyph_capacity = m_length + m_length / 2 + 16;
    const HRESULT hr = ScriptStringAnalyse(m_hdc, m_text, m_length, glyph_capacity, -1,
                                           SSA_GLYPHS | SSA_FALLBACK | SSA_LINK, 0,
                                           NULL, NULL, NULL, NULL, NULL, &analysis);
    if (hr == S_OK) {
        m_analysis = analysis;
    } else if (analysis) {
        ScriptStringFree(&analysis);
    }
}

GdiTextRenderer::~GdiTextRenderer()
{
    if (m_analysis) {
        SCRIPT_STRING_ANALYSIS analysis = static_cast<SCRIPT_STRING_ANALYSIS>(m_analysis);
        ScriptStringFree(&analysis);
        m_analysis = NULL;
    }
}

bool GdiTextRenderer::GetExtent(SIZE* extent) const
{
    if (m_analysis) {
        const SIZE* size = ScriptString_pSize(static_cast<SCRIPT_STRING_ANALYSIS>(m_analysis));
        if (size) {
            *extent = *size;
            return true;
        }
    }

    return !!GetTextExtentPoint32W(m_hdc, m_text, m_length, extent);
}

bool GdiTextRenderer::Draw(int x, int y) const
{
    if (m_analysis) {
        return ScriptStringOut(static_cast<SCRIPT_STRING_ANALYSIS>(m_analysis),
                               x, y, 0, NULL, 0, 0, FALSE) == S_OK;
    }

    return !!TextOutW(m_hdc, x, y, m_text, m_length);
}

bool GdiTextRenderer::NeedsFallback() const
{
    std::vector<WORD> glyphs(m_length);
    if (GetGlyphIndicesW(m_hdc, m_text, m_length, glyphs.data(),
                         GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR) {
        return false;
    }

    return std::find(glyphs.begin(), glyphs.end(), 0xffff) != glyphs.end();
}
