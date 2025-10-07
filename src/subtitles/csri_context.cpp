#include "stdafx.h"
#include "csri_context.h"
#include <atlconv.h>

bool CSRI_Context::csri_load_file(CString path)
{
    csri_unload();
    if (path.IsEmpty()) return false;

    if (!m_loader->is_loaded()) {
         return false;
    }

    m_renderer = m_loader->csri_renderer_default();
    if (!m_renderer) return false;

    m_sk_ext_impl = m_loader->get_sk_csri_ext_impl(m_renderer);

    struct csri_openflag flags = {0};
    m_inst = std::unique_ptr<csri_inst, CSRI_InstDeleter>(m_loader->csri_open_file(m_renderer, CT2A(path.GetString(), CP_UTF8), &flags), m_deleter);
    if (!m_inst) return false;

    m_csri_loaded = true;
    return true;
}

bool CSRI_Context::csri_load_memory(char *data, int size)
{
    csri_unload();
    if (size <= 0 || !data) return false;

    if (!m_loader->is_loaded()) {
        return false;
    }

    m_renderer = m_loader->csri_renderer_default();
    if (!m_renderer) return false;

    m_sk_ext_impl = m_loader->get_sk_csri_ext_impl(m_renderer);

    struct csri_openflag flags = {0};
    m_inst = std::unique_ptr<csri_inst, CSRI_InstDeleter>(m_loader->csri_open_mem(m_renderer, data, size, &flags), m_deleter);
    if (!m_inst) return false;

    m_csri_loaded = true;
    return true;
}

void CSRI_Context::csri_unload()
{
    m_csri_loaded = false;
    m_inst.reset();
    m_renderer = nullptr;
    m_sk_ext_impl = nullptr;
}

void CSRI_Context::process_data(const void *data, size_t length, double time_start, double time_stop, sk_csri_subtype subtype) const
{
    if (!m_csri_loaded || !m_sk_ext_impl || !m_sk_ext_impl->process_data || !m_inst) {
        return;
    }

    m_sk_ext_impl->process_data(m_inst.get(), data, length, time_start, time_stop, subtype);
}
