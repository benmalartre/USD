//
// Copyright 2021 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hd/dataSourceLegacyPrim.h"

#include "pxr/imaging/hd/camera.h"
#include "pxr/imaging/hd/dataSource.h"
#include "pxr/imaging/hd/extComputationCpuCallback.h"
#include "pxr/imaging/hd/material.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/imaging/hd/meshTopology.h"
#include "pxr/imaging/hd/renderSettings.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/utils.h"

#include "pxr/imaging/hd/basisCurvesSchema.h"
#include "pxr/imaging/hd/basisCurvesTopologySchema.h"
#include "pxr/imaging/hd/cameraSchema.h"
#include "pxr/imaging/hd/categoriesSchema.h"
#include "pxr/imaging/hd/collectionSchema.h"
#include "pxr/imaging/hd/collectionsSchema.h"
#include "pxr/imaging/hd/coordSysBindingSchema.h"
#include "pxr/imaging/hd/dependenciesSchema.h"
#include "pxr/imaging/hd/dependencySchema.h"
#include "pxr/imaging/hd/displayFilterSchema.h"
#include "pxr/imaging/hd/extComputationInputComputationSchema.h"
#include "pxr/imaging/hd/extComputationOutputSchema.h"
#include "pxr/imaging/hd/extComputationPrimvarSchema.h"
#include "pxr/imaging/hd/extComputationPrimvarsSchema.h"
#include "pxr/imaging/hd/extComputationSchema.h"
#include "pxr/imaging/hd/extentSchema.h"
#include "pxr/imaging/hd/imageShaderSchema.h"
#include "pxr/imaging/hd/instanceCategoriesSchema.h"
#include "pxr/imaging/hd/instancedBySchema.h"
#include "pxr/imaging/hd/instancerTopologySchema.h"
#include "pxr/imaging/hd/integratorSchema.h"
#include "pxr/imaging/hd/legacyDisplayStyleSchema.h"
#include "pxr/imaging/hd/lensDistortionSchema.h"
#include "pxr/imaging/hd/lightSchema.h"
#include "pxr/imaging/hd/materialBindingsSchema.h"
#include "pxr/imaging/hd/materialNodeSchema.h"
#include "pxr/imaging/hd/materialNodeParameterSchema.h"
#include "pxr/imaging/hd/materialSchema.h"
#include "pxr/imaging/hd/meshSchema.h"
#include "pxr/imaging/hd/meshTopologySchema.h"
#include "pxr/imaging/hd/primvarSchema.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/purposeSchema.h"
#include "pxr/imaging/hd/renderBufferSchema.h"
#include "pxr/imaging/hd/renderProductSchema.h"
#include "pxr/imaging/hd/renderSettingsSchema.h"
#include "pxr/imaging/hd/renderVarSchema.h"
#include "pxr/imaging/hd/sampleFilterSchema.h"
#include "pxr/imaging/hd/splitDiopterSchema.h"
#include "pxr/imaging/hd/subdivisionTagsSchema.h"
#include "pxr/imaging/hd/visibilitySchema.h"
#include "pxr/imaging/hd/volumeFieldBindingSchema.h"
#include "pxr/imaging/hd/volumeFieldSchema.h"
#include "pxr/imaging/hd/xformSchema.h"

#include "pxr/usd/sdf/path.h"

#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/types.h"

#include <algorithm>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdLegacyPrimTypeTokens, HD_LEGACY_PRIMTYPE_TOKENS);

TF_DEFINE_PUBLIC_TOKENS(HdLegacyFlagTokens, HD_LEGACY_FLAG_TOKENS);

// XXX: currently private and duplicated where used so as to not yet formally
//      define this convention.
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (binding)
    (coordSys)
    (lightLinkingCollectionsDependency)
);

// ----------------------------------------------------------------------------

bool
HdLegacyPrimTypeIsVolumeField(TfToken const &primType)
{
    return (primType == HdLegacyPrimTypeTokens->openvdbAsset ||
            primType == HdLegacyPrimTypeTokens->field3dAsset);
}

// ----------------------------------------------------------------------------

namespace {

class Hd_SceneDelegateExtComputationCpuCallback
      : public HdExtComputationCpuCallback
{
public:
    Hd_SceneDelegateExtComputationCpuCallback(
        const SdfPath &id, HdSceneDelegate * const sceneDelegate)
      : _id(id), _sceneDelegate(sceneDelegate) { }
    
    void Compute(HdExtComputationContext * const ctx) override
    {
        _sceneDelegate->InvokeExtComputation(_id, ctx);
    }

private:
    const SdfPath _id;
    HdSceneDelegate * const _sceneDelegate;
};
  
class Hd_DataSourceLegacyPrimvarValue : public HdSampledDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceLegacyPrimvarValue);

    Hd_DataSourceLegacyPrimvarValue(
        const TfToken &primvarName,
        const SdfPath &primId,
        HdSceneDelegate *sceneDelegate)
    : _primvarName(primvarName)
    , _primId(primId)
    , _sceneDelegate(sceneDelegate)
    {
        TF_VERIFY(_sceneDelegate);
    }

    VtValue GetValue(Time shutterOffset) override
    {
        if (shutterOffset == 0) {
            VtValue result = _sceneDelegate->Get(_primId, _primvarName);
            if (!result.IsEmpty()) {
                return result;
            }

            // XXX: In UsdImaging, lights derived from the base prim adapter
            //      directly and therefore its Get doesn't have "primvars:"
            //      namespace awareness. It is supported by SamplePrimvar so
            //      we can handle we'll fall back to use that if the Get
            //      query fails
            float sampleTimes;
            _sceneDelegate->SamplePrimvar(
                  _primId, _primvarName, 1,  &sampleTimes,  &result);

            return result;
        } else {
            _GetTimeSamples();
            return _timeSamples.Resample(shutterOffset);
        }
    }

    bool GetContributingSampleTimesForInterval(
        Time startTime, Time endTime,
        std::vector<Time> *outSampleTimes) override
    {
        _GetTimeSamples();

        // XXX: Start and end times come from the sene delegate, so we can't
        // get samples outside of those provided. However, we can clamp
        // returned samples to be in the right range.
        return _timeSamples.GetContributingSampleTimesForInterval(
            startTime, endTime, outSampleTimes);
    }

private:
    void _GetTimeSamples()
    {
        if (_timeSamples.count == 0) {
            _sceneDelegate->SamplePrimvar(_primId, _primvarName, &_timeSamples);
        }
    }

    TfToken _primvarName;
    SdfPath _primId;
    HdTimeSampleArray<VtValue, 1> _timeSamples;
    HdSceneDelegate *_sceneDelegate;
};

// ----------------------------------------------------------------------------

class Hd_DataSourceLegacyIndexedPrimvarValue : public HdSampledDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceLegacyIndexedPrimvarValue);

    Hd_DataSourceLegacyIndexedPrimvarValue(
        const TfToken &primvarName,
        const SdfPath &primId,
        HdSceneDelegate *sceneDelegate)
    : _primvarName(primvarName)
    , _primId(primId)
    , _sceneDelegate(sceneDelegate)
    {
        TF_VERIFY(_sceneDelegate);
    }

    VtValue GetValue(Time shutterOffset) override
    {
        if (shutterOffset == 0) {
            VtIntArray indices(0);
            return _sceneDelegate->GetIndexedPrimvar(_primId, _primvarName, 
                    &indices);
            
        } else {
            _GetTimeSamples();
            std::pair<VtValue, VtIntArray> pv =  
                _timeSamples.ResampleIndexed(shutterOffset);
            return pv.first;
        }
    }

    bool GetContributingSampleTimesForInterval(
        Time startTime,
        Time endTime,
        std::vector<Time> *outSampleTimes) override
    {
        _GetTimeSamples();

        // XXX: Start and end times come from the sene delegate, so we can't
        // get samples outside of those provided. However, we can clamp
        // returned samples to be in the right range.
        _timeSamples.GetContributingSampleTimesForInterval(
            startTime, endTime, outSampleTimes);

        return true;
    }

private:
    void _GetTimeSamples()
    {
        if (_timeSamples.count == 0) {
            _sceneDelegate->SampleIndexedPrimvar(_primId, _primvarName, 
                    &_timeSamples);
        }
    }

    TfToken _primvarName;
    SdfPath _primId;
    HdIndexedTimeSampleArray<VtValue, 1> _timeSamples;
    HdSceneDelegate *_sceneDelegate;
};

// ----------------------------------------------------------------------------

class Hd_DataSourceLegacyPrimvarIndices : 
    public HdTypedSampledDataSource<VtArray<int>>
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceLegacyPrimvarIndices);

    Hd_DataSourceLegacyPrimvarIndices(
        const TfToken &primvarName,
        const SdfPath &primId,
        HdSceneDelegate *sceneDelegate)
    : _primvarName(primvarName)
    , _primId(primId)
    , _sceneDelegate(sceneDelegate)
    {
        TF_VERIFY(_sceneDelegate);
    }

    VtValue GetValue(Time shutterOffset) override
    {
        return VtValue(GetTypedValue(shutterOffset));
    }

    VtIntArray GetTypedValue(Time shutterOffset) override
    {
        if (shutterOffset == 0) {
            VtIntArray indices(0);
            VtValue value = _sceneDelegate->GetIndexedPrimvar(_primId, 
                    _primvarName, &indices);
            return indices;
        } else {
            _GetTimeSamples();
            std::pair<VtValue, VtIntArray> pv = 
                    _timeSamples.ResampleIndexed(shutterOffset);
            return pv.second;
        }
    }

    bool GetContributingSampleTimesForInterval(
        Time startTime,
        Time endTime,
        std::vector<Time> *outSampleTimes) override
    {
        _GetTimeSamples();

        // XXX: Start and end times come from the sene delegate, so we can't
        // get samples outside of those provided. However, we can clamp
        // returned samples to be in the right range.
        _timeSamples.GetContributingSampleTimesForInterval(
            startTime, endTime, outSampleTimes);
        return true;
    }

private:
    void _GetTimeSamples()
    {
        if (_timeSamples.count == 0) {
            _sceneDelegate->SampleIndexedPrimvar(
                _primId, _primvarName, &_timeSamples);
        }
    }

    TfToken _primvarName;
    SdfPath _primId;
    HdIndexedTimeSampleArray<VtValue, 1> _timeSamples;
    HdSceneDelegate *_sceneDelegate;
};

// ----------------------------------------------------------------------------

class Hd_DataSourceLegacyPrimvarsContainer : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceLegacyPrimvarsContainer);

    Hd_DataSourceLegacyPrimvarsContainer(
        const SdfPath &primId,
        HdSceneDelegate *sceneDelegate)
    : _primId(primId)
    , _sceneDelegate(sceneDelegate)
    {
        TF_VERIFY(_sceneDelegate);
    }

    void AddDesc(const TfToken &name, const TfToken &interpolation,
        const TfToken &role, bool indexed)
    {
        _entries[name] = {interpolation, role, indexed};
    }

    TfTokenVector GetNames() override
    {
        TfTokenVector result;
        result.reserve(_entries.size());
        for (const auto &pair : _entries) {
            result.push_back(pair.first);
        }
        return result;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        _EntryMap::const_iterator it = _entries.find(name);
        if (it == _entries.end()) {
            return nullptr;
        }

        if ((*it).second.indexed) {
            return HdPrimvarSchema::Builder()
                .SetIndexedPrimvarValue(
                    Hd_DataSourceLegacyIndexedPrimvarValue::New(
                        name, _primId, _sceneDelegate))
                .SetIndices(
                    Hd_DataSourceLegacyPrimvarIndices::New(
                        name, _primId, _sceneDelegate))
                .SetInterpolation(
                    HdPrimvarSchema::BuildInterpolationDataSource(
                        (*it).second.interpolation))
                .SetRole(
                    HdPrimvarSchema::BuildRoleDataSource(
                        (*it).second.role))
                .Build();
        } else {
            return HdPrimvarSchema::Builder()
                .SetPrimvarValue(
                    Hd_DataSourceLegacyPrimvarValue::New(
                        name, _primId, _sceneDelegate))
                .SetInterpolation(
                    HdPrimvarSchema::BuildInterpolationDataSource(
                        (*it).second.interpolation))
                .SetRole(
                    HdPrimvarSchema::BuildRoleDataSource(
                        (*it).second.role))
                .Build();
        }
    }

private:
    struct _Entry
    {
        TfToken interpolation;
        TfToken role;
        bool indexed;
    };

    using _EntryMap =  TfDenseHashMap<TfToken, _Entry,
            TfToken::HashFunctor, std::equal_to<TfToken>, 32>;

    _EntryMap _entries;
    SdfPath _primId;
    HdSceneDelegate *_sceneDelegate;
};

HD_DECLARE_DATASOURCE_HANDLES(Hd_DataSourceLegacyPrimvarsContainer);

// ----------------------------------------------------------------------------

class Hd_DataSourceLegacyMatrixValue : public HdMatrixDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceLegacyMatrixValue);

    Hd_DataSourceLegacyMatrixValue(
        const TfToken &type,
        const SdfPath &primId,
        HdSceneDelegate *sceneDelegate)
    : _type(type)
    , _primId(primId)
    , _sceneDelegate(sceneDelegate)
    {
        TF_VERIFY(_sceneDelegate);
    }

    VtValue GetValue(Time shutterOffset) override
    {
        return VtValue(GetTypedValue(shutterOffset));
    }

    GfMatrix4d GetTypedValue(Time shutterOffset) override
    {
        if (shutterOffset == 0) {
            if (_type == HdPrimTypeTokens->instancer) {
                return _sceneDelegate->GetInstancerTransform(_primId);
            } else {
                return _sceneDelegate->GetTransform(_primId);
            }
        } else {
            _GetTimeSamples();
            return _timeSamples.Resample(shutterOffset);
        }
    }

    bool GetContributingSampleTimesForInterval(
        Time startTime,
        Time endTime,
        std::vector<Time> *outSampleTimes) override
    {
        _GetTimeSamples();

        // XXX: Start and end times come from the scene delegate, so we can't
        // get samples outside of those provided. However, we can clamp
        // returned samples to be in the right range.
        _timeSamples.GetContributingSampleTimesForInterval(
            startTime, endTime, outSampleTimes);
        return true;
    }

private:
    void _GetTimeSamples()
    {
        if (_timeSamples.count == 0) {
            if (_type == HdPrimTypeTokens->instancer) {
                _sceneDelegate->SampleInstancerTransform(_primId, 
                    &_timeSamples);
            } else {
                _sceneDelegate->SampleTransform(_primId, &_timeSamples);
            }
        }
    }

    TfToken _type;
    SdfPath _primId;
    HdTimeSampleArray<GfMatrix4d, 1> _timeSamples;
    HdSceneDelegate *_sceneDelegate;
};

// ----------------------------------------------------------------------------

using Hd_MeshTopologyStoreSharedPtr =
    std::shared_ptr<class Hd_MeshTopologyStore>;

class Hd_MeshTopologyStore
{
public:
    Hd_MeshTopologyStore(const SdfPath &id, HdSceneDelegate *sceneDelegate)
    : _id(id)
    , _sceneDelegate(sceneDelegate)
    {
    }

    HdMeshTopologySharedPtr Get()
    {
        HdMeshTopologySharedPtr mt = std::atomic_load(&_meshTopology);
        if (mt) {
            return mt;
        }

        mt = std::make_shared<HdMeshTopology>(
            _sceneDelegate->GetMeshTopology(_id));
        std::atomic_store(&_meshTopology, mt);
        return mt;
    }

    void Invalidate()
    {
        HdMeshTopologySharedPtr nullmt;
        std::atomic_store(&_meshTopology, nullmt);
    }

    template <typename T>
    class MemberDataSource : public HdTypedSampledDataSource<T>
    {
    public:
        HD_DECLARE_DATASOURCE_ABSTRACT(MemberDataSource<T>);

        T GetTypedValue(HdSampledDataSource::Time shutterOffset) override = 0;

        VtValue GetValue(HdSampledDataSource::Time shutterOffset) override
        {
            return VtValue(GetTypedValue(shutterOffset));
        }

        bool GetContributingSampleTimesForInterval( 
                HdSampledDataSource::Time startTime, 
                HdSampledDataSource::Time endTime,
                std::vector<HdSampledDataSource::Time> * outSampleTimes)
                    override
        {
            return false;
        }

        MemberDataSource(const Hd_MeshTopologyStoreSharedPtr &mts)
        : _mts(mts)
        {
        }

    protected:
        Hd_MeshTopologyStoreSharedPtr _mts;
    };


    #define DEFINE_MESH_TOPOLOGY_ACCESSOR_DATASOURCE(N, T, A) \
    class N : public MemberDataSource<T> \
    {                                                                         \
    public:                                                                   \
        HD_DECLARE_DATASOURCE(N);                                             \
                                                                              \
        N(const Hd_MeshTopologyStoreSharedPtr &mts)                           \
        : MemberDataSource(mts) {}                                            \
                                                                              \
        T GetTypedValue(HdSampledDataSource::Time shutterOffset) override     \
        {                                                                     \
            return _mts->Get()->A();                                          \
        }                                                                     \
    };

    DEFINE_MESH_TOPOLOGY_ACCESSOR_DATASOURCE(
        FaceVertexCountsDataSource, VtArray<int>, GetFaceVertexCounts);
    DEFINE_MESH_TOPOLOGY_ACCESSOR_DATASOURCE(
        FaceVertexIndicesDataSource, VtArray<int>, GetFaceVertexIndices);
    DEFINE_MESH_TOPOLOGY_ACCESSOR_DATASOURCE(
        HoleIndicesDataSource, VtArray<int>, GetHoleIndices);
    DEFINE_MESH_TOPOLOGY_ACCESSOR_DATASOURCE(
        OrientationDataSource, TfToken, GetOrientation);
    DEFINE_MESH_TOPOLOGY_ACCESSOR_DATASOURCE(
        SubdivisionSchemeDataSource, TfToken, GetScheme);

private:
    SdfPath _id;
    HdSceneDelegate *_sceneDelegate;
    HdMeshTopologySharedPtr _meshTopology;
};


class Hd_DataSourceMeshTopology : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceMeshTopology);
    Hd_DataSourceMeshTopology(const Hd_MeshTopologyStoreSharedPtr &mts)
    : _mts(mts)
    {
    }

    TfTokenVector GetNames() override
    {
        return {
            HdMeshTopologySchemaTokens->faceVertexCounts,
            HdMeshTopologySchemaTokens->faceVertexIndices,
            HdMeshTopologySchemaTokens->holeIndices,
            HdMeshTopologySchemaTokens->orientation,
        };
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (name == HdMeshTopologySchemaTokens->faceVertexCounts) {
            return Hd_MeshTopologyStore::FaceVertexCountsDataSource::New(_mts);
        }
        if (name == HdMeshTopologySchemaTokens->faceVertexIndices) {
            return Hd_MeshTopologyStore::FaceVertexIndicesDataSource::New(_mts);
        }
        if (name == HdMeshTopologySchemaTokens->holeIndices) {
            return Hd_MeshTopologyStore::HoleIndicesDataSource::New(_mts);
        }
        if (name == HdMeshTopologySchemaTokens->orientation) {
            return Hd_MeshTopologyStore::OrientationDataSource::New(_mts);
        }

        return nullptr;
    }

private:
    Hd_MeshTopologyStoreSharedPtr _mts;
};


class Hd_DataSourceMesh : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceMesh);

    Hd_DataSourceMesh(const SdfPath &id, HdSceneDelegate *sceneDelegate)
    : _id(id)
    , _sceneDelegate(sceneDelegate)
    {
    }

    TfTokenVector GetNames() override
    {
        return {
            HdMeshSchemaTokens->topology,
            HdMeshSchemaTokens->subdivisionTags,
            HdMeshSchemaTokens->subdivisionScheme,
            HdMeshSchemaTokens->doubleSided,
        };
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (name == HdMeshSchemaTokens->topology) {
            return Hd_DataSourceMeshTopology::New(_GetMeshTopologyStore());
        }

        if (name == HdMeshSchemaTokens->subdivisionTags) {
            PxOsdSubdivTags t = _sceneDelegate->GetSubdivTags(_id);
            return
                HdSubdivisionTagsSchema::Builder()
                    .SetFaceVaryingLinearInterpolation(
                        HdRetainedTypedSampledDataSource<TfToken>::New(
                            t.GetFaceVaryingInterpolationRule()))
                    .SetInterpolateBoundary(
                        HdRetainedTypedSampledDataSource<TfToken>::New(
                            t.GetVertexInterpolationRule()))
                    .SetTriangleSubdivisionRule(
                        HdRetainedTypedSampledDataSource<TfToken>::New(
                            t.GetTriangleSubdivision()))
                    .SetCornerIndices(
                        HdRetainedTypedSampledDataSource<VtIntArray>::New(
                            t.GetCornerIndices()))
                    .SetCornerSharpnesses(
                        HdRetainedTypedSampledDataSource<VtFloatArray>::New(
                            t.GetCornerWeights()))
                    .SetCreaseIndices(
                        HdRetainedTypedSampledDataSource<VtIntArray>::New(
                            t.GetCreaseIndices()))
                    .SetCreaseLengths(
                        HdRetainedTypedSampledDataSource<VtIntArray>::New(
                            t.GetCreaseLengths()))
                    .SetCreaseSharpnesses(
                        HdRetainedTypedSampledDataSource<VtFloatArray>::New(
                            t.GetCreaseWeights()))
                    .Build();
        }

        if (name == HdMeshSchemaTokens->subdivisionScheme) {
            return  Hd_MeshTopologyStore::SubdivisionSchemeDataSource::New(
                _GetMeshTopologyStore());
        }

        if (name == HdMeshSchemaTokens->doubleSided) {
            return HdRetainedTypedSampledDataSource<bool>::New(
                _sceneDelegate->GetDoubleSided(_id));
        }
        
        return nullptr;
    }

private:
    Hd_MeshTopologyStoreSharedPtr _GetMeshTopologyStore()
    {
        Hd_MeshTopologyStoreSharedPtr mts =
            std::atomic_load(&_meshTopologyStore);
        if (mts) {
            return mts;
        }

        mts = std::make_shared<Hd_MeshTopologyStore>(_id, _sceneDelegate);
        std::atomic_store(&_meshTopologyStore, mts);

        return mts;
    }

    SdfPath _id;
    HdSceneDelegate *_sceneDelegate;
    Hd_MeshTopologyStoreSharedPtr _meshTopologyStore;
};

// ----------------------------------------------------------------------------

using Hd_BasisCurvesTopologyStoreSharedPtr =
    std::shared_ptr<class Hd_BasisCurvesTopologyStore>;
using HdBasisCurvesTopologySharedPtr = std::shared_ptr<HdBasisCurvesTopology>;

class Hd_BasisCurvesTopologyStore
{
public:
    Hd_BasisCurvesTopologyStore(
        const SdfPath &id, HdSceneDelegate *sceneDelegate)
    : _id(id)
    , _sceneDelegate(sceneDelegate)
    {
    }

    HdBasisCurvesTopologySharedPtr Get()
    {
        HdBasisCurvesTopologySharedPtr bct = std::atomic_load(
            &_basisCurvesTopology);
        if (bct) {
            return bct;
        }

        bct = std::make_shared<HdBasisCurvesTopology>(
            _sceneDelegate->GetBasisCurvesTopology(_id));
        std::atomic_store(&_basisCurvesTopology, bct);
        return bct;
    }

    void Invalidate()
    {
        HdBasisCurvesTopologySharedPtr nullbct;
        std::atomic_store(&_basisCurvesTopology, nullbct);
    }

    template <typename T>
    class MemberDataSource : public HdTypedSampledDataSource<T>
    {
    public:
        HD_DECLARE_DATASOURCE_ABSTRACT(MemberDataSource<T>);

        T GetTypedValue(HdSampledDataSource::Time shutterOffset) override = 0;

        VtValue GetValue(HdSampledDataSource::Time shutterOffset) override
        {
            return VtValue(GetTypedValue(shutterOffset));
        }

        bool GetContributingSampleTimesForInterval( 
                HdSampledDataSource::Time startTime, 
                HdSampledDataSource::Time endTime,
                std::vector<HdSampledDataSource::Time> * outSampleTimes)
                    override
        {
            return false;
        }

        MemberDataSource(const Hd_BasisCurvesTopologyStoreSharedPtr &bcts)
        : _bcts(bcts)
        {
        }

    protected:
        Hd_BasisCurvesTopologyStoreSharedPtr _bcts;
    };

    #define DEFINE_BASISCURVES_TOPOLOGY_ACCESSOR_DATASOURCE(N, T, A) \
    class N : public MemberDataSource<T> \
    {                                                                         \
    public:                                                                   \
        HD_DECLARE_DATASOURCE(N);                                             \
                                                                              \
        N(const Hd_BasisCurvesTopologyStoreSharedPtr &bcts)                   \
        : MemberDataSource(bcts) {}                                           \
                                                                              \
        T GetTypedValue(HdSampledDataSource::Time shutterOffset) override     \
        {                                                                     \
            return _bcts->Get()->A();                                         \
        }                                                                     \
    };

    DEFINE_BASISCURVES_TOPOLOGY_ACCESSOR_DATASOURCE(
        CurveTypeDataSource, TfToken, GetCurveType);
    DEFINE_BASISCURVES_TOPOLOGY_ACCESSOR_DATASOURCE(
        CurveWrapDataSource, TfToken, GetCurveWrap);
    DEFINE_BASISCURVES_TOPOLOGY_ACCESSOR_DATASOURCE(
        CurveBasisDataSource, TfToken, GetCurveBasis);
    DEFINE_BASISCURVES_TOPOLOGY_ACCESSOR_DATASOURCE(
        CurveVertexCountsDataSource, VtArray<int>, GetCurveVertexCounts);
    DEFINE_BASISCURVES_TOPOLOGY_ACCESSOR_DATASOURCE(
        CurveIndicesDataSource, VtArray<int>, GetCurveIndices);

private:
    SdfPath _id;
    HdSceneDelegate *_sceneDelegate;
    HdBasisCurvesTopologySharedPtr _basisCurvesTopology;
};



class Hd_DataSourceBasisCurvesTopology : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceBasisCurvesTopology);
    Hd_DataSourceBasisCurvesTopology(const Hd_BasisCurvesTopologyStoreSharedPtr &bcts)
    : _bcts(bcts)
    {
    }

    TfTokenVector GetNames() override
    {
        return {
            HdBasisCurvesTopologySchemaTokens->curveVertexCounts,
            HdBasisCurvesTopologySchemaTokens->curveIndices,
            HdBasisCurvesTopologySchemaTokens->basis,
            HdBasisCurvesTopologySchemaTokens->type,
            HdBasisCurvesTopologySchemaTokens->wrap,
        };
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (name == HdBasisCurvesTopologySchemaTokens->curveVertexCounts) {
            return Hd_BasisCurvesTopologyStore::
                CurveVertexCountsDataSource::New(_bcts);
        }
        if (name == HdBasisCurvesTopologySchemaTokens->curveIndices) {
            return Hd_BasisCurvesTopologyStore::
                CurveIndicesDataSource::New(_bcts);
        }
        if (name == HdBasisCurvesTopologySchemaTokens->basis) {
            return Hd_BasisCurvesTopologyStore::
                CurveBasisDataSource::New(_bcts);
        }
        if (name == HdBasisCurvesTopologySchemaTokens->type) {
            return Hd_BasisCurvesTopologyStore::
                CurveTypeDataSource::New(_bcts);
        }
        if (name == HdBasisCurvesTopologySchemaTokens->wrap) {
            return Hd_BasisCurvesTopologyStore::
                CurveWrapDataSource::New(_bcts);
        }
        return nullptr;
    }

private:
    Hd_BasisCurvesTopologyStoreSharedPtr _bcts;
};

class Hd_DataSourceBasisCurves : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceBasisCurves);

    Hd_DataSourceBasisCurves(const SdfPath &id, HdSceneDelegate *sceneDelegate)
    : _id(id)
    , _sceneDelegate(sceneDelegate)
    {
    }

    TfTokenVector GetNames() override
    {
        return {
            HdBasisCurvesSchemaTokens->topology,
        };
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (name == HdBasisCurvesSchemaTokens->topology) {
            return Hd_DataSourceBasisCurvesTopology::New(
                _GetBasisCurvesTopologyStore());
        }

        return nullptr;
    }
private:
    Hd_BasisCurvesTopologyStoreSharedPtr _GetBasisCurvesTopologyStore()
    {
        Hd_BasisCurvesTopologyStoreSharedPtr bcts =
            std::atomic_load(&_basisCurvesTopologyStore);
        if (bcts) {
            return bcts;
        }

        bcts =
            std::make_shared<Hd_BasisCurvesTopologyStore>(_id, _sceneDelegate);
        std::atomic_store(&_basisCurvesTopologyStore, bcts);

        return bcts;
    }

    SdfPath _id;
    HdSceneDelegate *_sceneDelegate;
    Hd_BasisCurvesTopologyStoreSharedPtr _basisCurvesTopologyStore;

};

// ----------------------------------------------------------------------------

template <typename T>
class Hd_TypedDataSourceLegacyCameraParamValue : public HdTypedSampledDataSource<T>
{
public:
    HD_DECLARE_DATASOURCE(Hd_TypedDataSourceLegacyCameraParamValue<T>);

    Hd_TypedDataSourceLegacyCameraParamValue(
        const SdfPath &id,
        const TfToken &key,
        HdSceneDelegate *sceneDelegate)
    : _id(id)
    , _key(key)
    , _sceneDelegate(sceneDelegate)
    {
        TF_VERIFY(_sceneDelegate);
    }

    bool GetContributingSampleTimesForInterval( 
        HdSampledDataSource::Time startTime, 
        HdSampledDataSource::Time endTime,
        std::vector<HdSampledDataSource::Time> * outSampleTimes)  override
    {
        return Hd_DataSourceLegacyPrimvarValue::New(
            _key, _id, _sceneDelegate)->GetContributingSampleTimesForInterval(
                startTime, endTime, outSampleTimes);
    }

    T GetTypedValue(HdSampledDataSource::Time shutterOffset) override
    {
        VtValue v;
        if (shutterOffset == 0.0f) {
            v = _sceneDelegate->GetCameraParamValue(_id, _key);
        } else {
            v = Hd_DataSourceLegacyPrimvarValue::New(
                _key, _id, _sceneDelegate)->GetValue(shutterOffset);
        }

        if (v.IsHolding<T>()) {
            return v.UncheckedGet<T>();
        }

        return T();
    }

    VtValue GetValue(HdSampledDataSource::Time shutterOffset) override
    {
        if (shutterOffset == 0.0f) {
            return _sceneDelegate->GetCameraParamValue(_id, _key);
        }

        return VtValue(GetTypedValue(shutterOffset));
    }

private:
    SdfPath _id;
    TfToken _key;
    HdSceneDelegate *_sceneDelegate;
};


class Hd_DataSourceLegacyCameraParamValue : public HdSampledDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceLegacyCameraParamValue);

    Hd_DataSourceLegacyCameraParamValue(
        const SdfPath &id,
        const TfToken &key,
        HdSceneDelegate *sceneDelegate)
    : _id(id)
    , _key(key)
    , _sceneDelegate(sceneDelegate)
    {
        TF_VERIFY(_sceneDelegate);
    }

    bool GetContributingSampleTimesForInterval( 
        HdSampledDataSource::Time startTime, 
        HdSampledDataSource::Time endTime,
        std::vector<HdSampledDataSource::Time> * outSampleTimes)  override
    {
        return Hd_DataSourceLegacyPrimvarValue::New(
            _key, _id, _sceneDelegate)->GetContributingSampleTimesForInterval(
                startTime, endTime, outSampleTimes);
    }

    VtValue GetValue(HdSampledDataSource::Time shutterOffset) override
    {
        if (shutterOffset == 0.0f) {
            return _sceneDelegate->GetCameraParamValue(_id, _key);
        }

        return Hd_DataSourceLegacyPrimvarValue::New(
                _key, _id, _sceneDelegate)->GetValue(shutterOffset);
    }

private:
    SdfPath _id;
    TfToken _key;
    HdSceneDelegate *_sceneDelegate;
};


class Hd_DataSourceCamera : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceCamera);

    Hd_DataSourceCamera(const SdfPath &id, HdSceneDelegate *sceneDelegate)
    : _id(id)
    , _sceneDelegate(sceneDelegate)
    {
        TF_VERIFY(_sceneDelegate);
    }

    TfTokenVector GetNames() override
    {
        // HdSceneDelegate has no API for advertising what keys it will provide
        // an answer for in response to HdSceneDelegate::GetCameraParamValue.
        // Because HdContainerDataSource does (with this method), we take our
        // best guess by answering with the common ones defined by
        // HdCameraSchema.

        return {
            HdCameraSchemaTokens->projection,
            HdCameraSchemaTokens->horizontalAperture,
            HdCameraSchemaTokens->verticalAperture,
            HdCameraSchemaTokens->horizontalApertureOffset,
            HdCameraSchemaTokens->verticalApertureOffset,
            HdCameraSchemaTokens->focalLength,
            HdCameraSchemaTokens->clippingRange,
            HdCameraSchemaTokens->clippingPlanes,
            HdCameraSchemaTokens->fStop,
            HdCameraSchemaTokens->focusDistance,
            HdCameraSchemaTokens->shutterOpen,
            HdCameraSchemaTokens->shutterClose,
            HdCameraSchemaTokens->exposure,
            HdCameraSchemaTokens->exposureTime,
            HdCameraSchemaTokens->exposureIso,
            HdCameraSchemaTokens->exposureFStop,
            HdCameraSchemaTokens->exposureResponsivity,
            HdCameraSchemaTokens->linearExposureScale,
            HdCameraSchemaTokens->focusOn,
            HdCameraSchemaTokens->dofAspect,
            HdCameraSchemaTokens->splitDiopter,
            HdCameraSchemaTokens->lensDistortion,
        };
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        // Smooth out some incompatibilities between scene delegate and
        // datasource schemas...
        if (name == HdCameraSchemaTokens->projection) {
            VtValue v = _sceneDelegate->GetCameraParamValue(_id, name);
        
            HdCamera::Projection proj = HdCamera::Perspective;
            if (v.IsHolding<HdCamera::Projection>()) {
                proj = v.UncheckedGet<HdCamera::Projection>();
            }
            return HdRetainedTypedSampledDataSource<TfToken>::New(
                proj == HdCamera::Perspective ?
                HdCameraSchemaTokens->perspective :
                HdCameraSchemaTokens->orthographic);
        } else if (name == HdCameraSchemaTokens->clippingRange) {
            VtValue v = _sceneDelegate->GetCameraParamValue(_id, name);
        
            GfRange1f range;
            if (v.IsHolding<GfRange1f>()) {
                range = v.UncheckedGet<GfRange1f>();
            }
            return HdRetainedTypedSampledDataSource<GfVec2f>::New(
                GfVec2f(range.GetMin(), range.GetMax()));
        } else if (name == HdCameraTokens->windowPolicy) {
            VtValue v = _sceneDelegate->GetCameraParamValue(_id, name);
        
            // XXX: this should probably be in the schema, and a token...
            CameraUtilConformWindowPolicy wp = CameraUtilDontConform;
            if (v.IsHolding<CameraUtilConformWindowPolicy>()) {
                wp = v.UncheckedGet<CameraUtilConformWindowPolicy>();
            }
            return HdRetainedTypedSampledDataSource<
                        CameraUtilConformWindowPolicy>::New(wp);
        } else if (name == HdCameraSchemaTokens->clippingPlanes) {
            const VtValue v = _sceneDelegate->GetCameraParamValue(
                _id, HdCameraTokens->clipPlanes);
            VtArray<GfVec4d> array;
            if (v.IsHolding<std::vector<GfVec4d>>()) {
                const std::vector<GfVec4d> &vec =
                    v.UncheckedGet<std::vector<GfVec4d>>();
                array.resize(vec.size());
                for (size_t i = 0; i < vec.size(); i++) {
                    array[i] = vec[i];
                }
            }
            return HdRetainedTypedSampledDataSource<VtArray<GfVec4d>>::New(
                array);
        } else if (name == HdCameraSchemaTokens->shutterOpen ||
                   name == HdCameraSchemaTokens->shutterClose) {
            return Hd_TypedDataSourceLegacyCameraParamValue<double>::New(
                _id, name, _sceneDelegate);
        } else if (name == HdCameraSchemaTokens->focusOn) {
            return Hd_TypedDataSourceLegacyCameraParamValue<bool>::New(
                _id, name, _sceneDelegate);
        } else if (name == HdCameraSchemaTokens->splitDiopter) {
            return _BuildSplitDiopter();
        } else if (name == HdCameraSchemaTokens->lensDistortion) {
            return _BuildLensDistortion();
        } else if (std::find(HdCameraSchemaTokens->allTokens.begin(),
                HdCameraSchemaTokens->allTokens.end(), name)
                    != HdCameraSchemaTokens->allTokens.end()) {
            // all remaining HdCameraSchema members are floats and should
            // be returned as a typed data source for schema conformance.
            return Hd_TypedDataSourceLegacyCameraParamValue<float>::New(
                _id, name, _sceneDelegate);
        } else {
            return Hd_DataSourceLegacyCameraParamValue::New(
                _id, name, _sceneDelegate);
        }
    }

private:
    HdDataSourceBaseHandle _BuildSplitDiopter()
    {
        return
            HdSplitDiopterSchema::Builder()
                .SetCount(
                    Hd_TypedDataSourceLegacyCameraParamValue<int>::New(
                        _id, HdCameraTokens->splitDiopterCount, _sceneDelegate))
                .SetAngle(
                    Hd_TypedDataSourceLegacyCameraParamValue<float>::New(
                        _id, HdCameraTokens->splitDiopterAngle, _sceneDelegate))
                .SetOffset1(
                    Hd_TypedDataSourceLegacyCameraParamValue<float>::New(
                        _id, HdCameraTokens->splitDiopterOffset1, _sceneDelegate))
                .SetWidth1(
                    Hd_TypedDataSourceLegacyCameraParamValue<float>::New(
                        _id, HdCameraTokens->splitDiopterWidth1, _sceneDelegate))
                .SetFocusDistance1(
                    Hd_TypedDataSourceLegacyCameraParamValue<float>::New(
                        _id, HdCameraTokens->splitDiopterFocusDistance1, _sceneDelegate))
                .SetOffset2(
                    Hd_TypedDataSourceLegacyCameraParamValue<float>::New(
                        _id, HdCameraTokens->splitDiopterOffset2, _sceneDelegate))
                .SetWidth2(
                    Hd_TypedDataSourceLegacyCameraParamValue<float>::New(
                        _id, HdCameraTokens->splitDiopterWidth2, _sceneDelegate))
                .SetFocusDistance2(
                    Hd_TypedDataSourceLegacyCameraParamValue<float>::New(
                        _id, HdCameraTokens->splitDiopterFocusDistance2, _sceneDelegate))
                .Build();
    }

    HdDataSourceBaseHandle _BuildLensDistortion()
    {
        return
            HdLensDistortionSchema::Builder()
                .SetType(
                    Hd_TypedDataSourceLegacyCameraParamValue<TfToken>::New(
                        _id, HdCameraTokens->lensDistortionType, _sceneDelegate))
                .SetK1(
                    Hd_TypedDataSourceLegacyCameraParamValue<float>::New(
                        _id, HdCameraTokens->lensDistortionK1, _sceneDelegate))
                .SetK2(
                    Hd_TypedDataSourceLegacyCameraParamValue<float>::New(
                        _id, HdCameraTokens->lensDistortionK2, _sceneDelegate))
                .SetCenter(
                    Hd_TypedDataSourceLegacyCameraParamValue<GfVec2f>::New(
                        _id, HdCameraTokens->lensDistortionCenter, _sceneDelegate))
                .SetAnaSq(
                    Hd_TypedDataSourceLegacyCameraParamValue<float>::New(
                        _id, HdCameraTokens->lensDistortionAnaSq, _sceneDelegate))
                .SetAsym(
                    Hd_TypedDataSourceLegacyCameraParamValue<GfVec2f>::New(
                        _id, HdCameraTokens->lensDistortionAsym, _sceneDelegate))
                .SetScale(
                    Hd_TypedDataSourceLegacyCameraParamValue<float>::New(
                        _id, HdCameraTokens->lensDistortionScale, _sceneDelegate))
                .SetIor(
                    Hd_TypedDataSourceLegacyCameraParamValue<float>::New(
                        _id, HdCameraTokens->lensDistortionIor, _sceneDelegate))
                .Build();
    }

    SdfPath _id;
    HdSceneDelegate *_sceneDelegate;
};

// ----------------------------------------------------------------------------

class Hd_DataSourceLight : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceLight);

    Hd_DataSourceLight(const SdfPath &id, HdSceneDelegate *sceneDelegate)
    : _id(id)
    , _sceneDelegate(sceneDelegate)
    {
    }

    TfTokenVector GetNames() override
    {
        // XXX: return the schema tokens when we have them.
        //      for now, return the values which are non-material-based. 
        TfTokenVector result = {
            HdTokens->filters,
            HdTokens->lightLink,
            HdTokens->shadowLink,
            HdTokens->lightFilterLink,
            HdTokens->isLight,
            HdTokens->materialSyncMode,
            HdTokens->portals,
        };
        return result;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        VtValue v;
        if (_UseGet(name)) {
            v = _sceneDelegate->Get(_id, name);
        } else {
            v = _sceneDelegate->GetLightParamValue(_id, name);
        }

        // XXX: The simpleLight params all have problematic types. 'params' is
        // GlfSimpleLight, and 'shadowParams' is HdxShadowParams, neither of
        // which we can link to from here.  'shadowCollection' is an
        // HdRprimCollection, which we could technically add as a case to
        // HdCreateTypedRetaiendDataSource; but for now we're just passing all
        // of the simpleLight params through as retained VtValues.
        if (name == HdLightTokens->params) {
            return HdRetainedSampledDataSource::New(v);
        } else if (name == HdLightTokens->shadowParams) {
            return HdRetainedSampledDataSource::New(v);
        } else if (name == HdLightTokens->shadowCollection) {
            return HdRetainedSampledDataSource::New(v);
        } else {
            return HdCreateTypedRetainedDataSource(v);
        }
    }

private:
    SdfPath _id;
    HdSceneDelegate *_sceneDelegate;

    bool _UseGet(const TfToken &name) const {
        // Certain legacy attribute names use Get() instead of
        // GetLightParamValue(), which was the more modern implementation added
        // along with the UsdLux imaging adapter implementation

        if (name == HdLightTokens->params ||
            name == HdLightTokens->shadowParams ||
            name == HdLightTokens->shadowCollection) {
            return true;
        }

        return false;
    }
};

// ----------------------------------------------------------------------------

HdDataSourceBaseHandle
_BuildCollectionDataSourceFromLightParam(
    const SdfPath &id,
    HdSceneDelegate *sceneDelegate,
    const TfToken &exprToken)
{
    VtValue v = sceneDelegate->GetLightParamValue(id, exprToken);
    if (v.IsHolding<SdfPathExpression>()) {
        return
            HdCollectionSchema::Builder()
            .SetMembershipExpression(
                HdRetainedTypedSampledDataSource<SdfPathExpression>::New(
                    v.UncheckedGet<SdfPathExpression>()))
            .Build();
    }
    return nullptr;
}


class Hd_DataSourceLightCollections : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceLightCollections);

    Hd_DataSourceLightCollections(
        const SdfPath &id,
        HdSceneDelegate *sceneDelegate)
    : _id(id), _sceneDelegate(sceneDelegate)
    {
        TF_VERIFY(_sceneDelegate);
    }

    TfTokenVector GetNames() override
    {
        return
            {HdCollectionEmulationTokens->lightLinkCollection,
             HdCollectionEmulationTokens->shadowLinkCollection};
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (name == HdCollectionEmulationTokens->lightLinkCollection) {
            return _BuildCollectionDataSourceFromLightParam(
                _id, _sceneDelegate,
                HdCollectionEmulationTokens->lightLinkCollectionMembershipExpression);
        }

        if (name == HdCollectionEmulationTokens->shadowLinkCollection) {
            return _BuildCollectionDataSourceFromLightParam(
                _id, _sceneDelegate,
                HdCollectionEmulationTokens->shadowLinkCollectionMembershipExpression);
        }

        return nullptr;
    }

private:
    SdfPath _id;
    HdSceneDelegate *_sceneDelegate;
};

class Hd_DataSourceLightFilterCollections : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceLightFilterCollections);

    Hd_DataSourceLightFilterCollections(
        const SdfPath &id,
        HdSceneDelegate *sceneDelegate)
    : _id(id), _sceneDelegate(sceneDelegate)
    {
        TF_VERIFY(_sceneDelegate);
    }

    TfTokenVector GetNames() override
    {
        return {HdCollectionEmulationTokens->filterLinkCollection};
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (name == HdCollectionEmulationTokens->filterLinkCollection) {
            return _BuildCollectionDataSourceFromLightParam(
                _id, _sceneDelegate,
                HdCollectionEmulationTokens->filterLinkCollectionMembershipExpression);
        }

        return nullptr;
    }

private:
    SdfPath _id;
    HdSceneDelegate *_sceneDelegate;
};

// ----------------------------------------------------------------------------

class Hd_DataSourceVolumeField : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceVolumeField);

    Hd_DataSourceVolumeField(const SdfPath &id, HdSceneDelegate *sceneDelegate)
    : _id(id), _sceneDelegate(sceneDelegate)
    {
        TF_VERIFY(_sceneDelegate);
    }

    TfTokenVector GetNames() override
    {
        // We don't actually know? but return the schema tokens.
        TfTokenVector results;
        results.push_back(HdVolumeFieldSchemaTokens->filePath);
        results.push_back(HdVolumeFieldSchemaTokens->fieldName);
        results.push_back(HdVolumeFieldSchemaTokens->fieldIndex);
        results.push_back(HdVolumeFieldSchemaTokens->fieldDataType);
        results.push_back(HdVolumeFieldSchemaTokens->vectorDataRoleHint);

        return results;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        VtValue v = _sceneDelegate->Get(_id, name);
        return HdCreateTypedRetainedDataSource(v);
    }

private:
    SdfPath _id;
    HdSceneDelegate *_sceneDelegate;
};

// ----------------------------------------------------------------------------

class Hd_InstanceCategoriesVectorDataSource : public HdVectorDataSource
{
public: 
    HD_DECLARE_DATASOURCE(Hd_InstanceCategoriesVectorDataSource);

    Hd_InstanceCategoriesVectorDataSource(
            const SdfPath &id, HdSceneDelegate *sceneDelegate)
    : _id(id), _sceneDelegate(sceneDelegate), _checked(false)
    {
        TF_VERIFY(_sceneDelegate);
    }

    size_t GetNumElements() override
    {
        if (!_checked) {
            _FillArray();
        }
        return _values.size();
    }

    HdDataSourceBaseHandle GetElement(size_t element) override
    {
        if (!_checked) {
            _FillArray();
        }
        if (element < _values.size()) {
            return _values[element];
        }
        return nullptr;
    }

private:
    void _FillArray()
    {
        // NOTE: in emulation, multiple threads are reading from the scene
        // index, but only one thread is reading from any specific prim at a
        // time, so we don't need to worry about concurrent access.
        if (_checked) {
            return;
        }
        const std::vector<VtArray<TfToken>> values =
            _sceneDelegate->GetInstanceCategories(_id);
        if (!values.empty()) {
            _values.reserve(values.size());
            for (const VtArray<TfToken> &value: values) {
                // TODO, deduplicate by address or value
                _values.push_back(
                    HdCategoriesSchema::BuildRetained(
                        value.size(), value.data(),
                        0, nullptr));
            }
        }
        _checked = true;
    }

    SdfPath _id;
    HdSceneDelegate *_sceneDelegate;

    std::atomic_bool _checked;
    std::vector<HdDataSourceBaseHandle> _values;
};

// ----------------------------------------------------------------------------

class Hd_InstancerTopologyDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_InstancerTopologyDataSource);

    Hd_InstancerTopologyDataSource(
        const SdfPath &id, HdSceneDelegate *sceneDelegate)
    : _id(id), _sceneDelegate(sceneDelegate)
    {
        TF_VERIFY(_sceneDelegate);
        SdfPathVector protos = _sceneDelegate->GetInstancerPrototypes(_id);
        _protos.assign(protos.begin(), protos.end());
    }

    TfTokenVector GetNames() override
    {
        TfTokenVector results;
        results.push_back(HdInstancerTopologySchemaTokens->prototypes);
        results.push_back(HdInstancerTopologySchemaTokens->instanceIndices);
        results.push_back(HdLegacyFlagTokens->isLegacyInstancer);
        return results;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (name == HdInstancerTopologySchemaTokens->prototypes) {
            return HdRetainedTypedSampledDataSource<VtArray<SdfPath>>::New(
                    _protos);
        } else if (name == HdInstancerTopologySchemaTokens->instanceIndices) {
            return Hd_InstanceIndicesDataSource::New(
                    _id, _sceneDelegate, _protos);
        } else if (name == HdLegacyFlagTokens->isLegacyInstancer) {
            return HdRetainedTypedSampledDataSource<bool>::New(true);
        } else {
            return nullptr;
        }
    }

private:
    class Hd_InstanceIndicesDataSource : public HdVectorDataSource
    {
    public:
        HD_DECLARE_DATASOURCE(Hd_InstanceIndicesDataSource);

        Hd_InstanceIndicesDataSource(
            const SdfPath &id, HdSceneDelegate *sceneDelegate,
            const VtArray<SdfPath> protos)
            : _id(id), _sceneDelegate(sceneDelegate), _protos(protos)
        {
            TF_VERIFY(_sceneDelegate);
        }

        size_t GetNumElements() override
        {
            return _protos.size();
        }

        HdDataSourceBaseHandle GetElement(size_t element) override
        {
            if (element >= _protos.size()) {
                return nullptr;
            }

            VtIntArray instanceIndices =
                _sceneDelegate->GetInstanceIndices(_id, _protos[element]);

            return HdRetainedTypedSampledDataSource<VtIntArray>::New(
                    instanceIndices);
        }

    private:
        SdfPath _id;
        HdSceneDelegate *_sceneDelegate;
        const VtArray<SdfPath> _protos;
    };

    SdfPath _id;
    HdSceneDelegate *_sceneDelegate;
    VtArray<SdfPath> _protos;
};

// ----------------------------------------------------------------------------

class Hd_DisplayStyleDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DisplayStyleDataSource);

    Hd_DisplayStyleDataSource(
        HdSceneDelegate *sceneDelegate, const SdfPath &id)
    : _sceneDelegate(sceneDelegate), _id(id), _displayStyleRead(false)
    {
        TF_VERIFY(_sceneDelegate);
    }

    TfTokenVector GetNames() override
    {
        TfTokenVector results;
        results.push_back(HdLegacyDisplayStyleSchemaTokens->refineLevel);
        results.push_back(HdLegacyDisplayStyleSchemaTokens->flatShadingEnabled);
        results.push_back(HdLegacyDisplayStyleSchemaTokens->displacementEnabled);
        results.push_back(HdLegacyDisplayStyleSchemaTokens->displayInOverlay);
        results.push_back(HdLegacyDisplayStyleSchemaTokens->occludedSelectionShowsThrough);
        results.push_back(HdLegacyDisplayStyleSchemaTokens->pointsShadingEnabled);
        results.push_back(HdLegacyDisplayStyleSchemaTokens->materialIsFinal);
        results.push_back(HdLegacyDisplayStyleSchemaTokens->shadingStyle);
        results.push_back(HdLegacyDisplayStyleSchemaTokens->reprSelector);
        results.push_back(HdLegacyDisplayStyleSchemaTokens->cullStyle);
        return results;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (name == HdLegacyDisplayStyleSchemaTokens->refineLevel) {
            if (!_displayStyleRead) {
                _displayStyle = _sceneDelegate->GetDisplayStyle(_id);
                _displayStyleRead = true;
            }
            return (_displayStyle.refineLevel != 0)
                ? HdRetainedTypedSampledDataSource<int>::New(
                        _displayStyle.refineLevel)
                : nullptr;
        } else if (name == HdLegacyDisplayStyleSchemaTokens->flatShadingEnabled) {
            if (!_displayStyleRead) {
                _displayStyle = _sceneDelegate->GetDisplayStyle(_id);
                _displayStyleRead = true;
            }
            return HdRetainedTypedSampledDataSource<bool>::New(
                    _displayStyle.flatShadingEnabled);
        } else if (name == HdLegacyDisplayStyleSchemaTokens->displacementEnabled) {
            if (!_displayStyleRead) {
                _displayStyle = _sceneDelegate->GetDisplayStyle(_id);
                _displayStyleRead = true;
            }
            return HdRetainedTypedSampledDataSource<bool>::New(
                    _displayStyle.displacementEnabled);
        } else if (name == HdLegacyDisplayStyleSchemaTokens->displayInOverlay) {
            if (!_displayStyleRead) {
                _displayStyle = _sceneDelegate->GetDisplayStyle(_id);
                _displayStyleRead = true;
            }
            return HdRetainedTypedSampledDataSource<bool>::New(
                    _displayStyle.displayInOverlay);
        } else if (name == HdLegacyDisplayStyleSchemaTokens->occludedSelectionShowsThrough) {
            if (!_displayStyleRead) {
                _displayStyle = _sceneDelegate->GetDisplayStyle(_id);
                _displayStyleRead = true;
            }
            return HdRetainedTypedSampledDataSource<bool>::New(
                    _displayStyle.occludedSelectionShowsThrough);
        } else if (name == HdLegacyDisplayStyleSchemaTokens->pointsShadingEnabled) {
            if (!_displayStyleRead) {
                _displayStyle = _sceneDelegate->GetDisplayStyle(_id);
                _displayStyleRead = true;
            }
            return HdRetainedTypedSampledDataSource<bool>::New(
                    _displayStyle.pointsShadingEnabled);
        } else if (name == HdLegacyDisplayStyleSchemaTokens->materialIsFinal) {
            if (!_displayStyleRead) {
                _displayStyle = _sceneDelegate->GetDisplayStyle(_id);
                _displayStyleRead = true;
            }
            return HdRetainedTypedSampledDataSource<bool>::New(
                    _displayStyle.materialIsFinal);
        } else if (name == HdLegacyDisplayStyleSchemaTokens->shadingStyle) {
            TfToken shadingStyle = _sceneDelegate->GetShadingStyle(_id)
                .GetWithDefault<TfToken>();
            if (shadingStyle.IsEmpty()) {
                return nullptr;
            }
            return HdRetainedTypedSampledDataSource<TfToken>::New(shadingStyle);
        } else if (name == HdLegacyDisplayStyleSchemaTokens->reprSelector) {
            HdReprSelector repr = _sceneDelegate->GetReprSelector(_id);
            HdTokenArrayDataSourceHandle reprSelectorDs = nullptr;
            bool empty = true;
            for (size_t i = 0; i < HdReprSelector::MAX_TOPOLOGY_REPRS; ++i) {
                if (!repr[i].IsEmpty()) {
                    empty = false;
                    break;
                }
            }
            if (!empty) {
                VtArray<TfToken> array(HdReprSelector::MAX_TOPOLOGY_REPRS);
                for (size_t i = 0; i < HdReprSelector::MAX_TOPOLOGY_REPRS; ++i) {
                    array[i] = repr[i];
                }
                reprSelectorDs =
                    HdRetainedTypedSampledDataSource<VtArray<TfToken>>::New(
                            array);
            }
            return reprSelectorDs;
        } else if (name == HdLegacyDisplayStyleSchemaTokens->cullStyle) {
            HdCullStyle cullStyle = _sceneDelegate->GetCullStyle(_id);
            if (cullStyle == HdCullStyleDontCare) {
                return nullptr;
            }
            TfToken cullStyleToken;
            switch(cullStyle) {
                case HdCullStyleNothing:
                    cullStyleToken = HdCullStyleTokens->nothing;
                    break;
                case HdCullStyleBack:
                    cullStyleToken = HdCullStyleTokens->back;
                    break;
                case HdCullStyleFront:
                    cullStyleToken = HdCullStyleTokens->front;
                    break;
                case HdCullStyleBackUnlessDoubleSided:
                    cullStyleToken = HdCullStyleTokens->backUnlessDoubleSided;
                    break;
                case HdCullStyleFrontUnlessDoubleSided:
                    cullStyleToken = HdCullStyleTokens->frontUnlessDoubleSided;
                    break;
                default:
                    cullStyleToken = HdCullStyleTokens->dontCare;
                    break;
            }
            return HdRetainedTypedSampledDataSource<TfToken>::New(cullStyleToken);
        } else {
            return nullptr;
        }
    }

private:
    HdSceneDelegate *_sceneDelegate;
    SdfPath _id;

    HdDisplayStyle _displayStyle;
    bool _displayStyleRead;
};

// ----------------------------------------------------------------------------

class Hd_GenericGetSampledDataSource : public HdSampledDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_GenericGetSampledDataSource);

    Hd_GenericGetSampledDataSource(
        HdSceneDelegate *sceneDelegate, const SdfPath &id, const TfToken &key)
    : _sceneDelegate(sceneDelegate)
    , _id(id)
    , _key(key)
    {}

    VtValue GetValue(Time shutterOffset) override
    {
        return _sceneDelegate->Get(_id, _key);
    }

    // returning false indicates constant value for any time
    bool GetContributingSampleTimesForInterval(
        Time startTime,
        Time endTime,
        std::vector<Time> *outSampleTimes) override
    {
        return false;
    }

private:
    HdSceneDelegate *_sceneDelegate;
    SdfPath _id;
    TfToken _key;
};

// ----------------------------------------------------------------------------

// Duplicated here because they are currently only defined in hdSt -- on which
// we cannot depend on but must be able to emulate
TF_DEFINE_PRIVATE_TOKENS(
    _drawTargetTokens,
    (camera)
    (collection)
    (drawTargetSet)
    (enable)
    (resolution)
    (aovBindings) 
    (depthPriority)
);


class Hd_LegacyDrawTargetContainerDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_LegacyDrawTargetContainerDataSource);

    Hd_LegacyDrawTargetContainerDataSource(
        HdSceneDelegate *sceneDelegate, const SdfPath &id)
    : _sceneDelegate(sceneDelegate), _id(id) {}

    TfTokenVector GetNames() override
    {
        return _drawTargetTokens->allTokens;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        return HdSampledDataSourceHandle(
            Hd_GenericGetSampledDataSource::New(_sceneDelegate, _id, name));
    }
private:
    HdSceneDelegate *_sceneDelegate;
    SdfPath _id;
};

// ----------------------------------------------------------------------------

class Hd_DataSourceLegacyExtComputationPrimvarsContainer
    : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceLegacyExtComputationPrimvarsContainer);

    Hd_DataSourceLegacyExtComputationPrimvarsContainer(
        const SdfPath &primId)
    : _primId(primId)
    {
    }

    void AddDesc(const TfToken &name, const TfToken &interpolation,
        const TfToken &role, const SdfPath &sourceComputation,
        const TfToken &sourceComputationOutputName,
        const HdTupleType &valueType)
    {
        _entries[name] = {interpolation, role, sourceComputation,
                          sourceComputationOutputName, valueType};
    }

    TfTokenVector GetNames() override
    {
        TfTokenVector result;
        result.reserve(_entries.size());
        for (const auto &pair : _entries) {
            result.push_back(pair.first);
        }
        return result;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        _EntryMap::const_iterator const it = _entries.find(name);
        if (it == _entries.end()) {
            return nullptr;
        }

        return
            HdExtComputationPrimvarSchema::Builder()
                .SetInterpolation(
                    HdExtComputationPrimvarSchema::BuildInterpolationDataSource(
                        it->second.interpolation))
                .SetRole(
                    HdExtComputationPrimvarSchema::BuildRoleDataSource(
                        it->second.role))
                .SetSourceComputation(
                    HdRetainedTypedSampledDataSource<SdfPath>::New(
                        it->second.sourceComputation))
                .SetSourceComputationOutputName(
                    HdRetainedTypedSampledDataSource<TfToken>::New(
                        it->second.sourceComputationOutputName))
                .SetValueType(
                    HdRetainedTypedSampledDataSource<HdTupleType>::New(
                        it->second.valueType))
                .Build();
    }

private:
    struct _Entry
    {
        TfToken interpolation;
        TfToken role;
        SdfPath sourceComputation;
        TfToken sourceComputationOutputName;
        HdTupleType valueType;
    };

    using _EntryMap =  TfDenseHashMap<TfToken, _Entry,
            TfToken::HashFunctor, std::equal_to<TfToken>, 32>;

    _EntryMap _entries;
    SdfPath _primId;
};

HD_DECLARE_DATASOURCE_HANDLES(
    Hd_DataSourceLegacyExtComputationPrimvarsContainer);

// ----------------------------------------------------------------------------

class Hd_DataSourceLegacyExtComputationInput : public HdSampledDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceLegacyExtComputationInput);

    Hd_DataSourceLegacyExtComputationInput(
        const TfToken &name,
        const SdfPath &id,
        HdSceneDelegate *sceneDelegate)
    : _name(name), _id(id), _sceneDelegate(sceneDelegate)
    {
        TF_VERIFY(_sceneDelegate);
    }

    VtValue GetValue(Time shutterOffset) override
    {
        if (shutterOffset == 0) {
            return _sceneDelegate->GetExtComputationInput(_id, _name);
        } else {
            _GetTimeSamples();
            return _timeSamples.Resample(shutterOffset);
        }
    }

    bool GetContributingSampleTimesForInterval(
        Time startTime,
        Time endTime,
        std::vector<Time> *outSampleTimes) override
    {
        _GetTimeSamples();

        // XXX: Start and end times come from the sene delegate, so we can't
        // get samples outside of those provided. However, we can clamp
        // returned samples to be in the right range.
        _timeSamples.GetContributingSampleTimesForInterval(
            startTime, endTime, outSampleTimes);
        return true;
    }

private:
    void _GetTimeSamples()
    {
        if (_timeSamples.count == 0) {
            _sceneDelegate->SampleExtComputationInput(
                    _id, _name, &_timeSamples);
        }
    }

    TfToken _name;
    SdfPath _id;
    HdTimeSampleArray<VtValue, 1> _timeSamples;
    HdSceneDelegate *_sceneDelegate;
};

// ----------------------------------------------------------------------------

class Hd_DataSourceLegacyExtComputationInputValues
    : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceLegacyExtComputationInputValues);

    Hd_DataSourceLegacyExtComputationInputValues(
            const SdfPath &id, HdSceneDelegate *sceneDelegate)
    : _id(id), _sceneDelegate(sceneDelegate) {}

    TfTokenVector GetNames() override
    {
        return _sceneDelegate->GetExtComputationSceneInputNames(_id);
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        return Hd_DataSourceLegacyExtComputationInput::New(
            name, _id, _sceneDelegate);
    }

private:
    SdfPath _id;
    HdSceneDelegate *_sceneDelegate;
};

// ----------------------------------------------------------------------------

class Hd_DataSourceLegacyExtComputation : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceLegacyExtComputation);

    Hd_DataSourceLegacyExtComputation(
            const SdfPath &id, HdSceneDelegate *sceneDelegate)
    : _id(id), _sceneDelegate(sceneDelegate) {}

    TfTokenVector GetNames() override
    {
        static const TfTokenVector result = {
            HdExtComputationSchemaTokens->inputValues,
            HdExtComputationSchemaTokens->inputComputations,
            HdExtComputationSchemaTokens->outputs,
            HdExtComputationSchemaTokens->glslKernel,
            HdExtComputationSchemaTokens->cpuCallback,
            HdExtComputationSchemaTokens->dispatchCount,
            HdExtComputationSchemaTokens->elementCount };
        return result;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (name == HdExtComputationSchemaTokens->inputValues) {
            return Hd_DataSourceLegacyExtComputationInputValues::New(
                    _id, _sceneDelegate);
        } else if (name == HdExtComputationSchemaTokens->inputComputations) {
            const HdExtComputationInputDescriptorVector descs =
                _sceneDelegate->GetExtComputationInputDescriptors(_id);
            std::vector<TfToken> names;
            std::vector<HdDataSourceBaseHandle> dataSources;
            names.reserve(descs.size());
            dataSources.reserve(descs.size());
            for (const auto& desc : descs) {
                names.push_back(desc.name);
                dataSources.push_back(
                    HdExtComputationInputComputationSchema::Builder()
                        .SetSourceComputation(
                            HdRetainedTypedSampledDataSource<SdfPath>::New(
                                desc.sourceComputationId))
                        .SetSourceComputationOutputName(
                            HdRetainedTypedSampledDataSource<TfToken>::New(
                                desc.sourceComputationOutputName))
                        .Build());
            }
            return HdRetainedContainerDataSource::New(
                names.size(), names.data(), dataSources.data());
        } else if (name == HdExtComputationSchemaTokens->outputs) {
            const HdExtComputationOutputDescriptorVector descs =
                _sceneDelegate->GetExtComputationOutputDescriptors(_id);
            std::vector<TfToken> names;
            std::vector<HdDataSourceBaseHandle> dataSources;
            names.reserve(descs.size());
            dataSources.reserve(descs.size());
            for (const auto& desc : descs) {
                names.push_back(desc.name);
                dataSources.push_back(
                    HdExtComputationOutputSchema::Builder()
                        .SetValueType(
                            HdRetainedTypedSampledDataSource<HdTupleType>::New(
                                desc.valueType))
                        .Build());
            }
            return HdRetainedContainerDataSource::New(
                names.size(), names.data(), dataSources.data());
        } else if (name == HdExtComputationSchemaTokens->glslKernel) {
            std::string kernel = _sceneDelegate->GetExtComputationKernel(_id);
            return HdRetainedTypedSampledDataSource<std::string>::New(kernel);
        } else if (name == HdExtComputationSchemaTokens->cpuCallback) {
            return
                HdRetainedTypedSampledDataSource<
                    HdExtComputationCpuCallbackSharedPtr>::New(
                        std::make_shared<
                                Hd_SceneDelegateExtComputationCpuCallback>(
                            _id, _sceneDelegate));
        } else if (name == HdExtComputationSchemaTokens->dispatchCount) {
            const VtValue vDispatch = _sceneDelegate->GetExtComputationInput(
                _id, HdTokens->dispatchCount);
            return HdRetainedTypedSampledDataSource<size_t>::New(
                vDispatch.GetWithDefault<size_t>(0));
        } else if (name == HdExtComputationSchemaTokens->elementCount) {
            const VtValue vElement = _sceneDelegate->GetExtComputationInput(
                _id, HdTokens->elementCount);
            return HdRetainedTypedSampledDataSource<size_t>::New(
                vElement.GetWithDefault<size_t>(0));
        } else {
            return nullptr;
        }
    }

private:
    SdfPath _id;
    HdSceneDelegate *_sceneDelegate;
};

// ----------------------------------------------------------------------------

class Hd_DataSourceRenderBuffer : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceRenderBuffer);

    Hd_DataSourceRenderBuffer(
        HdSceneDelegate *sceneDelegate, const SdfPath &id)
    : _sceneDelegate(sceneDelegate), _id(id)
    {
        TF_VERIFY(_sceneDelegate);
        rb = _sceneDelegate->GetRenderBufferDescriptor(_id);
    }

    TfTokenVector GetNames() override
    {
        TfTokenVector v;
        v.push_back(HdRenderBufferSchemaTokens->dimensions);
        v.push_back(HdRenderBufferSchemaTokens->format);
        v.push_back(HdRenderBufferSchemaTokens->multiSampled);
        return v;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (name == HdRenderBufferSchemaTokens->dimensions) {
            return HdRetainedTypedSampledDataSource<GfVec3i>::New(
                    rb.dimensions);
        } else if (name == HdRenderBufferSchemaTokens->format) {
            return HdRetainedTypedSampledDataSource<HdFormat>::New(
                    rb.format);
        } else if (name == HdRenderBufferSchemaTokens->multiSampled) {
            return HdRetainedTypedSampledDataSource<bool>::New(
                    rb.multiSampled);
        } else {
            return HdSampledDataSourceHandle(
                Hd_GenericGetSampledDataSource::New(_sceneDelegate, _id, name));
        }
    }

private:
    HdSceneDelegate *_sceneDelegate;
    SdfPath _id;
    HdRenderBufferDescriptor rb;
};

// ----------------------------------------------------------------------------

using HdRenderProducts = HdRenderSettings::RenderProducts;
static HdVectorDataSourceHandle
_ToVectorDS(const HdRenderProducts &hdProducts)
{
    std::vector<HdDataSourceBaseHandle> productsDs;
    productsDs.reserve(hdProducts.size());

    for (const auto & hdProduct : hdProducts) {
        // Construct render var ds.
        std::vector<HdDataSourceBaseHandle> varsDs;
        for (const auto & hdVar : hdProduct.renderVars) {
            varsDs.push_back(
                HdRenderVarSchema::Builder()
                    .SetPath(
                        HdRetainedTypedSampledDataSource<SdfPath>::New(
                            hdVar.varPath))
                    .SetDataType(
                        HdRetainedTypedSampledDataSource<TfToken>::New(
                            hdVar.dataType))
                    .SetSourceName(
                        HdRetainedTypedSampledDataSource<TfToken>::New(
                            TfToken(hdVar.sourceName)))
                    .SetSourceType(
                        HdRetainedTypedSampledDataSource<TfToken>::New(
                            hdVar.sourceType))
                    .SetNamespacedSettings(
                        HdUtils::ConvertVtDictionaryToContainerDS(
                            hdVar.namespacedSettings))
                    .Build());
        }

        productsDs.push_back(
            HdRenderProductSchema::Builder()
                .SetPath(
                    HdRetainedTypedSampledDataSource<SdfPath>::New(
                        hdProduct.productPath))
                .SetType(
                    HdRetainedTypedSampledDataSource<TfToken>::New(
                        hdProduct.type))
                .SetName(
                    HdRetainedTypedSampledDataSource<TfToken>::New(
                        hdProduct.name))
                .SetResolution(
                    HdRetainedTypedSampledDataSource<GfVec2i>::New(
                        hdProduct.resolution))
                .SetRenderVars(
                    HdRetainedSmallVectorDataSource::New(
                        varsDs.size(), varsDs.data()))
                .SetCameraPrim(
                    HdRetainedTypedSampledDataSource<SdfPath>::New(
                        hdProduct.cameraPath))
                .SetPixelAspectRatio(
                    HdRetainedTypedSampledDataSource<float>::New(
                        hdProduct.pixelAspectRatio))
                .SetAspectRatioConformPolicy(
                    HdRetainedTypedSampledDataSource<TfToken>::New(
                        hdProduct.aspectRatioConformPolicy))
                .SetApertureSize(
                    HdRetainedTypedSampledDataSource<GfVec2f>::New(
                        hdProduct.apertureSize))
                .SetDataWindowNDC(
                    HdRetainedTypedSampledDataSource<GfVec4f>::New(
                        GfVec4f(
                            hdProduct.dataWindowNDC.GetMin()[0],
                            hdProduct.dataWindowNDC.GetMin()[1],
                            hdProduct.dataWindowNDC.GetMax()[0],
                            hdProduct.dataWindowNDC.GetMax()[1])))
                .SetDisableMotionBlur(
                    HdRetainedTypedSampledDataSource<bool>::New(
                        hdProduct.disableMotionBlur))
                .SetDisableDepthOfField(
                    HdRetainedTypedSampledDataSource<bool>::New(
                        hdProduct.disableDepthOfField))
                .SetNamespacedSettings(
                    HdUtils::ConvertVtDictionaryToContainerDS(
                        hdProduct.namespacedSettings))
                .Build());
    }

    return HdRetainedSmallVectorDataSource::New(
                productsDs.size(), productsDs.data());
}

class Hd_DataSourceRenderSettings : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceRenderSettings);

    Hd_DataSourceRenderSettings(
        HdSceneDelegate *sceneDelegate, const SdfPath &id)
    : _sceneDelegate(sceneDelegate), _id(id)
    {
        TF_VERIFY(_sceneDelegate);
    }

    TfTokenVector GetNames() override
    {
        static TfTokenVector names = {
            HdRenderSettingsSchemaTokens->namespacedSettings,
            HdRenderSettingsSchemaTokens->renderProducts,
            HdRenderSettingsSchemaTokens->includedPurposes,
            HdRenderSettingsSchemaTokens->materialBindingPurposes,
            HdRenderSettingsSchemaTokens->renderingColorSpace};
        return names;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (name == HdRenderSettingsSchemaTokens->namespacedSettings) {
            const VtValue value = _sceneDelegate->Get(
                _id, HdRenderSettingsPrimTokens->namespacedSettings);
            if (value.IsHolding<VtDictionary>()) {
                return HdUtils::ConvertVtDictionaryToContainerDS(
                    value.UncheckedGet<VtDictionary>());
            }
        }

        if (name == HdRenderSettingsSchemaTokens->renderProducts) {
            const VtValue value = _sceneDelegate->Get(
                _id, HdRenderSettingsPrimTokens->renderProducts);
            if (value.IsHolding<HdRenderProducts>()) {
                return _ToVectorDS(value.UncheckedGet<HdRenderProducts>());
            }
        }

        if (name == HdRenderSettingsSchemaTokens->includedPurposes) {
            const VtValue value = _sceneDelegate->Get(
                _id, HdRenderSettingsPrimTokens->includedPurposes);
            if (value.IsHolding<VtArray<TfToken>>()) {
                return HdRetainedTypedSampledDataSource<VtArray<TfToken>>::New(
                    value.UncheckedGet<VtArray<TfToken>>());
            }
        }

        if (name == HdRenderSettingsSchemaTokens->materialBindingPurposes) {
            const VtValue value = _sceneDelegate->Get(
                _id, HdRenderSettingsPrimTokens->materialBindingPurposes);
            if (value.IsHolding<VtArray<TfToken>>()) {
                return HdRetainedTypedSampledDataSource<VtArray<TfToken>>::New(
                    value.UncheckedGet<VtArray<TfToken>>());
            }
        }

        if (name == HdRenderSettingsSchemaTokens->renderingColorSpace) {
            const VtValue value = _sceneDelegate->Get(
                _id, HdRenderSettingsPrimTokens->renderingColorSpace);
            if (value.IsHolding<TfToken>()) {
                return HdRetainedTypedSampledDataSource<TfToken>::New(
                    value.UncheckedGet<TfToken>());
            }
        }

        // Note: Skip 'active' and 'shutterInterval' fields which are computed
        //       by a downstream scene index.

        return HdSampledDataSourceHandle(
            Hd_GenericGetSampledDataSource::New(_sceneDelegate, _id, name));
    }

private:
    HdSceneDelegate *_sceneDelegate;
    SdfPath _id;
};

// ----------------------------------------------------------------------------

class Hd_DataSourceImageShader : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(Hd_DataSourceImageShader);

    Hd_DataSourceImageShader(
        const SdfPath& id, HdSceneDelegate* sceneDelegate)
    : _id(id), _sceneDelegate(sceneDelegate)
    {
        TF_VERIFY(_sceneDelegate);
    }

    TfTokenVector GetNames() override
    {
        static const TfTokenVector names = {
            HdImageShaderSchemaTokens->enabled,
            HdImageShaderSchemaTokens->priority,
            HdImageShaderSchemaTokens->filePath,
            HdImageShaderSchemaTokens->constants,
            HdImageShaderSchemaTokens->materialNetwork
        };
        return names;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (name == HdImageShaderSchemaTokens->enabled) {
            const VtValue value = _sceneDelegate->Get(
                _id, HdImageShaderSchemaTokens->enabled);
            if (value.IsHolding<bool>()) {
                return HdRetainedTypedSampledDataSource<bool>::New(
                    value.UncheckedGet<bool>());
            } else {
                return nullptr;
            }
        }

        if (name == HdImageShaderSchemaTokens->priority) {
            const VtValue value = _sceneDelegate->Get(
                _id, HdImageShaderSchemaTokens->priority);
            if (value.IsHolding<int>()) {
                return HdRetainedTypedSampledDataSource<int>::New(
                    value.UncheckedGet<int>());
            } else {
                return nullptr;
            }
        }

        if (name == HdImageShaderSchemaTokens->filePath) {
            const VtValue value = _sceneDelegate->Get(
                _id, HdImageShaderSchemaTokens->filePath);
            if (value.IsHolding<std::string>()) {
                return HdRetainedTypedSampledDataSource<std::string>::New(
                    value.UncheckedGet<std::string>());
            } else {
                return nullptr;
            }
        }

        if (name == HdImageShaderSchemaTokens->constants) {
            const VtValue value = _sceneDelegate->Get(
                _id, HdImageShaderSchemaTokens->constants);
            if (value.IsHolding<VtDictionary>()) {
                return HdUtils::ConvertVtDictionaryToContainerDS(
                    value.UncheckedGet<VtDictionary>());
            } else {
                return nullptr;
            }
        }

        if (name == HdImageShaderSchemaTokens->materialNetwork) {
            const VtValue value = _sceneDelegate->Get(
                _id, HdImageShaderSchemaTokens->materialNetwork);
            if (value.IsHolding<HdMaterialNetworkMap>()) {
                return 
                    HdUtils::ConvertHdMaterialNetworkToHdMaterialNetworkSchema(
                        value.UncheckedGet<HdMaterialNetworkMap>());
            } else {
                return nullptr;
            }
        }

        return HdSampledDataSourceHandle(
            Hd_GenericGetSampledDataSource::New(_sceneDelegate, _id, name));
    }

private:
    SdfPath _id;
    HdSceneDelegate* _sceneDelegate;
};

// ----------------------------------------------------------------------------

TfToken _InterpolationAsToken(HdInterpolation interpolation)
{
    switch (interpolation) {
    case HdInterpolationConstant:
        return HdPrimvarSchemaTokens->constant;
    case HdInterpolationUniform:
        return HdPrimvarSchemaTokens->uniform;
    case HdInterpolationVarying:
        return HdPrimvarSchemaTokens->varying;
    case HdInterpolationVertex:
        return HdPrimvarSchemaTokens->vertex;
    case HdInterpolationFaceVarying:
        return HdPrimvarSchemaTokens->faceVarying;
    case HdInterpolationInstance:
        return HdPrimvarSchemaTokens->instance;

    default:
        return HdPrimvarSchemaTokens->constant;
    }
}

} //anonymous namespace

// ----------------------------------------------------------------------------

HdDataSourceLegacyPrim::HdDataSourceLegacyPrim(
    const SdfPath& id, 
    const TfToken& type, 
    HdSceneDelegate *sceneDelegate)
: _id(id),
  _type(type),
  _sceneDelegate(sceneDelegate),
  _primvarsBuilt(false),
  _extComputationPrimvarsBuilt(false)
{
    TF_VERIFY(_sceneDelegate);
}

void
HdDataSourceLegacyPrim::PrimDirtied(const HdDataSourceLocatorSet &locators)
{
    if (locators.Intersects(HdPrimvarsSchema::GetDefaultLocator())) {
        _primvarsBuilt.store(false);
        _extComputationPrimvarsBuilt = false;
        HdContainerDataSourceHandle null(nullptr);
        HdContainerDataSource::AtomicStore(_primvars, null);
        _extComputationPrimvars.reset();
    }

    if (locators.Intersects(HdInstancerTopologySchema::GetDefaultLocator())) {
        HdContainerDataSourceHandle null(nullptr);
        HdContainerDataSource::AtomicStore(_instancerTopology, null);
    }
}

const HdDataSourceLocatorSet&
HdDataSourceLegacyPrim::GetCachedLocators()
{
    static HdDataSourceLocatorSet locators = {
        HdPrimvarsSchema::GetDefaultLocator(),
        HdInstancerTopologySchema::GetDefaultLocator(),
    };

    return locators;
}

bool
HdDataSourceLegacyPrim::_IsLight()
{
    if (HdPrimTypeIsLight(_type)) {
        return true;
    }

    // NOTE: This convention allows for things like meshes to identify
    //       themselves also as lights.
    //       
    //       While downstream consumers might query for this equivalent data
    //       source directly (see Hd_DataSourceLight), its use here is only
    //       part of GetNames and Has for the prim-level data source.
    //       
    //       This specific method will not be invoked by rendering directly.
    //       Hydra Scene Browser is the only caller of GetNames for
    //       a prim-level container data source -- and only for the selected
    //       prim.
    const VtValue v = _sceneDelegate->GetLightParamValue(
        _id, HdTokens->isLight);
    return v.GetWithDefault<bool>(false);
}

bool
HdDataSourceLegacyPrim::_IsInstanceable()
{
    return HdPrimTypeIsGprim(_type)
        || _IsLight() 
        || _type == HdPrimTypeTokens->instancer;
}

TfTokenVector 
HdDataSourceLegacyPrim::GetNames()
{
    TfTokenVector result;

    if (_type == HdPrimTypeTokens->mesh) {
        result.push_back(HdMeshSchemaTokens->mesh);
    }

    if (_type == HdPrimTypeTokens->basisCurves) {
        result.push_back(HdBasisCurvesSchemaTokens->basisCurves);
    }

    // Allow all legacy prims to provide primvars as that's the only interface
    // for advertising what names/values are there.
    // Abstract prims which may need to be expressed via legacy scene delegate
    // APIs can make use of it without this code needing to be aware of any
    // custom types.
    result.push_back(HdPrimvarsSchemaTokens->primvars);

    if (HdPrimTypeIsGprim(_type)) {
        result.push_back(HdExtComputationPrimvarsSchemaTokens->extComputationPrimvars);
        result.push_back(HdMaterialBindingsSchema::GetSchemaToken());
        result.push_back(HdLegacyDisplayStyleSchemaTokens->displayStyle); 
        result.push_back(HdCoordSysBindingSchemaTokens->coordSysBinding);
        result.push_back(HdPurposeSchemaTokens->purpose);
        result.push_back(HdVisibilitySchemaTokens->visibility);
        result.push_back(HdCategoriesSchemaTokens->categories);
        result.push_back(HdXformSchemaTokens->xform);
        result.push_back(HdExtentSchemaTokens->extent);
    }

    if (_IsLight() || _type == HdPrimTypeTokens->lightFilter) {
        result.push_back(HdMaterialSchemaTokens->material);
        result.push_back(HdXformSchemaTokens->xform);
        result.push_back(HdLightSchemaTokens->light);
        result.push_back(HdCollectionsSchemaTokens->collections);
        result.push_back(HdDependenciesSchemaTokens->__dependencies);
    }

    if (_type == HdPrimTypeTokens->material) {
        result.push_back(HdMaterialSchemaTokens->material);
    }

    if (_type == HdPrimTypeTokens->instancer) {
        result.push_back(HdXformSchemaTokens->xform);
        result.push_back(HdInstancerTopologySchemaTokens->instancerTopology);
        result.push_back(HdInstanceCategoriesSchemaTokens->instanceCategories);

        // This is relevant for instancer prims created for point instancers.
        result.push_back(HdCategoriesSchemaTokens->categories);
    }

    if (_IsInstanceable()) {
        result.push_back(HdInstancedBySchemaTokens->instancedBy);
    }

    if (_type == HdPrimTypeTokens->camera) {
        result.push_back(HdCameraSchemaTokens->camera);
        result.push_back(HdXformSchemaTokens->xform);
    }

    if (_type == HdPrimTypeTokens->renderBuffer) {
        result.push_back(HdRenderBufferSchemaTokens->renderBuffer);
    }

    if (_type == HdPrimTypeTokens->renderSettings) {
        result.push_back(HdRenderSettingsSchemaTokens->renderSettings);
    }

    if (_type == HdPrimTypeTokens->integrator) {
        result.push_back(HdIntegratorSchemaTokens->integrator);
    }

    if (_type == HdPrimTypeTokens->sampleFilter) {
        result.push_back(HdSampleFilterSchemaTokens->sampleFilter);
    }

    if (_type == HdPrimTypeTokens->displayFilter) {
        result.push_back(HdDisplayFilterSchemaTokens->displayFilter);
    }

    if (HdLegacyPrimTypeIsVolumeField(_type)) {
        result.push_back(HdVolumeFieldSchemaTokens->volumeField);
    }

    if (_type == HdPrimTypeTokens->volume) {
        result.push_back(HdVolumeFieldBindingSchemaTokens->volumeFieldBinding);
    }

    if (_type == HdPrimTypeTokens->extComputation) {
        result.push_back(HdExtComputationSchemaTokens->extComputation);
    }

    if (_type == HdPrimTypeTokens->coordSys) {
        result.push_back(HdXformSchemaTokens->xform);
    }

    if (_type == HdPrimTypeTokens->drawTarget) {
        result.push_back(HdPrimTypeTokens->drawTarget);
    }

    if (_type == HdPrimTypeTokens->imageShader) {
        result.push_back(HdPrimTypeTokens->imageShader);
    }

    result.push_back(HdSceneIndexEmulationTokens->sceneDelegate);

    return result;
}

template <typename SchemaType>
static HdContainerDataSourceHandle
_ConvertRenderTerminalResourceToHdDataSource(const VtValue &outputNodeValue)
{
    HD_TRACE_FUNCTION();

    if (!outputNodeValue.IsHolding<HdMaterialNode2>()) {
        return nullptr;
    }

    const HdMaterialNode2 outputNode =
        outputNodeValue.UncheckedGet<HdMaterialNode2>();

    std::vector<TfToken> paramsNames;
    std::vector<HdDataSourceBaseHandle> paramsValues;
    for (const auto &p : outputNode.parameters) {
        paramsNames.push_back(p.first);
        paramsValues.push_back(
            HdMaterialNodeParameterSchema::Builder()
                .SetValue(
                    HdRetainedTypedSampledDataSource<VtValue>::New(p.second))
                .Build()
        );
    }

    HdContainerDataSourceHandle nodeDS =
        HdMaterialNodeSchema::Builder()
            .SetParameters(
                HdRetainedContainerDataSource::New(
                    paramsNames.size(), 
                    paramsNames.data(),
                    paramsValues.data()))
            .SetInputConnections(
                HdRetainedContainerDataSource::New())
            .SetNodeIdentifier(
                HdRetainedTypedSampledDataSource<TfToken>::New(
                    outputNode.nodeTypeId))
            .Build();

    return
        typename SchemaType::Builder()
            .SetResource(nodeDS)
            .Build();
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetPrimvarsDataSource()
{
    if (_primvarsBuilt.load()) {
        return HdContainerDataSource::AtomicLoad(_primvars);
    }

    TRACE_FUNCTION();

    Hd_DataSourceLegacyPrimvarsContainerHandle primvarsDs;

    for (size_t interpolation = HdInterpolationConstant;
        interpolation < HdInterpolationCount; ++interpolation) {

        HdPrimvarDescriptorVector v = _sceneDelegate->GetPrimvarDescriptors(
            _id, 
            (HdInterpolation)interpolation);

        TfToken interpolationToken = _InterpolationAsToken(
            (HdInterpolation)interpolation);

        for (const auto &primvarDesc : v) {
            if (!primvarsDs) {
                primvarsDs = Hd_DataSourceLegacyPrimvarsContainer::New(
                    _id, _sceneDelegate);
            }
            primvarsDs->AddDesc(
                primvarDesc.name, interpolationToken, primvarDesc.role,
                primvarDesc.indexed);
        }
    }

    HdContainerDataSourceHandle ds = primvarsDs;
    HdContainerDataSource::AtomicStore(_primvars, ds);
    _primvarsBuilt.store(true);

    return primvarsDs;
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetExtComputationPrimvarsDataSource()
{
    if (_extComputationPrimvarsBuilt) {
        return _extComputationPrimvars;
    }

    Hd_DataSourceLegacyExtComputationPrimvarsContainerHandle primvarsDs;

    for (size_t interpolation = HdInterpolationConstant;
         interpolation < HdInterpolationCount; ++interpolation) {
        HdExtComputationPrimvarDescriptorVector v =
            _sceneDelegate->GetExtComputationPrimvarDescriptors(
                _id, (HdInterpolation)interpolation);

        TfToken interpolationToken = _InterpolationAsToken(
            (HdInterpolation)interpolation);

        for (const auto &primvarDesc : v) {
            if (!primvarsDs) {
                primvarsDs =
                    Hd_DataSourceLegacyExtComputationPrimvarsContainer::New(
                        _id);
            }
            primvarsDs->AddDesc(
                primvarDesc.name, interpolationToken, primvarDesc.role,
                primvarDesc.sourceComputationId,
                primvarDesc.sourceComputationOutputName,
                primvarDesc.valueType);
        }
    }

    _extComputationPrimvars = primvarsDs;
    _extComputationPrimvarsBuilt = true;
    return _extComputationPrimvars;
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetMaterialBindingsDataSource()
{
    const SdfPath path = _sceneDelegate->GetMaterialId(_id);
    if (path.IsEmpty()) {
        return nullptr;
    }
    static const TfToken purposes[] = {
        HdMaterialBindingsSchemaTokens->allPurpose
    };
    HdDataSourceBaseHandle const materialBindingSources[] = {
        HdMaterialBindingSchema::Builder()
            .SetPath(
                HdRetainedTypedSampledDataSource<SdfPath>::New(path))
            .Build()
    };

    return
        HdMaterialBindingsSchema::BuildRetained(
            TfArraySize(purposes), purposes, materialBindingSources);
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetXformDataSource()
{
    HdContainerDataSourceHandle t = 
        HdXformSchema::Builder()
            .SetMatrix(
                Hd_DataSourceLegacyMatrixValue::New(_type, _id, _sceneDelegate))
            .SetResetXformStack(
                // Mark this transform as fully composed, since scene delegate
                // transforms are always fully composed.
                HdRetainedTypedSampledDataSource<bool>::New(true))
            .Build();
    return t;
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetMaterialDataSource()
{
    VtValue materialContainer = _sceneDelegate->GetMaterialResource(_id);

    if (!materialContainer.IsHolding<HdMaterialNetworkMap>()) {
        return nullptr;
    }

    HdMaterialNetworkMap hdNetworkMap = 
        materialContainer.UncheckedGet<HdMaterialNetworkMap>();
    return HdUtils::ConvertHdMaterialNetworkToHdMaterialSchema(hdNetworkMap);
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetIntegratorDataSource()
{
    return _ConvertRenderTerminalResourceToHdDataSource<HdIntegratorSchema>(
                _sceneDelegate->Get(_id, HdIntegratorSchemaTokens->resource));
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetSampleFilterDataSource()
{
    return _ConvertRenderTerminalResourceToHdDataSource<HdSampleFilterSchema>(
                _sceneDelegate->Get(_id, HdSampleFilterSchemaTokens->resource));
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetDisplayFilterDataSource()
{
    return _ConvertRenderTerminalResourceToHdDataSource<HdDisplayFilterSchema>(
                _sceneDelegate->Get(_id, HdDisplayFilterSchemaTokens->resource));
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetDisplayStyleDataSource()
{
    return Hd_DisplayStyleDataSource::New(_sceneDelegate, _id);
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetInstancedByDataSource()
{
    SdfPath instancerId = _sceneDelegate->GetInstancerId(_id);
    if (instancerId.IsEmpty()) {
        return nullptr;
    }
    return
        HdInstancedBySchema::Builder()
            .SetPaths(
                HdRetainedTypedSampledDataSource<VtArray<SdfPath>>::New(
                    VtArray<SdfPath>({instancerId})))
            .Build();
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetInstancerTopologyDataSource()
{
    TRACE_FUNCTION();

    HdContainerDataSourceHandle instancerTopology =
        HdContainerDataSource::AtomicLoad(_instancerTopology);

    if (instancerTopology) {
        return instancerTopology;
    }

    instancerTopology =
        Hd_InstancerTopologyDataSource::New(_id, _sceneDelegate);

    HdContainerDataSource::AtomicStore(
        _instancerTopology, instancerTopology);

    return instancerTopology;
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetVolumeFieldBindingDataSource()
{
    HdVolumeFieldDescriptorVector volumeFields =
        _sceneDelegate->GetVolumeFieldDescriptors(_id);

    if (volumeFields.empty()) {
        return nullptr;
    }

    std::vector<TfToken> names;
    std::vector<HdDataSourceBaseHandle> paths;
    for (HdVolumeFieldDescriptor const &desc : volumeFields) {
        names.push_back(desc.fieldName);
        paths.push_back(HdRetainedTypedSampledDataSource<SdfPath>::New(
                desc.fieldId));
    }
    return HdVolumeFieldBindingSchema::BuildRetained(
        names.size(), names.data(), paths.data());
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetCoordSysBindingDataSource()
{
    HdIdVectorSharedPtr coordSysBindings =
        _sceneDelegate->GetCoordSysBindings(_id);

    if (coordSysBindings == nullptr || coordSysBindings->empty()) {
        return nullptr;
    }

    std::vector<TfToken> names;
    std::vector<HdDataSourceBaseHandle> paths;
    for (SdfPath const &path : *coordSysBindings) {
        // Note: the scene delegate API just binds prims to unnamed
        // coord sys objects.  These coord sys objects have paths of the
        // form /path/to/object.coordSys:foo:binding, where "foo" is the name 
        // the shader gets to access.  We pull these names out to store in the
        // schema.
        // XXX: Note that for backward compatibility of non-applied
        // UsdShadeCoordSysAPI api schema, a form like 
        // /path/to/object.coordSys:foo is supporterd!
        const std::string &attrName = path.GetName();
        const std::string &nameSpacedCoordSysName =
            TfStringEndsWith(attrName, _tokens->binding.GetString())
            ? TfStringGetBeforeSuffix(
                    attrName, *SdfPathTokens->namespaceDelimiter.GetText())
            : attrName;
        names.push_back(
                TfToken(
                    SdfPath::StripPrefixNamespace(
                        nameSpacedCoordSysName, _tokens->coordSys).first));

        paths.push_back(HdRetainedTypedSampledDataSource<SdfPath>::New(
            path));
    }
    return HdCoordSysBindingSchema::BuildRetained(
            names.size(), names.data(), paths.data());
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetVisibilityDataSource()
{
    bool vis = _sceneDelegate->GetVisible(_id);
    if (vis) {
        static const HdContainerDataSourceHandle visOn = 
            HdVisibilitySchema::Builder()
                .SetVisibility(
                    HdRetainedTypedSampledDataSource<bool>::New(true))
                .Build();
        return visOn;
    } else {
        static const HdContainerDataSourceHandle visOff = 
            HdVisibilitySchema::Builder()
                .SetVisibility(
                    HdRetainedTypedSampledDataSource<bool>::New(false))
                .Build();
        return visOff;
    }
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetPurposeDataSource()
{
    const TfToken purpose = _sceneDelegate->GetRenderTag(_id);
    return
        HdPurposeSchema::Builder()
            .SetPurpose(
                HdRetainedTypedSampledDataSource<TfToken>::New(purpose))
            .Build();
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetExtentDataSource()
{
    const GfRange3d extent = _sceneDelegate->GetExtent(_id);
    return
        HdExtentSchema::Builder()
            .SetMin(
                HdRetainedTypedSampledDataSource<GfVec3d>::New(extent.GetMin()))
            .SetMax(
                HdRetainedTypedSampledDataSource<GfVec3d>::New(extent.GetMax()))
            .Build();
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetCategoriesDataSource()
{
    const VtArray<TfToken> categories = _sceneDelegate->GetCategories(_id);
    if (!categories.empty()) {
        return HdCategoriesSchema::BuildRetained(
            categories.size(), categories.data(),
            0, nullptr);
    }
    return nullptr;
}

HdDataSourceBaseHandle
HdDataSourceLegacyPrim::_GetInstanceCategoriesDataSource()
{
    return
        HdInstanceCategoriesSchema::Builder()
            .SetCategoriesValues(
                HdVectorDataSourceHandle(
                    Hd_InstanceCategoriesVectorDataSource::New(
                        _id, _sceneDelegate)))
            .Build();
}

HdDataSourceBaseHandle 
HdDataSourceLegacyPrim::Get(const TfToken &name)
{
    if (name == HdMeshSchemaTokens->mesh) {
        if (_type == HdPrimTypeTokens->mesh) {
            return Hd_DataSourceMesh::New(_id, _sceneDelegate);
        }
        
    } else if (name == HdBasisCurvesSchemaTokens->basisCurves) {
        if (_type == HdPrimTypeTokens->basisCurves) {
            return Hd_DataSourceBasisCurves::New(_id, _sceneDelegate);
        }
    } else if (name == HdPrimvarsSchemaTokens->primvars) {
        return _GetPrimvarsDataSource();
    } else if (
        name == HdExtComputationPrimvarsSchemaTokens->extComputationPrimvars) {
        return _GetExtComputationPrimvarsDataSource();
    } else if (name == HdMaterialBindingsSchema::GetSchemaToken()) {
        return _GetMaterialBindingsDataSource();
    } else if (name == HdXformSchemaTokens->xform) {
       return _GetXformDataSource();
    } else if (name == HdMaterialSchemaTokens->material) {
        return _GetMaterialDataSource();
    } else if (name == HdLegacyDisplayStyleSchemaTokens->displayStyle) {
        return _GetDisplayStyleDataSource();
    } else if (name == HdSceneIndexEmulationTokens->sceneDelegate) {
        return HdRetainedTypedSampledDataSource<HdSceneDelegate*>::New(
            _sceneDelegate);
    } else if (name == HdInstancedBySchemaTokens->instancedBy) {
        return _GetInstancedByDataSource();
    } else if (name == HdInstancerTopologySchemaTokens->instancerTopology) {
        return _GetInstancerTopologyDataSource();
    } else if (name == HdVolumeFieldBindingSchemaTokens->volumeFieldBinding) {
        return _GetVolumeFieldBindingDataSource();
    } else if (name == HdCoordSysBindingSchemaTokens->coordSysBinding) {
        return _GetCoordSysBindingDataSource();
    } else if (name == HdVisibilitySchemaTokens->visibility) {
        return _GetVisibilityDataSource();
    } else if (name == HdPurposeSchemaTokens->purpose) {
        return _GetPurposeDataSource();
    } else if (name == HdExtentSchemaTokens->extent) {
        return _GetExtentDataSource();
    } else if (name == HdCameraSchemaTokens->camera) {
        return Hd_DataSourceCamera::New(_id, _sceneDelegate);
    } else if (name == HdLightSchemaTokens->light) {
        return Hd_DataSourceLight::New(_id, _sceneDelegate);
    } else if (name == HdCategoriesSchemaTokens->categories) {
        return _GetCategoriesDataSource();
    } else if (name == HdInstanceCategoriesSchemaTokens->instanceCategories) {
        return _GetInstanceCategoriesDataSource();
    } else if (name == HdRenderBufferSchemaTokens->renderBuffer) {
        return Hd_DataSourceRenderBuffer::New(_sceneDelegate, _id);
    } else if (name == HdRenderSettingsSchemaTokens->renderSettings) {
        return Hd_DataSourceRenderSettings::New(_sceneDelegate, _id);
    } else if (name == HdSampleFilterSchemaTokens->sampleFilter) {
        return _GetSampleFilterDataSource();
    } else if (name == HdIntegratorSchemaTokens->integrator) {
        return _GetIntegratorDataSource();
    } else if (name == HdDisplayFilterSchemaTokens->displayFilter) {
        return _GetDisplayFilterDataSource();
    } else if (name == HdVolumeFieldSchemaTokens->volumeField) {
        return Hd_DataSourceVolumeField::New(_id, _sceneDelegate);
    } else if (name == HdPrimTypeTokens->drawTarget) {
        return HdContainerDataSourceHandle(
            Hd_LegacyDrawTargetContainerDataSource::New(_sceneDelegate, _id));
    } else if (name == HdExtComputationSchemaTokens->extComputation) {
        return Hd_DataSourceLegacyExtComputation::New(_id, _sceneDelegate);
    } else if (name == HdImageShaderSchemaTokens->imageShader) {
        return Hd_DataSourceImageShader::New(_id, _sceneDelegate);
    } else if (name == HdCollectionsSchemaTokens->collections) {
        // Hydra 1.0 doesn't transport the USD notion of collections.
        // Instead, behaviors that leverage collections (e.g. light linking)
        // are implemented on the scene delegate end.
        //
        // Below, we "promote" membership expression attributes on the light or
        // light filter as collections. This bit of glue code helps unify
        // the light linking scene index handling for emulated and native
        // scene indices.
        //
        if (_IsLight()) {
            return Hd_DataSourceLightCollections::New(_id, _sceneDelegate);
        }
        if (_type == HdPrimTypeTokens->lightFilter) {
            return Hd_DataSourceLightFilterCollections::New(
                _id, _sceneDelegate);
        }
    }

    return nullptr;
}

PXR_NAMESPACE_CLOSE_SCOPE
