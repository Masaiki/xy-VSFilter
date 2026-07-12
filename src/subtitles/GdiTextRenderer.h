#pragma once

#include <windows.h>

#include "TextRendererMode.h"

class GdiTextRenderer
{
public:
    GdiTextRenderer(HDC hdc, LPCWSTR text, int length, TextRendererMode mode);
    ~GdiTextRenderer();

    bool GetExtent(SIZE* extent) const;
    bool Draw(int x, int y) const;

private:
    GdiTextRenderer(const GdiTextRenderer&);
    GdiTextRenderer& operator=(const GdiTextRenderer&);

    bool NeedsFallback() const;

    HDC m_hdc;
    LPCWSTR m_text;
    int m_length;
    void* m_analysis;
};
