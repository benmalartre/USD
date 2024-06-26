//
// Copyright 2023 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
////////////////////////////////////////////////////////////////////////

/* ************************************************************************** */
/* **                                                                      ** */
/* ** This file is generated by a script.                                  ** */
/* **                                                                      ** */
/* ** Do not edit it directly (unless it is within a CUSTOM CODE section)! ** */
/* ** Edit hdGen/schema.py instead to make changes.                        ** */
/* **                                                                      ** */
/* ************************************************************************** */

#ifndef PXR_USD_IMAGING_USD_IMAGING_COLLECTION_MATERIAL_BINDINGS_SCHEMA_H
#define PXR_USD_IMAGING_USD_IMAGING_COLLECTION_MATERIAL_BINDINGS_SCHEMA_H

#include "pxr/usdImaging/usdImaging/api.h"

#include "pxr/imaging/hd/schema.h" 

// --(BEGIN CUSTOM CODE: Includes)--

#include "pxr/imaging/hd/vectorSchema.h"

// --(END CUSTOM CODE: Includes)--

PXR_NAMESPACE_OPEN_SCOPE

//-----------------------------------------------------------------------------

// --(BEGIN CUSTOM CODE: Declares)--

using UsdImagingCollectionMaterialBindingVectorSchema =
    HdSchemaBasedVectorSchema<class UsdImagingCollectionMaterialBindingSchema>;

// --(END CUSTOM CODE: Declares)--

//-----------------------------------------------------------------------------

#define USD_IMAGING_COLLECTION_MATERIAL_BINDINGS_SCHEMA_TOKENS \
    (collectionMaterialBindings) \
    ((allPurpose, "")) \

TF_DECLARE_PUBLIC_TOKENS(UsdImagingCollectionMaterialBindingsSchemaTokens, USDIMAGING_API,
    USD_IMAGING_COLLECTION_MATERIAL_BINDINGS_SCHEMA_TOKENS);

//-----------------------------------------------------------------------------
class UsdImagingCollectionMaterialBindingsSchema : public HdSchema
{
public:
    UsdImagingCollectionMaterialBindingsSchema(HdContainerDataSourceHandle container)
    : HdSchema(container) {}

// --(BEGIN CUSTOM CODE: Schema Methods)--

    /// Returns the purposes for which bindings may be available.
    /// \note This API is preferable to schema.GetContainer()->GetNames().
    USDIMAGING_API
    TfTokenVector GetPurposes();

    /// Returns the bindings for 'allPurpose'.
    UsdImagingCollectionMaterialBindingVectorSchema
    GetCollectionMaterialBindings();

    /// Returns the bindings for the requested purpose.
    UsdImagingCollectionMaterialBindingVectorSchema
    GetCollectionMaterialBindings(const TfToken &purpose);

// --(END CUSTOM CODE: Schema Methods)--


    /// Retrieves a container data source with the schema's default name token
    /// "collectionMaterialBindings" from the parent container and constructs a
    /// UsdImagingCollectionMaterialBindingsSchema instance.
    /// Because the requested container data source may not exist, the result
    /// should be checked with IsDefined() or a bool comparison before use.
    USDIMAGING_API
    static UsdImagingCollectionMaterialBindingsSchema GetFromParent(
        const HdContainerDataSourceHandle &fromParentContainer);

    /// Returns a token where the container representing this schema is found in
    /// a container by default.
    USDIMAGING_API
    static const TfToken &GetSchemaToken();

    /// Returns an HdDataSourceLocator (relative to the prim-level data source)
    /// where the container representing this schema is found by default.
    USDIMAGING_API
    static const HdDataSourceLocator &GetDefaultLocator();

};

PXR_NAMESPACE_CLOSE_SCOPE

#endif