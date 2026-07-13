#include "StdAfx.h"
#include "MediaSideData.h"

#include <map>
#include <new>
#include <stdexcept>
#include <vector>

const GUID IID_MediaSideDataHDR =
    {0x53820dbc, 0xa7b8, 0x49c4, {0xb1, 0x7b, 0xe5, 0x11, 0x59, 0x1a, 0x79, 0x0c}};
const GUID IID_MediaSideDataHDRContentLightLevel =
    {0xed6ae576, 0x7cbe, 0x41a6, {0x9d, 0xc3, 0x07, 0xc3, 0x5d, 0xc1, 0x3e, 0xf9}};
const GUID IID_MediaSideDataHDR10PlusOld =
    {0x183ed511, 0x8910, 0x4262, {0x88, 0xf6, 0x49, 0x46, 0xbc, 0x79, 0x9c, 0x84}};
const GUID IID_MediaSideDataHDR10Plus =
    {0x6ff47d98, 0x3ef9, 0x4335, {0xa6, 0x5e, 0xe7, 0x18, 0x80, 0xc1, 0x5b, 0xd9}};
const GUID IID_MediaSideDataDOVIRPU =
    {0xbae40e6c, 0x5b93, 0x4170, {0x90, 0xcc, 0x5d, 0x5f, 0x02, 0xa2, 0x96, 0x38}};
const GUID IID_MediaSideDataDOVIMetadata =
    {0x277ee779, 0x13f4, 0x434e, {0xbd, 0xec, 0x3d, 0x6f, 0x8c, 0x0e, 0x15, 0xd2}};
const GUID IID_MediaSideDataDOVIMetadataV2 =
    {0xf1949f1c, 0x7474, 0x4f0f, {0xb1, 0x10, 0xfb, 0x73, 0x4c, 0x08, 0x85, 0x1d}};

namespace {

struct GUIDComparer
{
    bool operator()(const GUID& left, const GUID& right) const
    {
        return memcmp(&left, &right, sizeof(GUID)) < 0;
    }
};

class CMediaSampleWithSideData : public CMediaSample, public IMediaSideData
{
public:
    CMediaSampleWithSideData(
        TCHAR *name,
        CBaseAllocator *allocator,
        HRESULT *result,
        LPBYTE buffer,
        LONG length)
        : CMediaSample(name, allocator, result, buffer, length)
    {
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **object)
    {
        CheckPointer(object, E_POINTER);

        if(riid == __uuidof(IMediaSideData))
            return GetInterface(static_cast<IMediaSideData *>(this), object);

        return CMediaSample::QueryInterface(riid, object);
    }

    STDMETHODIMP_(ULONG) AddRef()
    {
        return CMediaSample::AddRef();
    }

    STDMETHODIMP_(ULONG) Release()
    {
        if(m_cRef == 1)
        {
            CAutoLock lock(&m_side_data_lock);
            m_side_data.clear();
        }

        return CMediaSample::Release();
    }

    STDMETHODIMP SetSideData(GUID guidType, const BYTE *data, size_t size)
    {
        if(!data || !size)
            return E_POINTER;

        CAutoLock lock(&m_side_data_lock);
        try
        {
            m_side_data[guidType].assign(data, data + size);
        }
        catch(const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
        catch(const std::length_error&)
        {
            return E_OUTOFMEMORY;
        }

        return S_OK;
    }

    STDMETHODIMP GetSideData(GUID guidType, const BYTE **data, size_t *size)
    {
        if(!data || !size)
            return E_POINTER;

        CAutoLock lock(&m_side_data_lock);
        SideDataMap::const_iterator entry = m_side_data.find(guidType);
        if(entry == m_side_data.end() || entry->second.empty())
            return E_FAIL;

        *data = &entry->second[0];
        *size = entry->second.size();
        return S_OK;
    }

private:
    typedef std::map<GUID, std::vector<BYTE>, GUIDComparer> SideDataMap;

    CCritSec m_side_data_lock;
    SideDataMap m_side_data;
};

const GUID * const g_hdr_side_data_types[] =
{
    // Keep both generations used by released MPC components. Side data is opaque here.
    &IID_MediaSideDataHDR,
    &IID_MediaSideDataHDRContentLightLevel,
    &IID_MediaSideDataHDR10PlusOld,
    &IID_MediaSideDataHDR10Plus,
    &IID_MediaSideDataDOVIRPU,
    &IID_MediaSideDataDOVIMetadata,
    &IID_MediaSideDataDOVIMetadataV2,
};

} // namespace

CMediaSample *CreateMediaSampleWithSideData(
    TCHAR *name,
    CBaseAllocator *allocator,
    HRESULT *result,
    LPBYTE buffer,
    LONG length)
{
    return new(std::nothrow) CMediaSampleWithSideData(name, allocator, result, buffer, length);
}

HRESULT CopyHDRMediaSideData(IMediaSample *source, IMediaSample *destination)
{
    CheckPointer(source, E_POINTER);
    CheckPointer(destination, E_POINTER);

    CComQIPtr<IMediaSideData> source_side_data(source);
    CComQIPtr<IMediaSideData> destination_side_data(destination);
    if(!source_side_data || !destination_side_data)
        return S_FALSE;

    HRESULT result = S_FALSE;
    for(size_t i = 0; i < _countof(g_hdr_side_data_types); i++)
    {
        const BYTE *data = NULL;
        size_t size = 0;
        if(SUCCEEDED(source_side_data->GetSideData(*g_hdr_side_data_types[i], &data, &size))
        && data
        && size)
        {
            const HRESULT copy_result = destination_side_data->SetSideData(
                *g_hdr_side_data_types[i], data, size);
            if(FAILED(copy_result))
                return copy_result;

            result = S_OK;
        }
    }

    return result;
}
