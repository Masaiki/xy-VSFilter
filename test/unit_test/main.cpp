#include <afx.h>
#include <gtest/gtest.h>
#include <iostream>

#define XY_UNIT_TEST
//#include "test_interlaced_uv_alphablend.h"
//#include "test_subsample_and_interlace.h"
//#include "test_alphablend.h"
//#include "test_instrinsics_macro.h"

//#include "test_xy_filter.h"
//#include "xy_filter_benchmark.h"
#include "test_color_conv_table.h"
#include "test_font_fallback.h"
#include "test_media_side_data.h"
#include "test_video_info2_color_info.h"
#include "test_overall.h"

#ifdef DEBUG
extern bool g_fUseKASSERT;
#endif

int wmain(int argc, wchar_t ** argv)
{
    if (GetEnvironmentVariableW(L"XY_TEST_NONINTERACTIVE", NULL, 0)) {
        SetErrorMode(SEM_FAILCRITICALERRORS |
                     SEM_NOGPFAULTERRORBOX |
                     SEM_NOOPENFILEERRORBOX);
#ifdef DEBUG
        g_fUseKASSERT = true;
#endif
    }

    if (argc!=2)
    {
        std::wcout<<argv[0]<<L" script_name"<<std::endl;
        return -1;
    }
    char namebuf[256];
    WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, namebuf, sizeof(namebuf)/sizeof(char), NULL, NULL);
    OpenTestScript(namebuf);

    testing::InitGoogleTest(&argc, argv);

    const int result = RUN_ALL_TESTS();
    CloseTestScript();
    return result;
}
