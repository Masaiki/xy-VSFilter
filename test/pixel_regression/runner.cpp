#include <afxwin.h>
#include <streams.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <wincrypt.h>
#include "flyweight_base_types.h"
#include "RTS.h"
#include "subpixel_position_controler.h"
#include "csri.h"

#pragma comment(lib, "advapi32.lib")

static CStringW Wide(const std::string& value)
{
    return CStringW(CA2W(value.c_str(), CP_UTF8));
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) return 2;
    AfxWinInit(GetModuleHandle(NULL), NULL, GetCommandLine(), 0);
    HCRYPTPROV provider = 0;
    if (!CryptAcquireContext(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return 3;
    HMODULE reference = argc > 4 ? LoadLibraryW(argv[4]) : NULL;
    typedef csri_inst* (*Open)(csri_rend*, const char*, csri_openflag*);
    typedef int (*Format)(csri_inst*, const csri_fmt*);
    typedef void (*Render)(csri_inst*, csri_frame*, double);
    typedef void (*Close)(csri_inst*);
    Open open = reference ? reinterpret_cast<Open>(GetProcAddress(reference, "csri_open_file")) : NULL;
    Format format = reference ? reinterpret_cast<Format>(GetProcAddress(reference, "csri_request_fmt")) : NULL;
    Render render = reference ? reinterpret_cast<Render>(GetProcAddress(reference, "csri_render")) : NULL;
    Close close = reference ? reinterpret_cast<Close>(GetProcAddress(reference, "csri_close")) : NULL;
    if (argc > 4 && (!open || !format || !render || !close)) return 4;
    std::ifstream jobs(argv[1]);
    std::ofstream results(argv[2]);
    std::string line;
    while (std::getline(jobs, line)) {
        std::istringstream fields(line);
        std::string id, path, config, times;
        std::getline(fields, id, '\t');
        std::getline(fields, path, '\t');
        std::getline(fields, config, '\t');
        std::getline(fields, times);
        fprintf(stderr, "BEGIN %s\n", id.c_str());
        fflush(stderr);
        std::vector<CStringW> fonts;
        wchar_t fontDirectory[MAX_PATH], fontSource[32768];
        if (GetEnvironmentVariableW(L"PIXEL_TEST_FONT_DIR", fontDirectory, MAX_PATH)
                && GetEnvironmentVariableW(L"PIXEL_TEST_FONT_SOURCE", fontSource, 32768)
                && Wide(path) == fontSource) {
            WIN32_FIND_DATAW entry;
            CStringW pattern = CStringW(fontDirectory) + L"\\*.ttf";
            HANDLE search = FindFirstFileW(pattern, &entry);
            if (search == INVALID_HANDLE_VALUE) return 8;
            do {
                CStringW filename = CStringW(fontDirectory) + L"\\" + entry.cFileName;
                if (!AddFontResourceExW(filename, FR_PRIVATE, NULL)) return 9;
                fonts.push_back(filename);
            } while (FindNextFileW(search, &entry));
            FindClose(search);
            fprintf(stderr, "Loaded %zu private fonts\n", fonts.size());
        }
        int width, height, mode, level, surface, background;
        std::istringstream(config) >> width >> height >> mode >> level >> surface >> background;
        CCritSec lock;
        CRenderedTextSubtitle subtitle(&lock);
        subtitle.m_render_backend = SUBTITLE_RENDER_BACKEND_VSFILTER;
        subtitle.SetVsFilterCompatibilityMode(static_cast<VsFilterCompatibilityMode>(mode));
        SubpixelPositionControler::GetGlobalControler().SetSubpixelLevel(
            static_cast<SubpixelPositionControler::SUBPIXEL_LEVEL>(level));
        csri_inst* instance = NULL;
        if (reference) {
            instance = open(NULL, path.c_str(), NULL);
            csri_fmt fmt = {CSRI_F_BGR_, static_cast<unsigned>(width), static_cast<unsigned>(height)};
            if (!instance || format(instance, &fmt)) return 5;
        } else if (!subtitle.Open(Wide(path), DEFAULT_CHARSET)) {
            results << id << "\tOPEN_FAILED\n";
            continue;
        }
        const size_t plane = static_cast<size_t>(width) * height;
        std::vector<BYTE> initial(plane * 4), pixels(plane * 4);
        for (size_t i = 0; i < plane; ++i) {
            if (surface == 2) {
                initial[i] = 255;
                initial[plane + i] = background ? 97 : 16;
                initial[2 * plane + i] = 128;
                initial[3 * plane + i] = 128;
            } else {
                initial[4*i] = background ? 37 : 0;
                initial[4*i+1] = background ? 83 : 0;
                initial[4*i+2] = background ? 129 : 0;
                initial[4*i+3] = 255;
            }
        }
        std::istringstream clock(times);
        double time;
        int frame = 0;
        while (clock >> time) {
            pixels = initial;
            SubPicDesc spd;
            spd.type = surface == 0 ? MSP_RGB32 : surface == 1 ? MSP_RGBA : MSP_AYUV_PLANAR;
            spd.w = width; spd.h = height;
            spd.bpp = surface == 2 ? 8 : 32;
            spd.pitch = width * spd.bpp / 8;
            spd.bits = &pixels[0];
            spd.vidrect = CRect(0, 0, width, height);
            CAtlList<CRectCoor2> dirty;
            HRESULT hr = S_OK;
            if (reference) {
                csri_frame target = {};
                target.pixfmt = CSRI_F_BGR_;
                target.planes[0] = &pixels[0]; target.strides[0] = width * 4;
                render(instance, &target, time);
            } else {
                hr = subtitle.RenderEx(spd, static_cast<REFERENCE_TIME>(time * 10000000), 24, dirty);
            }
            HCRYPTHASH hash;
            BYTE digest[32]; DWORD count = sizeof(digest);
            if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) return 6;
            CryptHashData(hash, &pixels[0], static_cast<DWORD>(pixels.size()), 0);
            CryptGetHashParam(hash, HP_HASHVAL, digest, &count, 0);
            CryptDestroyHash(hash);
            const char* hex = "0123456789abcdef";
            results << id << '\t' << frame << '\t' << hr << '\t';
            for (BYTE byte : digest) results << hex[byte >> 4] << hex[byte & 15];
            results << '\t' << (pixels != initial) << '\n';
            if (argc > 3 && wcscmp(argv[3], L"-")) {
                CStringW filename;
                filename.Format(L"%s\\%s_%d.raw", argv[3], Wide(id).GetString(), frame);
                std::ofstream raw(filename.GetString(), std::ios::binary);
                raw.write(reinterpret_cast<const char*>(&pixels[0]), pixels.size());
            }
            ++frame;
        }
        if (instance) close(instance);
        for (const auto& font : fonts) RemoveFontResourceExW(font, FR_PRIVATE, NULL);
        results.flush();
    }
    CryptReleaseContext(provider, 0);
    return jobs.eof() && results.good() ? 0 : 7;
}
