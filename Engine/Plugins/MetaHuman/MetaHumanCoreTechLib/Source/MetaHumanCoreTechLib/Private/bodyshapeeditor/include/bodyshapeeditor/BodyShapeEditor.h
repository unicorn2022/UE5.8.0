// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "bodyshapeeditor/BodyJointEstimator.h"
#include <bodyshapeeditor/BodyShapeEditorTarget.h>
#include <bodyshapeeditor/BodySolveConfiguration.h>
#include <carbon/utils/TaskThreadPool.h>
#include <bodyshapeeditor/BodyMeasurement.h>
#include <trio/Stream.h>
#include <carbon/common/Defs.h>
#include <arrayview/ArrayView.h>
#include <arrayview/StringView.h>
#include <rig/BodyLogic.h>
#include <rig/BodyGeometry.h>
#include <nls/geometry/Affine.h>
#include <rig/RigLogic.h>
#include <nls/geometry/Mesh.h>
#include <nls/geometry/LodGeneration.h>
#include <nrr/VertexWeights.h>
#include <nrr/PatchBlendModel.h>
#include <nls/utils/ConfigurationParameter.h>
#include <dna/Reader.h>
#include <dna/Writer.h>

#include <memory>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

class JsonElement;

struct Constraint2D
{
    Eigen::Vector2f screenPosition;  // Normalized screen coords [-1,1]
    int index = -1;                        // Joint or vertex index
    bool isJoint = false;
    float weight = 1.0f;
};

// <projection matrix, list of constraints> — kept here for callers that #include BodyShapeEditor.h;
// also declared (unnamespaced) in BodyShapeEditorTarget.h.
// (Both declarations resolve to the same typedef in TITAN_NAMESPACE.)

template <class T> class Mesh;
template <class T> class LandmarkConstraints2D;

class BodyShapeEditor
{
public:
    class State;

    struct Keypoint
    {
        int index = 0;
        std::string name;
    };

    enum class BodyAttribute
    {
        Skeleton,
        Shape,
        Both
    };

public:
    ~BodyShapeEditor();
    BodyShapeEditor();

    BodyShapeEditor(const BodyShapeEditor& other) = delete;
    BodyShapeEditor(BodyShapeEditor&& other) = delete;
    BodyShapeEditor& operator=(const BodyShapeEditor& other) = delete;
    BodyShapeEditor& operator=(BodyShapeEditor&& other) = delete;

    void SetThreadPool(const std::shared_ptr<TaskThreadPool>& threadPool);

    void Init(const dna::Reader* reader,
              trio::BoundedIOStream* rbfModelStream,
              trio::BoundedIOStream* skinModelStream,
              dna::Reader* InCombinedBodyArchetypeDnaReader,
              const std::vector<std::map<std::string, std::map<std::string, float>>>& JointSkinningWeightLodPropagationMap,
              const std::vector<int>& maxSkinWeightsPerVertexForEachLod = { 12, 8, 8, 4},
              std::shared_ptr<const LodGeneration<float>> combinedLodGenerationData = nullptr);
    void Init(std::shared_ptr<BodyLogic<float>> BodyLogic,
              std::shared_ptr<BodyGeometry<float>> CombinedBodyArchetypeGeometry,
              std::shared_ptr<RigLogic<float>> CombinedBodyRigLogic,
              std::shared_ptr<BodyGeometry<float>> BodyGeometry,
              av::ConstArrayView<BodyMeasurement> contours,
              const std::vector<std::map<std::string, std::map<std::string, float>>>& JointSkinningWeightLodPropagationMap,
              const std::vector<int>& maxSkinWeightsPerVertexForEachLod = { 12, 8, 8, 4 },
              std::shared_ptr<const LodGeneration<float>> combinedLodGenerationData = nullptr,
              const std::map<std::string, VertexWeights<float>>& partWeights = {});

    void SetFittingVertexIDs(const std::vector<int>& vertexIds);

    void SetNeckSeamVertexIDs(const std::vector<std::vector<int>>& vertexIds);

    void SetBodyToCombinedMapping(int lod, const std::vector<int>& bodyToCombinedMapping);

    const std::vector<int>& GetBodyToCombinedMapping(int lod = 0) const;

    int NumLODs() const;

    void EvaluateConstraintRange(const State& state, av::ArrayView<float> MinValues, av::ArrayView<float> MaxValues, bool bScaleWithHeight) const;

    std::shared_ptr<State> CreateState() const;

    //! Body-geometry computation strategy. Independent of the EvaluatePose
    //! flag — applying the pose and the path used to compute shape are
    //! orthogonal, even if the public EvaluateState entry point currently
    //! pairs them.
    //!  Auto:      defer to State::EvaluatePose (true → Nonlinear, false →
    //!             Linear). Default — preserves existing call-site behavior.
    //!  Linear:    identityVertexEvaluationMatrix approximation + a closed-
    //!             form root pose application. Fast.
    //!  Nonlinear: full EvaluateBodyGeometry skinning (RBF + twist/swing).
    //!             Highest fidelity.
    enum class CalcStrategy
    {
        Auto,
        Linear,
        Nonlinear
    };

    //! Evaluate the state and update the meshes. Convenience wrapper that
    //! reads both axes off State (CalcStrategy::Auto routes via
    //! State::EvaluatePose; applyPose is taken directly from
    //! State::EvaluatePose).
    void EvaluateState(State& State) const;

    //! Explicit form — caller chooses both axes independently of the
    //! State's flag. `isEvaluatingPose` drives whether pose values are
    //! applied (RBF correctives, pose-driver shape controls, root/pelvis
    //! math); `strategy` selects the shape evaluator (linear approximation
    //! vs full skeletal). Auto pairs the path with isEvaluatingPose to
    //! preserve legacy behavior.
    void EvaluateState(State& State, CalcStrategy strategy, bool isEvaluatingPose) const;

    //! Flip the state's EvaluatePose flag while preserving the visible
    //! mesh. Captures the current mesh under the OLD flag, flips the flag
    //! on the state, then re-derives VertexDeltas (via SetNeutralMesh) so
    //! the NEXT EvaluateState — under the new flag — reproduces the same
    //! vertices. Use this instead of State::SetEvaluatePose when you don't
    //! want the linear-vs-skeletal cross-term drift to surface on toggle.
    void SetEvaluatePose(State& State, bool evaluatePose) const;

    std::shared_ptr<BodyLogic<float>> GetBodyLogic() const;

    const std::vector<std::string>& GetGuiControlNames() const;
    const std::vector<std::string>& GetRawControlNames() const;

    //! Estimate Gui from Raw controls
    void UpdateGuiFromRawControls(State& state) const;

    std::shared_ptr<State> RestoreState(trio::BoundedIOStream* InputStream);
    void DumpState(const State& State, trio::BoundedIOStream* OutputStream);

    void Solve(State& State, float priorWeight = 1.0f, const int iterations = 2) const;

    struct FitToTargetOptions {
        int iterations = 20;
        float epsilon1 = 1e-3f;
        float epsilon2 = 1e-3f;
    };

    using IterationFunc = std::function<void(const Eigen::Matrix<float, 3, -1>&, const Eigen::Matrix<float, 3, -1>&, int, float, std::vector<Eigen::Transform<float, 3, Eigen::Affine>>)>;
    Vector<float> SolveForTemplateMesh(State& State, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> InVertices, const FitToTargetOptions& fitToTargetOptions, av::ConstArrayView<float> inJointRotations = {}, Eigen::VectorXf vertexWeightsOverride = {}, 
    IterationFunc func = [](const Eigen::Matrix<float, 3, -1>&,
						const Eigen::Matrix<float, 3, -1>&,
                        int,
                        float,
                        std::vector<Eigen::Transform<float, 3, Eigen::Affine>>) {} );

    //! AdaptNeckSeam smooths the neck seam via Laplacian relaxation on a ring
    //! band around the seam loop. See SeamLockSide in BodySolveConfiguration.h
    //! for the lock-side semantics. Default lockSide = None (seam free, both
    //! sides relax between outer pins).
    void AdaptNeckSeam(State& state, float laplacianWeight, int rings, int iterations,
                       SeamLockSide lockSide = SeamLockSide::None);

    CARBON_SUPRESS_MS_WARNING(4324)
    struct FacePlaneResult
    {
        Camera<float> camera;
        Eigen::Vector3f centroid = Eigen::Vector3f::Zero();
        Eigen::Vector3f normal = Eigen::Vector3f::Zero();
        float radius = 0;
        std::vector<Eigen::Vector3f> positions;
        bool valid = false;
    };
	CARBON_REENABLE_MS_WARNING

    static Camera<float> ComputeCameraFromLandmarks(const std::vector<Eigen::Vector3f>& positions, int imageWidth = 1024, int imageHeight = 1024);
    static FacePlaneResult ComputeCameraFromLandmarksEx(const std::vector<Eigen::Vector3f>& positions, int imageWidth = 1024, int imageHeight = 1024);
    static Camera<float> ComputeOptimalLandmarkCamera(
        const LandmarkConstraints2D<float>& landmarks,
        const Mesh<float>& targetMesh,
        const Eigen::Vector3f& targetOffset = Eigen::Vector3f::Zero());
    static FacePlaneResult ComputeOptimalLandmarkCameraEx(
        const LandmarkConstraints2D<float>& landmarks,
        const Mesh<float>& targetMesh,
        const Eigen::Vector3f& targetOffset = Eigen::Vector3f::Zero(),
        const AABBTree<float>* aabbTree = nullptr);
    int GetNumFaceVertices() const;

    void SetFacePatchBlendModel(std::shared_ptr<PatchBlendModel<float>> facePatchModel);

    Eigen::Vector3f GetModelMeshExtents() const;

    bool SetTargetScaleAndRigidSolve(State& state,
        const Eigen::Matrix<float, 3, -1>& targetVertices,
        const BodySolveConfiguration& options);

    /// Fit the rig to a target scan via rigid (R=I) + uniform scale + translation.
    /// Correspondence sources are auto-selected from what the target carries: face
    /// landmarks/curves if a Landmarks2D is present, body keypoints if any are set.
    /// Writes pose_rigid_root.tx/ty/tz and clears pose_rigid_pelvis.r*. No rotation
    /// is ever applied — rigs and scans are expected to share a canonical orientation.
    void AlignToTargetMesh(State& state, const BodyShapeEditorTarget& target);

    Vector<float> SolveForArbitraryMeshWithICP(
        State& state,
        const BodyShapeEditorTarget& target,
        const BodySolveConfiguration& options,
        const std::vector<int>& lockedControlIndices = {},
        IterationFunc iterationFunc = [](const Eigen::Matrix<float, 3, -1>&,
                                        const Eigen::Matrix<float, 3, -1>&,
                                        int,
                                        float,
                                        std::vector<Eigen::Transform<float, 3, Eigen::Affine>>) {} );

    void SolveFace(
        State& state,
        const BodyShapeEditorTarget& target,
        const BodySolveConfiguration& options,
        IterationFunc iterationFunc = [](const Eigen::Matrix<float, 3, -1>&,
                                       const Eigen::Matrix<float, 3, -1>&,
                                       int,
                                       float,
                                       std::vector<Eigen::Transform<float, 3, Eigen::Affine>>) {} );

    void RunPipeline(
        State& state,
        const BodyShapeEditorTarget& target,
        const BodySolveConfiguration& defaultConfig,
        const std::vector<SolveStep>& steps,
        IterationFunc iterationFunc = [](const Eigen::Matrix<float, 3, -1>&,
                                        const Eigen::Matrix<float, 3, -1>&, int, float,
                                        std::vector<Eigen::Transform<float, 3, Eigen::Affine>>) {},
        std::function<void(int, int, const std::string&)> progressFunc = [](int, int, const std::string&) {});

    void RefineVertices(
        State& state,
        const BodyShapeEditorTarget& target,
        const BodySolveConfiguration& solveConfig);

    void SetNeutralJointRotations(State& state, av::ConstArrayView<float> inJointRotations);

    void VolumetricallyFitHandAndFeetJoints(State& state);

    void SetNeutralJointsTranslations(State& State, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> InJoints);
    bool SetNeutralMesh(State& State, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> inMesh) const;
    
    void ResetPoseGuiControls(State& State);

    void ResetFaceState(State& State);

    void ClearJointDeltas(State& State);

    const BodyJointEstimator& JointEstimator();

    void UpdateMeasurementPoints(State& State) const;

    void StateToDna(const State& State, dna::Writer* InOutDnaWriter, bool combinedBodyAndFace = false, bool usePosedJoints = false) const;

    int NumJoints() const;
    const std::vector<std::string>& GetJointNames() const;
    const std::vector<int>& GetJointParentIndices() const;
    void GetNeutralJointTransform(const State& State, std::uint16_t JointIndex, Eigen::Vector3f& OutJointTranslation, Eigen::Vector3f& OutJointRotation) const;
    void GetNeutralJointTransform(const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& BindPoseMatrices, std::uint16_t JointIndex, Eigen::Vector3f& OutJointTranslation, Eigen::Vector3f& OutJointRotation) const;

    void SetCustomGeometryToState(State& state, std::shared_ptr<const BodyGeometry<float>> Geometry, bool Fit);

    //! calculate the skinning weights for the supplied body state at each lod; the body must have a skin weights pca present for this to work
    void GetVertexInfluenceWeights(const State& state, std::vector<SparseMatrix<float>>& vertexInfluenceWeights) const;

    //! get the maximum number of skin weights for each joint at each LOD of the combined body model (by default this is set to 12, 8, 8, 4)
    const std::vector<int>& GetMaxSkinWeights() const;
    //! set the maximum number of skin weights for each joint at each LOD of the combined body model
    void SetMaxSkinWeights(const std::vector<int>& MaxSkinWeights);

    int GetJointIndex(const std::string& JointName) const;

    //! Move `posedVertices` (in the state's current pose frame) back to the
    //! state's shape-adjusted bind frame, treating them as a rigid attachment
    //! to the joint named `jointName`. Useful for taking eyes / teeth meshes
    //! that the user provided on a fitted, posed character and converting
    //! them into the bind-frame inputs the PBM eyes/teeth fitter expects —
    //! cheaper and more accurate than a Procrustes guess because the rig
    //! state already tells us the transform.
    //!
    //! Works regardless of the state's `EvaluatePose` flag: both bind and
    //! world matrices are composed via `ComposePoseFrame` from the same
    //! evaluation, so callers can A-pose the body (`UpdateEvaluatePose(false)`)
    //! before invoking this — the head-joint un-skin still uses the
    //! fitted-pose transform implied by the state's GuiControls.
    //!
    //! The matrices used are NOT the static rig bind / world matrices —
    //! they're the per-state, shape-adjusted versions (JointDeltas + scale
    //! + root offset baked in). `FloorOffsetApplied` is forced off internally
    //! so the bind/world pair is self-consistent regardless of the caller's
    //! flag (floor offset is computed from the body's lowest Y vertex, which
    //! differs between linear and skeletal modes — mismatched Δ would inject
    //! a residual translation into the rigid transform).
    //!
    //! Returns an empty matrix when `jointName` is unknown or the state's
    //! joint matrices aren't populated.
    Eigen::Matrix3Xf RigidAttachmentToBind(
        const State& state,
        const std::string& jointName,
        const Eigen::Ref<const Eigen::Matrix3Xf>& posedVertices) const;

    //! Reverse direction: bind-frame vertices → current pose frame, treating
    //! them as a rigid attachment to `jointName`. Like `RigidAttachmentToBind`,
    //! this is independent of the state's `EvaluatePose` and
    //! `FloorOffsetApplied` flags.
    Eigen::Matrix3Xf BindToRigidAttachment(
        const State& state,
        const std::string& jointName,
        const Eigen::Ref<const Eigen::Matrix3Xf>& bindVertices) const;

    //! Bind+world matrix pair for one evaluation, used to drive the
    //! rigid-attachment helpers above.
    struct PoseFrame
    {
        std::vector<Eigen::Transform<float, 3, Eigen::Affine>> bind;
        std::vector<Eigen::Transform<float, 3, Eigen::Affine>> world;
    };

    //! Return the posed bind+world matrices implied by `state`'s GuiControls,
    //! regardless of `EvaluatePose`. With the flag on, returns the cached
    //! matrices off the state; with it off, re-runs the nonlinear evaluator
    //! on a throwaway clone (the cached `WorldMatrices` equal
    //! `JointBindMatrices` in that mode and would otherwise collapse
    //! `bind * world^-1` to identity).
    PoseFrame ComposePoseFrame(const State& state) const;

    //! @returns the region names
    const std::vector<std::string>& GetRegionNames() const;

    //! Blends the states
    bool Blend(State& state, int RegionIndex, const std::vector<std::pair<float, const State*>>& States, BodyAttribute Type);

    //! Calculate measurements on the combined body vertices
    bool GetMeasurements(Eigen::Ref<const Eigen::Matrix<float, 3, -1>> combinedBodyAndFaceVertices, Eigen::VectorXf& measurements, std::vector<std::string>& measurementNames) const;

    //! Calculate measurements on the body and face vertices
    bool GetMeasurements(Eigen::Ref<const Eigen::Matrix<float, 3, -1>> faceVertices, Eigen::Ref<const Eigen::Matrix<float, 3, -1>> bodyVertices, Eigen::VectorXf& measurements, std::vector<std::string>& measurementNames) const;

    //! Get the number of mesh vertices for LOD 0, either for the standalone body, or the combined body
    int GetNumLOD0MeshVertices(bool bInCombined) const;

    //! Get the mesh triangle indices for LOD
    const Eigen::Matrix<int, 3, -1>& GetMeshTriangles(int lod) const;

    //! Get the names of all available part weights (masks)
    std::vector<std::string> GetPartWeightNames() const;

    //! Get a specific part weight by name, returns nullptr if not found
    const VertexWeights<float>* GetPartWeight(const std::string& name) const;

    //! Set the body-blend mask (alpha=1 pure body, alpha=0 pure face, soft ramp
    //! across the neck seam). Used by AdaptNeckSeam to define pinned boundary
    //! values outside the ring band. Indexed against combined-mesh vertices.
    void SetBodyBlendMask(const VertexWeights<float>& mask);
    const VertexWeights<float>* GetBodyBlendMask() const;

    //! Get the list of default keypoints
    const std::vector<Keypoint>& GetKeypoints() const;

private:
    void UpdateSkinningAndRBF(const Eigen::VectorXf& rawControls, const Eigen::VectorXf& joints, std::shared_ptr<BodyGeometry<float>> poseLogic, std::shared_ptr<BodyLogic<float>> poseGeometry) const;
	
	//! Compute re-linearized RBF joint group values for posed DNA export.
	//! Evaluates the Jacobian of WorldMatrices w.r.t. RBF controls at the current pose operating point
	//! using central finite differences, then writes the result to the DNA writer.
	void ComputePosedRBFJointGroups(const State& State, dna::Writer* InOutDnaWriter) const;

private:
    struct Private;
    Private* m;
};


class BodyShapeEditor::State
{
private:
    State();

public:
    ~State();
    State(const State&);

    const Mesh<float>& GetMesh(int lod) const;
    const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& GetJointBindMatrices() const;
    const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& GetWorldMatrices() const;
    const Eigen::VectorX<float>& GetNamedConstraintMeasurements() const;
    const Eigen::VectorX<float>& GetCustomPose() const;
    Eigen::Matrix3Xf GetContourVertices(int ConstraintIndex) const;
    Eigen::Matrix3Xf GetContourDebugVertices(int ConstraintIndex) const;

    void Reset();

    int GetConstraintNum() const;
    const std::string& GetConstraintName(int ConstraintIndex) const;

    bool GetConstraintTarget(int ConstraintIndex, float& OutTarget) const;
    void SetConstraintTarget(int ConstraintIndex, float Target);
    void RemoveConstraintTarget(int ConstraintIndex);

    void SetVertexInfluenceWeights(const SparseMatrix<float>& vertexInfluenceWeights);

    float VertexDeltaScale() const;
    void SetVertexDeltaScale(float VertexDeltaScale);

    const Eigen::Matrix<float, 3, -1>& GetVertexDeltas() const;
    void SetVertexDeltas(const Eigen::Matrix<float, 3, -1>& vertexDeltas);
    const Eigen::Matrix<float, 3, -1>& GetBodySeamDelta() const;
    void SetBodySeamDelta(const Eigen::Matrix<float, 3, -1>& delta);

    void SetSymmetry(const bool sym);
    bool GetSymmetric() const;
    void SetSemanticWeight(float weight);
    float GetSemanticWeight();

    bool GetApplyFloorOffset() const;
    void SetApplyFloorOffset(bool floorOffset);

    bool GetEvaluatePose() const;
    // SetEvaluatePose lives on BodyShapeEditor (takes State&) — it's not a
    // pure flag flip; it re-derives VertexDeltas so the mesh is preserved
    // across the toggle.

    void SetGuiControls(const Eigen::VectorXf& guiControls);
    const Eigen::VectorXf& GetGuiControls() const;
    const Eigen::VectorXf& GetRawControls() const;
    
    float GetUniformScale() const;
    void SetUniformScale(float scale);

    void SetLockedControlIndices(const std::vector<int>& indices);
    void ClearLockedControls();
    const std::vector<int>& GetLockedControlIndices() const;

    PatchBlendModel<float>::State* GetFaceState();
    const PatchBlendModel<float>::State* GetFaceState() const;

    float GetFaceBlend() const;
    void SetFaceBlend(float blend);

    float GetPoseBlend() const;
    void SetPoseBlend(float blend);

    //! Scales the BodySeamDelta contribution at evaluation time. Not serialized
    //! (always defaults to 1.0 on load, like PoseBlend). Clamped to [0,1].
    float GetSeamBlend() const;
    void SetSeamBlend(float blend);

public:
    State(State&&) = delete;
    State& operator=(State&&) = delete;
    State& operator=(const State&) = delete;

private:
    friend BodyShapeEditor;
    struct Private;
    std::unique_ptr<Private> m;
};

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
