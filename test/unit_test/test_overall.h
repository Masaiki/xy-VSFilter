#ifndef __TEST_OVERALL_09562649_9C15_413C_81EA_6B6EE98E9CBB_H__
#define __TEST_OVERALL_09562649_9C15_413C_81EA_6B6EE98E9CBB_H__

#include <gtest/gtest.h>
#include <tchar.h>
#include <wtypes.h>

bool OpenTestScript( const char *filename );
void OverallTest(float fps = 25, int width=1280, int height=720,
    double start=0, double end=60);
HRESULT RenderFrameToPng(const char *subtitle_file, LPCTSTR image_file,
    double time, int width=1280, int height=720,
    const char *renderer_name=nullptr);

TEST(OverallTest, xxx)
{
    OverallTest();
}


#endif // __TEST_OVERALL_09562649_9C15_413C_81EA_6B6EE98E9CBB_H__