#ifndef __TEST_MEDIA_SIDE_DATA_H__
#define __TEST_MEDIA_SIDE_DATA_H__

#include <gtest/gtest.h>
#include "../../src/filters/BaseClasses/streams.h"
#include "../../src/filters/transform/basevideofilter/BaseVideoFilter.h"
#include "../../src/filters/transform/basevideofilter/MediaSideData.h"

namespace {

TEST(MediaSideDataTest, InputAllocatorExposesAndClearsMediaSideData)
{
    HRESULT result = S_OK;
    CBaseVideoInputAllocator *allocator = new CBaseVideoInputAllocator(&result);
    ASSERT_TRUE(allocator != NULL);
    ASSERT_EQ(S_OK, result);

    allocator->AddRef();
    CComPtr<IMemAllocator> allocator_lifetime;
    allocator_lifetime.Attach(allocator);

    ALLOCATOR_PROPERTIES requested = {1, 64, 1, 0};
    ALLOCATOR_PROPERTIES actual = {};
    ASSERT_EQ(S_OK, allocator->SetProperties(&requested, &actual));
    ASSERT_EQ(1, actual.cBuffers);
    ASSERT_EQ(S_OK, allocator->Commit());

    CComPtr<IMediaSample> sample;
    ASSERT_EQ(S_OK, allocator->GetBuffer(&sample, NULL, NULL, 0));

    CComQIPtr<IMediaSideData> side_data(sample);
    ASSERT_TRUE(side_data != NULL);

    const BYTE mastering_data[] = {0x10, 0x20, 0x30, 0x40};
    ASSERT_EQ(S_OK, side_data->SetSideData(
        IID_MediaSideDataHDR, mastering_data, sizeof(mastering_data)));

    const BYTE *read_data = NULL;
    size_t read_size = 0;
    ASSERT_EQ(S_OK, side_data->GetSideData(IID_MediaSideDataHDR, &read_data, &read_size));
    ASSERT_EQ(sizeof(mastering_data), read_size);
    EXPECT_EQ(0, memcmp(mastering_data, read_data, read_size));

    side_data.Release();
    sample.Release();

    ASSERT_EQ(S_OK, allocator->GetBuffer(&sample, NULL, NULL, 0));
    side_data = sample;
    ASSERT_TRUE(side_data != NULL);
    EXPECT_TRUE(FAILED(side_data->GetSideData(IID_MediaSideDataHDR, &read_data, &read_size)));

    side_data.Release();
    sample.Release();
    EXPECT_EQ(S_OK, allocator->Decommit());
}

TEST(MediaSideDataTest, CopiesAllKnownHDRAndDolbyVisionPayloads)
{
    const GUID canonical_guids[] =
    {
        {0x53820dbc, 0xa7b8, 0x49c4, {0xb1, 0x7b, 0xe5, 0x11, 0x59, 0x1a, 0x79, 0x0c}},
        {0xed6ae576, 0x7cbe, 0x41a6, {0x9d, 0xc3, 0x07, 0xc3, 0x5d, 0xc1, 0x3e, 0xf9}},
        {0x183ed511, 0x8910, 0x4262, {0x88, 0xf6, 0x49, 0x46, 0xbc, 0x79, 0x9c, 0x84}},
        {0x6ff47d98, 0x3ef9, 0x4335, {0xa6, 0x5e, 0xe7, 0x18, 0x80, 0xc1, 0x5b, 0xd9}},
        {0xbae40e6c, 0x5b93, 0x4170, {0x90, 0xcc, 0x5d, 0x5f, 0x02, 0xa2, 0x96, 0x38}},
        {0x277ee779, 0x13f4, 0x434e, {0xbd, 0xec, 0x3d, 0x6f, 0x8c, 0x0e, 0x15, 0xd2}},
        {0xf1949f1c, 0x7474, 0x4f0f, {0xb1, 0x10, 0xfb, 0x73, 0x4c, 0x08, 0x85, 0x1d}},
    };
    const GUID * const side_data_guids[] =
    {
        &IID_MediaSideDataHDR,
        &IID_MediaSideDataHDRContentLightLevel,
        &IID_MediaSideDataHDR10PlusOld,
        &IID_MediaSideDataHDR10Plus,
        &IID_MediaSideDataDOVIRPU,
        &IID_MediaSideDataDOVIMetadata,
        &IID_MediaSideDataDOVIMetadataV2,
    };
    C_ASSERT(_countof(canonical_guids) == _countof(side_data_guids));

    HRESULT result = S_OK;
    CBaseVideoInputAllocator *allocator = new CBaseVideoInputAllocator(&result);
    ASSERT_TRUE(allocator != NULL);
    ASSERT_EQ(S_OK, result);

    allocator->AddRef();
    CComPtr<IMemAllocator> allocator_lifetime;
    allocator_lifetime.Attach(allocator);

    ALLOCATOR_PROPERTIES requested = {2, 64, 1, 0};
    ALLOCATOR_PROPERTIES actual = {};
    ASSERT_EQ(S_OK, allocator->SetProperties(&requested, &actual));
    ASSERT_EQ(S_OK, allocator->Commit());

    CComPtr<IMediaSample> source;
    CComPtr<IMediaSample> destination;
    ASSERT_EQ(S_OK, allocator->GetBuffer(&source, NULL, NULL, 0));
    ASSERT_EQ(S_OK, allocator->GetBuffer(&destination, NULL, NULL, 0));

    CComQIPtr<IMediaSideData> source_side_data(source);
    CComQIPtr<IMediaSideData> destination_side_data(destination);
    ASSERT_TRUE(source_side_data != NULL);
    ASSERT_TRUE(destination_side_data != NULL);

    BYTE payloads[_countof(side_data_guids)][8] = {};
    for(size_t i = 0; i < _countof(side_data_guids); i++)
    {
        EXPECT_TRUE(IsEqualGUID(canonical_guids[i], *side_data_guids[i]) != FALSE);
        for(size_t j = 0; j <= i; j++)
            payloads[i][j] = (BYTE)(0x10 * (i + 1) + j);

        ASSERT_EQ(S_OK, source_side_data->SetSideData(
            *side_data_guids[i], payloads[i], i + 1));
    }

    ASSERT_EQ(S_OK, CopyHDRMediaSideData(source, destination));

    for(size_t i = 0; i < _countof(side_data_guids); i++)
    {
        const BYTE *copied_data = NULL;
        size_t copied_size = 0;
        ASSERT_EQ(S_OK, destination_side_data->GetSideData(
            *side_data_guids[i], &copied_data, &copied_size));
        ASSERT_EQ(i + 1, copied_size);
        EXPECT_EQ(0, memcmp(payloads[i], copied_data, copied_size));
    }

    destination_side_data.Release();
    source_side_data.Release();
    destination.Release();
    source.Release();
    EXPECT_EQ(S_OK, allocator->Decommit());
}

} // namespace

#endif // __TEST_MEDIA_SIDE_DATA_H__
