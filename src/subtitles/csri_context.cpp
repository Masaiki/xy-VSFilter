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

    m_stream_ext = m_loader->get_csri_stream_ext(m_renderer);

    struct csri_openflag flags = {0};
    if (m_stream_ext) {
        m_inst = std::unique_ptr<csri_inst, CSRI_InstDeleter>(m_stream_ext->init_stream(m_renderer, data, size, &flags), m_deleter);
    } else {
        m_inst = std::unique_ptr<csri_inst, CSRI_InstDeleter>(m_loader->csri_open_mem(m_renderer, data, size, &flags), m_deleter);
    }
    if (!m_inst) return false;

    m_csri_loaded = true;
    return true;
}

void CSRI_Context::csri_unload()
{
    m_csri_loaded = false;
    m_inst.reset();
    m_renderer = nullptr;
    m_stream_ext = nullptr;
}

void CSRI_Context::push_packet(const void *data, size_t length, double time_start, double time_stop) const
{
    if (!m_csri_loaded || !m_stream_ext || !m_stream_ext->push_packet || !m_inst) {
        return;
    }

    m_stream_ext->push_packet(m_inst.get(), data, length, time_start, time_stop);
}

void CSRI_Context::discard(int all) const
{
    if (!m_csri_loaded || !m_stream_ext || !m_stream_ext->discard || !m_inst) {
        return;
    }

    m_stream_ext->discard(m_inst.get(), all);
}
