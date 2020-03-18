//
// Copyright 2017 Pixar
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
#ifndef PXR_IMAGING_PLUGIN_HD_EMBREE3_GEOMETRY_H
#define PXR_IMAGING_PLUGIN_HD_EMBREE3_GEOMETRY_H

#include "pxr/pxr.h"
#include "pxr/imaging/plugin/hdEmbree3/sampler.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/base/vt/types.h"

#include <embree3/rtcore.h>
#include <embree3/rtcore_geometry.h>

#include <bitset>

// Old versions of embree didn't define this, and had 2 hardcoded user buffers.
#ifndef RTC_MAX_USER_VERTEX_BUFFERS
#define RTC_MAX_USER_VERTEX_BUFFERS 2
#endif

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdEmbree3RTCBufferAllocator
///
/// Utility class to track which embree user vertex buffers are currently
/// in use.
class HdEmbree3RTCGeometry
{
public:
    /// Constructor
    HdEmbree3RTCGeometry(RTCDevice device, RTCGeometryType type)
        : _bitset(0) 
        , _type(type)
        , _geom(rtcNewGeometry(device, type)){};

    /// Destructor
    ~HdEmbree3RTCGeometry();

    /// Get underlying RTCGeometry
    RTCGeometry Get(){return _geom;};

    /// Get geometry ID in RTCScene
    int GetId(){return _geomId;};

    /// Get next free slot by finding the first clear bit, using that as
    /// the buffer number, and setting the bit to mark it as used.
    /// \return An unused RTC user vertex buffer id, or -1 on failure.
    int GetSlot();

    /// Get format for this slot
    /// \return An RTC_FLOAT_FORMAT specific for this slot
    RTCFormat GetFormat(const HdVtBufferSource& buffer);

    void Commit(RTCScene scene);

    /// Free a buffer by clearing its bit.
    /// \param buffer The buffer to mark as unused.
    void Free(int buffer);
private:
    std::bitset<RTC_MAX_USER_VERTEX_BUFFERS>  _bitset;
    RTCGeometryType                           _type;
    RTCGeometry                               _geom;
    int                                       _geomId;
};

// ----------------------------------------------------------------------
// The classes below implement the HdEmbree3PrimvarSampler interface for
// the different interpolation modes that hydra supports. In some cases,
// implementations are broken out by geometry type (e.g. triangles vs
// subdiv).

/// \class HdEmbree3ConstantSampler
///
/// This class implements the HdEmbree3PrimvarSampler interface for primvars
/// with "constant" interpolation mode. This means that the buffer only has
/// one item, which should be returned for any (element, u, v) tuple.
class HdEmbree3ConstantSampler : public HdEmbree3PrimvarSampler {
public:
    /// Constructor.
    /// \param name The name of the primvar.
    /// \param value The buffer data for the primvar.
    HdEmbree3ConstantSampler(TfToken const& name,
                            VtValue const& value)
        : _buffer(name, value)
        , _sampler(_buffer) {}

    /// Sample the primvar at an (element, u, v) location. For constant
    /// primvars, the buffer only contains one item, so we always return
    /// that item.
    /// \param element The element index to sample.
    /// \param u The u coordinate to sample.
    /// \param v The v coordinate to sample.
    /// \param value The memory to write the value to (only written on success).
    /// \param dataType The HdTupleType describing element values.
    /// \return True if the value was successfully sampled.
    virtual bool Sample(unsigned int element, float u, float v, void* value,
                        HdTupleType dataType) const;

private:
    HdVtBufferSource const _buffer;
    HdEmbree3BufferSampler const _sampler;
};

/// \class HdEmbree3UniformSampler
///
/// This class implements the HdEmbree3PrimvarSampler interface for primvars
/// with "uniform" interpolation mode. This means that the buffer has one
/// item per authored face. For unrefined meshes, HdEmbree3 will convert
/// mesh polygons to triangles, so this class optionally takes an array
/// called "primitiveParams" which maps from the face index embree reports
/// to the original authored face in the scene data. If primitiveParams is not
/// provided, this translation step is skipped.
class HdEmbree3UniformSampler : public HdEmbree3PrimvarSampler {
public:
    /// Constructor.
    /// \param name The name of the primvar.
    /// \param value The buffer data for the primvar.
    /// \param primitiveParams A mapping from geometry face index to authored
    ///                        face index.
    HdEmbree3UniformSampler(TfToken const& name,
                           VtValue const& value,
                           VtIntArray const& primitiveParams)
        : _buffer(name, value)
        , _sampler(_buffer)
        , _primitiveParams(primitiveParams) {}

    /// Constructor.
    /// \param name The name of the primvar.
    /// \param value The buffer data for the primvar.
    HdEmbree3UniformSampler(TfToken const& name,
                           VtValue const& value)
        : _buffer(name, value)
        , _sampler(_buffer) {}

    /// Sample the primvar at an (element, u, v) location. For uniform
    /// primvars, optionally look up the authored face index in
    /// _primitiveParams[element] (which is stored encoded); then return
    /// _buffer[element].
    ///
    /// \param element The element index to sample.
    /// \param u The u coordinate to sample.
    /// \param v The v coordinate to sample.
    /// \param value The memory to write the value to (only written on success).
    /// \param dataType The HdTupleType describing element values.
    /// \return True if the value was successfully sampled.
    virtual bool Sample(unsigned int element, float u, float v, void* value,
                        HdTupleType dataType) const;

private:
    HdVtBufferSource const _buffer;
    HdEmbree3BufferSampler const _sampler;
    VtIntArray const _primitiveParams;
};

/// \class HdEmbree3TriangleVertexSampler
///
/// This class implements the HdEmbree3PrimvarSampler interface for primvars on
/// triangle meshes with "vertex" or "varying" interpolation modes. This means
/// the buffer has one item per vertex, and the result of sampling is a
/// barycentric interpolation of the hit face vertices. This class
/// requires the triangulated mesh topology, to map from the triangle index
/// (in "element") to the triangle vertices.
class HdEmbree3TriangleVertexSampler : public HdEmbree3PrimvarSampler {
public:
    /// Constructor.
    /// \param name The name of the primvar.
    /// \param value The buffer data for the primvar.
    /// \param indices A map from triangle index to vertex indices in the
    ///                triangulated geometry.
    HdEmbree3TriangleVertexSampler(TfToken const& name,
                                  VtValue const& value,
                                  VtVec3iArray const& indices)
        : _buffer(name, value)
        , _sampler(_buffer)
        , _indices(indices) {}

    /// Sample the primvar at an (element, u, v) location. For vertex primvars,
    /// the vertex indices of the triangle are stored in _indices[element][0-2].
    /// After fetching the primvar value for each of the three vertices,
    /// they are interpolated as follows, per Embree3 specification:
    /// t_uv = (1-u-v)*t0 + u*t1 + v*t2
    /// 
    /// \param element The element index to sample.
    /// \param u The u coordinate to sample.
    /// \param v The v coordinate to sample.
    /// \param value The memory to write the value to (only written on success).
    /// \param dataType The HdTupleType describing element values.
    /// \return True if the value was successfully sampled.
    virtual bool Sample(unsigned int element, float u, float v, void* value,
                        HdTupleType dataType) const;

private:
    HdVtBufferSource const _buffer;
    HdEmbree3BufferSampler const _sampler;
    VtVec3iArray const _indices;
};

/// \class HdEmbree3TriangleFaceVaryingSampler
///
/// This class implements the HdEmbree3PrimvarSampler interface for primvars on
/// triangle meshes with "face-varying" interpolation modes. This means that
/// each vertex of each face gets its own buffer item: vertex 0 as part of
/// face 0 might have value 1.0f, but vertex 0 as part of face 1 might have
/// value 2.0f. The primvar's memory layout is grouped by face, with one item
/// per vertex.
///
/// Concretely, a cube with 8 vertices would have 24 items
/// (6 faces * 4 vertices) in a face-varying primvar, and the index of the
/// item for face 2, vertex 3, would be (2 * 4 + 3) = 11.
///
/// Face-varying primvars are provided to the sampler un-triangulated, but
/// the size of the buffer is tied to the size of the topology, so
/// this class triangulates the input buffer before sampling.
class HdEmbree3TriangleFaceVaryingSampler : public HdEmbree3PrimvarSampler {
public:
    /// Constructor. Triangulates the provided buffer data.
    /// \param name The name of the primvar.
    /// \param value The buffer data for the primvar.
    /// \param meshUtil An HdMeshUtil instance that knows how to triangulate
    ///                 the input buffer data.
    HdEmbree3TriangleFaceVaryingSampler(TfToken const& name,
                                       VtValue const& value,
                                       HdMeshUtil &meshUtil)
        : _buffer(name, _Triangulate(name, value, meshUtil))
        , _sampler(_buffer) {}

    /// Sample the primvar at an (element, u, v) location. For face varying
    /// primvars, the vertex indices are simply (element * 3 + 0->2), since
    /// all faces are triangles. After fetching the primvar value for each of
    /// the three vertices, they are interpolated as follows, per Embree3
    /// specification:
    /// t_uv = (1-u-v)*t0 + u*t1 + v*t2
    /// 
    /// \param element The element index to sample.
    /// \param u The u coordinate to sample.
    /// \param v The v coordinate to sample.
    /// \param value The memory to write the value to (only written on success).
    /// \param dataType The HdTupleType describing element values.
    /// \return True if the value was successfully sampled.
    virtual bool Sample(unsigned int element, float u, float v, void* value,
                        HdTupleType dataType) const;
    
private:
    HdVtBufferSource const _buffer;
    HdEmbree3BufferSampler const _sampler;

    // Pass the "value" parameter through HdMeshUtils'
    // ComputeTriangulatedFaceVaryingPrimvar(), which adjusts the primvar
    // buffer data for the triangulated topology. HdMeshUtil is provided
    // the source topology at construction time, so this class doesn't need
    // to provide it.
    static VtValue _Triangulate(TfToken const& name, VtValue const& value,
                                HdMeshUtil &meshUtil);
};

/// \class HdEmbree3SubdivVertexSampler
///
/// This class implements the HdEmbree3PrimvarSampler interface for primvars on
/// subdiv meshes with "vertex" interpolation mode. This means
/// the buffer has one item per vertex, and the result of sampling is a
/// reconstruction using the subdivision scheme basis weights. It uses embree's
/// user vertex buffers and rtcInterpolate API to accomplish the sampling.
class HdEmbree3SubdivVertexSampler : public HdEmbree3PrimvarSampler {
public:
    /// Constructor. Allocates an embree user vertex buffer, and uploads
    /// the primvar data. Only float-based types (float, GfVec3f, GfMatrix4f)
    /// are allowed, and embree has an exhaustible number of user vertex
    /// buffers (16 at last count).
    ///
    /// \param name The name of the primvar.
    /// \param value The buffer data for the primvar.
    /// \param meshScene The owning mesh's embree prototype scene.
    /// \param geometry The RTCGeometry 
    HdEmbree3SubdivVertexSampler(TfToken const& name,
                                VtValue const& value,
                                RTCScene meshScene,
                                HdEmbree3RTCGeometry *geometry);

    /// Destructor. Frees the embree user vertex buffer.
    virtual ~HdEmbree3SubdivVertexSampler();

    /// Sample the primvar at an (element, u, v) location. This implementation
    /// delegates to rtcInterpolate(). Only float-based types (float, GfVec3f,
    /// GfMatrix4f) are allowed.
    /// 
    /// \param element The element index to sample.
    /// \param u The u coordinate to sample.
    /// \param v The v coordinate to sample.
    /// \param value The memory to write the value to (only written on success).
    /// \param dataType The HdTupleType describing element values.
    /// \return True if the value was successfully sampled.
    virtual bool Sample(unsigned int element, float u, float v, void* value,
                        HdTupleType dataType) const;

private:
    int _slotId;
    int _slotFormat;
    HdVtBufferSource const _buffer;
    RTCScene _meshScene;
    HdEmbree3RTCGeometry *_geometry;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_PLUGIN_HD_EMBREE3_MESH_SAMPLERS_H
