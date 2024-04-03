//
// Copyright 2021 benmalartre
//
// Unlicensed
//
#ifndef PXR_IMAGING_LOFI_FIELD_TEXTURE_CPU_DATA_H
#define PXR_IMAGING_LOFI_FIELD_TEXTURE_CPU_DATA_H

#include "pxr/pxr.h"
#include "pxr/imaging/plugin/LoFi/api.h"

#include "pxr/imaging/plugin/LoFi/textureCpuData.h"
#include "pxr/imaging/hgi/texture.h"

#include "pxr/base/tf/declarePtrs.h"

#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

using HioFieldTextureDataSharedPtr = std::shared_ptr<class HioFieldTextureData>;

/// \class HdStTextureCpuData
///
/// An implmentation of HdStTextureCpuData that can be initialized
/// from HioFieldTextureData.
///
class LoFiFieldTextureCpuData : public LoFiTextureCpuData
{
public:
    /// It is assumed that Read(...) has already been called
    /// on textureData.

    LOFI_API
    LoFiFieldTextureCpuData(
        HioFieldTextureDataSharedPtr const &textureData,
        const std::string &debugName,
        bool premultiplyAlpha = true);

    LOFI_API
    ~LoFiFieldTextureCpuData() override;
    
    LOFI_API
    const HgiTextureDesc &GetTextureDesc() const override;

    LOFI_API
    bool GetGenerateMipmaps() const override;

    LOFI_API
    bool IsValid() const override;

private:
    // The result, including a pointer to the potentially
    // converted texture data in _textureDesc.initialData.
    HgiTextureDesc _textureDesc;

    // If true, initialData only contains mip level 0 data
    // and the GPU is supposed to generate the other mip levels.
    bool _generateMipmaps;

    // To avoid a copy, hold on to original data if we
    // can use them.
    HioFieldTextureDataSharedPtr _textureData;

    // Buffer if we had to convert the data.
    std::unique_ptr<const unsigned char[]> _convertedData;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif