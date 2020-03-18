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
#ifndef PXR_IMAGING_PLUGIN_HD_EMBREE3_CONFIG_H
#define PXR_IMAGING_PLUGIN_HD_EMBREE3_CONFIG_H

#include "pxr/pxr.h"
#include "pxr/base/tf/singleton.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdEmbree3Config
///
/// This class is a singleton, holding configuration parameters for HdEmbree3.
/// Everything is provided with a default, but can be overridden using
/// environment variables before launching a hydra process.
///
/// Many of the parameters can be used to control quality/performance
/// tradeoffs, or to alter how HdEmbree3 takes advantage of parallelism.
///
/// At startup, this class will print config parameters if
/// *HDEMBREE3_PRINT_CONFIGURATION* is true. Integer values greater than zero
/// are considered "true".
///
class HdEmbree3Config {
public:
    /// \brief Return the configuration singleton.
    static const HdEmbree3Config &GetInstance();

    /// How many samples do we need before a pixel is considered
    /// converged?
    ///
    /// Override with *HDEMBREE3_SAMPLES_TO_CONVERGENCE*.
    unsigned int samplesToConvergence;

    /// How many pixels are in an atomic unit of parallel work?
    /// A work item is a square of size [tileSize x tileSize] pixels.
    ///
    /// Override with *HDEMBREE3_TILE_SIZE*.
    unsigned int tileSize;

    /// How many ambient occlusion rays should we generate per
    /// camera ray?
    ///
    /// Override with *HDEMBREE3_AMBIENT_OCCLUSION_SAMPLES*.
    unsigned int ambientOcclusionSamples;

    /// How many bytes should we allocate for the embree subdivision
    /// surface cache?
    ///
    /// Override with *HDEMBREE3_SUBDIVISION_CACHE*.
    unsigned int subdivisionCache;

    /// Should the renderpass jitter camera rays for antialiasing?
    ///
    /// Override with *HDEMBREE3_JITTER_CAMERA*. Integer values greater than
    /// zero are considered "true".
    bool jitterCamera;

    /// Should the renderpass use the color primvar, or flat white colors?
    /// (Flat white shows off ambient occlusion better).
    ///
    /// Override with *HDEMBREE3_USE_FACE_COLORS*. Integer values greater than
    /// zero are considered "true".
    bool useFaceColors;

    /// What should the intensity of the camera light be, specified as a
    /// percent of <1, 1, 1>.  For example, 300 would be <3, 3, 3>.
    ///
    /// Override with *HDEMBREE3_CAMERA_LIGHT_INTENSITY*.
    float cameraLightIntensity;

private:
    // The constructor initializes the config variables with their
    // default or environment-provided override, and optionally prints
    // them.
    HdEmbree3Config();
    ~HdEmbree3Config() = default;

    HdEmbree3Config(const HdEmbree3Config&) = delete;
    HdEmbree3Config& operator=(const HdEmbree3Config&) = delete;

    friend class TfSingleton<HdEmbree3Config>;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_PLUGIN_HD_EMBREE3_CONFIG_H
