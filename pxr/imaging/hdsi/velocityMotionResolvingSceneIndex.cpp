//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hdsi/velocityMotionResolvingSceneIndex.h"

#include "pxr/imaging/hd/dataSource.h"
#include "pxr/imaging/hd/dataSourceLocator.h"
#include "pxr/imaging/hd/dependenciesSchema.h"
#include "pxr/imaging/hd/dependencySchema.h"
#include "pxr/imaging/hd/filteringSceneIndex.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/primvarSchema.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndex.h"
#include "pxr/imaging/hd/sceneGlobalsSchema.h"
#include "pxr/imaging/hd/sceneIndexObserver.h"
#include "pxr/imaging/hd/tokens.h"

#include "pxr/usd/sdf/path.h"

#include "pxr/base/gf/rotation.h"
#include "pxr/base/tf/debug.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/refPtr.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/typeHeaders.h"
#include "pxr/base/vt/types.h"
#include "pxr/base/vt/value.h"
#include "pxr/base/vt/visitValue.h"

#include "pxr/pxr.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(HDSI_VELOCITY_MOTION, "Velocity-based motion");
}

TF_DEFINE_PUBLIC_TOKENS(HdsiVelocityMotionResolvingSceneIndexTokens,
    HDSI_VELOCITY_MOTION_RESOLVING_SCENE_INDEX_TOKENS);

namespace {

const double _fallbackTimeCodesPerSecond = 24.0f;

bool
_PrimvarAffectedByVelocity(const TfToken& primvar)
{
    static const TfToken::Set primvars {
        HdPrimvarsSchemaTokens->points,
        HdInstancerTokens->instanceTranslations,
        HdInstancerTokens->instanceRotations,
        HdInstancerTokens->instanceScales };
    return primvars.count(primvar) > 0;
}

// -----------------------------------------------------------------------------

// Since we can have rotations as VtQuathArray or VtQuatfArray, these helpers
// make applying angular velocities to them less ugly.

template <typename T>
VtValue
_ApplyAngularVelocities(
    const VtArray<T>& rotations,
    const VtVec3fArray& velocities,
    const HdSampledDataSource::Time scaledTime)
{
    VtArray<T> result(rotations.size());
    for (size_t i = 0; i < rotations.size(); ++i) {
        GfRotation rotation = GfRotation(rotations[i]);
        rotation *= GfRotation(velocities[i],
            scaledTime * velocities[i].GetLength());
        result[i] = T(rotation.GetQuat());
    }
    return VtValue(result);
}

VtValue
_ApplyAngularVelocities(
    const VtValue& rotations,
    const VtVec3fArray& velocities,
    const HdSampledDataSource::Time scaledTime)
{
    if (rotations.IsHolding<VtQuathArray>()) {
        return _ApplyAngularVelocities(
            rotations.UncheckedGet<VtQuathArray>(), velocities, scaledTime);
    }
    if (rotations.IsHolding<VtQuatfArray>()) {
        return _ApplyAngularVelocities(
            rotations.UncheckedGet<VtQuatfArray>(), velocities, scaledTime);
    }
    TF_CODING_ERROR("Unexpected rotations type");
    return VtValue();
}

// -----------------------------------------------------------------------------

class _VelocityHelper
{
public:
    using Time = HdSampledDataSource::Time;

    _VelocityHelper(
        const TfToken& name,
        const HdSampledDataSourceHandle& source,
        const SdfPath& primPath,
        const HdContainerDataSourceHandle& primSource,
        const HdSceneIndexBaseRefPtr& inputSceneIndex)
      : _name(name)
      , _source(source)
      , _primPath(primPath)
      , _primSource(primSource)
      , _inputSceneIndex(inputSceneIndex)
    { }

protected:
    bool
    _GetContributingSampleTimesForInterval(
        Time startTime,
        Time endTime,
        std::vector<Time>* const outSampleTimes)
    {
        const TfToken mode = _GetMode();
        if (mode == HdsiVelocityMotionResolvingSceneIndexTokens->ignore) {
            // velocity-based motion is ignored; defer to source
            TF_DEBUG(HDSI_VELOCITY_MOTION).Msg(
                "<%s.%s>: Ignoring velocity-based motion (mode)\n",
                _primPath.GetText(), _name.GetText());
            return _source->GetContributingSampleTimesForInterval(
                startTime, endTime, outSampleTimes);
        }

        VtValue sourceValue;
        Time sampleTime;
        if (!_VelocityMotionValidForCurrentFrame(
            &sourceValue, nullptr, &sampleTime)) {
            // velocity-based motion is invalid; defer to source
            return _source->GetContributingSampleTimesForInterval(
                startTime, endTime, outSampleTimes);
        }

        // velocity-based motion is valid

        // XXX: These next two are handled separately to make nice debug
        if (mode == HdsiVelocityMotionResolvingSceneIndexTokens->disable) {
            // velocity-based motion is disabled
            TF_DEBUG(HDSI_VELOCITY_MOTION).Msg(
                "<%s.%s>: Velocity-based motion disabled (mode)\n",
                _primPath.GetText(), _name.GetText());
            outSampleTimes->clear();
            return false;
        }

        // Instance scales are always frozen when doing velocity motion.
        if (_name == HdInstancerTokens->instanceScales) {
            TF_DEBUG(HDSI_VELOCITY_MOTION).Msg(
                "<%s.%s>: Frozen\n", _primPath.GetText(), _name.GetText());
            outSampleTimes->clear();
            return false;
        }

        *outSampleTimes = { startTime, endTime };

        // Check for non-linear motion and insert any additional required sample
        // times according to nonlinearSampleCount.
        if (_name == HdInstancerTokens->instanceRotations ||
            (mode == HdsiVelocityMotionResolvingSceneIndexTokens->enable &&
                _GetAccelerations(sampleTime).size()
                    >= sourceValue.GetArraySize())) {
            const int n = std::max(3, _GetNonlinearSampleCount()) - 1;
            for (int k = 1; k < n; ++k) {
                outSampleTimes->insert(outSampleTimes->end() - 1,
                    startTime + float(k) / float(n) * (endTime - startTime));
            }
        }
        if (TfDebug::IsEnabled(HDSI_VELOCITY_MOTION)) {
            std::string s;
            for (const Time& t : *outSampleTimes) {
                if (!s.empty()) {
                    s += ", ";
                }
                s += TfStringPrintf("%f", t);
            }
            TF_DEBUG(HDSI_VELOCITY_MOTION).Msg(
                "<%s.%s>: Sample times: [%s]\n",
                _primPath.GetText(), _name.GetText(), s.c_str());
        }
        return true;
    }

    VtValue
    _GetValue(Time shutterOffset)
    {
        const TfToken mode = _GetMode();
        if (mode == HdsiVelocityMotionResolvingSceneIndexTokens->ignore) {
            // velocity-based motion is ignored; defer to source
            return _source->GetValue(shutterOffset);
        }
        VtVec3fArray velocities;
        VtValue sourceVal;
        Time sampleTime;
        if (!_VelocityMotionValidForCurrentFrame(
            &sourceVal, &velocities, &sampleTime)) {
            // velocity-based motion is invalid; defer to source
            return _source->GetValue(shutterOffset);
        }

        // velocity-based motion is valid

        // when velocity motion is disabled, or when handling instancer
        // scales, freeze to left-bracketing time sample
        if (mode == HdsiVelocityMotionResolvingSceneIndexTokens->disable ||
            _name == HdInstancerTokens->instanceScales) {
            return _source->GetValue(sampleTime);
        }

        // USD defines timeCodesPerSecond as double; convert to float.
        const auto timeCodesPerSecond = float(_GetTimeCodesPerSecond());
        const Time scaledTime =
            (shutterOffset - sampleTime) / timeCodesPerSecond;

        // rotations
        if (_name == HdInstancerTokens->instanceRotations) {
            return _ApplyAngularVelocities(sourceVal, velocities, scaledTime);
        }

        // positions
        const auto positions = sourceVal.UncheckedGet<VtVec3fArray>();

        // check for accelerations
        VtVec3fArray accelerations { };
        bool useAccelerations = mode !=
            HdsiVelocityMotionResolvingSceneIndexTokens->noAcceleration;
        if (useAccelerations) {
            accelerations = _GetAccelerations(sampleTime);
            useAccelerations = accelerations.size() >= positions.size();
        }

        // perform velocity motion on positions
        VtVec3fArray result(positions.size());
        if (useAccelerations) {
            const float timeSqrHalf = 0.5f * scaledTime * scaledTime;
            for (size_t i = 0; i < positions.size(); ++i) {
                result[i] = positions[i]
                  + scaledTime * velocities[i]
                  + timeSqrHalf * accelerations[i];
            }
        } else {
            for (size_t i = 0; i < positions.size(); ++i) {
                result[i] = positions[i]
                  + scaledTime * velocities[i];
            }
        }
        return VtValue(result);
    }

private:
    // Gets timeCodesPerSecond with the following priority (first found wins):
    //  - From HdSceneGlobalsSchema
    //  - From the static fallback defined in this file
    double
    _GetTimeCodesPerSecond() const
    {
        if (const auto& sg =
            HdSceneGlobalsSchema::GetFromSceneIndex(_inputSceneIndex)) {
            if (const auto& ds = sg.GetTimeCodesPerSecond()) {
                return ds->GetTypedValue(0.f);
            }
        }

        return _fallbackTimeCodesPerSecond;
    }

    // Retrieves the value of the accelerations primvar for the current frame,
    // if present. If not present, or incorrect type, or the contributing sample
    // time doesn't match the given one, returns an empty VtVec3fArray.
    // Caller still needs to check that there are enough accelerations
    // values to cover all the positions needing transformation.
    VtVec3fArray
    _GetAccelerations(
        const Time sampleTime) const
    {
        static const VtVec3fArray empty { };
        static const HdDataSourceLocator accelerationsLocator {
            HdPrimvarsSchema::GetSchemaToken(),
            HdTokens->accelerations,
            HdPrimvarSchemaTokens->primvarValue };
        const auto accelerationsDs = HdSampledDataSource::Cast(
            HdContainerDataSource::Get(_primSource, accelerationsLocator));
        if (!accelerationsDs) {
            // accelerations not present
            return empty;
        }
        std::vector<Time> times;
        if (!accelerationsDs->GetContributingSampleTimesForInterval(
            0.0, 0.0, &times)) {
            // accelerations has constant value across all time; sample timing
            // does not matter
            times.resize(1);
            times.front() = sampleTime;
        }
        if (times.front() != sampleTime) {
            // accelerations not authored at same starting time as source
            return empty;
        }
        const VtValue accelerationsVal = accelerationsDs->GetValue(times.front());
        if (!accelerationsVal.IsHolding<VtVec3fArray>()) {
            // accelerations are wrong type
            return empty;
        }
        return accelerationsVal.UncheckedGet<VtVec3fArray>();
    }

    /// \brief Determines whether the conditions are met for performing velocity-
    /// based motion on the underlying source primvar at the current frame.
    /// Populates \p srcVal, \p velocities, and \p outSampleTime, if provided,
    /// with their respective values so they do not need to be fetched again by
    /// the caller.
    /// \param srcValue (optional) will be filled with the sampled value of the
    /// underlying source for the current frame. This will be 0-order points,
    /// positions, or orientations at the first authored sample time at or
    /// before shutterOffset 0.
    /// \param velocities (optional) will be filled with the sampled velocities
    /// value at for the current frame. This will be the 1-order velocities or
    /// angular velocities t the first authored sample time at or before
    /// shutterOffset 0.
    /// \param outSampleTime (optional) will be filled with the shutterOffset
    /// value at which \p srcValue and \p velocities were actually sampled. This
    /// is the offset to the left-bracketing (most recent authored) time sample,
    /// and will always be <= 0.
    /// \returns true if conditions are met for velocity motion, false
    /// otherwise. The optional parameters will not be populated if the return
    /// value is false.
    bool
    _VelocityMotionValidForCurrentFrame(
        VtValue* srcValue = nullptr,
        VtVec3fArray* velocities = nullptr,
        Time* outSampleTime = nullptr) const
    {
        const HdDataSourceLocator velocitiesLocator {
            HdPrimvarsSchema::GetSchemaToken(),
            _name == HdInstancerTokens->instanceRotations
              ? HdTokens->angularVelocities
              : HdTokens->velocities,
            HdPrimvarSchemaTokens->primvarValue };
        const HdSampledDataSourceHandle velocitiesDs =
            HdSampledDataSource::Cast(HdContainerDataSource::Get(
                _primSource, velocitiesLocator));
        if (!velocitiesDs) {
            // velocities not present
            TF_DEBUG(HDSI_VELOCITY_MOTION).Msg(
                "<%s.%s>: No velocities\n",
                _primPath.GetText(), _name.GetText());
            return false;
        }

        // Source and velocities must both be authored at the same frame to be
        // valid. For example, when positions are authored at [1, 2, 3] but
        // velocities are only authored at [1, 3], there is no velocity-based
        // motion at frame [2]. But when positions and velocities are both
        // authored at [1, 3], there is valid velocity-based motion at frame [2],
        // propagated from the authored values at [1]. We check here that the
        // left-bracketing sample time for the current frame is the same for
        // both source and velocities. The catch is that one or both might be
        // constant, which we treat as equivalent to there being time samples
        // at every point in time. GetContributingSampleTimesForInterval()
        // is supposed to return false in this situation, and is not required
        // to put anything into outSampleTimes, so if we get a false we have to
        // assume the left-bracketing time sample is exactly at the current
        // frame.
        std::vector<std::vector<Time>> times(2);
        if (!_source->GetContributingSampleTimesForInterval(
            0.f, 0.f, &times[0])) {
            // Source has no time samples, so has the same value at every time.
            // The frame-relative left-bracketing sample time is 0.
            times[0].resize(1);
            times[0][0] = 0.f;
        }
        if (!velocitiesDs->GetContributingSampleTimesForInterval(
                0.f, 0.f, &times[1])) {
            // Velocities has no time samples, so has the same value at every
            // time. The frame-relative left-bracketing sample time is 0.
            times[1].resize(1);
            times[1][0] = 0.f;
        }
        if (times[1][0] != times[0][0]) {
            // Source and velocities do not share a common frame-relative
            // left-bracketing sample time
            TF_DEBUG(HDSI_VELOCITY_MOTION).Msg(
                "<%s.%s>: Time sample ordinality mismatch (src: %f != vel: %f)\n",
                _primPath.GetText(), _name.GetText(),
                times[0].front(), times[1].front());
            return false;
        }
        const Time sampleTime = times[0][0];
        const VtValue velocitiesVal = velocitiesDs->GetValue(sampleTime);
        if (!velocitiesVal.IsHolding<VtVec3fArray>()) {
            TF_DEBUG(HDSI_VELOCITY_MOTION).Msg(
                "<%s.%s>: Velocities wrong type\n",
                _primPath.GetText(), _name.GetText());
            return false;
        }
        const VtValue sourceVal = _source->GetValue(sampleTime);
        if (sourceVal.GetArraySize() > velocitiesVal.GetArraySize()) {
            // not enough velocities
            TF_DEBUG(HDSI_VELOCITY_MOTION).Msg(
                "<%s.%s>: Fewer velocities than source\n",
                _primPath.GetText(), _name.GetText());
            return false;
        }
        if (_name == HdInstancerTokens->instanceRotations) {
            if (!(sourceVal.IsHolding<VtQuathArray>() ||
                sourceVal.IsHolding<VtQuatfArray>())) {
                // source rotations are wrong type
                TF_DEBUG(HDSI_VELOCITY_MOTION).Msg(
                    "<%s.%s>: Source rotations wrong type\n",
                    _primPath.GetText(), _name.GetText());
                return false;
            }
        } else if (!sourceVal.IsHolding<VtVec3fArray>()) {
            // source points/positions or scales are wrong type
            TF_DEBUG(HDSI_VELOCITY_MOTION).Msg(
                "<%s.%s>: Source positions/scales wrong type\n",
                _primPath.GetText(), _name.GetText());
            return false;
        }
        if (srcValue != nullptr) {
            *srcValue = sourceVal;
        }
        if (velocities != nullptr) {
            *velocities = velocitiesVal.UncheckedGet<VtVec3fArray>();
        }
        if (outSampleTime != nullptr) {
            *outSampleTime = sampleTime;
        }
        TF_DEBUG(HDSI_VELOCITY_MOTION).Msg(
            "<%s.%s>: Valid velocity-based motion\n",
            _primPath.GetText(), _name.GetText());
        return true;
    }

    int
    _GetNonlinearSampleCount()
    {
        static const int defaultValue = 3; // From UsdGeomMotionAPI
        static const HdDataSourceLocator locator = {
            HdPrimvarsSchema::GetSchemaToken(),
            HdTokens->nonlinearSampleCount,
            HdPrimvarSchemaTokens->primvarValue };
        const auto ds = HdSampledDataSource::Cast(HdContainerDataSource::Get(
            _primSource, locator));
        if (!ds) {
            return defaultValue;
        }
        return ds->GetValue(0.0).GetWithDefault(defaultValue);
    }

    TfToken
    _GetMode()
    {
        static const HdDataSourceLocator locator(
            HdsiVelocityMotionResolvingSceneIndexTokens->velocityMotionMode);
        static const TfToken::Set validModes = {
            HdsiVelocityMotionResolvingSceneIndexTokens->enable,
            HdsiVelocityMotionResolvingSceneIndexTokens->disable,
            HdsiVelocityMotionResolvingSceneIndexTokens->noAcceleration,
            HdsiVelocityMotionResolvingSceneIndexTokens->ignore };
        static const TfToken defaultMode =
            HdsiVelocityMotionResolvingSceneIndexTokens->enable;
        const auto ds = HdSampledDataSource::Cast(HdContainerDataSource::Get(
            _primSource, locator));
        if (!ds) {
            return defaultMode;
        }
        TfToken value = ds->GetValue(0.0).GetWithDefault(defaultMode);
        if (validModes.count(value) == 0) {
            TF_DEBUG(HDSI_VELOCITY_MOTION).Msg(
                "<%s.%s>: Unrecognized velocity motion mode token '%s'; "
                "assuming 'enable'\n", _primPath.GetText(), _name.GetText(),
                value.GetText());
            return defaultMode;
        }
        return value;
    }

    // name of the 0-order parameter this data source wraps
    // (positions, points, or orientations)
    TfToken _name;
    // the incoming data source for the 0-order parameter to be wrapped
    HdSampledDataSourceHandle _source;
    SdfPath _primPath;
    HdContainerDataSourceHandle _primSource;
    HdSceneIndexBaseRefPtr _inputSceneIndex;
};

// -----------------------------------------------------------------------------

class _UntypedValueDataSource final
  : public HdSampledDataSource
  , private _VelocityHelper
{
public:
    using Time = HdSampledDataSource::Time;

    HD_DECLARE_DATASOURCE(_UntypedValueDataSource);

    _UntypedValueDataSource(
        const TfToken& name,
        const HdSampledDataSourceHandle& source,
        const SdfPath& primPath,
        const HdContainerDataSourceHandle& primSource,
        const HdSceneIndexBaseRefPtr& inputSceneIndex)
      : _VelocityHelper(
            name, source, primPath, primSource, inputSceneIndex)
    { }

    VtValue
    GetValue(Time shutterOffset) override
    {
        return _GetValue(shutterOffset);
    }

    bool GetContributingSampleTimesForInterval(
        Time startTime, Time endTime,
        std::vector<Time>* outSampleTimes) override
    {
        return _GetContributingSampleTimesForInterval(
            startTime, endTime, outSampleTimes);
    }
};

HD_DECLARE_DATASOURCE_HANDLES(_UntypedValueDataSource);

// -----------------------------------------------------------------------------

template <typename T>
class _TypedValueDataSource final
  : public HdTypedSampledDataSource<T>
  , private _VelocityHelper
{
public:
    using Time = HdSampledDataSource::Time;

    HD_DECLARE_DATASOURCE_ABSTRACT(_TypedValueDataSource<T>);

    _TypedValueDataSource(
        const TfToken& name,
        const HdSampledDataSourceHandle& source,
        const SdfPath& primPath,
        const HdContainerDataSourceHandle& primSource,
        const HdSceneIndexBaseRefPtr& inputSceneIndex)
      : _VelocityHelper(name, source, primPath, primSource, inputSceneIndex)
    { }

    VtValue
    GetValue(Time shutterOffset) override
    {
        return _GetValue(shutterOffset);
    }

    bool
    GetContributingSampleTimesForInterval(
        Time startTime, Time endTime,
        std::vector<Time>* outSampleTimes) override
    {
        return _GetContributingSampleTimesForInterval(
            startTime, endTime, outSampleTimes);
    }

    T
    GetTypedValue(Time shutterOffset) override
    {
        const VtValue& v = GetValue(shutterOffset);
        if (v.IsHolding<T>()) {
            return v.UncheckedGet<T>();
        }
        return T();
    }

    static typename
    _TypedValueDataSource<T>::Handle New(
        const TfToken& name,
        const HdSampledDataSourceHandle& source,
        const SdfPath& primPath,
        const HdContainerDataSourceHandle& primSource,
        const HdSceneIndexBaseRefPtr& inputSceneIndex)
    {
        return _TypedValueDataSource<T>::Handle(
            new _TypedValueDataSource<T>(
                name, source, primPath, primSource, inputSceneIndex));
    }
};

// -----------------------------------------------------------------------------

struct _PrimvarSourceTypeVisitor
{
    const TfToken& name;
    const HdSampledDataSourceHandle& source;
    const SdfPath& primPath;
    const HdContainerDataSourceHandle& primSource;
    const HdSceneIndexBaseRefPtr& inputSceneIndex;

    template <typename T>
    HdDataSourceBaseHandle
    operator()(const T&)
    {
        return _TypedValueDataSource<T>::New(
            name, source, primPath, primSource, inputSceneIndex);
    }

    HdDataSourceBaseHandle
    operator()(const VtValue&)
    {
        return _UntypedValueDataSource::New(
            name, source, primPath, primSource, inputSceneIndex);
    }
};

// -----------------------------------------------------------------------------

class _PrimvarDataSource final
  : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_PrimvarDataSource);

    _PrimvarDataSource(
        const TfToken& name,
        const HdContainerDataSourceHandle& source,
        const SdfPath& primPath,
        const HdContainerDataSourceHandle& primSource,
        const HdSceneIndexBaseRefPtr& inputSceneIndex)
      : _name(name)
      , _source(source)
      , _primPath(primPath)
      , _primSource(primSource)
      , _inputSceneIndex(inputSceneIndex)
    { }

    TfTokenVector
    GetNames() override
    {
        if (!_source) {
            return { };
        }

        return _source->GetNames();
    }

    HdDataSourceBaseHandle
    Get(const TfToken& name) override
    {
        if (!_source) {
            return nullptr;
        }
        HdDataSourceBaseHandle ds = _source->Get(name);
        if (ds && name == HdPrimvarSchemaTokens->primvarValue) {
            if (const auto source = HdSampledDataSource::Cast(ds)) {
                // XXX: source is sampled at time 0 only to determine its type
                return VtVisitValue(
                    source->GetValue(0.0f),
                    _PrimvarSourceTypeVisitor {
                        _name, source, _primPath,
                        _primSource, _inputSceneIndex });
            }
        }
        return ds;
    }

private:
    TfToken _name;
    HdContainerDataSourceHandle _source;
    SdfPath _primPath;
    HdContainerDataSourceHandle _primSource;
    HdSceneIndexBaseRefPtr _inputSceneIndex;
};

HD_DECLARE_DATASOURCE_HANDLES(_PrimvarDataSource);

// -----------------------------------------------------------------------------

class _PrimvarsDataSource final
  : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_PrimvarsDataSource);

    _PrimvarsDataSource(
        const HdContainerDataSourceHandle& source,
        const SdfPath& primPath,
        const HdContainerDataSourceHandle& primSource,
        const HdSceneIndexBaseRefPtr& inputSceneIndex)
      : _source(source)
      , _primPath(primPath)
      , _primSource(primSource)
      , _inputSceneIndex(inputSceneIndex)
    { }

    TfTokenVector
    GetNames() override
    {
        if (!_source) {
            return { };
        }
        return _source->GetNames();
    }

    HdDataSourceBaseHandle
    Get(const TfToken& name) override
    {
        if (!_source) {
            return nullptr;
        }
        HdDataSourceBaseHandle ds = _source->Get(name);
        if (ds && _PrimvarAffectedByVelocity(name)) {
            return _PrimvarDataSource::New(
                name, HdContainerDataSource::Cast(ds),
                _primPath, _primSource, _inputSceneIndex);
        }
        return ds;
    }
private:
    HdContainerDataSourceHandle _source;
    SdfPath _primPath;
    HdContainerDataSourceHandle _primSource;
    HdSceneIndexBaseRefPtr _inputSceneIndex;
};

HD_DECLARE_DATASOURCE_HANDLES(_PrimvarsDataSource);

// -----------------------------------------------------------------------------

class _PrimDataSource final
  : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_PrimDataSource);

    _PrimDataSource(
        const SdfPath& primPath,
        const HdContainerDataSourceHandle& primSource,
        const HdSceneIndexBaseRefPtr& inputSceneIndex)
      : _primPath(primPath)
      , _primSource(primSource)
      , _inputSceneIndex(inputSceneIndex)
    { }

    TfTokenVector
    GetNames() override
    {
        if (!_primSource) {
            return { };
        }
        TfTokenVector names = _primSource->GetNames();
        if (std::find(names.cbegin(), names.cend(),
            HdDependenciesSchema::GetSchemaToken()) == names.cend()) {
            names.push_back(HdDependenciesSchema::GetSchemaToken());
        }
        return names;
    }

    HdDataSourceBaseHandle
    Get(const TfToken& name) override
    {
        if (!_primSource) {
            return nullptr;
        }
        HdDataSourceBaseHandle ds = _primSource->Get(name);
        if (name == HdDependenciesSchema::GetSchemaToken()) {
            // Entire prim depends on </.sceneGlobals.timeCodesPerSecond>
            static const std::vector<TfToken> names = {
                TfToken("prim_dep_globals_timeCodesPerSecond") };
            static const std::vector<HdDataSourceBaseHandle> sources = {
                HdDependencySchema::Builder()
                .SetDependedOnPrimPath(
                    HdRetainedTypedSampledDataSource<SdfPath>::New(
                        HdSceneGlobalsSchema::GetDefaultPrimPath()))
                .SetDependedOnDataSourceLocator(
                    HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
                        HdSceneGlobalsSchema::GetTimeCodesPerSecondLocator()))
                .SetAffectedDataSourceLocator(
                    HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
                        HdDataSourceLocator::EmptyLocator()))
                .Build() };
            static const HdContainerDataSourceHandle overlayDs =
                HdDependenciesSchema::BuildRetained(
                    names.size(), names.data(), sources.data());
            if (auto dependenciesDs = HdContainerDataSource::Cast(ds)) {
                return HdOverlayContainerDataSource::New(
                    overlayDs, dependenciesDs);
            }
            return overlayDs;
        }
        if (ds && name == HdPrimvarsSchema::GetSchemaToken()) {
            return _PrimvarsDataSource::New(
                HdContainerDataSource::Cast(ds),
                _primPath, _primSource, _inputSceneIndex);
        }
        return ds;
    }
private:
    SdfPath _primPath;
    HdContainerDataSourceHandle _primSource;
    HdSceneIndexBaseRefPtr _inputSceneIndex;
};

HD_DECLARE_DATASOURCE_HANDLES(_PrimDataSource);

} // anonymous namespace

HdsiVelocityMotionResolvingSceneIndexRefPtr
HdsiVelocityMotionResolvingSceneIndex::New(
    const HdSceneIndexBaseRefPtr& inputSceneIndex,
    const HdContainerDataSourceHandle& /* inputArgs */)
{
    return TfCreateRefPtr(
        new HdsiVelocityMotionResolvingSceneIndex(inputSceneIndex));
}

HdsiVelocityMotionResolvingSceneIndex::HdsiVelocityMotionResolvingSceneIndex(
    const HdSceneIndexBaseRefPtr& inputSceneIndex,
    const HdContainerDataSourceHandle& /* inputArgs */)
  : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
{ }

bool
HdsiVelocityMotionResolvingSceneIndex::PrimTypeSupportsVelocityMotion(
    const TfToken& primType)
{
    static const TfToken::Set types {
        HdPrimTypeTokens->points,
        HdPrimTypeTokens->basisCurves,
        HdPrimTypeTokens->nurbsCurves,
        HdPrimTypeTokens->nurbsPatch,
        HdPrimTypeTokens->tetMesh,
        HdPrimTypeTokens->mesh,
        HdPrimTypeTokens->instancer };
    return types.count(primType) > 0;
}

HdSceneIndexPrim
HdsiVelocityMotionResolvingSceneIndex::GetPrim(
    const SdfPath& primPath) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
    if (PrimTypeSupportsVelocityMotion(prim.primType)) {
        prim.dataSource = _PrimDataSource::New(
            primPath, prim.dataSource, _GetInputSceneIndex());
    }
    return prim;
}

SdfPathVector
HdsiVelocityMotionResolvingSceneIndex::GetChildPrimPaths(
    const SdfPath& primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void
HdsiVelocityMotionResolvingSceneIndex::_PrimsAdded(
    const HdSceneIndexBase&  /*sender*/,
    const HdSceneIndexObserver::AddedPrimEntries& entries)
{
    return _SendPrimsAdded(entries);
}

void
HdsiVelocityMotionResolvingSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase&  /*sender*/,
    const HdSceneIndexObserver::RemovedPrimEntries& entries)
{
    return _SendPrimsRemoved(entries);
}

void
HdsiVelocityMotionResolvingSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase&  /*sender*/,
    const HdSceneIndexObserver::DirtiedPrimEntries& entries)
{
    // Scales-freezing depends on whether velocity-based motion is valid, so
    // if either positions or rotations is dirty, we will dirty scales as well.
    static const HdDataSourceLocatorSet positionsLocators {
        HdPrimvarsSchema::GetPointsLocator(),
        HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdInstancerTokens->instanceTranslations),
        HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdInstancerTokens->instanceScales) };
    static const HdDataSourceLocatorSet rotationsLocators {
        HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdInstancerTokens->instanceRotations),
        HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdInstancerTokens->instanceScales) };
    static const HdDataSourceLocatorSet positionsAffectingLocators {
        HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdTokens->velocities),
        HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdTokens->accelerations),
        HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdTokens->nonlinearSampleCount),
        HdDataSourceLocator(
            HdsiVelocityMotionResolvingSceneIndexTokens->velocityMotionMode) };
    static const HdDataSourceLocatorSet rotationsAffectingLocators {
        HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdTokens->angularVelocities),
        HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdTokens->nonlinearSampleCount),
        HdDataSourceLocator(
            HdsiVelocityMotionResolvingSceneIndexTokens->velocityMotionMode) };

    // TODO: Avoid copying entries where possible
    HdSceneIndexObserver::DirtiedPrimEntries newEntries;
    for (const auto& entry : entries) {
        HdSceneIndexObserver::DirtiedPrimEntry newEntry(entry);
        if (entry.dirtyLocators.Intersects(positionsAffectingLocators)) {
            newEntry.dirtyLocators.insert(positionsLocators);
        }
        if (entry.dirtyLocators.Intersects(rotationsAffectingLocators)) {
            newEntry.dirtyLocators.insert(rotationsLocators);
        }
        newEntries.push_back(newEntry);
    }
    return _SendPrimsDirtied(newEntries);
}

PXR_NAMESPACE_CLOSE_SCOPE
