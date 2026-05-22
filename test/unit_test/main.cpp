#include <afx.h>
#include <gtest/gtest.h>
#include <iostream>
#include <string>

#define XY_UNIT_TEST
//#include "test_interlaced_uv_alphablend.h"
//#include "test_subsample_and_interlace.h"
//#include "test_alphablend.h"
//#include "test_instrinsics_macro.h"

//#include "test_xy_filter.h"
//#include "xy_filter_benchmark.h"
#include "test_overall.h"

static bool WideToUtf8(const wchar_t *src, std::string& dst)
{
    int size = WideCharToMultiByte(CP_UTF8, 0, src, -1, NULL, 0, NULL, NULL);
    if (size <= 0) {
        return false;
    }

    dst.resize(size - 1);
    return WideCharToMultiByte(CP_UTF8, 0, src, -1, &dst[0], size, NULL, NULL) > 0;
}

static void PrintUsage(const wchar_t *program)
{
    std::wcout
        << L"usage:\n"
        << L"  " << program << L" script_name\n"
        << L"  " << program << L" --render-frame image.png script_name time_seconds [width height] [--renderer libass|vsfilter]\n";
}

int wmain(int argc, wchar_t ** argv)
{
    if (argc >= 2 && wcscmp(argv[1], L"--render-frame") == 0) {
        if (argc < 5) {
            PrintUsage(argv[0]);
            return -1;
        }

        int width = 1280;
        int height = 720;
        int arg = 5;
        if (arg < argc && wcscmp(argv[arg], L"--renderer") != 0) {
            if (arg + 1 >= argc) {
                PrintUsage(argv[0]);
                return -1;
            }
            width = _wtoi(argv[arg]);
            height = _wtoi(argv[arg + 1]);
            if (width <= 0 || height <= 0) {
                std::wcerr << L"width and height must be positive" << std::endl;
                return -1;
            }
            arg += 2;
        }

        std::string renderer_name;
        if (arg < argc) {
            if (arg + 1 >= argc || wcscmp(argv[arg], L"--renderer") != 0) {
                PrintUsage(argv[0]);
                return -1;
            }
            if (!WideToUtf8(argv[arg + 1], renderer_name)) {
                std::wcerr << L"failed to convert renderer name to UTF-8" << std::endl;
                return -1;
            }
            arg += 2;
        }
        if (arg != argc) {
            PrintUsage(argv[0]);
            return -1;
        }

        std::string subtitle_file;
        if (!WideToUtf8(argv[3], subtitle_file)) {
            std::wcerr << L"failed to convert subtitle path to UTF-8" << std::endl;
            return -1;
        }

        double time = wcstod(argv[4], NULL);
        HRESULT hr = RenderFrameToPng(subtitle_file.c_str(), argv[2], time, width, height,
            renderer_name.empty() ? nullptr : renderer_name.c_str());
        if (FAILED(hr)) {
            std::wcerr << L"failed to render frame: 0x" << std::hex << hr << std::endl;
            return -1;
        }

        return 0;
    }

    if (argc!=2)
    {
        PrintUsage(argv[0]);
        return -1;
    }

    std::string script_name;
    if (!WideToUtf8(argv[1], script_name) || !OpenTestScript(script_name.c_str())) {
        std::wcerr << L"failed to open script: " << argv[1] << std::endl;
        return -1;
    }

    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
