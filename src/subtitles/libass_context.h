#pragma once
#include <ass/ass.h>
#include "LibassRenderOptions.h"

struct ASS_LibraryDeleter
{
    void operator()(ASS_Library *p) { if (p) ass_library_done(p); }
};

struct ASS_RendererDeleter
{
    void operator()(ASS_Renderer *p) { if (p) ass_renderer_done(p); }
};

struct ASS_TrackDeleter
{
    void operator()(ASS_Track *p) { if (p) ass_free_track(p); }
};

struct ASS_Context
{
	bool m_assloaded = false;
	bool m_assfontloaded = false;
	LibassRenderOptions m_options;
	std::unique_ptr<ASS_Library, ASS_LibraryDeleter> m_ass;
	std::unique_ptr<ASS_Renderer, ASS_RendererDeleter> m_renderer;
	std::unique_ptr<ASS_Track, ASS_TrackDeleter> m_track;
	void ApplyRenderOptions(const LibassRenderOptions &opts, const ASS_Style *selective_style);
	void SetExtraStyleOverrides(std::vector<CStringA> extra);
	bool LoadASSFile(CString path);
	bool LoadASSTrack(char *data, int size);
	void LoadASSFont(IPin *pPin, IFilterGraph *pGraph);
	void UnloadASS();
private:
	void ApplyStyleOverrides();
	CStringW m_styles_file_applied;
	std::vector<CStringA> m_extra_style_overrides;
};
