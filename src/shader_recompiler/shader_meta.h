#pragma once

#include <array>
#include "shader_recompiler/gcn_constants.h"
#include "shader_recompiler/shader_binary.h"

namespace Shader::Gcn {

typedef enum BufferFormat {
    kBufferFormatInvalid = 0x00000000, ///< Invalid Buffer format.
    kBufferFormat8 = 0x00000001,       ///< One 8-bit channel. X=0xFF
    kBufferFormat16 = 0x00000002,      ///< One 16-bit channel. X=0xFFFF
    kBufferFormat8_8 = 0x00000003,     ///< Two 8-bit channels. X=0x00FF, Y=0xFF00
    kBufferFormat32 = 0x00000004,      ///< One 32-bit channel. X=0xFFFFFFFF
    kBufferFormat16_16 = 0x00000005,   ///< Two 16-bit channels. X=0x0000FFFF, Y=0xFFFF0000
    kBufferFormat10_11_11 =
        0x00000006, ///< One 10-bit channel (Z) and two 11-bit channels (Y,X). X=0x000007FF,
                    ///< Y=0x003FF800, Z=0xFFC00000 Interpreted only as floating-point by texture
                    ///< unit, but also as integer by rasterizer.
    kBufferFormat11_11_10 =
        0x00000007, ///< Two 11-bit channels (Z,Y) and one 10-bit channel (X). X=0x000003FF,
                    ///< Y=0x001FFC00, Z=0xFFE00000 Interpreted only as floating-point by texture
                    ///< unit, but also as integer by rasterizer.
    kBufferFormat10_10_10_2 = 0x00000008, ///< Three 10-bit channels (W,Z,Y) and one 2-bit channel
                                          ///< (X). X=0x00000003, Y=0x00000FFC, Z=0x003FF000,
                                          ///< W=0xFFC00000. X is never negative, even when YZW are.
    kBufferFormat2_10_10_10 = 0x00000009, ///< One 2-bit channel (W) and three 10-bit channels
                                          ///< (Z,Y,X). X=0x000003FF, Y=0x000FFC00, Z=0x3FF00000,
                                          ///< W=0xC0000000. W is never negative, even when XYZ are.
    kBufferFormat8_8_8_8 =
        0x0000000a, ///< Four 8-bit channels. X=0x000000FF, Y=0x0000FF00, Z=0x00FF0000, W=0xFF000000
    kBufferFormat32_32 = 0x0000000b,       ///< Two 32-bit channels.
    kBufferFormat16_16_16_16 = 0x0000000c, ///< Four 16-bit channels.
    kBufferFormat32_32_32 = 0x0000000d,    ///< Three 32-bit channels.
    kBufferFormat32_32_32_32 = 0x0000000e, ///< Four 32-bit channels.
} BufferFormat;

typedef enum BufferChannelType {
    kBufferChannelTypeUNorm =
        0x00000000, ///< Stored as <c>uint X\<N</c>, interpreted as <c>float X/(N-1)</c>.
    kBufferChannelTypeSNorm = 0x00000001, ///< Stored as <c>int -N/2\<=X\<N/2</c>, interpreted as
                                          ///< <c>float MAX(-1,X/(N/2-1))</c>.
    kBufferChannelTypeUScaled =
        0x00000002, ///< Stored as <c>uint X\<N</c>, interpreted as <c>float X</c>.
    kBufferChannelTypeSScaled =
        0x00000003, ///< Stored as <c>int -N/2\<=X\<N/2</c>, interpreted as <c>float X</c>.
    kBufferChannelTypeUInt =
        0x00000004, ///< Stored as <c>uint X\<N</c>, interpreted as <c>uint X</c>. Not filterable.
    kBufferChannelTypeSInt = 0x00000005, ///< Stored as <c>int -N/2\<=X\<N/2</c>, interpreted as
                                         ///< <c>int X</c>. Not filterable.
    kBufferChannelTypeSNormNoZero = 0x00000006, ///< Stored as <c>int -N/2<=X\<N/2</c>, interpreted
                                                ///< as <c>float ((X+N/2)/(N-1))*2-1</c>.
    kBufferChannelTypeFloat =
        0x00000007, ///< Stored as <c>float</c>, interpreted as <c>float</c>.
                    ///<  – 32-bit: SE8M23, bias 127, range <c>(-2^129..2^129)</c>
                    ///<  – 16-bit: SE5M10, bias 15, range <c>(-2^17..2^17)</c>
                    ///<  – 11-bit: E5M6 bias 15, range <c>[0..2^17)</c>
                    ///<  – 10-bit: E5M5 bias 15, range <c>[0..2^17)</c>
} BufferChannelType;

typedef enum TextureType {
    kTextureType1d = 0x00000008,      ///< One-dimensional texture.
    kTextureType2d = 0x00000009,      ///< Two-dimensional texture.
    kTextureType3d = 0x0000000A,      ///< Three-dimensional volume texture.
    kTextureTypeCubemap = 0x0000000B, ///< Cubic environment map texture (six 2D textures arranged
                                      ///< in a cube and indexed by a 3D direction vector). This
                                      ///< TextureType is also used for cubemap arrays.
    kTextureType1dArray = 0x0000000C, ///< Array of 1D textures.
    kTextureType2dArray = 0x0000000D, ///< Array of 2D textures.
    kTextureType2dMsaa = 0x0000000E,  ///< Two-dimensional texture with multiple samples per pixel.
                                      ///< Usually an alias into an MSAA render target.
    kTextureType2dArrayMsaa = 0x0000000F, ///< Array of 2D MSAA textures.
} TextureType;

typedef enum TextureChannelType {
    kTextureChannelTypeUNorm =
        0x00000000, ///< Stored as <c>uint X\<N</c>, interpreted as <c>float X/(N-1)</c>.
    kTextureChannelTypeSNorm = 0x00000001, ///< Stored as <c>int -N/2\<=X\<N/2</c>, interpreted as
                                           ///< <c>float MAX(-1,X/(N/2-1))</c>.
    kTextureChannelTypeUScaled =
        0x00000002, ///< Stored as <c>uint X\<N</c>, interpreted as <c>float X</c>.
    kTextureChannelTypeSScaled =
        0x00000003, ///< Stored as <c>int -N/2\<=X\<N/2</c>, interpreted as <c>float X</c>.
    kTextureChannelTypeUInt =
        0x00000004, ///< Stored as <c>uint X\<N</c>, interpreted as <c>uint X</c>. Not filterable.
    kTextureChannelTypeSInt = 0x00000005, ///< Stored as <c>int -N/2\<=X\<N/2</c>, interpreted as
                                          ///< <c>int X</c>. Not filterable.
    kTextureChannelTypeSNormNoZero =
        0x00000006, ///< Stored as <c>int -N/2\<=X\<N/2</c>, interpreted as <c>float
                    ///< ((X+N/2)/(N-1))*2-1</c>.
    kTextureChannelTypeFloat =
        0x00000007, ///< Stored as <c>float</c>, interpreted as <c>float</c>.
                    ///<  – 32-bit: SE8M23, bias 127, range <c>(-2^129..2^129)</c>
                    ///<  – 16-bit: SE5M10, bias 15, range <c>(-2^17..2^17)</c>
                    ///<  – 11-bit: E5M6 bias 15, range <c>[0..2^17)</c>
                    ///<  – 10-bit: E5M5 bias 15, range <c>[0..2^17)</c>
    kTextureChannelTypeSrgb =
        0x00000009, ///< Stored as <c>uint X\<N</c>, interpreted as <c>float sRGB(X/(N-1))</c>. Srgb
                    ///< only applies to the XYZ channels of the texture; the W channel is always
                    ///< linear.
    kTextureChannelTypeUBNorm = 0x0000000A, ///< Stored as <c>uint X\<N</c>, interpreted as <c>float
                                            ///< MAX(-1,(X-N/2)/(N/2-1))</c>.
    kTextureChannelTypeUBNormNoZero =
        0x0000000B, ///< Stored as <c>uint X\<N</c>, interpreted as <c>float (X/(N-1))*2-1</c>.
    kTextureChannelTypeUBInt = 0x0000000C, ///< Stored as <c>uint X\<N</c>, interpreted as <c>int
                                           ///< X-N/2</c>. Not blendable or filterable.
    kTextureChannelTypeUBScaled =
        0x0000000D, ///< Stored as <c>uint X\<N</c>, interpreted as <c>float X-N/2</c>.
} TextureChannelType;

/// TODO:
/// Currently, we assume the V# T# and S# bound to shader remains permanent during rendering (except
/// for address). But they can be changed in theory. If that happens, we have to to store these
/// information in resource buffers, like push constants or uniform buffers, then parse them at
/// shader runtime. That would be much more complicated.

struct GcnBufferMeta {
    u32 stride;
    u32 numRecords;
    BufferFormat dfmt;
    BufferChannelType nfmt;
    bool isSwizzle;
    u32 indexStride;
    u32 elementSize;
};

struct GcnTextureMeta {
    TextureType textureType;
    TextureChannelType channelType;
    bool isDepth;
};

struct GcnMetaCommon {
    u32 userSgprCount;

    std::array<GcnBufferMeta, 128> bufferInfos;
    std::array<GcnTextureMeta, 128> textureInfos;
};

struct GcnMetaVS : public GcnMetaCommon {
    u32 inputSemanticCount;
    std::array<VertexInputSemantic, kMaxVertexBufferCount> inputSemanticTable;
};

struct GcnMetaPS : public GcnMetaCommon {
    u32 inputSemanticCount;
    std::array<PixelSemanticMapping, kMaxPsInputUsageCount> semanticMapping;

    bool perspSampleEn;
    bool perspCenterEn;
    bool perspCentroidEn;
    bool perspPullModelEn;
    bool linearSampleEn;
    bool linearCenterEn;
    bool linearCentroidEn;
    bool posXEn;
    bool posYEn;
    bool posZEn;
    bool posWEn;
};

struct GcnMetaCS : public GcnMetaCommon {
    u32 computeNumThreadX;
    u32 computeNumThreadY;
    u32 computeNumThreadZ;

    bool enableTgidX;
    bool enableTgidY;
    bool enableTgidZ;
    bool enableTgSize;
    bool enableScratch;

    u32 threadIdInGroupCount;

    u32 ldsSize;
};

struct GcnMetaGS : public GcnMetaCommon {};

struct GcnMetaHS : public GcnMetaCommon {};

struct GcnMetaDS : public GcnMetaCommon {};

/**
 * \brief Shader meta information
 *
 * Stores some meta information of the compiled shader binary file but is
 * ripped while uploading to GPU. These meta information is provided at runtime.
 *
 */
union GcnShaderMeta {
    GcnMetaVS vs;
    GcnMetaPS ps;
    GcnMetaCS cs;
    GcnMetaGS gs;
    GcnMetaHS hs;
    GcnMetaDS ds;
};

} // namespace Shader::Gcn
