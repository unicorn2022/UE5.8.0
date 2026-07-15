// Copyright Epic Games, Inc. All Rights Reserved.
//
// Unified on-disk representation of a solve-pipeline preset file + its masks,
// shared between the scan-fitting app (which authors presets) and the MHC
// body API (which loads them inside the engine).
//
// Layout:
//   <path>.json         - pipelines, active name, controlGroups, maskGroups, masksStorage hint
//   <path>.masks.bin    - binary mask weights (MLB1 format). Optional: JSON-only files fall
//                         back to embedded "masks" object.
//
// Masks from BSE part weights can be ingested into the bundle's mask map via
// the `bse` argument to LoadPipelineBundle so step.maskNames that reference
// BSE part names (e.g. "body", "head") resolve during the pipeline run.

#pragma once

#include <bodyshapeeditor/BodySolveConfiguration.h>
#include <carbon/Common.h>
#include <nrr/VertexWeights.h>

#include <map>
#include <string>
#include <vector>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

class BodyShapeEditor;

struct PipelineBundle
{
    std::map<std::string, std::vector<SolveStep>> pipelines;
    std::string activePipeline;
    std::map<std::string, std::vector<int>> controlGroups;
    std::map<std::string, VertexWeights<float>> masks;
    std::map<std::string, std::vector<std::string>> maskGroups;
};

/// Write <jsonPath> (JSON config) + <jsonPath>.masks.bin (binary weights sidecar).
/// Returns true on success. If the mask map is empty the binary sidecar is not written.
bool SavePipelineBundle(const std::string& jsonPath, const PipelineBundle& bundle);

/// Read <jsonPath> (JSON config) and its companion <jsonPath>.masks.bin if present,
/// falling back to any JSON-embedded `masks` / `externalMasks` object when the
/// binary sidecar is missing.
///
/// If `bse` is non-null its part weights are ingested into bundle.masks (padded
/// to the body vertex count). This mirrors the behaviour of the scan-fitting
/// app's MaskLibrary so callers don't have to know which masks are user-authored
/// and which come from BSE parts — everything resolves through bundle.masks by
/// name.
bool LoadPipelineBundle(const std::string& jsonPath, BodyShapeEditor* bse, PipelineBundle& bundle);

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
