#pragma once
#include "csri_loader.h"
#include "csri_wrapper.h"
#include "sk_csri_ext.h"
#include <atlstr.h>
#include <memory>

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
    const struct sk_csri_ext_impl *m_sk_ext_impl;
    std::unique_ptr<csri_inst, CSRI_InstDeleter> m_inst;
    CSRI_InstDeleter m_deleter;

    CSRI_Context() : m_csri_loaded(false), m_renderer(nullptr), m_sk_ext_impl(nullptr), m_deleter{m_loader.get()} {}

    bool csri_load_file(CString path);
    bool csri_load_memory(char *data, int size);
    void csri_unload();
    void process_data(const void *data, size_t length, double time_start, double time_stop, sk_csri_subtype subtype) const;
};
