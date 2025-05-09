//
// Copyright 2023 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
////////////////////////////////////////////////////////////////////////

/* ************************************************************************** */
/* **                                                                      ** */
/* ** This file is generated by a script.                                  ** */
/* **                                                                      ** */
/* ** Do not edit it directly (unless it is within a CUSTOM CODE section)! ** */
/* ** Edit hdSchemaDefs.py instead to make changes.                        ** */
/* **                                                                      ** */
/* ************************************************************************** */

#ifndef EXT_RMANPKG_PLUGIN_RENDERMAN_PLUGIN_HD_PRMAN_RILEY_SHADING_NODE_SCHEMA_H
#define EXT_RMANPKG_PLUGIN_RENDERMAN_PLUGIN_HD_PRMAN_RILEY_SHADING_NODE_SCHEMA_H

/// \file

#include "hdPrman/api.h"
#include "hdPrman/rileyParamListSchema.h"

#include "pxr/imaging/hd/schema.h"
#include "pxr/imaging/hd/version.h"

// --(BEGIN CUSTOM CODE: Includes)--
// --(END CUSTOM CODE: Includes)--

PXR_NAMESPACE_OPEN_SCOPE

// --(BEGIN CUSTOM CODE: Declares)--
// --(END CUSTOM CODE: Declares)--

#define HD_PRMAN_RILEY_SHADING_NODE_SCHEMA_TOKENS \
    (type) \
    (name) \
    (handle) \
    (params) \
    (pattern) \
    (bxdf) \
    (integrator) \
    (light) \
    (lightFilter) \
    (projection) \
    (displacement) \
    (sampleFilter) \
    (displayFilter) \

TF_DECLARE_PUBLIC_TOKENS(HdPrmanRileyShadingNodeSchemaTokens, HDPRMAN_API,
    HD_PRMAN_RILEY_SHADING_NODE_SCHEMA_TOKENS);

//-----------------------------------------------------------------------------


class HdPrmanRileyShadingNodeSchema : public HdSchema
{
public:
    /// \name Schema retrieval
    /// @{

    HdPrmanRileyShadingNodeSchema(HdContainerDataSourceHandle container)
      : HdSchema(container) {}

    /// @}

// --(BEGIN CUSTOM CODE: Schema Methods)--
// --(END CUSTOM CODE: Schema Methods)--

    /// \name Member accessor
    /// @{

    HDPRMAN_API
    HdTokenDataSourceHandle GetType()
#if HD_API_VERSION >= 66
                                            const;
#else
                                                 ;
#endif


    HDPRMAN_API
    HdTokenDataSourceHandle GetName()
#if HD_API_VERSION >= 66
                                            const;
#else
                                                 ;
#endif


    HDPRMAN_API
    HdTokenDataSourceHandle GetHandle()
#if HD_API_VERSION >= 66
                                            const;
#else
                                                 ;
#endif


    HDPRMAN_API
    HdPrmanRileyParamListSchema GetParams()
#if HD_API_VERSION >= 66
                                            const;
#else
                                                 ;
#endif
 

    /// @} 

    /// \name Schema construction
    /// @{

    /// \deprecated Use Builder instead.
    ///
    /// Builds a container data source which includes the provided child data
    /// sources. Parameters with nullptr values are excluded. This is a
    /// low-level interface. For cases in which it's desired to define
    /// the container with a sparse set of child fields, the Builder class
    /// is often more convenient and readable.
    HDPRMAN_API
    static HdContainerDataSourceHandle
    BuildRetained(
        const HdTokenDataSourceHandle &type,
        const HdTokenDataSourceHandle &name,
        const HdTokenDataSourceHandle &handle,
        const HdContainerDataSourceHandle &params
    );

    /// \class HdPrmanRileyShadingNodeSchema::Builder
    /// 
    /// Utility class for setting sparse sets of child data source fields to be
    /// filled as arguments into BuildRetained. Because all setter methods
    /// return a reference to the instance, this can be used in the "builder
    /// pattern" form.
    class Builder
    {
    public:
        HDPRMAN_API
        Builder &SetType(
            const HdTokenDataSourceHandle &type);
        HDPRMAN_API
        Builder &SetName(
            const HdTokenDataSourceHandle &name);
        HDPRMAN_API
        Builder &SetHandle(
            const HdTokenDataSourceHandle &handle);
        HDPRMAN_API
        Builder &SetParams(
            const HdContainerDataSourceHandle &params);

        /// Returns a container data source containing the members set thus far.
        HDPRMAN_API
        HdContainerDataSourceHandle Build();

    private:
        HdTokenDataSourceHandle _type;
        HdTokenDataSourceHandle _name;
        HdTokenDataSourceHandle _handle;
        HdContainerDataSourceHandle _params;

    };

    /// Returns token data source for use as type value.
    ///
    /// The following values will be stored statically and reused for future
    /// calls:
    /// - HdPrmanRileyShadingNodeSchemaTokens->pattern
    /// - HdPrmanRileyShadingNodeSchemaTokens->bxdf
    /// - HdPrmanRileyShadingNodeSchemaTokens->integrator
    /// - HdPrmanRileyShadingNodeSchemaTokens->light
    /// - HdPrmanRileyShadingNodeSchemaTokens->lightFilter
    /// - HdPrmanRileyShadingNodeSchemaTokens->projection
    /// - HdPrmanRileyShadingNodeSchemaTokens->displacement
    /// - HdPrmanRileyShadingNodeSchemaTokens->sampleFilter
    /// - HdPrmanRileyShadingNodeSchemaTokens->displayFilter
    HDPRMAN_API
    static HdTokenDataSourceHandle BuildTypeDataSource(
        const TfToken &type);

    /// @}
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif