#pragma once

#include <cstddef>

// Binary side data interface shared by MPC-BE and MPC Video Renderer.
interface __declspec(uuid("F940AE7F-48EB-4377-806C-8FC48CAB2292")) IMediaSideData : public IUnknown
{
    STDMETHOD(SetSideData)(GUID guidType, const BYTE *pData, size_t size) PURE;
    STDMETHOD(GetSideData)(GUID guidType, const BYTE **pData, size_t *pSize) PURE;
};

extern const GUID IID_MediaSideDataHDR;
extern const GUID IID_MediaSideDataHDRContentLightLevel;
extern const GUID IID_MediaSideDataHDR10PlusOld;
extern const GUID IID_MediaSideDataHDR10Plus;
extern const GUID IID_MediaSideDataDOVIRPU;
extern const GUID IID_MediaSideDataDOVIMetadata;
extern const GUID IID_MediaSideDataDOVIMetadataV2;

class CBaseAllocator;
class CMediaSample;
interface IMediaSample;

CMediaSample *CreateMediaSampleWithSideData(
    TCHAR *name,
    CBaseAllocator *allocator,
    HRESULT *result,
    LPBYTE buffer,
    LONG length);

HRESULT CopyHDRMediaSideData(IMediaSample *source, IMediaSample *destination);
