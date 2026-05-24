#define CSRIAPI extern "C" __declspec(dllimport)
#include "csri.h"
#include <windows.h>
#include <gdiplus.h>
#include <stdint.h>
#include <vector>

using namespace std;

static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
    UINT num = 0;
    UINT size = 0;

    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    vector<unsigned char> codecInfoBuffer(size);
    Gdiplus::ImageCodecInfo* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)codecInfoBuffer.data();

    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            return j;
        }
    }
    return -1;
}

csri_inst * g_csri_inst_yyy = NULL;

bool OpenTestScript( const char *filename )
{
    csri_rend * csri_rend_xxx = csri_renderer_default();

    g_csri_inst_yyy = csri_open_file(csri_rend_xxx, filename, NULL);
    return g_csri_inst_yyy != NULL;
}

void OverallTest( float fps /*= 25*/, int width/*=1280*/, int height/*=720*/, double start/*=0*/, double end/*=60*/ )
{
    csri_fmt fmt = { CSRI_F_BGR_, (unsigned)width, (unsigned)height };

    csri_request_fmt(g_csri_inst_yyy, &fmt);

    vector<unsigned char> buf( 4*(width+15)*(height+1)+256 );
    csri_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.pixfmt = CSRI_F_BGR_;
	frame.planes[0] = &buf[0]+16;
    frame.planes[0] = (unsigned char*)((uintptr_t)(frame.planes[0]) & ~((uintptr_t)15));
    frame.strides[0] = ((width*4+15)&~15);

    for (double time=start;time<end;time+=1/fps)
    {
        csri_render(g_csri_inst_yyy, &frame, time);
    }
    csri_close(g_csri_inst_yyy);
    g_csri_inst_yyy = NULL;
}

HRESULT RenderFrameToPng(const char *subtitle_file, LPCTSTR image_file,
    double time, int width, int height, const char *renderer_name)
{
    if (width <= 0 || height <= 0) {
        return E_INVALIDARG;
    }

    csri_rend *renderer = renderer_name && *renderer_name
        ? csri_renderer_byname(renderer_name, NULL)
        : csri_renderer_default();
    if (!renderer) {
        return E_INVALIDARG;
    }

    csri_inst *inst = csri_open_file(renderer, subtitle_file, NULL);
    if (!inst) {
        return E_FAIL;
    }

    csri_fmt fmt = { CSRI_F_BGRA, (unsigned)width, (unsigned)height };
    if (csri_request_fmt(inst, &fmt) != 0) {
        csri_close(inst);
        return E_FAIL;
    }

    const int stride = ((width * 4 + 15) & ~15);
    vector<unsigned char> buf(stride * height);

    csri_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.pixfmt = CSRI_F_BGRA;
    frame.planes[0] = &buf[0];
    frame.strides[0] = stride;
    csri_render(inst, &frame, time);

    {
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        ULONG_PTR gdiplusToken;
        Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

        HRESULT hr = E_FAIL;
        {
            Gdiplus::Bitmap bmp(width, height, stride, PixelFormat32bppPARGB, &buf[0]);
            CLSID pngClsid;
            if (GetEncoderClsid(L"image/png", &pngClsid) >= 0) {
                Gdiplus::Status status = bmp.Save(image_file, &pngClsid, NULL);
                hr = (status == Gdiplus::Ok) ? S_OK : E_FAIL;
            }
        }

        Gdiplus::GdiplusShutdown(gdiplusToken);
        csri_close(inst);
        return hr;
    }
}
