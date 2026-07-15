# api/act_api

The Actor Creation and Refinement API. Provides high-level C++ interfaces for creating and refining MetaHuman identities from depth maps or 3D scans, and for evaluating rig DNA. This is the API layer consumed by Unreal Engine's MetaHuman Creator pipeline when working with captured scan data.

All classes live in the `titan::api` namespace (alias `TITAN_API_NAMESPACE`).

## Classes

### `ActorCreationAPI` — `ActorCreationAPI.h`
Fits the MetaHuman template mesh to a new identity from either a depth map sequence or a 3D scan. The fitting pipeline runs in stages:

1. **Init** — load `template_description.json` and `identity_model.json` (as files or flattened JSON strings).
2. **SetCameras** — configure the camera rig.
3. **SetDepthInputData** or **SetScanInputData** — supply the input geometry:
   - Depth maps: per-frame depth images with 2D landmarks per camera.
   - Scan: a triangulated mesh with 3D and 2D landmarks.
4. **FitRigid** → **FitNonRigid** → **FitPerVertex** — three-stage fitting:
   - Rigid: aligns template pose to the input.
   - Non-rigid (PCA): fits coarse identity shape using the PCA identity model.
   - Per-vertex: fine-grained vertex-level fitting.
5. **FitTeeth** / **FitEyes** — fit teeth and eye meshes independently.
6. **FitRigLogic** / **FitPcaRig** — fit expressions using a RigLogic or PCA rig.
7. **GetFittingState** — retrieve current vertex positions and transform matrices.
8. **GenerateBrowMeshLandmarks** — project 2D brow landmarks onto the fitted mesh for downstream tracking.

Additional controls:
- Fitting masks (`FittingMaskType`) and scan masks (`ScanMaskType`) to exclude regions from fitting.
- Regularization weights for model, per-vertex offset, and Laplacian smoothness.
- `CalculatePcaModelFromDnaRig` — static utility to build a PCA rig from an existing RigLogic DNA.
- `SaveDebuggingData` — saves cameras, landmarks, and meshes for offline debugging.

### `ActorRefinementAPI` — `ActorRefinementAPI.h`
Updates an existing MetaHuman DNA after fitting — writes refined joint positions and mesh deltas back into the DNA stream. Used after `ActorCreationAPI` produces fitted mesh vertices.

Key operations:
- **UpdateRigWithHeadMeshVertices** — recomputes joints and blendshapes from fitted head/teeth/eye vertex positions.
- **UpdateRigWithTeethMeshVertices** — updates only the teeth subrig.
- **RefineRig** — general-purpose per-mesh refinement with configurable mesh and joint correspondence types (`RIGID`, `DELTA_TRANSFER`, `UV_SPACE_PROJECTION`).
- **RefineTeethPlacement** — optimizes teeth position relative to a reference rig using rig controls.
- **TransformRigOrigin** / **ScaleRig** / **ScaleAndTransformRig** — apply world-space transforms to the entire rig (useful for coordinate system alignment).
- **ApplyDNA** / **GenerateDeltaDNA** — utility functions for DNA delta arithmetic (add/subtract DNA deltas with optional per-vertex masks).

### `EvaluateRigAPI` — `EvaluateRigAPI.h`
Lightweight rig evaluator for mesh deformation. Given raw control values, evaluates the RigLogic rig and returns per-mesh vertex positions at a specified LOD.

- **LoadDNA** — load a RigLogic DNA.
- **EvaluateRawControls** — evaluate specific meshes at a specific LOD from a control name→value map.
- **GetNumMeshes** / **GetMeshIndex** / **GetMeshNames** — mesh introspection.
- **GetRawControlNames** — enumerate all animatable controls.
- **GetNumLODs** — query available LODs.

## CMake Target
`ActAPI` (built when `BUILD_ACT_API=ON`)

## Key Dependencies
`Conformer`, `NRR` (NonRigidRegistration), `Rig`, `RigCalibration`, `common` (API common types)
