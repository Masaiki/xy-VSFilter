#pragma once
extern "C" {
#include <font_loader.h>
}

class FontLoaderContext {
    FL_LoaderCtx c;
    FS_Stat stat;
    HANDLE heap;
    allocator_t alloc;
public:
    FontLoaderContext(const wchar_t *font_path);
    ~FontLoaderContext();
    void LoadFontFromWideString(const wchar_t *wdata, size_t wlen);
    void LoadFontFromPath(const wchar_t* path);
};
