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
#include "pxr/imaging/plugin/hdEmbree3/geometry.h"

#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/vtBufferSource.h"

PXR_NAMESPACE_OPEN_SCOPE

// HdEmbree3RTCGeometry

HdEmbree3RTCGeometry::~HdEmbree3RTCGeometry()
{

}

int
HdEmbree3RTCGeometry::GetSlot()
{
    for (size_t i = 0; i < _bitset.size(); ++i) {
        if (!_bitset.test(i)) {
            _bitset.set(i);
            return i;
        }
    }
    return -1;
}

RTCFormat
HdEmbree3RTCGeometry::GetFormat(const HdVtBufferSource& buffer)
{
    return (RTCFormat)(RTC_FORMAT_FLOAT + buffer.GetTupleType().count - 1);
}

void
HdEmbree3RTCGeometry::Commit(RTCScene scene)
{
    rtcCommitGeometry(_geom);
    _geomId = rtcAttachGeometry(scene, _geom);
    rtcReleaseGeometry(_geom);
    _bitset = 0;
}

// HdEmbree3ConstantSampler

bool
HdEmbree3ConstantSampler::Sample(unsigned int element, float u, float v,
    void* value, HdTupleType dataType) const
{
    return _sampler.Sample(0, value, dataType);
}

// HdEmbree3UniformSampler

bool
HdEmbree3UniformSampler::Sample(unsigned int element, float u, float v,
    void* value, HdTupleType dataType) const
{
    if (_primitiveParams.empty()) {
        return _sampler.Sample(element, value, dataType);
    }
    if (element >= _primitiveParams.size()) {
        return false;
    }
    return _sampler.Sample(
        HdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(
            _primitiveParams[element]),
        value, dataType);
}

// HdEmbree3TriangleVertexSampler

bool
HdEmbree3TriangleVertexSampler::Sample(unsigned int element, float u, float v,
    void* value, HdTupleType dataType) const
{
    if (element >= _indices.size()) {
        return false;
    }
    HdEmbree3TypeHelper::PrimvarTypeContainer corners[3];
    if (!_sampler.Sample(_indices[element][0], &corners[0], dataType) ||
        !_sampler.Sample(_indices[element][1], &corners[1], dataType) ||
        !_sampler.Sample(_indices[element][2], &corners[2], dataType)) {
        return false;
    }
    void* samples[3] = { static_cast<void*>(&corners[0]),
                         static_cast<void*>(&corners[1]),
                         static_cast<void*>(&corners[2]) };
    // Embree3 specification of triangle interpolation:
    // t_uv = (1-u-v)*t0 + u*t1 + v*t2
    float weights[3] = { 1.0f - u - v, u, v };
    return _Interpolate(value, samples, weights, 3, dataType);
}

// HdEmbree3TriangleFaceVaryingSampler

bool
HdEmbree3TriangleFaceVaryingSampler::Sample(unsigned int element, float u,
    float v, void* value, HdTupleType dataType) const
{
    HdEmbree3TypeHelper::PrimvarTypeContainer corners[3];
    if (!_sampler.Sample(element*3 + 0, &corners[0], dataType) ||
        !_sampler.Sample(element*3 + 1, &corners[1], dataType) ||
        !_sampler.Sample(element*3 + 2, &corners[2], dataType)) {
        return false;
    }
    void* samples[3] = { static_cast<void*>(&corners[0]),
                         static_cast<void*>(&corners[1]),
                         static_cast<void*>(&corners[2]) };
    // Embree3 specification of triangle interpolation:
    // t_uv = (1-u-v)*t0 + u*t1 + v*t2
    float weights[3] = { 1.0f - u - v, u, v };
    return _Interpolate(value, samples, weights, 3, dataType);
}

/* static */ VtValue
HdEmbree3TriangleFaceVaryingSampler::_Triangulate(TfToken const& name,
    VtValue const& value, HdMeshUtil &meshUtil)
{
    HdVtBufferSource buffer(name, value);
    VtValue triangulated;
    if (!meshUtil.ComputeTriangulatedFaceVaryingPrimvar(
            buffer.GetData(),
            buffer.GetNumElements(),
            buffer.GetTupleType().type,
            &triangulated)) {
        TF_CODING_ERROR("[%s] Could not triangulate face-varying data.",
            name.GetText());
        return VtValue();
    }
    return triangulated;
}

// HdEmbree3SubdivVertexSampler

HdEmbree3SubdivVertexSampler::HdEmbree3SubdivVertexSampler(TfToken const& name,
    VtValue const& value, RTCScene meshScene, HdEmbree3RTCGeometry *geometry)
    : _slotId(-1)
    , _buffer(name, value)
    , _meshScene(meshScene)
    , _geometry(geometry)
{
    // The embree API only supports float-component primvars.
    if (HdGetComponentType(_buffer.GetTupleType().type) != HdTypeFloat) {
        TF_CODING_ERROR("Embree3 subdivision meshes only support float-based"
            " primvars for vertex interpolation mode");
        return;
    }
    _slotId = _geometry->GetSlot();
    // The embree API has a constant number of primvar slots (16 at last
    // count), shared between vertex and face-varying modes.
    if (_slotId == -1) {
        TF_CODING_ERROR("Embree3 subdivision meshes only support %d primvars"
            " in vertex interpolation mode", RTC_MAX_USER_VERTEX_BUFFERS);
        return;
    }

    _slotFormat = _geometry->GetFormat(_buffer);

    // set embree buffer
    rtcSetSharedGeometryBuffer(geometry->Get(),                                 // RTCGeometry
                              RTC_BUFFER_TYPE_VERTEX,                           // RTCBufferType
                              _slotId,                                          // Slot
                              (RTCFormat)_slotFormat,                           // RTCFormat
                              _buffer.GetData(),                                // Datas Ptr
                              0,                                                // Offset
                              HdDataSizeOfTupleType(_buffer.GetTupleType()),    // Stride
                              _buffer.GetNumElements());                        // Num Elements
    //// Tag the embree mesh object with the primvar buffer, for use by
    //// rtcInterpolate.
    //rtcSetBuffer(_meshScene, _meshId, _embreeBufferId,
    //    _buffer.GetData(),
    //    /* offset */ 0,
    //    /* stride */ HdDataSizeOfTupleType(_buffer.GetTupleType()));
}

HdEmbree3SubdivVertexSampler::~HdEmbree3SubdivVertexSampler()
{
    if (_slotId != -1) {
        _geometry->Free(_slotId);
    }
}

bool
HdEmbree3SubdivVertexSampler::Sample(unsigned int element, float u, float v,
    void* value, HdTupleType dataType) const
{
    // Make sure the buffer type and sample type have the same arity.
    // dataType of -1 indicates this sampler failed to initialize.
    if (_slotId == -1 || dataType != _buffer.GetTupleType()) {
        return false;
    }

    // Combine number of components in the underlying type and tuple arity.
    size_t numFloats = HdGetComponentCount(dataType.type) * dataType.count;

    RTCInterpolateArguments args;
    args.geometry = _geometry->Get();
    args.primID = element;
    args.u = u;
    args.v = v;
    args.bufferType = RTC_BUFFER_TYPE_VERTEX;
    args.bufferSlot = _slotId;
    args.P = static_cast<float*>(value);
    args.dPdu = NULL;
    args.dPdv = NULL;
    args.ddPdudu = NULL;
    args.ddPdvdv = NULL;
    args.ddPdudv = NULL;
    args.valueCount = numFloats;
    rtcInterpolate(&args);

    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
