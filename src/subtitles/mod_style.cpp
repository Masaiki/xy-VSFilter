#include "stdafx.h"
#include "mod_style.h"
#include "xy_bitmap.h"

#include <algorithm>
#include <list>
#include <set>
#include <Shlwapi.h>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")

namespace
{
void HashBytes(size_t& hash, const void* data, size_t size)
{
    const BYTE* bytes = static_cast<const BYTE*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= static_cast<size_t>(1099511628211ull);
    }
}

template<class T>
void HashValue(size_t& hash, const T& value)
{
    HashBytes(hash, &value, sizeof(value));
}

void HashString(size_t& hash, const CStringW& value)
{
    HashBytes(hash, value.GetString(), static_cast<size_t>(value.GetLength()) * sizeof(wchar_t));
}

CStringW NormalizeResourceKey(CStringW value)
{
    value.Trim();
    value.Replace(L'/', L'\\');
    value.MakeLower();
    return value;
}

CStringW GetDirectoryName(CStringW path)
{
    path.Replace(L'/', L'\\');
    const int slash = path.ReverseFind(L'\\');
    return slash >= 0 ? path.Left(slash + 1) : CStringW();
}

CStringW CombinePath(const CStringW& directory, const CStringW& name)
{
    if (directory.IsEmpty() || !PathIsRelativeW(name)) {
        return name;
    }

    CStringW result(directory);
    if (!result.IsEmpty() && result[result.GetLength() - 1] != L'\\') {
        result += L'\\';
    }
    result += name;
    return result;
}

CStringW GetFullPath(const CStringW& path)
{
    if (path.IsEmpty()) {
        return path;
    }

    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetFullPathNameW(path, static_cast<DWORD>(buffer.size()), &buffer[0], NULL);
    if (length == 0 || length >= buffer.size()) {
        return path;
    }
    return CStringW(&buffer[0], static_cast<int>(length));
}

class ComInitialization
{
    bool m_uninitialize;

public:
    ComInitialization()
        : m_uninitialize(false)
    {
        const HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        m_uninitialize = SUCCEEDED(hr);
    }

    ~ComInitialization()
    {
        if (m_uninitialize) {
            CoUninitialize();
        }
    }
};

SharedPtrConstModImageResource DecodeWicImage(IWICImagingFactory* factory,
    IWICBitmapDecoder* decoder, const CStringW& resource_id)
{
    if (!factory || !decoder) {
        return SharedPtrConstModImageResource();
    }

    GUID container_format = GUID_NULL;
    if (FAILED(decoder->GetContainerFormat(&container_format))
            || container_format != GUID_ContainerFormatPng) {
        return SharedPtrConstModImageResource();
    }

    CComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) {
        return SharedPtrConstModImageResource();
    }

    UINT width = 0;
    UINT height = 0;
    if (FAILED(frame->GetSize(&width, &height))
            || width == 0 || height == 0
            || width > 16384 || height > 16384) {
        return SharedPtrConstModImageResource();
    }

    const uint64_t byte_count = static_cast<uint64_t>(width) * height * 4;
    if (byte_count > 128ull * 1024 * 1024 || byte_count > UINT_MAX) {
        return SharedPtrConstModImageResource();
    }

    CComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) || !converter
            || FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom))) {
        return SharedPtrConstModImageResource();
    }

    std::vector<BYTE> pixels(static_cast<size_t>(byte_count));
    const UINT stride = width * 4;
    if (FAILED(converter->CopyPixels(NULL, stride, static_cast<UINT>(byte_count), &pixels[0]))) {
        return SharedPtrConstModImageResource();
    }

    return SharedPtrConstModImageResource(DEBUG_NEW ModImageResource(
        static_cast<int>(width), static_cast<int>(height), &pixels[0], pixels.size(), resource_id));
}

SharedPtrConstModImageResource DecodeWicFile(const CStringW& path)
{
    ComInitialization com;
    CComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))) || !factory) {
        return SharedPtrConstModImageResource();
    }

    CComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path, NULL, GENERIC_READ,
            WICDecodeMetadataCacheOnLoad, &decoder))) {
        return SharedPtrConstModImageResource();
    }
    return DecodeWicImage(factory, decoder, path);
}

SharedPtrConstModImageResource DecodeWicMemory(const BYTE* data, size_t byte_count,
    const CStringW& resource_id)
{
    if (!data || byte_count == 0 || byte_count > UINT_MAX) {
        return SharedPtrConstModImageResource();
    }

    ComInitialization com;
    CComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))) || !factory) {
        return SharedPtrConstModImageResource();
    }

    CComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) || !stream
            || FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data),
                static_cast<DWORD>(byte_count)))) {
        return SharedPtrConstModImageResource();
    }

    CComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream, NULL,
            WICDecodeMetadataCacheOnLoad, &decoder))) {
        return SharedPtrConstModImageResource();
    }
    return DecodeWicImage(factory, decoder, resource_id);
}

BYTE Div255(uint32_t value)
{
    return static_cast<BYTE>((value + 1 + ((value + 1) >> 8)) >> 8);
}

int WrapCoordinate(int value, int size)
{
    if (size <= 0) {
        return 0;
    }
    value %= size;
    return value < 0 ? value + size : value;
}

DWORD ReverseColor(DWORD color)
{
    return ((color & 0x0000ff) << 16)
        | (color & 0x00ff00)
        | ((color & 0xff0000) >> 16);
}

BYTE InterpolateByte(BYTE bottom_left, BYTE bottom_right, BYTE top_left, BYTE top_right,
    double x, double y)
{
    const double value =
        bottom_left * (1.0 - x) * y
        + bottom_right * x * y
        + top_left * (1.0 - y) * (1.0 - x)
        + top_right * x * (1.0 - y);
    return static_cast<BYTE>(static_cast<int>(value) & 0xff);
}

bool SameImage(const SharedPtrConstModImageResource& lhs,
    const SharedPtrConstModImageResource& rhs)
{
    if (lhs.get() == rhs.get()) {
        return true;
    }
    return lhs && rhs
        && lhs->GetWidth() == rhs->GetWidth()
        && lhs->GetHeight() == rhs->GetHeight()
        && lhs->GetHash() == rhs->GetHash()
        && lhs->GetResourceId() == rhs->GetResourceId();
}
}

ModImageResource::ModImageResource(int width, int height, const BYTE* rgba,
    size_t byte_count, const CStringW& resource_id)
    : m_width(width)
    , m_height(height)
    , m_pixels(rgba, rgba + byte_count)
    , m_hash(static_cast<size_t>(1469598103934665603ull))
    , m_resource_id(resource_id)
{
    HashValue(m_hash, m_width);
    HashValue(m_hash, m_height);
    if (!m_pixels.empty()) {
        HashBytes(m_hash, &m_pixels[0], m_pixels.size());
    }
}

struct ModImageCache::Impl
{
    struct Embedded
    {
        CStringW id;
        CStringW key;
        std::vector<BYTE> data;
    };

    struct Decoded
    {
        CStringW key;
        SharedPtrConstModImageResource image;
        size_t byte_count;
    };

    std::list<Embedded> embedded;
    std::list<Decoded> decoded;
    size_t decoded_bytes;

    Impl()
        : decoded_bytes(0)
    {
    }

    void Trim()
    {
        while (!decoded.empty()
                && (decoded.size() > 64 || decoded_bytes > 128u * 1024 * 1024)) {
            decoded_bytes -= decoded.back().byte_count;
            decoded.pop_back();
        }
    }
};

ModImageCache::ModImageCache()
    : m_impl(NULL)
{
}

ModImageCache::ModImageCache(const ModImageCache& rhs)
    : m_impl(rhs.m_impl ? DEBUG_NEW Impl(*rhs.m_impl) : NULL)
{
}

ModImageCache::~ModImageCache()
{
    delete m_impl;
}

ModImageCache& ModImageCache::operator=(const ModImageCache& rhs)
{
    if (this != &rhs) {
        Impl* replacement = rhs.m_impl ? DEBUG_NEW Impl(*rhs.m_impl) : NULL;
        delete m_impl;
        m_impl = replacement;
    }
    return *this;
}

void ModImageCache::RegisterEmbedded(const CStringW& resource_id, const BYTE* data,
    size_t byte_count)
{
    if (!data || byte_count == 0) {
        return;
    }
    if (!m_impl) {
        m_impl = DEBUG_NEW Impl();
        if (!m_impl) {
            return;
        }
    }

    const CStringW key = NormalizeResourceKey(resource_id);
    for (auto it = m_impl->embedded.begin(); it != m_impl->embedded.end(); ++it) {
        if (it->key == key) {
            it->id = resource_id;
            it->data.assign(data, data + byte_count);
            ResetDecoded();
            return;
        }
    }

    Impl::Embedded embedded;
    embedded.id = resource_id;
    embedded.key = key;
    embedded.data.assign(data, data + byte_count);
    m_impl->embedded.push_back(embedded);
}

SharedPtrConstModImageResource ModImageCache::Resolve(const CStringW& resource_id,
    const CStringW& subtitle_path, const CStringW& resource_path)
{
    if (!m_impl) {
        m_impl = DEBUG_NEW Impl();
        if (!m_impl) {
            return SharedPtrConstModImageResource();
        }
    }

    const CStringW subtitle_directory = GetDirectoryName(subtitle_path);
    CStringW request_key = NormalizeResourceKey(resource_id);
    request_key += L"|";
    request_key += NormalizeResourceKey(subtitle_directory);
    request_key += L"|";
    request_key += NormalizeResourceKey(resource_path);

    for (auto it = m_impl->decoded.begin(); it != m_impl->decoded.end(); ++it) {
        if (it->key == request_key) {
            SharedPtrConstModImageResource result = it->image;
            m_impl->decoded.splice(m_impl->decoded.begin(), m_impl->decoded, it);
            return result;
        }
    }

    SharedPtrConstModImageResource image;
    const CStringW embedded_key = NormalizeResourceKey(resource_id);
    for (const auto& embedded : m_impl->embedded) {
        if (embedded.key == embedded_key) {
            image = DecodeWicMemory(&embedded.data[0], embedded.data.size(), embedded.id);
            break;
        }
    }

    if (!image) {
        std::vector<CStringW> candidates;
        candidates.push_back(resource_id);
        if (!resource_path.IsEmpty()) {
            candidates.push_back(CombinePath(resource_path, resource_id));
        }
        if (!subtitle_directory.IsEmpty()) {
            candidates.push_back(CombinePath(subtitle_directory, resource_id));
        }

        std::set<std::wstring> tried;
        for (const CStringW& candidate : candidates) {
            CStringW full_path = GetFullPath(candidate);
            CStringW normalized = NormalizeResourceKey(full_path);
            if (!tried.insert(std::wstring(normalized.GetString(),
                    normalized.GetLength())).second) {
                continue;
            }
            image = DecodeWicFile(full_path);
            if (image) {
                break;
            }
        }
    }

    Impl::Decoded decoded;
    decoded.key = request_key;
    decoded.image = image;
    decoded.byte_count = image ? image->GetByteCount() : 0;
    m_impl->decoded.push_front(decoded);
    m_impl->decoded_bytes += decoded.byte_count;
    m_impl->Trim();
    return image;
}

void ModImageCache::ResetDecoded()
{
    if (!m_impl) {
        return;
    }
    m_impl->decoded.clear();
    m_impl->decoded_bytes = 0;
}

void ModImageCache::Clear()
{
    delete m_impl;
    m_impl = NULL;
}

ModPaintLayerState::ModPaintLayerState()
    : mode(MOD_PAINT_SOLID)
    , image_x_offset(0)
    , image_y_offset(0)
{
    ZeroMemory(colors, sizeof(colors));
    ZeroMemory(alpha, sizeof(alpha));
}

bool ModPaintLayerState::operator==(const ModPaintLayerState& rhs) const
{
    return mode == rhs.mode
        && memcmp(colors, rhs.colors, sizeof(colors)) == 0
        && memcmp(alpha, rhs.alpha, sizeof(alpha)) == 0
        && image.get() == rhs.image.get()
        && image_id == rhs.image_id
        && image_x_offset == rhs.image_x_offset
        && image_y_offset == rhs.image_y_offset;
}

ModJitterState::ModJitterState()
    : left(0)
    , right(0)
    , up(0)
    , down(0)
    , period_100ns(1)
    , seed(0)
{
}

bool ModJitterState::operator==(const ModJitterState& rhs) const
{
    return left == rhs.left
        && right == rhs.right
        && up == rhs.up
        && down == rhs.down
        && period_100ns == rhs.period_100ns
        && seed == rhs.seed;
}

CPoint ModJitterState::GetOffset(REFERENCE_TIME rt) const
{
    const int64_t period = period_100ns > 0 ? period_100ns : 1;
    const uint32_t frame_seed = static_cast<uint32_t>((seed + rt / period) * 100);
    ModRandomGenerator random(frame_seed);
    const int x_range = left + right;
    const int y_range = up + down;
    const int x = x_range > 0 ? random.Next() % x_range - left : 0;
    const int y = y_range > 0 ? random.Next() % y_range - up : 0;
    return CPoint(x, y);
}

ModStyleState::ModStyleState()
    : feature_mask(MOD_FEATURE_NONE)
    , hash(0)
    , path_hash(0)
    , vertical_spacing(0)
    , z(0)
    , random_x(0)
    , random_y(0)
    , random_z(0)
    , random_seed(0)
    , font_orientation(0)
    , body_gradient_alpha(false)
{
    distort_x[0] = 1;
    distort_y[0] = 0;
    distort_x[1] = 1;
    distort_y[1] = 1;
    distort_x[2] = 0;
    distort_y[2] = 1;
}

bool ModStyleState::operator==(const ModStyleState& rhs) const
{
    return feature_mask == rhs.feature_mask
        && vertical_spacing == rhs.vertical_spacing
        && z == rhs.z
        && random_x == rhs.random_x
        && random_y == rhs.random_y
        && random_z == rhs.random_z
        && random_seed == rhs.random_seed
        && font_orientation == rhs.font_orientation
        && memcmp(distort_x, rhs.distort_x, sizeof(distort_x)) == 0
        && memcmp(distort_y, rhs.distort_y, sizeof(distort_y)) == 0
        && jitter == rhs.jitter
        && paint[0] == rhs.paint[0]
        && paint[1] == rhs.paint[1]
        && paint[2] == rhs.paint[2]
        && paint[3] == rhs.paint[3]
        && body_gradient_alpha == rhs.body_gradient_alpha;
}

void ModStyleState::RecomputeHash()
{
    size_t value = static_cast<size_t>(1469598103934665603ull);
    HashValue(value, feature_mask);
    HashValue(value, vertical_spacing);
    HashValue(value, z);
    HashValue(value, random_x);
    HashValue(value, random_y);
    HashValue(value, random_z);
    HashValue(value, random_seed);
    HashValue(value, font_orientation);
    HashBytes(value, distort_x, sizeof(distort_x));
    HashBytes(value, distort_y, sizeof(distort_y));
    HashValue(value, jitter.left);
    HashValue(value, jitter.right);
    HashValue(value, jitter.up);
    HashValue(value, jitter.down);
    HashValue(value, jitter.period_100ns);
    HashValue(value, jitter.seed);
    for (const auto& layer : paint) {
        HashValue(value, layer.mode);
        HashBytes(value, layer.colors, sizeof(layer.colors));
        HashBytes(value, layer.alpha, sizeof(layer.alpha));
        HashValue(value, layer.image_x_offset);
        HashValue(value, layer.image_y_offset);
        HashString(value, layer.image_id);
        if (layer.image) {
            HashValue(value, layer.image->GetHash());
            HashString(value, layer.image->GetResourceId());
        }
    }
    HashValue(value, body_gradient_alpha);
    hash = value;

    size_t path_value = static_cast<size_t>(1469598103934665603ull);
    const uint32_t path_features = feature_mask
        & (MOD_FEATURE_Z | MOD_FEATURE_RANDOM | MOD_FEATURE_SYMBOL_ROTATION
            | MOD_FEATURE_DISTORT);
    HashValue(path_value, path_features);
    HashValue(path_value, z);
    HashValue(path_value, random_x);
    HashValue(path_value, random_y);
    HashValue(path_value, random_z);
    HashValue(path_value, random_seed);
    HashValue(path_value, font_orientation);
    HashBytes(path_value, distort_x, sizeof(distort_x));
    HashBytes(path_value, distort_y, sizeof(distort_y));
    path_hash = path_value;
}

ModStyleState& EnsureWritableModStyleState(SharedPtrModStyleState& state)
{
    if (!state) {
        state.reset(DEBUG_NEW ModStyleState());
    } else if (!state.unique()) {
        state.reset(DEBUG_NEW ModStyleState(*state));
    }
    return *state;
}

SharedPtrConstModStyleState FreezeModStyleState(SharedPtrModStyleState& state)
{
    if (state) {
        state->RecomputeHash();
    }
    return state;
}

ModPaintSource::Layer::Layer()
    : mode(MOD_PAINT_SOLID)
    , solid_color(0)
    , gradient_fade_alpha(0xff)
    , image_opacity(0xff)
    , image_x_offset(0)
    , image_y_offset(0)
{
    ZeroMemory(colors, sizeof(colors));
    ZeroMemory(gradient_alpha, sizeof(gradient_alpha));
}

bool ModPaintSource::Layer::operator==(const Layer& rhs) const
{
    return mode == rhs.mode
        && solid_color == rhs.solid_color
        && memcmp(colors, rhs.colors, sizeof(colors)) == 0
        && memcmp(gradient_alpha, rhs.gradient_alpha, sizeof(gradient_alpha)) == 0
        && gradient_fade_alpha == rhs.gradient_fade_alpha
        && SameImage(image, rhs.image)
        && image_opacity == rhs.image_opacity
        && image_x_offset == rhs.image_x_offset
        && image_y_offset == rhs.image_y_offset;
}

ModPaintSource::ModPaintSource()
    : m_layer_count(0)
    , m_hash(static_cast<size_t>(1469598103934665603ull))
{
}

bool ModPaintSource::operator==(const ModPaintSource& rhs) const
{
    if (this == &rhs) {
        return true;
    }
    if (m_hash != rhs.m_hash || m_layer_count != rhs.m_layer_count) {
        return false;
    }
    for (int i = 0; i < m_layer_count; ++i) {
        if (!(m_layers[i] == rhs.m_layers[i])) {
            return false;
        }
    }
    return true;
}

DWORD ModPaintSource::GetColor(int layer, int x, int y_from_bottom, int width, int height,
    int subpixel_x, int subpixel_y, int image_clip_diff) const
{
    if (m_layer_count <= 0) {
        return 0;
    }
    layer = max(0, min(layer, m_layer_count - 1));
    const Layer& paint = m_layers[layer];

    if (paint.mode == MOD_PAINT_GRADIENT && width > 0 && height > 0) {
        const double gradient_x = static_cast<double>(x) / width;
        const double gradient_y = static_cast<double>(y_from_bottom) / height;
        DWORD color = 0;
        for (int channel = 0; channel < 3; ++channel) {
            const int shift = channel * 8;
            const BYTE value = InterpolateByte(
                static_cast<BYTE>(paint.colors[0] >> shift),
                static_cast<BYTE>(paint.colors[1] >> shift),
                static_cast<BYTE>(paint.colors[2] >> shift),
                static_cast<BYTE>(paint.colors[3] >> shift),
                gradient_x, gradient_y);
            color |= static_cast<DWORD>(value) << shift;
        }
        const double alpha_value =
            paint.gradient_alpha[0] * (1.0 - gradient_x) * gradient_y
            + paint.gradient_alpha[1] * gradient_x * gradient_y
            + paint.gradient_alpha[2] * (1.0 - gradient_y) * (1.0 - gradient_x)
            + paint.gradient_alpha[3] * gradient_x * (1.0 - gradient_y);
        const BYTE alpha = static_cast<BYTE>(static_cast<int>(alpha_value) & 0xff);
        const BYTE opacity = static_cast<BYTE>(
            (((0xff - alpha) * (0xff - paint.gradient_fade_alpha)) & 0xff00) >> 8);
        color |= static_cast<DWORD>(opacity) << 24;
        return color;
    }

    if (paint.mode == MOD_PAINT_IMAGE && paint.image) {
        const int image_width = paint.image->GetWidth();
        const int image_height = paint.image->GetHeight();
        if (image_width <= 0 || image_height <= 0) {
            return paint.solid_color;
        }

        const int image_x = WrapCoordinate(x + paint.image_x_offset, image_width);
        const int image_y = WrapCoordinate(
            y_from_bottom + image_clip_diff + paint.image_y_offset, image_height);
        const int previous_x = image_x > 0 ? image_x - 1 : image_x;
        const int next_y = image_y < image_height - 1 ? image_y + 1 : image_y;
        const BYTE* pixels = paint.image->GetPixels();

        auto pixel = [pixels, image_width, image_height](int px, int py, int channel) -> BYTE {
            const int top_down_y = image_height - 1 - py;
            return pixels[(static_cast<size_t>(top_down_y) * image_width + px) * 4 + channel];
        };

        BYTE rgba[4];
        for (int channel = 0; channel < 4; ++channel) {
            const int current = pixel(image_x, image_y, channel);
            const int left = pixel(previous_x, image_y, channel);
            const int upper = pixel(image_x, next_y, channel);
            const int upper_left = pixel(previous_x, next_y, channel);

            int value = current;
            if (previous_x != image_x && next_y == image_y) {
                value = (current * (8 - subpixel_x) + left * subpixel_x) >> 3;
            } else if (previous_x == image_x && next_y != image_y) {
                value = (current * subpixel_y + upper * (8 - subpixel_y)) >> 3;
            } else if (previous_x != image_x && next_y != image_y) {
                const int upper_row =
                    (upper * (8 - subpixel_x) + upper_left * subpixel_x) >> 3;
                const int current_row =
                    (current * (8 - subpixel_x) + left * subpixel_x) >> 3;
                value = (upper_row * subpixel_y
                    + current_row * (8 - subpixel_y)) >> 3;
            }
            rgba[channel] = static_cast<BYTE>(value);
        }

        const BYTE opacity = Div255(static_cast<uint32_t>(rgba[3]) * paint.image_opacity);
        const DWORD argb = (static_cast<DWORD>(opacity) << 24)
            | (static_cast<DWORD>(rgba[0]) << 16)
            | (static_cast<DWORD>(rgba[1]) << 8)
            | rgba[2];
        return XySubRenderFrameCreater::GetDefaultCreater()->TransColor(argb);
    }

    return paint.solid_color;
}

bool HasVariableModPaint(const ModStyleState& state, int first_layer, int second_layer)
{
    const int layers[2] = {first_layer, second_layer};
    for (int i = 0; i < 2; ++i) {
        if (layers[i] < 0 || layers[i] >= 4) {
            continue;
        }
        const ModPaintLayerState& paint = state.paint[layers[i]];
        if (paint.mode == MOD_PAINT_GRADIENT
                || (paint.mode == MOD_PAINT_IMAGE && paint.image)) {
            return true;
        }
    }
    return false;
}

SharedPtrConstModPaintSource CreateModPaintSource(const ModStyleState& state,
    int first_layer, int second_layer, const DWORD* solid_colors, int fade_alpha)
{
    if (!solid_colors || !HasVariableModPaint(state, first_layer, second_layer)) {
        return SharedPtrConstModPaintSource();
    }

    boost::shared_ptr<ModPaintSource> result(DEBUG_NEW ModPaintSource());
    const int source_layers[2] = {first_layer, second_layer};
    result->m_layer_count = second_layer >= 0 ? 2 : 1;
    for (int source_index = 0; source_index < result->m_layer_count; ++source_index) {
        const int style_index = source_layers[source_index];
        ModPaintSource::Layer& output = result->m_layers[source_index];
        const ModPaintLayerState& input = state.paint[style_index];
        output.solid_color = solid_colors[source_index];
        output.mode = input.mode;

        if (input.mode == MOD_PAINT_GRADIENT) {
            for (int corner = 0; corner < 4; ++corner) {
                const DWORD argb = ReverseColor(input.colors[corner]);
                output.colors[corner] =
                    XySubRenderFrameCreater::GetDefaultCreater()->TransColor(argb);
                output.gradient_alpha[corner] = input.alpha[corner];
            }
            output.gradient_fade_alpha = static_cast<BYTE>(fade_alpha);
        } else if (input.mode == MOD_PAINT_IMAGE && input.image) {
            output.image = input.image;
            output.image_opacity = static_cast<BYTE>(solid_colors[source_index] >> 24);
            output.image_x_offset = input.image_x_offset;
            output.image_y_offset = input.image_y_offset;
        } else {
            output.mode = MOD_PAINT_SOLID;
        }

        HashValue(result->m_hash, output.mode);
        HashValue(result->m_hash, output.solid_color);
        HashBytes(result->m_hash, output.colors, sizeof(output.colors));
        HashBytes(result->m_hash, output.gradient_alpha, sizeof(output.gradient_alpha));
        HashValue(result->m_hash, output.gradient_fade_alpha);
        HashValue(result->m_hash, output.image_opacity);
        HashValue(result->m_hash, output.image_x_offset);
        HashValue(result->m_hash, output.image_y_offset);
        if (output.image) {
            HashValue(result->m_hash, output.image->GetHash());
            HashString(result->m_hash, output.image->GetResourceId());
        }
    }
    return result;
}

ModEffectState::ModEffectState()
    : feature_mask(MOD_EFFECT_NONE)
    , move_type(MOD_MOVE_NONE)
    , move_radius(0, 0)
{
    ZeroMemory(move_points, sizeof(move_points));
    ZeroMemory(move_angle, sizeof(move_angle));
    move_t[0] = move_t[1] = -1;
    ZeroMemory(org_points, sizeof(org_points));
    org_t[0] = org_t[1] = -1;
    ZeroMemory(clip_points, sizeof(clip_points));
    clip_t[0] = clip_t[1] = -1;
}

ModEffectState& EnsureWritableModEffectState(SharedPtrModEffectState& state)
{
    if (!state) {
        state.reset(DEBUG_NEW ModEffectState());
    } else if (!state.unique()) {
        state.reset(DEBUG_NEW ModEffectState(*state));
    }
    return *state;
}

ModRandomGenerator::ModRandomGenerator(uint32_t seed)
    : m_state(seed)
{
}

int ModRandomGenerator::Next()
{
    m_state = m_state * 214013u + 2531011u;
    return static_cast<int>((m_state >> 16) & 0x7fffu);
}
