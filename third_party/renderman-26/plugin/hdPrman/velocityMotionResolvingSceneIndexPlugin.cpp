//
// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "hdPrman/velocityMotionResolvingSceneIndexPlugin.h"
#include "hdPrman/tokens.h"

#include "pxr/imaging/hd/dataSource.h"
#include "pxr/imaging/hd/dataSourceTypeDefs.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndexObserver.h"
#include "pxr/imaging/hd/sceneIndexPlugin.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"

#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/type.h"

#include "pxr/pxr.h"

#if PXR_VERSION >= 2401
#include "pxr/imaging/hdsi/version.h"
#else
#define HDSI_API_VERSION 0
#endif

#if HDSI_API_VERSION >= 15
#include "pxr/imaging/hdsi/velocityMotionResolvingSceneIndex.h"
#else
#include "pxr/imaging/hd/dataSource.h"
#include "pxr/imaging/hd/dataSourceLocator.h"
#include "pxr/imaging/hd/filteringSceneIndex.h"
#include "pxr/imaging/hd/primvarSchema.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/sceneIndex.h"
#include "pxr/imaging/hd/sceneIndexObserver.h"
#include "pxr/imaging/hd/tokens.h"

#include "pxr/usd/sdf/path.h"

#include "pxr/base/gf/rotation.h"
#include "pxr/base/tf/debug.h"
#include "pxr/base/tf/declarePtrs.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/refPtr.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/typeHeaders.h"
#include "pxr/base/vt/types.h"
#include "pxr/base/vt/value.h"
#include "pxr/base/vt/visitValue.h"

#include "pxr/pxr.h"

#include <algorithm>
#include <cstddef>
#include <vector>
#endif

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (fps)
    ((vblur, "ri:object:vblur"))
    ((vblur_enable, "Acceleration Blur"))
    ((vblur_ignore, "No Velocity Blur"))
    ((vblur_noAcceleration, "Velocity Blur"))

    // Remove after dropping support for USD < 24.06
    (angularVelocities)

    );

static float _fallbackFps = 24.f;

#if HDSI_API_VERSION < 15

TF_DEBUG_CODES(HDSI_VELOCITY_MOTION);

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(HDSI_VELOCITY_MOTION, "Velocity-based motion");
}

TF_DEFINE_PRIVATE_TOKENS(
    HdsiVelocityMotionResolvingSceneIndexTokens,
    (disable)
    (enable)
    (ignore)
    (noAcceleration)
    ((velocityMotionMode, "__velocityMotionMode")));

namespace {

float _GetFps(const HdContainerDataSourceHandle& inputArgs)
{
    if (!inputArgs) {
        return _fallbackFps;
    }
    const auto source = HdSampledDataSource::Cast(inputArgs->Get(_tokens->fps));
    if (!source) {
        return _fallbackFps;
    }
    const VtValue &value = source->GetValue(0.0f);
    if (!value.IsHolding<float>()) {
        return _fallbackFps;
    }
    return value.UncheckedGet<float>();
}

bool
_PrimvarAffectedByVelocity(const TfToken& primvar)
{
    static const TfToken::Set primvars {
        HdPrimvarsSchemaTokens->points
#if HD_API_VERSION < 67
        , HdInstancerTokens->translate
        , HdInstancerTokens->rotate
        , HdInstancerTokens->scale
#endif
#if HD_API_VERSION >= 56
        , HdInstancerTokens->instanceTranslations
        , HdInstancerTokens->instanceRotations
        , HdInstancerTokens->instanceScales
#endif
    };
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
TF_DECLARE_REF_PTRS(HdsiVelocityMotionResolvingSceneIndex);

class HdsiVelocityMotionResolvingSceneIndex
  : public HdSingleInputFilteringSceneIndexBase
{
public:
    static HdsiVelocityMotionResolvingSceneIndexRefPtr
    New(
        const HdSceneIndexBaseRefPtr& inputSceneIndex,
        const HdContainerDataSourceHandle& inputArgs);

    HdSceneIndexPrim
    GetPrim(const SdfPath& primPath) const override;

    SdfPathVector
    GetChildPrimPaths(const SdfPath& primPath) const override;

    static bool
    PrimTypeSupportsVelocityMotion(const TfToken& primType);

protected:
    HdsiVelocityMotionResolvingSceneIndex(
        const HdSceneIndexBaseRefPtr& inputSceneIndex,
        const HdContainerDataSourceHandle& inputArgs);

    void
    _PrimsAdded(
        const HdSceneIndexBase& sender,
        const HdSceneIndexObserver::AddedPrimEntries& entries) override;

    void
    _PrimsRemoved(
        const HdSceneIndexBase& sender,
        const HdSceneIndexObserver::RemovedPrimEntries& entries) override;

    void
    _PrimsDirtied(
        const HdSceneIndexBase& sender,
        const HdSceneIndexObserver::DirtiedPrimEntries& entries) override;

private:
    HdContainerDataSourceHandle _inputArgs;
};

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
        const HdContainerDataSourceHandle& inputArgs)
      : _name(name)
      , _source(source)
      , _primPath(primPath)
      , _primSource(primSource)
      , _inputArgs(inputArgs)
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
        if (
#if HD_API_VERSION < 56
            _name == HdInstancerTokens->scale
#elif HD_API_VERSION < 67
            _name == HdInstancerTokens->scale ||
            _name == HdInstancerTokens->instancerScales
#else
            _name == HdInstancerTokens->instanceScales
#endif
            ) {
            TF_DEBUG(HDSI_VELOCITY_MOTION).Msg(
                "<%s.%s>: Frozen\n", _primPath.GetText(), _name.GetText());
            outSampleTimes->clear();
            return false;
        }

        *outSampleTimes = { startTime, endTime };

        // Check for non-linear motion and insert any additional required sample
        // times according to nonlinearSampleCount.
        if (
#if HD_API_VERSION < 56
            _name == HdInstancerTokens->rotate ||
#elif HD_API_VERSION < 67
            _name == HdInstancerTokens->rotate ||
            _name == HdInstancerTokens->instanceRotations ||
#else
            _name == HdInstancerTokens->instanceRotations ||
#endif
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
#if HD_API_VERSION < 56
            _name == HdInstancerTokens->scale
#elif HD_API_VERSION < 67
            _name == HdInstancerTokens->scale ||
            _name == HdInstancerTokens->instancerScales
#else
            _name == HdInstancerTokens->instanceScales
#endif
            ) {
            return _source->GetValue(sampleTime);
        }

        const float fps = _GetFps(_inputArgs);
        const Time scaledTime = (shutterOffset - sampleTime) / fps;

        // rotations
        if (
#if HD_API_VERSION < 56
            _name == HdInstancerTokens->rotate
#elif HD_API_VERSION < 67
            _name == HdInstancerTokens->rotate ||
            _name == HdInstancerTokens->instanceRotations
#else
            _name == HdInstancerTokens->instanceRotations
#endif
        ) {
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
#if PXR_VERSION < 2306
            HdPrimvarsSchemaTokens->primvars,
#else
            HdPrimvarsSchema::GetSchemaToken(),
#endif
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
#if PXR_VERSION < 2306
            HdPrimvarsSchemaTokens->primvars,
#else
            HdPrimvarsSchema::GetSchemaToken(),
#endif
#if HD_API_VERSION < 56
            _name == HdInstancerTokens->rotate
#elif HD_API_VERSION < 67
            (_name == HdInstancerTokens->rotate ||
            _name == HdInstancerTokens->instanceRotations)
#else
            (_name == HdInstancerTokens->instanceRotations)
#endif
#if PXR_VERSION < 2406
              ? _tokens->angularVelocities
#else
              ? HdTokens->angularVelocities
#endif
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
        if (
#if HD_API_VERSION < 56
            _name == HdInstancerTokens->rotate
#elif HD_API_VERSION < 67
            (_name == HdInstancerTokens->rotate ||
            _name == HdInstancerTokens->instanceRotations)
#else
            (_name == HdInstancerTokens->instanceRotations)
#endif
        ) {
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
#if PXR_VERSION < 2306
            HdPrimvarsSchemaTokens->primvars,
#else
            HdPrimvarsSchema::GetSchemaToken(),
#endif
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
    HdContainerDataSourceHandle _inputArgs;
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
        const HdContainerDataSourceHandle& inputArgs)
      : _VelocityHelper(name, source, primPath, primSource, inputArgs)
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
        const HdContainerDataSourceHandle& inputArgs)
      : _VelocityHelper(name, source, primPath, primSource, inputArgs)
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
        const HdContainerDataSourceHandle& inputArgs)
    {
        return _TypedValueDataSource<T>::Handle(
            new _TypedValueDataSource<T>(
                name, source, primPath, primSource, inputArgs));
    }
};

// -----------------------------------------------------------------------------

struct _PrimvarSourceTypeVisitor
{
    const TfToken name;
    const HdSampledDataSourceHandle source;
    const SdfPath& primPath;
    const HdContainerDataSourceHandle primSource;
    const HdContainerDataSourceHandle inputArgs;

    template <typename T>
    HdDataSourceBaseHandle
    operator()(const T&)
    {
        return _TypedValueDataSource<T>::New(
            name, source, primPath, primSource, inputArgs);
    }

    HdDataSourceBaseHandle
    operator()(const VtValue&)
    {
        return _UntypedValueDataSource::New(
            name, source, primPath, primSource, inputArgs);
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
        const HdContainerDataSourceHandle& inputArgs)
      : _name(name)
      , _source(source)
      , _primPath(primPath)
      , _primSource(primSource)
      , _inputArgs(inputArgs)
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
                        _name, source, _primPath, _primSource, _inputArgs });
            }
        }
        return ds;
    }

#if PXR_VERSION < 2301
    bool Has(const TfToken &name) override
    {
        const TfTokenVector names = GetNames();
        return std::find(names.begin(), names.end(), name) != names.end();
    }
#endif

private:
    TfToken _name;
    HdContainerDataSourceHandle _source;
    SdfPath _primPath;
    HdContainerDataSourceHandle _primSource;
    HdContainerDataSourceHandle _inputArgs;
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
        const HdContainerDataSourceHandle& inputArgs)
      : _source(source)
      , _primPath(primPath)
      , _primSource(primSource)
      , _inputArgs(inputArgs)
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
                _primPath, _primSource, _inputArgs);
        }
        return ds;
    }

#if PXR_VERSION < 2301
    bool Has(const TfToken &name) override
    {
        const TfTokenVector names = GetNames();
        return std::find(names.begin(), names.end(), name) != names.end();
    }
#endif

private:
    HdContainerDataSourceHandle _source;
    SdfPath _primPath;
    HdContainerDataSourceHandle _primSource;
    HdContainerDataSourceHandle _inputArgs;
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
        const HdContainerDataSourceHandle& inputArgs)
      : _primPath(primPath)
      , _primSource(primSource)
      , _inputArgs(inputArgs)
    { }

    TfTokenVector
    GetNames() override
    {
        if (!_primSource) {
            return { };
        }
        return _primSource->GetNames();
    }

    HdDataSourceBaseHandle
    Get(const TfToken& name) override
    {
        if (!_primSource) {
            return nullptr;
        }
        HdDataSourceBaseHandle ds = _primSource->Get(name);
        if (ds &&
#if PXR_VERSION < 2306
            name == HdPrimvarsSchemaTokens->primvars
#else
            name == HdPrimvarsSchema::GetSchemaToken()
#endif
        ) {
            return _PrimvarsDataSource::New(
                HdContainerDataSource::Cast(ds),
                _primPath, _primSource, _inputArgs);
        }
        return ds;
    }

#if PXR_VERSION < 2301
    bool Has(const TfToken &name) override
    {
        const TfTokenVector names = GetNames();
        return std::find(names.begin(), names.end(), name) != names.end();
    }
#endif

private:
    SdfPath _primPath;
    HdContainerDataSourceHandle _primSource;
    HdContainerDataSourceHandle _inputArgs;
};

HD_DECLARE_DATASOURCE_HANDLES(_PrimDataSource);

} // anonymous namespace

HdsiVelocityMotionResolvingSceneIndexRefPtr
HdsiVelocityMotionResolvingSceneIndex::New(
    const HdSceneIndexBaseRefPtr& inputSceneIndex,
    const HdContainerDataSourceHandle& inputArgs)
{
    return TfCreateRefPtr(
        new HdsiVelocityMotionResolvingSceneIndex(inputSceneIndex, inputArgs));
}

HdsiVelocityMotionResolvingSceneIndex::HdsiVelocityMotionResolvingSceneIndex(
    const HdSceneIndexBaseRefPtr& inputSceneIndex,
    const HdContainerDataSourceHandle& inputArgs)
 : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
 , _inputArgs(inputArgs)
{ }

bool
HdsiVelocityMotionResolvingSceneIndex::PrimTypeSupportsVelocityMotion(
    const TfToken& primType)
{
    static const TfToken::Set types {
        HdPrimTypeTokens->points,
        HdPrimTypeTokens->basisCurves,
#if PXR_VERSION >= 2306
        HdPrimTypeTokens->nurbsCurves,
        HdPrimTypeTokens->nurbsPatch,
#endif
#if PXR_VERSION >= 2404
        HdPrimTypeTokens->tetMesh,
#endif
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
            primPath, prim.dataSource, _inputArgs);
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
        HdPrimvarsSchema::GetPointsLocator()
#if HD_API_VERSION < 67
        , HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdInstancerTokens->translate)
        , HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdInstancerTokens->scale)
#endif
#if HD_API_VERSION >= 56
        , HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdInstancerTokens->instanceTranslations),
        , HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdInstancerTokens->instanceScales)
#endif
        };
    static const HdDataSourceLocatorSet rotationsLocators {
#if HD_API_VERSION < 67
        HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdInstancerTokens->rotate)
        , HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdInstancerTokens->scale)
#endif
#if HD_API_VERSION >= 56
        , HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdInstancerTokens->instanceRotations),
        , HdPrimvarsSchema::GetDefaultLocator()
            .Append(HdInstancerTokens->instanceScales)
#endif
        };
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
            .Append(
#if PXR_VERSION < 2406
                _tokens->angularVelocities
#else
                HdTokens->angularVelocities
#endif
            ),
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

#endif // HDSI_API_VERSION

// -----------------------------------------------------------------------------

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry
        ::Define<HdPrman_VelocityMotionResolvingSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    const HdContainerDataSourceHandle inputArgs =
        HdRetainedContainerDataSource::New(
            // TODO: Get the real framerate!
            _tokens->fps,
            HdRetainedTypedSampledDataSource<float>::New(_fallbackFps));

    // This plugin must come after extcomp but before motion blur
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 2;

    for (const auto& rendererDisplayName : HdPrman_GetPluginDisplayNames()) {
        HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
            rendererDisplayName,
            HdPrmanPluginTokens->velocityMotion,
            inputArgs,
            insertionPhase,
            HdSceneIndexPluginRegistry::InsertionOrderAtEnd);
    }
}

HdPrman_VelocityMotionResolvingSceneIndexPlugin::
HdPrman_VelocityMotionResolvingSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
HdPrman_VelocityMotionResolvingSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr& inputScene,
    const HdContainerDataSourceHandle& inputArgs)
{
    return HdsiVelocityMotionResolvingSceneIndex::New(inputScene, inputArgs);
}

void
HdPrman_VelocityMotionResolvingSceneIndexPlugin::SetFPS(float fps)
{
    _fallbackFps = fps;
}

// -----------------------------------------------------------------------------

namespace {

class _VelocityMotionModeDataSource final
  : public HdTokenDataSource
{
public:
    HD_DECLARE_DATASOURCE(_VelocityMotionModeDataSource);

    _VelocityMotionModeDataSource(
        const HdContainerDataSourceHandle& primSource)
      : _primSource(primSource)
    { }

    bool
    GetContributingSampleTimesForInterval(
        Time /* startTime */,
        Time /* endTime */,
        std::vector<Time>* outSampleTimes) override
    {
        *outSampleTimes = { };
        return false;
    }

    TfToken
    GetTypedValue(Time /* shutterOffset */) override
    {
        static const HdDataSourceLocator vblurLocator =
            HdPrimvarsSchema::GetDefaultLocator()
                .Append(_tokens->vblur)
                .Append(HdPrimvarSchemaTokens->primvarValue);
        if (const auto& vblurDs = HdSampledDataSource::Cast(
            HdContainerDataSource::Get(_primSource, vblurLocator))) {
            const TfToken& vblur = vblurDs->GetValue(0.f)
                .GetWithDefault<TfToken>(_tokens->vblur_enable);
            if (vblur == _tokens->vblur_ignore) {
                return HdsiVelocityMotionResolvingSceneIndexTokens->ignore;
            }
            if (vblur == _tokens->vblur_noAcceleration) {
                return HdsiVelocityMotionResolvingSceneIndexTokens->noAcceleration;
            }
        } else {
        }
        return HdsiVelocityMotionResolvingSceneIndexTokens->enable;
    }

    VtValue
    GetValue(Time shutterOffset) override
    {
        return VtValue(GetTypedValue(shutterOffset));
    }

private:
    HdContainerDataSourceHandle _primSource;
};

HD_DECLARE_DATASOURCE_HANDLES(_VelocityMotionModeDataSource);

TF_DECLARE_REF_PTRS(_VblurInterpretingSceneIndex);

class _VblurInterpretingSceneIndex
  : public HdSingleInputFilteringSceneIndexBase
{
public:
    static _VblurInterpretingSceneIndexRefPtr
    New(const HdSceneIndexBaseRefPtr& inputSceneIndex)
    {
        return TfCreateRefPtr(
            new _VblurInterpretingSceneIndex(inputSceneIndex));
    }

    HdSceneIndexPrim
    GetPrim(const SdfPath& primPath) const override
    {
        HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
        if (HdsiVelocityMotionResolvingSceneIndex::
            PrimTypeSupportsVelocityMotion(prim.primType)) {
#if PXR_VERSION < 2302
            std::vector<HdContainerDataSourceHandle> sources {
                HdRetainedContainerDataSource::New(
                    HdsiVelocityMotionResolvingSceneIndexTokens->velocityMotionMode,
                    _VelocityMotionModeDataSource::New(prim.dataSource)),
                prim.dataSource };
            prim.dataSource = HdOverlayContainerDataSource::New(
                sources.size(), sources.data());
#else
            prim.dataSource = HdOverlayContainerDataSource::New(
                HdRetainedContainerDataSource::New(
                    HdsiVelocityMotionResolvingSceneIndexTokens->velocityMotionMode,
                    _VelocityMotionModeDataSource::New(prim.dataSource)),
                prim.dataSource);
#endif
        }
        return prim;
    }

    SdfPathVector
    GetChildPrimPaths(const SdfPath& primPath) const override
    {
        return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
    }

protected:
    _VblurInterpretingSceneIndex(
        const HdSceneIndexBaseRefPtr& inputSceneIndex)
      : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    { }

    void
    _PrimsAdded(
        const HdSceneIndexBase& /* sender */,
        const HdSceneIndexObserver::AddedPrimEntries& entries) override
    {
        _SendPrimsAdded(entries);
    }

    void
    _PrimsRemoved(
        const HdSceneIndexBase& /* sender */,
        const HdSceneIndexObserver::RemovedPrimEntries& entries) override
    {
        _SendPrimsRemoved(entries);
    }

    void
    _PrimsDirtied(
        const HdSceneIndexBase& /* sender */,
        const HdSceneIndexObserver::DirtiedPrimEntries& entries) override
    {
        static const HdDataSourceLocator vblurLocator {
            HdPrimvarsSchema::GetDefaultLocator()
                .Append(_tokens->vblur) };
        static const HdDataSourceLocator modeLocator {
            HdsiVelocityMotionResolvingSceneIndexTokens->velocityMotionMode
        };

        HdSceneIndexObserver::DirtiedPrimEntries newEntries;
        for (const auto& entry : entries) {
            if (entry.dirtyLocators.Intersects(vblurLocator)) {
                newEntries.emplace_back(entry.primPath, modeLocator);
            }
        }
        if (newEntries.empty()) {
            return _SendPrimsDirtied(entries);
        }
        newEntries.insert(newEntries.begin(), entries.cbegin(), entries.cend());
        _SendPrimsDirtied(newEntries);
    }
};

} // anonymous namespace

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry
        ::Define<HdPrman_VblurInterpretingSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    // Must come before velocity motion resolving plug-in
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 2;

    for (const auto& rendererDisplayName : HdPrman_GetPluginDisplayNames()) {
        HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
            rendererDisplayName,
            HdPrmanPluginTokens->vblurInterpreting,
            nullptr, // inputArgs
            insertionPhase,
            HdSceneIndexPluginRegistry::InsertionOrderAtStart);
    }
}

HdPrman_VblurInterpretingSceneIndexPlugin::
HdPrman_VblurInterpretingSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
HdPrman_VblurInterpretingSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr& inputScene,
    const HdContainerDataSourceHandle& /* inputArgs */)
{
    return _VblurInterpretingSceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE
