#include "stdafx.h"
#include "libass_context.h"
#include "LibassLog.h"

static void LibassMessageCallback(int level, const char* format, va_list args, void*)
{
    // libass recommends level 5 for application logs; per-frame debug is omitted.
    if (level > 5) return;

    char message[LibassLog::MessageCapacity];
    const int length = vsnprintf(message, sizeof(message), format, args);
    if (length >= static_cast<int>(sizeof(message))) {
        const char suffix[] = "... [truncated]";
        memcpy(message + sizeof(message) - sizeof(suffix), suffix, sizeof(suffix));
    }
    if (length < 0) strcpy_s(message, "Unable to format libass message");

    // Preserve libass's default stderr output for info and higher severities.
    if (level <= 4) fprintf(stderr, "[ass] %s\n", message);

    SYSTEMTIME time;
    GetLocalTime(&time);
    const char* levels[] = { "FATAL", "ERROR", "WARN", "INFO", "INFO", "INFO" };
    char entry[LibassLog::MessageCapacity + 64];
    sprintf_s(entry, "[%02u:%02u:%02u.%03u] [%s] %s",
        time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
        level >= 0 ? levels[level] : "UNKNOWN", message);
    GetLibassLog().Append(entry);
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

static ASS_Hinting ToAssHinting(LibassHintingMode mode)
{
    switch (NormalizeLibassHintingMode(static_cast<int>(mode))) {
    case LIBASS_HINTING_LIGHT:
        return ASS_HINTING_LIGHT;
    case LIBASS_HINTING_NORMAL:
        return ASS_HINTING_NORMAL;
    case LIBASS_HINTING_NATIVE:
        return ASS_HINTING_NATIVE;
    case LIBASS_HINTING_NONE:
    default:
        return ASS_HINTING_NONE;
    }
}

std::vector<CStringA> ParseLibassStyleOverrideString(const CStringW &str)
{
    std::vector<CStringA> result;
    int pos = 0;
    while (pos >= 0) {
        CStringW token = str.Tokenize(L",", pos);
        token.Trim();
        if (!token.IsEmpty()) {
            result.push_back(UTF16To8(token.GetString()));
        }
    }
    return result;
}

void ASS_Context::ApplyRenderOptions(const LibassRenderOptions &opts, const ASS_Style *selective_style)
{
    m_options = NormalizeLibassRenderOptions(opts);

    if (m_ass) {
        if (!m_options.fonts_dir.IsEmpty()) {
            ass_set_fonts_dir(m_ass.get(), UTF16To8(m_options.fonts_dir.GetString()).GetString());
        }
        ass_set_extract_fonts(m_ass.get(), m_options.use_embedded_fonts ? 1 : 0);
    }

    if (m_renderer) {
        ass_set_font_scale(m_renderer.get(), m_options.font_scale);
        ass_set_line_spacing(m_renderer.get(), m_options.line_spacing);
        // libass: 0 = bottom (default), 100 = top; mpv --sub-pos: 0 = top, 100 = bottom (default)
        ass_set_line_position(m_renderer.get(), 100.0 - m_options.line_position);
        ass_set_shaper(m_renderer.get(), m_options.shaper == LIBASS_SHAPER_SIMPLE ? ASS_SHAPING_SIMPLE : ASS_SHAPING_COMPLEX);
        ass_set_hinting(m_renderer.get(), ToAssHinting(m_options.hinting_mode));
        ass_set_cache_limits(m_renderer.get(), m_options.glyph_cache_limit, m_options.bitmap_cache_max_size);
        ass_set_selective_style_override_enabled(m_renderer.get(),
            LibassOverrideBits(m_options.style_override, m_options.justify, m_options.scale_signs));
        if (selective_style) {
            // libass only reads the style (strings are copied by the function).
            ass_set_selective_style_override(m_renderer.get(), const_cast<ASS_Style *>(selective_style));
        }
    }

    if (m_track) {
        ass_configure_prune(m_track.get(), static_cast<long long>(m_options.prune_delay * 1000.0));

        // Read the styles file at most once per track and per file, so repeated
        // option changes do not accumulate styles in the track.
        const bool read_styles = m_options.style_override != LIBASS_STYLE_OVERRIDE_NO && !m_options.styles_file.IsEmpty();
        if (read_styles && m_styles_file_applied != m_options.styles_file) {
            ass_read_styles(m_track.get(), UTF16To8(m_options.styles_file.GetString()).GetString(), NULL);
            m_styles_file_applied = m_options.styles_file;
        } else if (!read_styles) {
            m_styles_file_applied.Empty();
        }
    }

    ApplyStyleOverrides();
}

void ASS_Context::SetExtraStyleOverrides(std::vector<CStringA> extra)
{
    m_extra_style_overrides = std::move(extra);
    ApplyStyleOverrides();
}

void ASS_Context::ApplyStyleOverrides()
{
    if (!m_ass) return;

    // Combine the filter-generated overrides (Force Default Style) with the
    // user override string; ass_set_style_overrides replaces the list as a whole.
    std::vector<CStringA> overrides = m_extra_style_overrides;
    if (m_options.style_override != LIBASS_STYLE_OVERRIDE_NO) {
        std::vector<CStringA> user = ParseLibassStyleOverrideString(m_options.style_overrides);
        overrides.insert(overrides.end(), user.begin(), user.end());
    }

    if (overrides.empty()) {
        ass_set_style_overrides(m_ass.get(), NULL);
        return;
    }

    std::vector<const char *> tmp;
    tmp.reserve(overrides.size() + 1);
    for (const auto &override : overrides) {
        tmp.push_back(override.GetString());
    }
    tmp.push_back(NULL);
    ass_set_style_overrides(m_ass.get(), tmp.data());
    if (m_track) {
        ass_process_force_style(m_track.get());
    }
}

bool ASS_Context::LoadASSFile(CString path)
{
    UnloadASS();

    if (path.IsEmpty()) return false;

    m_ass = decltype(m_ass)(ass_library_init());
    if (!m_ass) return false;
    ass_set_message_cb(m_ass.get(), LibassMessageCallback, nullptr);
    m_renderer = decltype(m_renderer)(ass_renderer_init(m_ass.get()));
    if (!m_renderer) return false;

    // Library-level options (font extraction, fonts dir, style overrides) must
    // be set before the file is read, so they apply while parsing.
    ApplyRenderOptions(m_options, nullptr);

    size_t bufsize = 0;
    FILE *fp = _wfopen(path.GetString(), L"rb");
    auto buf = read_file_bytes(fp, &bufsize);
    const char *encoding = detect_bom(buf.get(), bufsize);

    m_track = decltype(m_track)(ass_read_memory(m_ass.get(), buf.get(), bufsize, const_cast<char *>(encoding)));

    if (!m_track) return false;

    ass_set_fonts(m_renderer.get(), NULL, NULL, ASS_FONTPROVIDER_DIRECTWRITE, NULL, 0);
    ApplyRenderOptions(m_options, nullptr);

    m_assloaded = true;
    m_assfontloaded = true;
    return true;
}

bool ASS_Context::LoadASSTrack(char *data, int size)
{
    UnloadASS();

    m_ass = decltype(m_ass)(ass_library_init());
    if (!m_ass) return false;
    ass_set_message_cb(m_ass.get(), LibassMessageCallback, nullptr);
    m_renderer = decltype(m_renderer)(ass_renderer_init(m_ass.get()));
    if (!m_renderer) return false;

    // Library-level options (font extraction, fonts dir, style overrides) must
    // be set before the track data is processed.
    ApplyRenderOptions(m_options, nullptr);

    m_track = decltype(m_track)(ass_new_track(m_ass.get()));

    if (!m_track) return false;

    ass_process_codec_private(m_track.get(), data, size);

    ass_set_fonts(m_renderer.get(), NULL, NULL, ASS_FONTPROVIDER_DIRECTWRITE, NULL, 0);
    ApplyRenderOptions(m_options, nullptr);

    m_assloaded = true;
    return true;
}

void ASS_Context::UnloadASS()
{
    m_assloaded = false;
    m_assfontloaded = false;
    m_styles_file_applied.Empty();
    m_extra_style_overrides.clear();
    if (m_track) m_track.reset();
    if (m_renderer) m_renderer.reset();
    if (m_ass) m_ass.reset();
}

#include <DSMPropertyBag.h>
#include <comdef.h>

void ASS_Context::LoadASSFont(IPin *pPin, IFilterGraph *pGraph)
{
    if (!m_options.use_embedded_fonts) {
        m_assfontloaded = true;
        return;
    }

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
