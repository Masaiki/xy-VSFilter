#pragma once
#include "csri_wrapper.h"
#include "csri/stream.h"
#include <atlstr.h>

class CSRI_Loader
{
public:
	CSRI_Loader();
	~CSRI_Loader();

	bool load(const CStringW & lib_path);
	void unload();
	bool is_loaded() const { return m_hModule != nullptr; }
	const struct csri_stream_ext *get_csri_stream_ext(csri_rend *renderer) const;

	// CSRI function pointers
	csri_rend *(*csri_renderer_byname)(const char *name, const char *specific);
	csri_rend *(*csri_renderer_default)();
	csri_inst *(*csri_open_file)(csri_rend *renderer, const char *filename, struct csri_openflag *flags);
	csri_inst *(*csri_open_mem)(csri_rend *renderer, const void *data, size_t length, struct csri_openflag *flags);
	void (*csri_close)(csri_inst *inst);
	int (*csri_request_fmt)(csri_inst *inst, const struct csri_fmt *fmt);
	void (*csri_render)(csri_inst *inst, struct csri_frame *frame, double time);
	void *(*csri_query_ext)(csri_rend *rend, csri_ext_id extname);
private:
	void init_function_pointers();

	HMODULE m_hModule;
};