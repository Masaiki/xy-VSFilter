#include "stdafx.h"
#include "csri_loader.h"

CSRI_Loader::CSRI_Loader()
	: m_hModule(nullptr)
	, csri_renderer_byname(nullptr)
	, csri_renderer_default(nullptr)
	, csri_open_file(nullptr)
	, csri_open_mem(nullptr)
	, csri_close(nullptr)
	, csri_request_fmt(nullptr)
	, csri_render(nullptr)
	, csri_query_ext(nullptr)
{
}

CSRI_Loader::~CSRI_Loader()
{
	unload();
}

bool CSRI_Loader::load(const CStringW & lib_path)
{
	if (m_hModule) {
		unload();
	}

	m_hModule = LoadLibraryW(lib_path);
	if (!m_hModule) {
		return false;
	}

	init_function_pointers();

	if (!csri_renderer_byname || !csri_renderer_default || !csri_open_file || !csri_open_mem ||
		!csri_close || !csri_request_fmt || !csri_render || !csri_query_ext) {
		unload();
		return false;
	}

	return true;
}

void CSRI_Loader::unload()
{
	if (m_hModule) {
		FreeLibrary(m_hModule);
		m_hModule = nullptr;
	}

	csri_renderer_byname = nullptr;
	csri_renderer_default = nullptr;
	csri_open_file = nullptr;
	csri_open_mem = nullptr;
	csri_close = nullptr;
	csri_request_fmt = nullptr;
	csri_render = nullptr;
	csri_query_ext = nullptr;
}

const struct csri_stream_ext *CSRI_Loader::get_csri_stream_ext(csri_rend *renderer) const
{
	if (!renderer || !csri_query_ext) {
		return nullptr;
	}

	return (const struct csri_stream_ext *)csri_query_ext(renderer, CSRI_EXT_STREAM_ASS);
}

void CSRI_Loader::init_function_pointers()
{
	if (!m_hModule) {
		return;
	}

	csri_renderer_byname = (csri_rend * (*)(const char *, const char *))GetProcAddress(m_hModule, "csri_renderer_byname");
	csri_renderer_default = (csri_rend * (*)())GetProcAddress(m_hModule, "csri_renderer_default");
	csri_open_file = (csri_inst * (*)(csri_rend *, const char *, struct csri_openflag *))GetProcAddress(m_hModule, "csri_open_file");
	csri_open_mem = (csri_inst * (*)(csri_rend *, const void *, size_t, struct csri_openflag *))GetProcAddress(m_hModule, "csri_open_mem");
	csri_close = (void (*)(csri_inst *))GetProcAddress(m_hModule, "csri_close");
	csri_request_fmt = (int (*)(csri_inst *, const struct csri_fmt *))GetProcAddress(m_hModule, "csri_request_fmt");
	csri_render = (void (*)(csri_inst *, struct csri_frame *, double))GetProcAddress(m_hModule, "csri_render");
	csri_query_ext = (void *(*)(csri_rend *, csri_ext_id))GetProcAddress(m_hModule, "csri_query_ext");
}
