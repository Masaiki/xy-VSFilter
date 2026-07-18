#pragma once

/*
 * VSFilterMod compatibility state, image-resource, and paint-source support.
 * Semantics are adapted from Masaiki/VSFilterMod commit
 * 8556b1bc362fc46977eeb86ee9d9f2a7d1312399.
 * See NOTICE-VSFilterMod.md for provenance and licensing.
 */

#include <cstdint>
#include <vector>
#include <atlstr.h>
#include <atltypes.h>
#include <boost/smart_ptr.hpp>

enum ModFeature : uint32_t
{
    MOD_FEATURE_NONE             = 0,
    MOD_FEATURE_VERTICAL_SPACING = 1u << 0,
    MOD_FEATURE_Z                = 1u << 1,
    MOD_FEATURE_RANDOM           = 1u << 2,
    MOD_FEATURE_GRADIENT         = 1u << 3,
    MOD_FEATURE_IMAGE            = 1u << 4,
    MOD_FEATURE_SYMBOL_ROTATION  = 1u << 5,
    MOD_FEATURE_DISTORT          = 1u << 6,
    MOD_FEATURE_JITTER           = 1u << 7,
};

enum ModPaintMode : uint8_t
{
    MOD_PAINT_SOLID = 0,
    MOD_PAINT_GRADIENT,
    MOD_PAINT_IMAGE,
};

class ModImageResource
{
public:
    ModImageResource(int width, int height, const BYTE* rgba, size_t byte_count,
        const CStringW& resource_id);

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    const BYTE* GetPixels() const { return m_pixels.empty() ? NULL : &m_pixels[0]; }
    size_t GetByteCount() const { return m_pixels.size(); }
    size_t GetHash() const { return m_hash; }
    const CStringW& GetResourceId() const { return m_resource_id; }

private:
    int m_width;
    int m_height;
    std::vector<BYTE> m_pixels;
    size_t m_hash;
    CStringW m_resource_id;
};

typedef boost::shared_ptr<const ModImageResource> SharedPtrConstModImageResource;

class ModImageCache
{
public:
    ModImageCache();
    ModImageCache(const ModImageCache& rhs);
    ~ModImageCache();
    ModImageCache& operator=(const ModImageCache& rhs);

    void RegisterEmbedded(const CStringW& resource_id, const BYTE* data, size_t byte_count);
    SharedPtrConstModImageResource Resolve(const CStringW& resource_id,
        const CStringW& subtitle_path, const CStringW& resource_path);
    void ResetDecoded();
    void Clear();

private:
    struct Impl;
    Impl* m_impl;
};

struct ModPaintLayerState
{
    ModPaintMode mode;
    DWORD colors[4];
    BYTE alpha[4];
    SharedPtrConstModImageResource image;
    CStringW image_id;
    int image_x_offset;
    int image_y_offset;

    ModPaintLayerState();
    bool operator==(const ModPaintLayerState& rhs) const;
};

struct ModJitterState
{
    int left;
    int right;
    int up;
    int down;
    int period_100ns;
    int seed;

    ModJitterState();
    bool operator==(const ModJitterState& rhs) const;
    CPoint GetOffset(REFERENCE_TIME rt) const;
};

struct ModStyleState
{
    uint32_t feature_mask;
    size_t hash;
    size_t path_hash;

    double vertical_spacing;
    double z;
    double random_x;
    double random_y;
    double random_z;
    int random_seed;
    double font_orientation;
    double distort_x[3];
    double distort_y[3];
    ModJitterState jitter;
    ModPaintLayerState paint[4];
    bool body_gradient_alpha;

    ModStyleState();
    bool operator==(const ModStyleState& rhs) const;
    void RecomputeHash();
};

typedef boost::shared_ptr<ModStyleState> SharedPtrModStyleState;
typedef boost::shared_ptr<const ModStyleState> SharedPtrConstModStyleState;

ModStyleState& EnsureWritableModStyleState(SharedPtrModStyleState& state);
SharedPtrConstModStyleState FreezeModStyleState(SharedPtrModStyleState& state);

class ModPaintSource
{
public:
    DWORD GetColor(int layer, int x, int y_from_bottom, int width, int height,
        int subpixel_x, int subpixel_y, int image_clip_diff) const;
    size_t GetHash() const { return m_hash; }
    bool operator==(const ModPaintSource& rhs) const;

private:
    struct Layer
    {
        ModPaintMode mode;
        DWORD solid_color;
        DWORD colors[4];
        SharedPtrConstModImageResource image;
        BYTE image_opacity;
        int image_x_offset;
        int image_y_offset;

        Layer();
        bool operator==(const Layer& rhs) const;
    };

    Layer m_layers[2];
    int m_layer_count;
    size_t m_hash;

    ModPaintSource();
    friend boost::shared_ptr<const ModPaintSource> CreateModPaintSource(
        const ModStyleState&, int, int, const DWORD*, int);
};

typedef boost::shared_ptr<const ModPaintSource> SharedPtrConstModPaintSource;

bool HasVariableModPaint(const ModStyleState& state, int first_layer, int second_layer = -1);
SharedPtrConstModPaintSource CreateModPaintSource(const ModStyleState& state,
    int first_layer, int second_layer, const DWORD* solid_colors, int fade_alpha);

enum ModEffectFeature : uint32_t
{
    MOD_EFFECT_NONE        = 0,
    MOD_EFFECT_MOVE        = 1u << 0,
    MOD_EFFECT_MOVING_ORG  = 1u << 1,
    MOD_EFFECT_MOVING_CLIP = 1u << 2,
};

enum ModMoveType : uint8_t
{
    MOD_MOVE_NONE = 0,
    MOD_MOVE_RADIAL,
    MOD_MOVE_QUADRATIC,
    MOD_MOVE_CUBIC,
};

struct ModEffectState
{
    uint32_t feature_mask;
    ModMoveType move_type;
    CPoint move_points[4];
    double move_angle[2];
    CPoint move_radius;
    int move_t[2];

    CPoint org_points[2];
    int org_t[2];

    CPoint clip_points[2];
    int clip_t[2];

    ModEffectState();
};

typedef boost::shared_ptr<ModEffectState> SharedPtrModEffectState;
typedef boost::shared_ptr<const ModEffectState> SharedPtrConstModEffectState;

ModEffectState& EnsureWritableModEffectState(SharedPtrModEffectState& state);

class ModRandomGenerator
{
    uint32_t m_state;

public:
    explicit ModRandomGenerator(uint32_t seed);
    int Next();
};
