#include "FontLoaderContext.h"
#include <Windows.h>
extern "C" {
#include <ass_parser.h>
}

// codes copied from FontLoaderSub
#define kCacheFile L"fc-subs.db"
#define kBlackFile L"fc-ignore.txt"
static void *mem_realloc(void *existing, size_t size, void *arg) {
    HANDLE heap = (HANDLE)arg;
    if (size == 0) {
        HeapFree(heap, 0, existing);
        return NULL;
    }
    if (existing == NULL) {
        return HeapAlloc(heap, HEAP_ZERO_MEMORY, size);
    }
    return HeapReAlloc(heap, HEAP_ZERO_MEMORY, existing, size);
}
// codes copied from FontLoaderSub end

FontLoaderContext::FontLoaderContext(const wchar_t *font_path) {
    heap = HeapCreate(0, 0, 0);
    alloc.alloc = mem_realloc;
    alloc.arg = heap;
    fl_init(&c, &alloc);
    fl_scan_fonts(&c, font_path, kCacheFile, kBlackFile);
    stat = { 0 };
    fs_stat(c.font_set, &stat);
}

FontLoaderContext::~FontLoaderContext() {
    fl_unload_fonts(&c);
    fl_free(&c);
}

void FontLoaderContext::LoadFontFromWideString(const wchar_t *wdata, size_t wlen) {
    fl_ass_process_data(wdata, wlen, fl_sub_font_callback, &c);
    fl_load_fonts(&c);
}

void FontLoaderContext::LoadFontFromPath(const wchar_t* path) {
    fl_add_subs(&c, path);
    fl_load_fonts(&c);
}