#pragma once
#include <ass/ass.h>
#include <deque>
#include <mutex>

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
	ASS_Context();
	~ASS_Context();

	ASS_Context(const ASS_Context&) = delete;
	ASS_Context& operator=(const ASS_Context&) = delete;

	bool m_assloaded;
	bool m_assfontloaded;
	std::unique_ptr<ASS_Library, ASS_LibraryDeleter> m_ass;
	std::unique_ptr<ASS_Renderer, ASS_RendererDeleter> m_renderer;
	std::unique_ptr<ASS_Track, ASS_TrackDeleter> m_track;
	bool LoadASSFile(CString path);
	bool LoadASSTrack(char *data, int size);
	void LoadASSFont(IPin *pPin, IFilterGraph *pGraph);
	void UnloadASS();
	void Reset();
	void AppendLog(int level, const char *fmt, va_list args);
	CStringW GetLog();

private:
	void InitLibrary();

	std::mutex m_log_mutex;
	std::deque<CStringW> m_log_lines;
};
