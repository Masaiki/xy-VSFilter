#pragma once
#include "csri_loader.h"
#include "csri_wrapper.h"
#include "csri/stream.h"
#include <atlstr.h>
#include <memory>
#include <vector>

struct CSRI_InstDeleter
{
    void operator()(csri_inst *p)
    {
        if (p && loader && loader->csri_close)
            loader->csri_close(p);
    }
    CSRI_Loader* loader;
};

struct CSRI_Context
{
    std::shared_ptr<CSRI_Loader> m_loader;
    bool m_csri_loaded;
    csri_rend *m_renderer;
    const struct csri_stream_ext *m_stream_ext;
    std::unique_ptr<csri_inst, CSRI_InstDeleter> m_inst;
    CSRI_InstDeleter m_deleter;
    std::vector<unsigned char> m_memory_source;

    CSRI_Context() : m_csri_loaded(false), m_renderer(nullptr), m_stream_ext(nullptr), m_deleter{m_loader.get()} {}

    bool csri_load_file(CString path);
    bool csri_load_memory(const void *data, size_t size);
    bool csri_reload_memory();
    bool has_memory_source() const { return !m_memory_source.empty(); }
    void csri_unload();
    void push_packet(const void *data, size_t length, double time_start, double time_stop) const;
    void discard(int all) const;
};
