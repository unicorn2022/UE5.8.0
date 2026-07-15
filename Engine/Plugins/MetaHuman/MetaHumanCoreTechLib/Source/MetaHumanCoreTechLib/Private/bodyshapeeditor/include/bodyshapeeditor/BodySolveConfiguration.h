// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <bodyshapeeditor/BodyShapeEditorTarget.h>
#include <nls/utils/ConfigurationParameter.h>
#include <nls/math/Math.h>
#include <carbon/io/JsonIO.h>

#include <string>
#include <map>
#include <vector>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

struct BodySolveConfiguration {
    Configuration body;
    Configuration face;
    Configuration refinement;
    Eigen::VectorXf vertexMask;   // single mask shared across all sub-solvers

    // Runtime flag — when true, SolveForArbitraryMeshWithICP pins the uniform
    // scaleVar as constant (MakeConstant) so only the pose + proportions solve
    // and ScaleFactor stays wherever AlignToTargetMesh left it. Not serialised
    // via Configuration because it's a per-invocation toggle, not a knob.
    bool lockScale = false;

    BodySolveConfiguration();
    BodySolveConfiguration(Configuration body, Configuration face, Configuration refinement);
};

// A single step in a RunPipeline execution. Masks are resolved + combined
// into `config.vertexMask` by the app before handoff. `lockedControls` is
// the runtime-resolved set of locked indices computed from `controlGroupNames`
// via the app's ControlGroupLibrary; BSE::RunPipeline reads `lockedControls`,
// not `controlGroupNames`.
//! Which side of the neck seam stays locked during AdaptNeckSeam.
//! - None: seam is free, both sides relax between outer pins (default).
//! - Face: face outside the seam stays at the face-solve offset; body ring adapts.
//! - Body: body outside the seam stays at 0; face ring adapts. Useful e.g. when
//!         template-fitting to a body-only target and the body must stay as-is.
enum class SeamLockSide : int { None = 0, Face = 1, Body = 2 };

//! One kind per SolveStep. RunPipeline dispatches by this. Fields on SolveStep
//! that only make sense for a particular kind (alignRegion / seam* / …) are
//! ignored when `kind` is something else.
enum class StepKind : int {
    BodySolve  = 0,
    FaceSolve  = 1,
    Align      = 2,
    AdaptNeck  = 3,
    Refine     = 4,
};

struct SolveStep {
    std::string name;
    bool enabled = true;
    StepKind kind = StepKind::BodySolve;

    // Common — used by most kinds.
    std::vector<std::string> maskNames;                           // UI-facing mask list
    BodyShapeEditorTarget::MeshSlot targetSlot =                  // which scan slot to solve against
        BodyShapeEditorTarget::MeshSlot::Combined;
    std::vector<std::string> controlGroupNames;                   // UI-facing, JSON-persisted
    std::vector<int> lockedControls;                              // runtime-resolved, not persisted
    std::map<std::string, float> initialControls;

    // Per-step solver config. A single Configuration object, sized/named for
    // the step's kind (CreateBodySolveConfig / CreateFaceSolveConfig /
    // CreateRefinementConfig). Empty for Align / AdaptNeck kinds — those carry
    // their params as fields below. Default-initialised to an empty
    // Configuration so SolveStep is default-constructible.
    Configuration config{"", {}};

    // Per-step vertex mask (resolved from `maskNames` by the app before dispatch).
    Eigen::VectorXf vertexMask;

    // Kind::BodySolve — if true, the uniform scaleVar is locked during solve
    // (pose + proportions still optimise). Lets the Align-produced scale stick
    // through later passes like finger_foot that should only refine pose.
    bool lockScale = false;

    // Kind::Align — no per-step params (region auto-detected from target, rotation
    // always identity, scale from landmarks).

    // Kind::AdaptNeck.
    SeamLockSide seamLockSide = SeamLockSide::None;
    int   seamRings        = 10;
    int   seamIterations   = 3;
    float seamLaplacian    = 1.5f;  // SOR relaxation factor
};

Configuration CreateBodySolveConfig();
Configuration CreateFaceSolveConfig();
Configuration CreateRefinementConfig();

JsonElement SolveConfigurationToJson(const BodySolveConfiguration& cfg);
bool SolveConfigurationFromJson(BodySolveConfiguration& cfg, const JsonElement& json);

JsonElement ControlGroupsToJson(const std::map<std::string, std::vector<int>>& groups);
bool ControlGroupsFromJson(std::map<std::string, std::vector<int>>& groups, const JsonElement& json);

// Pipeline JSON is versioned. New writes emit version:2 with targetSlot as int.
// Legacy (version unset / <2) input is migrated:
//   - targetMeshName string → targetSlot enum
//   - runScale=true → runAlign=true, alignLockScale=false
//   - configPreset (name ref) → dropped (no preset table in v2)
//   - legacy maskName / masks arrays merged into maskNames
JsonElement PipelineToJson(const std::vector<SolveStep>& steps);
bool PipelineFromJson(std::vector<SolveStep>& steps, const JsonElement& json);

JsonElement PipelinePresetsToJson(const std::map<std::string, std::vector<SolveStep>>& pipelines);
bool PipelinePresetsFromJson(std::map<std::string, std::vector<SolveStep>>& pipelines, const JsonElement& json);

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
