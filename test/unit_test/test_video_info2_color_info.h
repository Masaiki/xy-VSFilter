#ifndef __TEST_VIDEO_INFO2_COLOR_INFO_H__
#define __TEST_VIDEO_INFO2_COLOR_INFO_H__

#include <gtest/gtest.h>
#include "../../src/filters/BaseClasses/streams.h"
#include <dvdmedia.h>
#include "../../src/filters/transform/BaseVideoFilter/BaseVideoFilter.h"

namespace {

class TestVideoSourcePin : public CBaseOutputPin
{
public:
    TestVideoSourcePin(CBaseFilter *filter, CCritSec *lock, HRESULT *result,
                       const CMediaType& media_type)
        : CBaseOutputPin(NAME("TestVideoSourcePin"), filter, lock, result, L"Output")
        , m_media_type(media_type)
    {
    }

    HRESULT CheckMediaType(const CMediaType *media_type)
    {
        // The source accepts the negotiated output formats so tests can
        // exercise metadata handling across subtype changes.
        return media_type && media_type->majortype == MEDIATYPE_Video
            ? S_OK : S_FALSE;
    }

    HRESULT GetMediaType(int position, CMediaType *media_type)
    {
        if(position < 0) return E_INVALIDARG;
        if(position > 0) return VFW_S_NO_MORE_ITEMS;
        *media_type = m_media_type;
        return S_OK;
    }

    HRESULT DecideBufferSize(IMemAllocator *, ALLOCATOR_PROPERTIES *)
    {
        return E_NOTIMPL;
    }

private:
    CMediaType m_media_type;
};

class TestVideoSourceFilter : public CBaseFilter
{
public:
    TestVideoSourceFilter(const CMediaType& media_type, HRESULT *result)
        : CBaseFilter(NAME("TestVideoSourceFilter"), NULL, &m_lock, CLSID_NULL)
        , m_pin(this, &m_lock, result, media_type)
    {
    }

    int GetPinCount() { return 1; }
    CBasePin *GetPin(int position) { return position == 0 ? &m_pin : NULL; }

private:
    CCritSec m_lock;
    TestVideoSourcePin m_pin;
};

class TestBaseVideoFilter : public CBaseVideoFilter
{
public:
    explicit TestBaseVideoFilter(HRESULT *result)
        : CBaseVideoFilter(NAME("TestBaseVideoFilter"), NULL, result, CLSID_NULL)
    {
    }

protected:
    HRESULT Transform(IMediaSample *) { return E_NOTIMPL; }
};

CMediaType MakeP010VideoType(DWORD control_flags)
{
    static const GUID p010 = {
        0x30313050, 0x0000, 0x0010,
        {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}
    };

    CMediaType media_type;
    media_type.SetType(&MEDIATYPE_Video);
    media_type.SetSubtype(&p010);
    media_type.SetFormatType(&FORMAT_VideoInfo2);
    media_type.SetTemporalCompression(FALSE);

    VIDEOINFOHEADER2 *video_info =
        (VIDEOINFOHEADER2 *)media_type.AllocFormatBuffer(sizeof(VIDEOINFOHEADER2));
    ZeroMemory(video_info, sizeof(*video_info));
    video_info->AvgTimePerFrame = 416667;
    video_info->dwPictAspectRatioX = 16;
    video_info->dwPictAspectRatioY = 9;
    video_info->dwControlFlags = control_flags;
    video_info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    video_info->bmiHeader.biWidth = 1280;
    video_info->bmiHeader.biHeight = 720;
    video_info->bmiHeader.biPlanes = 2;
    video_info->bmiHeader.biBitCount = 24;
    video_info->bmiHeader.biCompression = 0x30313050;
    video_info->bmiHeader.biSizeImage = 1280 * 720 * 3;
    media_type.SetSampleSize(video_info->bmiHeader.biSizeImage);
    return media_type;
}

TEST(BaseVideoFilterTest, PreservesVideoInfo2ControlFlagsForP010Output)
{
    static const DWORD control_flags = 0xABCDEF00 | AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT;
    const CMediaType input_type = MakeP010VideoType(control_flags);

    HRESULT result = S_OK;
    TestBaseVideoFilter *filter = new TestBaseVideoFilter(&result);
    ASSERT_TRUE(filter != NULL);
    ASSERT_EQ(S_OK, result);
    CComPtr<IBaseFilter> filter_lifetime = filter;

    TestVideoSourceFilter *source = new TestVideoSourceFilter(input_type, &result);
    ASSERT_TRUE(source != NULL);
    ASSERT_EQ(S_OK, result);
    CComPtr<IBaseFilter> source_lifetime = source;

    IPin *input = filter->GetPin(0);
    ASSERT_TRUE(input != NULL);
    ASSERT_EQ(S_OK, input->ReceiveConnection(source->GetPin(0), &input_type));

    CMediaType output_type;
    ASSERT_EQ(S_OK, filter->GetMediaType(0, &output_type));
    ASSERT_EQ(FORMAT_VideoInfo2, output_type.formattype);
    ASSERT_GE(output_type.FormatLength(), sizeof(VIDEOINFOHEADER2));
    EXPECT_EQ(control_flags, ((VIDEOINFOHEADER2 *)output_type.Format())->dwControlFlags);

    EXPECT_EQ(S_OK, input->Disconnect());
}

TEST(BaseVideoFilterTest, IgnoresIncompleteVideoInfo2ColorFlags)
{
    const DWORD color_info = 0xABCDEF00 | AMCONTROL_COLORINFO_PRESENT;
    const CMediaType input_type = MakeP010VideoType(color_info);

    HRESULT result = S_OK;
    TestBaseVideoFilter *filter = new TestBaseVideoFilter(&result);
    ASSERT_EQ(S_OK, result);
    CComPtr<IBaseFilter> filter_lifetime = filter;

    TestVideoSourceFilter *source = new TestVideoSourceFilter(input_type, &result);
    ASSERT_EQ(S_OK, result);
    CComPtr<IBaseFilter> source_lifetime = source;

    IPin *input = filter->GetPin(0);
    ASSERT_EQ(S_OK, input->ReceiveConnection(source->GetPin(0), &input_type));

    CMediaType output_type;
    ASSERT_EQ(S_OK, filter->GetMediaType(0, &output_type));
    EXPECT_EQ(0u, ((VIDEOINFOHEADER2 *)output_type.Format())->dwControlFlags);
    EXPECT_EQ(S_OK, input->Disconnect());
}

TEST(BaseVideoFilterTest, PreservesValidColorInfoForSameSubtype)
{
    const DWORD control_flags = 0xABCDEF00 | AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT;
    const CMediaType input_type = MakeP010VideoType(control_flags);

    HRESULT result = S_OK;
    TestBaseVideoFilter *filter = new TestBaseVideoFilter(&result);
    ASSERT_EQ(S_OK, result);
    CComPtr<IBaseFilter> filter_lifetime = filter;

    TestVideoSourceFilter *source = new TestVideoSourceFilter(input_type, &result);
    ASSERT_EQ(S_OK, result);
    CComPtr<IBaseFilter> source_lifetime = source;

    IPin *input = filter->GetPin(0);
    ASSERT_EQ(S_OK, input->ReceiveConnection(source->GetPin(0), &input_type));

    CMediaType output_type;
    ASSERT_EQ(S_OK, filter->GetMediaType(0, &output_type));
    EXPECT_EQ(control_flags, ((VIDEOINFOHEADER2 *)output_type.Format())->dwControlFlags);
    EXPECT_EQ(S_OK, input->Disconnect());
}

TEST(BaseVideoFilterTest, PreservesPaddingFlagsWithoutColorInfo)
{
    const DWORD control_flags = AMCONTROL_USED | AMCONTROL_PAD_TO_16x9;
    const CMediaType input_type = MakeP010VideoType(control_flags);

    HRESULT result = S_OK;
    TestBaseVideoFilter *filter = new TestBaseVideoFilter(&result);
    ASSERT_EQ(S_OK, result);
    CComPtr<IBaseFilter> filter_lifetime = filter;
    TestVideoSourceFilter *source = new TestVideoSourceFilter(input_type, &result);
    ASSERT_EQ(S_OK, result);
    CComPtr<IBaseFilter> source_lifetime = source;

    IPin *input = filter->GetPin(0);
    ASSERT_EQ(S_OK, input->ReceiveConnection(source->GetPin(0), &input_type));
    CMediaType output_type;
    ASSERT_EQ(S_OK, filter->GetMediaType(0, &output_type));
    EXPECT_EQ(control_flags, ((VIDEOINFOHEADER2 *)output_type.Format())->dwControlFlags);
    EXPECT_EQ(S_OK, input->Disconnect());
}

}

#endif // __TEST_VIDEO_INFO2_COLOR_INFO_H__
