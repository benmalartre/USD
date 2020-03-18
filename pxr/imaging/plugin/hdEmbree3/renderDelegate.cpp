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
#include "pxr/imaging/glf/glew.h"
#include "pxr/imaging/plugin/hdEmbree3/renderDelegate.h"

#include "pxr/imaging/plugin/hdEmbree3/config.h"
#include "pxr/imaging/plugin/hdEmbree3/instancer.h"
#include "pxr/imaging/plugin/hdEmbree3/renderParam.h"
#include "pxr/imaging/plugin/hdEmbree3/renderPass.h"

#include "pxr/imaging/hd/extComputation.h"
#include "pxr/imaging/hd/resourceRegistry.h"
#include "pxr/imaging/hd/tokens.h"

#include "pxr/imaging/plugin/hdEmbree3/mesh.h"
//XXX: Add other Rprim types later
#include "pxr/imaging/hd/camera.h"
//XXX: Add other Sprim types later
#include "pxr/imaging/hd/bprim.h"
//XXX: Add bprim types

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdEmbree3RenderSettingsTokens, HDEMBREE3_RENDER_SETTINGS_TOKENS);

const TfTokenVector HdEmbree3RenderDelegate::SUPPORTED_RPRIM_TYPES =
{
    HdPrimTypeTokens->mesh,
};

const TfTokenVector HdEmbree3RenderDelegate::SUPPORTED_SPRIM_TYPES =
{
    HdPrimTypeTokens->camera,
    HdPrimTypeTokens->extComputation,
};

const TfTokenVector HdEmbree3RenderDelegate::SUPPORTED_BPRIM_TYPES =
{
    HdPrimTypeTokens->renderBuffer,
};

std::mutex HdEmbree3RenderDelegate::_mutexResourceRegistry;
std::atomic_int HdEmbree3RenderDelegate::_counterResourceRegistry;
HdResourceRegistrySharedPtr HdEmbree3RenderDelegate::_resourceRegistry;

/* static */
void
HdEmbree3RenderDelegate::HandleRtcError(const RTCError code, const char* msg)
{
    // Forward RTC error messages through to hydra logging.
    switch (code) {
        case RTC_UNKNOWN_ERROR:
            TF_CODING_ERROR("Embree3 unknown error: %s", msg);
            break;
        case RTC_INVALID_ARGUMENT:
            TF_CODING_ERROR("Embree3 invalid argument: %s", msg);
            break;
        case RTC_INVALID_OPERATION:
            TF_CODING_ERROR("Embree3 invalid operation: %s", msg);
            break;
        case RTC_OUT_OF_MEMORY:
            TF_CODING_ERROR("Embree3 out of memory: %s", msg);
            break;
        case RTC_UNSUPPORTED_CPU:
            TF_CODING_ERROR("Embree3 unsupported CPU: %s", msg);
            break;
        case RTC_CANCELLED:
            TF_CODING_ERROR("Embree3 cancelled: %s", msg);
            break;
        default:
            TF_CODING_ERROR("Embree3 invalid error code: %s", msg);
            break;
    }
}

static void _RenderCallback(HdEmbree3Renderer *renderer,
                            HdRenderThread *renderThread)
{
    renderer->Clear();
    renderer->Render(renderThread);
}

HdEmbree3RenderDelegate::HdEmbree3RenderDelegate()
    : HdRenderDelegate()
{
    _Initialize();
}

HdEmbree3RenderDelegate::HdEmbree3RenderDelegate(
    HdRenderSettingsMap const& settingsMap)
    : HdRenderDelegate(settingsMap)
{
    _Initialize();
}

void
HdEmbree3RenderDelegate::_Initialize()
{
    // Initialize the settings and settings descriptors.
    _settingDescriptors.resize(4);
    _settingDescriptors[0] = { "Enable Scene Colors",
        HdEmbree3RenderSettingsTokens->enableSceneColors,
        VtValue(HdEmbree3Config::GetInstance().useFaceColors) };
    _settingDescriptors[1] = { "Enable Ambient Occlusion",
        HdEmbree3RenderSettingsTokens->enableAmbientOcclusion,
        VtValue(HdEmbree3Config::GetInstance().ambientOcclusionSamples > 0) };
    _settingDescriptors[2] = { "Ambient Occlusion Samples",
        HdEmbree3RenderSettingsTokens->ambientOcclusionSamples,
        VtValue(int(HdEmbree3Config::GetInstance().ambientOcclusionSamples)) };
    _settingDescriptors[3] = { "Samples To Convergence",
        HdRenderSettingsTokens->convergedSamplesPerPixel,
        VtValue(int(HdEmbree3Config::GetInstance().samplesToConvergence)) };
    _PopulateDefaultSettings(_settingDescriptors);

    // Initialize the embree library handle (_rtcDevice).
    _rtcDevice = rtcNewDevice(nullptr);

    // Register our error message callback.
    rtcDeviceSetErrorFunction(_rtcDevice, HandleRtcError);

    // Embree3 has an internal cache for subdivision surface computations.
    // HdEmbree3 exposes the size as an environment variable.
    unsigned int subdivisionCache =
        HdEmbree3Config::GetInstance().subdivisionCache;
    rtcDeviceSetParameter1i(_rtcDevice, RTC_SOFTWARE_CACHE_SIZE,
        subdivisionCache);

    // Create the top-level scene.
    //
    // RTC_SCENE_DYNAMIC indicates we'll be updating the scene between draw
    // calls. RTC_INTERSECT1 indicates we'll be casting single rays, and
    // RTC_INTERPOLATE indicates we'll be storing primvars in embree objects
    // and querying them with rtcInterpolate.
    //
    // XXX: Investigate ray packets.
    _rtcScene = rtcDeviceNewScene(_rtcDevice, RTC_SCENE_DYNAMIC,
        RTC_INTERSECT1 | RTC_INTERPOLATE);

    // Store top-level embree objects inside a render param that can be
    // passed to prims during Sync(). Also pass a handle to the render thread.
    _renderParam = std::make_shared<HdEmbree3RenderParam>(
        _rtcDevice, _rtcScene, &_renderThread, &_sceneVersion);

    // Pass the scene handle to the renderer.
    _renderer.SetScene(_rtcScene);

    // Set the background render thread's rendering entrypoint to
    // HdEmbree3Renderer::Render.
    _renderThread.SetRenderCallback(
        std::bind(_RenderCallback, &_renderer, &_renderThread));
    // Start the background render thread.
    _renderThread.StartThread();

    // Initialize one resource registry for all embree plugins
    std::lock_guard<std::mutex> guard(_mutexResourceRegistry);

    if (_counterResourceRegistry.fetch_add(1) == 0) {
        _resourceRegistry.reset( new HdResourceRegistry() );
    }
}

HdEmbree3RenderDelegate::~HdEmbree3RenderDelegate()
{
    // Clean the resource registry only when it is the last Embree3 delegate
    {
        std::lock_guard<std::mutex> guard(_mutexResourceRegistry);
        if (_counterResourceRegistry.fetch_sub(1) == 1) {
            _resourceRegistry.reset();
        }
    }

    _renderThread.StopThread();

    // Destroy embree library and scene state.
    _renderParam.reset();
    rtcDeleteScene(_rtcScene);
    rtcDeleteDevice(_rtcDevice);
}

HdRenderSettingDescriptorList
HdEmbree3RenderDelegate::GetRenderSettingDescriptors() const
{
    return _settingDescriptors;
}

HdRenderParam*
HdEmbree3RenderDelegate::GetRenderParam() const
{
    return _renderParam.get();
}

void
HdEmbree3RenderDelegate::CommitResources(HdChangeTracker *tracker)
{
}

TfTokenVector const&
HdEmbree3RenderDelegate::GetSupportedRprimTypes() const
{
    return SUPPORTED_RPRIM_TYPES;
}

TfTokenVector const&
HdEmbree3RenderDelegate::GetSupportedSprimTypes() const
{
    return SUPPORTED_SPRIM_TYPES;
}

TfTokenVector const&
HdEmbree3RenderDelegate::GetSupportedBprimTypes() const
{
    return SUPPORTED_BPRIM_TYPES;
}

HdResourceRegistrySharedPtr
HdEmbree3RenderDelegate::GetResourceRegistry() const
{
    return _resourceRegistry;
}

HdAovDescriptor
HdEmbree3RenderDelegate::GetDefaultAovDescriptor(TfToken const& name) const
{
    if (name == HdAovTokens->color) {
        return HdAovDescriptor(HdFormatUNorm8Vec4, true,
                               VtValue(GfVec4f(0.0f)));
    } else if (name == HdAovTokens->normal || name == HdAovTokens->Neye) {
        return HdAovDescriptor(HdFormatFloat32Vec3, false,
                               VtValue(GfVec3f(-1.0f)));
    } else if (name == HdAovTokens->depth) {
        return HdAovDescriptor(HdFormatFloat32, false, VtValue(1.0f));
    } else if (name == HdAovTokens->cameraDepth) {
        return HdAovDescriptor(HdFormatFloat32, false, VtValue(0.0f));
    } else if (name == HdAovTokens->primId ||
               name == HdAovTokens->instanceId ||
               name == HdAovTokens->elementId) {
        return HdAovDescriptor(HdFormatInt32, false, VtValue(-1));
    } else {
        HdParsedAovToken aovId(name);
        if (aovId.isPrimvar) {
            return HdAovDescriptor(HdFormatFloat32Vec3, false,
                                   VtValue(GfVec3f(0.0f)));
        }
    }

    return HdAovDescriptor();
}

VtDictionary 
HdEmbree3RenderDelegate::GetRenderStats() const
{
    VtDictionary stats;
    stats[HdPerfTokens->numCompletedSamples.GetString()] = 
        _renderer.GetCompletedSamples();
    return stats;
}

bool
HdEmbree3RenderDelegate::IsPauseSupported() const
{
    return true;
}

bool
HdEmbree3RenderDelegate::Pause()
{
    _renderThread.PauseRender();
    return true;
}

bool
HdEmbree3RenderDelegate::Resume()
{
    _renderThread.ResumeRender();
    return true;
}

HdRenderPassSharedPtr
HdEmbree3RenderDelegate::CreateRenderPass(HdRenderIndex *index,
                            HdRprimCollection const& collection)
{
    return HdRenderPassSharedPtr(new HdEmbree3RenderPass(
        index, collection, &_renderThread, &_renderer, &_sceneVersion));
}

HdInstancer *
HdEmbree3RenderDelegate::CreateInstancer(HdSceneDelegate *delegate,
                                        SdfPath const& id,
                                        SdfPath const& instancerId)
{
    return new HdEmbree3Instancer(delegate, id, instancerId);
}

void
HdEmbree3RenderDelegate::DestroyInstancer(HdInstancer *instancer)
{
    delete instancer;
}

HdRprim *
HdEmbree3RenderDelegate::CreateRprim(TfToken const& typeId,
                                    SdfPath const& rprimId,
                                    SdfPath const& instancerId)
{
    if (typeId == HdPrimTypeTokens->mesh) {
        return new HdEmbree3Mesh(rprimId, instancerId);
    } else {
        TF_CODING_ERROR("Unknown Rprim Type %s", typeId.GetText());
    }

    return nullptr;
}

void
HdEmbree3RenderDelegate::DestroyRprim(HdRprim *rPrim)
{
    delete rPrim;
}

HdSprim *
HdEmbree3RenderDelegate::CreateSprim(TfToken const& typeId,
                                    SdfPath const& sprimId)
{
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdCamera(sprimId);
    } else if (typeId == HdPrimTypeTokens->extComputation) {
        return new HdExtComputation(sprimId);
    } else {
        TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    }

    return nullptr;
}

HdSprim *
HdEmbree3RenderDelegate::CreateFallbackSprim(TfToken const& typeId)
{
    // For fallback sprims, create objects with an empty scene path.
    // They'll use default values and won't be updated by a scene delegate.
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdCamera(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->extComputation) {
        return new HdExtComputation(SdfPath::EmptyPath());
    } else {
        TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    }

    return nullptr;
}

void
HdEmbree3RenderDelegate::DestroySprim(HdSprim *sPrim)
{
    delete sPrim;
}

HdBprim *
HdEmbree3RenderDelegate::CreateBprim(TfToken const& typeId,
                                    SdfPath const& bprimId)
{
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        return new HdEmbree3RenderBuffer(bprimId);
    } else {
        TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());
    }
    return nullptr;
}

HdBprim *
HdEmbree3RenderDelegate::CreateFallbackBprim(TfToken const& typeId)
{
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        return new HdEmbree3RenderBuffer(SdfPath::EmptyPath());
    } else {
        TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());
    }
    return nullptr;
}

void
HdEmbree3RenderDelegate::DestroyBprim(HdBprim *bPrim)
{
    delete bPrim;
}

PXR_NAMESPACE_CLOSE_SCOPE
