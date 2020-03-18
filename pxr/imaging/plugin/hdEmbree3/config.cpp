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
#include "pxr/imaging/plugin/hdEmbree3/config.h"

#include "pxr/base/tf/envSetting.h"
#include "pxr/base/tf/instantiateSingleton.h"

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

// Instantiate the config singleton.
TF_INSTANTIATE_SINGLETON(HdEmbree3Config);

// Each configuration variable has an associated environment variable.
// The environment variable macro takes the variable name, a default value,
// and a description...
TF_DEFINE_ENV_SETTING(HDEMBREE3_SAMPLES_TO_CONVERGENCE, 100,
        "Samples per pixel before we stop rendering (must be >= 1)");

TF_DEFINE_ENV_SETTING(HDEMBREE3_TILE_SIZE, 8,
        "Size (per axis) of threading work units (must be >= 1)");

TF_DEFINE_ENV_SETTING(HDEMBREE3_AMBIENT_OCCLUSION_SAMPLES, 16,
        "Ambient occlusion samples per camera ray (must be >= 0; a value of 0 disables ambient occlusion)");

TF_DEFINE_ENV_SETTING(HDEMBREE3_SUBDIVISION_CACHE, 128*1024*1024,
        "Number of bytes to allocate for the embree subdivision surface cache (must be >= 128MB)");

TF_DEFINE_ENV_SETTING(HDEMBREE3_JITTER_CAMERA, 1,
        "Should HdEmbree3 jitter camera rays while rendering? (values >0 are true)");

TF_DEFINE_ENV_SETTING(HDEMBREE3_USE_FACE_COLORS, 1,
        "Should HdEmbree3 use face colors while rendering? (values > 0 are true)");

TF_DEFINE_ENV_SETTING(HDEMBREE3_CAMERA_LIGHT_INTENSITY, 300,
        "Intensity of the camera light, specified as a percentage of <1,1,1>.");

TF_DEFINE_ENV_SETTING(HDEMBREE3_PRINT_CONFIGURATION, 0,
        "Should HdEmbree3 print configuration on startup? (values > 0 are true)");

HdEmbree3Config::HdEmbree3Config()
{
    // Read in values from the environment, clamping them to valid ranges.
    samplesToConvergence = std::max(1,
            TfGetEnvSetting(HDEMBREE3_SAMPLES_TO_CONVERGENCE));
    tileSize = std::max(1,
            TfGetEnvSetting(HDEMBREE3_TILE_SIZE));
    ambientOcclusionSamples = std::max(0,
            TfGetEnvSetting(HDEMBREE3_AMBIENT_OCCLUSION_SAMPLES));
    subdivisionCache = std::max(128*1024*1024,
            TfGetEnvSetting(HDEMBREE3_SUBDIVISION_CACHE));
    jitterCamera = (TfGetEnvSetting(HDEMBREE3_JITTER_CAMERA) > 0);
    useFaceColors = (TfGetEnvSetting(HDEMBREE3_USE_FACE_COLORS) > 0);
    cameraLightIntensity = (std::max(100,
            TfGetEnvSetting(HDEMBREE3_CAMERA_LIGHT_INTENSITY)) / 100.0f);

    if (TfGetEnvSetting(HDEMBREE3_PRINT_CONFIGURATION) > 0) {
        std::cout
            << "HdEmbree3 Configuration: \n"
            << "  samplesToConvergence       = "
            <<    samplesToConvergence    << "\n"
            << "  tileSize                   = "
            <<    tileSize                << "\n"
            << "  ambientOcclusionSamples    = "
            <<    ambientOcclusionSamples << "\n"
            << "  subdivisionCache           = "
            <<    subdivisionCache        << "\n"
            << "  jitterCamera               = "
            <<    jitterCamera            << "\n"
            << "  useFaceColors              = "
            <<    useFaceColors           << "\n"
            << "  cameraLightIntensity      = "
            <<    cameraLightIntensity    << "\n"
            ;
    }
}

/*static*/
const HdEmbree3Config&
HdEmbree3Config::GetInstance()
{
    return TfSingleton<HdEmbree3Config>::GetInstance();
}

PXR_NAMESPACE_CLOSE_SCOPE
