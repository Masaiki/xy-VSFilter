#include "stdafx.h"
#include "libass_context.h"

static const size_t MAX_LIBASS_LOG_LINES = 1000;

static void libass_message_callback(int level, const char *fmt, va_list args, void *data)
{
    if (data) {
        static_cast<ASS_Context *>(data)->AppendLog(level, fmt, args);
    }
}

static const wchar_t *libass_log_level_name(int level)
{
    switch (level) {
    case 0: return L"fatal";
    case 1: return L"error";
    case 2: return L"warn";
    case 3: return L"info";
    case 4: return L"verbose";
    default: return L"debug";
    }
}

static std::unique_ptr<char[]> read_file_bytes(FILE *fp, size_t *bufsize)
{
    int res = fseek(fp, 0, SEEK_END);
    if (res == -1) {
        fclose(fp);
        return nullptr;
    }
    long sz = ftell(fp);
    rewind(fp);
    std::unique_ptr<char[]> buf = std::make_unique<char[]>(sz + 1);
    size_t bytes_read = 0;
    do {
        res = fread(buf.get() + bytes_read, sizeof(char), sz - bytes_read, fp);
        if (res <= 0) {
            fclose(fp);
            return nullptr;
        }
        bytes_read += res;
    } while (sz - bytes_read > 0);
    buf[sz] = '\0';
    if (bufsize) *bufsize = sz;
    fclose(fp);
    return buf;
}

static const char *detect_bom(const char *buf, const size_t bufsize) {
    if (bufsize >= 4) {
        if (!strncmp(buf, "\xef\xbb\xbf", 3))
            return "UTF-8";
        if (!strncmp(buf, "\x00\x00\xfe\xff", 4))
            return "UTF-32BE";
        if (!strncmp(buf, "\xff\xfe\x00\x00", 4))
            return "UTF-32LE";
        if (!strncmp(buf, "\xfe\xff", 2))
            return "UTF-16BE";
        if (!strncmp(buf, "\xff\xfe", 2))
            return "UTF-16LE";
    }
    return "UTF-8";
}

ASS_Context::ASS_Context()
    : m_assloaded(false)
    , m_assfontloaded(false)
{
}

ASS_Context::~ASS_Context()
{
    UnloadASS();
}

bool ASS_Context::LoadASSFile(CString path)
{
    UnloadASS();

    if (path.IsEmpty()) return false;

    InitLibrary();
    if (!m_ass) return false;

    m_renderer = decltype(m_renderer)(ass_renderer_init(m_ass.get()));
    if (!m_renderer) {
        UnloadASS();
        return false;
    }

    ass_set_extract_fonts(m_ass.get(), 1);

    size_t bufsize = 0;
    FILE *fp = _wfopen(path.GetString(), L"rb");
    if (!fp) {
        UnloadASS();
        return false;
    }

    auto buf = read_file_bytes(fp, &bufsize);
    if (!buf) {
        UnloadASS();
        return false;
    }

    const char *encoding = detect_bom(buf.get(), bufsize);

    m_track = decltype(m_track)(ass_read_memory(m_ass.get(), buf.get(), bufsize, const_cast<char *>(encoding)));

    if (!m_track) {
        UnloadASS();
        return false;
    }

    ass_set_fonts(m_renderer.get(), NULL, NULL, ASS_FONTPROVIDER_DIRECTWRITE, NULL, 0);

    m_assloaded = true;
    m_assfontloaded = true;
    return true;
}

bool ASS_Context::LoadASSTrack(char *data, int size)
{
    UnloadASS();

    InitLibrary();
    if (!m_ass) return false;

    m_renderer = decltype(m_renderer)(ass_renderer_init(m_ass.get()));
    if (!m_renderer) {
        UnloadASS();
        return false;
    }

    m_track = decltype(m_track)(ass_new_track(m_ass.get()));

    ass_set_extract_fonts(m_ass.get(), 1);

    if (!m_track) {
        UnloadASS();
        return false;
    }

    ass_process_codec_private(m_track.get(), data, size);

    ass_set_fonts(m_renderer.get(), NULL, NULL, ASS_FONTPROVIDER_DIRECTWRITE, NULL, 0);

    m_assloaded = true;
    return true;
}

void ASS_Context::UnloadASS()
{
    m_assloaded = false;
    m_assfontloaded = false;
    if (m_ass) ass_set_message_cb(m_ass.get(), nullptr, nullptr);
    if (m_track) m_track.reset();
    if (m_renderer) m_renderer.reset();
    if (m_ass) m_ass.reset();
}

void ASS_Context::Reset()
{
    UnloadASS();

    std::lock_guard<std::mutex> lock(m_log_mutex);
    m_log_lines.clear();
}

void ASS_Context::InitLibrary()
{
    m_ass = decltype(m_ass)(ass_library_init());
    if (m_ass) {
        ass_set_message_cb(m_ass.get(), libass_message_callback, this);
    }
}

void ASS_Context::AppendLog(int level, const char *fmt, va_list args)
{
    char message[2048];
    va_list args_copy;
    va_copy(args_copy, args);
    _vsnprintf_s(message, _countof(message), _TRUNCATE, fmt, args_copy);
    va_end(args_copy);

    CStringW line;
    line.Format(L"[%s] %s", libass_log_level_name(level), UTF8To16(message).GetString());

    std::lock_guard<std::mutex> lock(m_log_mutex);
    m_log_lines.push_back(line);
    while (m_log_lines.size() > MAX_LIBASS_LOG_LINES) {
        m_log_lines.pop_front();
    }
}

CStringW ASS_Context::GetLog()
{
    std::lock_guard<std::mutex> lock(m_log_mutex);

    CStringW log;
    for (const auto& line : m_log_lines) {
        log += line;
        log += L"\r\n";
    }
    return log;
}

#include <DSMPropertyBag.h>
#include <comdef.h>

void ASS_Context::LoadASSFont(IPin *pPin, IFilterGraph *pGraph)
{
    // Try to load fonts in the container
    CComPtr<IAMGraphStreams> graphStreams;
    CComPtr<IDSMResourceBag> bag;
    if (SUCCEEDED(pGraph->QueryInterface(IID_PPV_ARGS(&graphStreams))) &&
        SUCCEEDED(graphStreams->FindUpstreamInterface(pPin, IID_PPV_ARGS(&bag), AM_INTF_SEARCH_FILTER)))
    {
        for (DWORD i = 0; i < bag->ResGetCount(); ++i)
        {
            _bstr_t name, desc, mime;
            BYTE *pData = nullptr;
            DWORD len = 0;
            if (SUCCEEDED(bag->ResGet(i, &name.GetBSTR(), &desc.GetBSTR(), &mime.GetBSTR(), &pData, &len, nullptr)))
            {
                if (wcscmp(mime.GetBSTR(), L"application/x-truetype-font") == 0 ||
                    wcscmp(mime.GetBSTR(), L"application/vnd.ms-opentype") == 0 ||
                    wcsncmp(mime.GetBSTR(), L"application/font-", 17) == 0 ||
                    wcsncmp(mime.GetBSTR(), L"font/", 5) == 0) // TODO: more mimes?
                {
                    auto utf8_name = UTF16To8(name.GetBSTR());
                    ass_add_font(m_ass.get(), utf8_name.GetString(), (char *)pData, len);
                    // TODO: clear these fonts somewhere?
                }
                CoTaskMemFree(pData);
            }
        }
    }
    ass_set_fonts(m_renderer.get(), NULL, NULL, ASS_FONTPROVIDER_DIRECTWRITE, NULL, NULL);
}
