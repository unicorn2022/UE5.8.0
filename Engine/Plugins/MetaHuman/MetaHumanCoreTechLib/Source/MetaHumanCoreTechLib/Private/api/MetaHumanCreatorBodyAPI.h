// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Defs.h"
#include <bodyshapeeditor/BodySolveConfiguration.h>
#include <bodyshapeeditor/BodyShapeEditorTarget.h>

#include <LandmarkData.h>
#include <arrayview/ArrayView.h>
#include <arrayview/StringView.h>
#include <nls/math/Math.h>
#include <nls/geometry/MetaShapeCamera.h>

#include <memory>
#include <string>
#include <vector>

namespace dna
{

class Reader;
class Writer;

} // namespace dna

namespace trio
{

class BoundedIOStream;

} // namespace trio

namespace TITAN_NAMESPACE
{

template <class T> class PatchBlendModel;
template <class T> class MeshLandmarks;
template <class T> class LandmarkConstraints2D;

} // namespace TITAN_NAMESPACE

namespace TITAN_API_NAMESPACE
{

class MetaHumanCreatorBodyAPI : public std::enable_shared_from_this<MetaHumanCreatorBodyAPI>
{
public:
    class State;

    enum class BodyAttribute
    {
        Skeleton,
        Shape,
        Both
    };

public:
    ~MetaHumanCreatorBodyAPI();
    MetaHumanCreatorBodyAPI(MetaHumanCreatorBodyAPI&&) = delete;
    MetaHumanCreatorBodyAPI(const MetaHumanCreatorBodyAPI&) = delete;
    MetaHumanCreatorBodyAPI& operator=(MetaHumanCreatorBodyAPI&&) = delete;
    MetaHumanCreatorBodyAPI& operator=(const MetaHumanCreatorBodyAPI&) = delete;

    static std::shared_ptr<MetaHumanCreatorBodyAPI> TITAN_API CreateMHCBodyApi(const dna::Reader* PCABodyModel,
        dna::Reader* InCombinedBodyArchetypeDnaReader,
        const std::string& RBFModelPath,
        const std::string& SkinModelPath,
        const std::string& CombinedSkinningWeightGenerationConfigPath,
        const std::string& CombinedLodGenerationConfigPath = {},
        const std::string& PhysicsBodiesConfigPath = {},
        const std::string& BodyMasksPath = {},
        const std::string& RegionLandmarksPath = {},
        const std::string& PipelinePresetsPath = {},
        std::shared_ptr<TITAN_NAMESPACE::PatchBlendModel<float>> FacePatchBlendModel = nullptr,
        std::shared_ptr<TITAN_NAMESPACE::MeshLandmarks<float>> FaceTrackingLandmarks = nullptr,
        int numThreads = -1);

    void TITAN_API SetNumThreads(int numThreads);
    int TITAN_API GetNumThreads() const;

    TITAN_API std::shared_ptr<State> CreateState() const;

    TITAN_API bool GetVertex(int lod, const float* InVertices, int DNAVertexIndex, float OutVertexXYZ[3]) const;

    TITAN_API void Evaluate(State& State) const;
    TITAN_API void EvaluateConstraintRange(const State& state, av::ArrayView<float> MinValues, av::ArrayView<float> MaxValues, bool bScaleWithHeight = false) const;
    TITAN_API void StateToDna(const State& State, dna::Writer* InOutDnaWriter, bool combinedBodyAndFace = false, bool usePosedJoints = false) const;

    TITAN_API void DumpState(const State& State, trio::BoundedIOStream* Stream) const;
    TITAN_API bool RestoreState(trio::BoundedIOStream* Stream, std::shared_ptr<State> OutState) const;

    //! Add Legacy body - dna needs to contine the combined body/face model
    TITAN_API void AddLegacyBody(const dna::Reader* LegacyBody, const av::StringView& LegacyBodyName);

    //! @returns the number of legacy bodies
    TITAN_API int NumLegacyBodies() const;

    //! @returns the name of legacy body @p LegacyBodyIndex
    TITAN_API const std::string& LegacyBodyName(int LegacyBodyIndex) const;

    //! Update @p State using legacy body at index @p LegacyBodyIndex
    TITAN_API void SelectLegacyBody(State& State, int LegacyBodyIndex, bool Fit = false) const;

    //! @returns the number of LODs supported by the API
    TITAN_API int NumLODs() const;

	//! @returns the triangle indices of LODs supported by the API
    av::ConstArrayView<int> GetTriangles(int lod) const;

    //! @returns the number of preset bodies
    TITAN_API int NumPresetBodies() const;

    //! @returns all preset names
    TITAN_API const std::vector<std::string>& GetPresetNames() const;

    //! @returns the name of preset body @p PresetBodyIndex
    TITAN_API const std::string& PresetBodyName(int PresetBodyIndex) const;

    //! Update @p State using preset body at index @p PresetBodyIndex
    TITAN_API void SelectPresetBody(State& State, int PresetBodyIndex) const;

    //! Calculates the combined body vertex influence weights for the supplied body state at each lod; the body must have a pca skinning model
    TITAN_API void GetVertexInfluenceWeights(const State& State, std::vector<TITAN_NAMESPACE::SparseMatrix<float>>& vertexInfluenceWeights) const;

    //! @returns the numbers of physics body volumes
    TITAN_API int NumPhysicsBodyVolumes(const ::std::string& JointName) const;

    //! Calculates the physics bounding box for the joint and volume index
    TITAN_API bool GetPhysicsBodyBoundingBox(const State& State, const ::std::string& JointName, int BodyVolumeIndex, Eigen::Vector3f& OutCenter, Eigen::Vector3f& OutExtents) const;

    TITAN_API int NumJoints() const;
    TITAN_API void GetNeutralJointTransform(const State& State, std::uint16_t JointIndex, Eigen::Vector3f& OutJointTranslation, Eigen::Vector3f& OutJointRotation) const;
    TITAN_API void GetNeutralJointTransform(const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& BindPoseMatrices, std::uint16_t JointIndex, Eigen::Vector3f& OutJointTranslation, Eigen::Vector3f& OutJointRotation) const;
   
    TITAN_API void SetNeutralJointsTranslations(State& State, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> InJoints) const;

    TITAN_API void SetNeutralJointRotations(State& State, av::ConstArrayView<float> inJointRotations) const;

    TITAN_API void ResetGuiControls(State& State) const;
    //! Overwrite the full GUI-control vector on the state and re-evaluate.
    //! Caller must pass exactly GetGuiControlNames().size() values; existing
    //! values are replaced wholesale. Mirrors ResetGuiControls but lets the
    //! caller specify the pose (e.g. for tests that need to set
    //! pose_rigid_root.t* / pose_rigid_pelvis.r* before evaluation).
    TITAN_API void SetGuiControls(State& State, av::ConstArrayView<float> guiControls) const;
    TITAN_API void ResetScale(State& State) const;
    TITAN_API void UpdateEvaluatePose(State& State, bool bEvaluatePose) const;
	TITAN_API void UpdateApplyFloorOffset(State& State, bool bApplyFloorOffset) const;

    TITAN_API bool SetNeutralMesh(State& State, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> inMesh) const;

    //! Move `posedVertices` from the state's current pose frame back to the
    //! state's shape-adjusted bind frame, treating them as a rigid attachment
    //! to the joint named `jointName` (typically "head" for face attachments
    //! like eyes / teeth). Thin wrapper around
    //! `BodyShapeEditor::RigidAttachmentToBind` that pulls the BSE + BSE
    //! state from the PImpl, so callers don't need direct access to the
    //! underlying editor. Returns an empty matrix when `jointName` can't be
    //! resolved or the state's joint matrices aren't populated.
    TITAN_API Eigen::Matrix3Xf RigidAttachmentToBind(
        const State& state,
        const std::string& jointName,
        const Eigen::Ref<const Eigen::Matrix3Xf>& posedVertices) const;

    TITAN_API av::ConstArrayView<int> CoreJoints() const;
    TITAN_API av::ConstArrayView<int> HelperJoints() const;
    
    // Sets neutral joint translation based on vertex positions 
    TITAN_API void FixJoints(State& State) const;

    TITAN_API void VolumetricallyFitHandAndFeetJoints(State& state) const;

    //! @returns all number of gizmos used for region blending
    TITAN_API int NumGizmos() const;

    //! Gets the positions of the gizmos used for region blending
    TITAN_API bool EvaluateGizmos(const State& State, float* OutGizmos) const;

    //! @returns all region names
    TITAN_API const std::vector<std::string>& GetRegionNames() const;

    //! @returns all gui control names
    TITAN_API const std::vector<std::string>& GetGuiControlNames() const;

    //! @returns all raw control names
    TITAN_API const std::vector<std::string>& GetRawControlNames() const;

    /**
     * Blend region @p RegionIndex (all regions if -1) to @p States.
     * @p Type defines whether skeleton, shaping, or both are blended.
     */
    TITAN_API bool Blend(State& state, int RegionIndex, const std::vector<std::pair<float, const State*>>& States, BodyAttribute Type) const;

    TITAN_API bool SelectPreset(State& state, int RegionIndex, const std::string& PresetName, BodyAttribute Type) const;

    TITAN_API bool BlendPresets(State& state, int RegionIndex, const std::vector<std::pair<float, std::string>>& alphaAndPresetNames, BodyAttribute Type) const;

    TITAN_API bool SetVertexDeltaScale(State& state, float VertexDeltaScale) const;
    TITAN_API bool ClearVertexDeltas(State& State) const;
    TITAN_API bool ClearJointDeltas(State& State) const;

    //! Blend the face PCA into the body mesh — 0 keeps the body face,
    //! 1 fully replaces it with the MHC face state's face. Re-evaluates.
    TITAN_API bool SetFaceBlend(State& state, float faceBlend) const;
    //! Blend the body-seam delta (smooth seam between head and body) — 0
    //! disables, 1 fully applies. Effective only when faceBlend > 0.
    TITAN_API bool SetSeamBlend(State& state, float seamBlend) const;

	struct AdaptNeckSeamParams
    {
        TITAN_NAMESPACE::SeamLockSide seamLockSide  = TITAN_NAMESPACE::SeamLockSide::None;
        float laplacianWeight      = 1.5f;
        int   rings                = 12;
        int   iterations           = 15;
    };

    TITAN_API bool AdaptNeckSeam(State& State, const AdaptNeckSeamParams& Params) const;
	TITAN_API bool ResetNeckSeam(State& State) const;

    struct FitToTargetOptions
    {
        bool isAPose{false};
        bool enforceAnatomicalPose {false};
        int iterations = 9;
    };
	
	struct ArbitraryFitSolveOptions
	{
	    TITAN_NAMESPACE::BodySolveConfiguration bodySolveConfiguration; 
	    
		bool bReloadSolveConfigurations = false;
		bool bSolveForPose = true;
	};
	
    enum class ESolveStepType { ScaleSolve, BodySolve, FaceSolve, RefineVerticesSolve };

    using IterationFunc = std::function<bool(const av::ConstArrayView<float> /*vertices*/, const av::ConstArrayView<float> /*normals*/, av::ConstArrayView<float> /*bindpose*/, int, ESolveStepType)>;

    TITAN_API static bool BuildCombinedSolveTarget(
        const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> vertices,
        const Eigen::Ref<const Eigen::Matrix<int, 3, -1>> triangleIndices,
        const std::vector<std::pair<int, Eigen::Vector3f>>& keyPointCorrespondences,
        const std::shared_ptr<TITAN_NAMESPACE::LandmarkConstraints2D<float>>& landmarkConstraints2D,
        TITAN_NAMESPACE::BodyShapeEditorTarget& outSolveTarget);

    TITAN_API static bool BuildHeadOnlySolveTarget(
        const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> vertices,
        const Eigen::Ref<const Eigen::Matrix<int, 3, -1>> triangleIndices,
        const std::vector<std::pair<int, Eigen::Vector3f>>& keyPointCorrespondences,
        const std::shared_ptr<TITAN_NAMESPACE::LandmarkConstraints2D<float>>& landmarkConstraints2D,
        TITAN_NAMESPACE::BodyShapeEditorTarget& outSolveTarget);

    TITAN_API static bool BuildBodyOnlySolveTarget(
        const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> vertices,
        const Eigen::Ref<const Eigen::Matrix<int, 3, -1>> triangleIndices,
        const std::vector<std::pair<int, Eigen::Vector3f>>& keyPointCorrespondences,
        const std::shared_ptr<TITAN_NAMESPACE::LandmarkConstraints2D<float>>& landmarkConstraints2D,
        TITAN_NAMESPACE::BodyShapeEditorTarget& outSolveTarget);

    TITAN_API static bool BuildHeadAndBodySolveTarget(
        const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> headVertices,
        const Eigen::Ref<const Eigen::Matrix<int, 3, -1>> headTriangleIndices,
        const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> bodyVertices,
        const Eigen::Ref<const Eigen::Matrix<int, 3, -1>> bodyTriangleIndices,
        const std::vector<std::pair<int, Eigen::Vector3f>>& keyPointCorrespondences,
        const std::shared_ptr<TITAN_NAMESPACE::LandmarkConstraints2D<float>>& landmarkConstraints2D,
        TITAN_NAMESPACE::BodyShapeEditorTarget& outSolveTarget);

    TITAN_API std::shared_ptr<TITAN_NAMESPACE::LandmarkConstraints2D<float>> CreateLandmarkConstraints(
        const std::map<std::string, FaceTrackingLandmarkData>& faceLandmarks,
        const TITAN_NAMESPACE::MetaShapeCamera<float>& camera) const;

    TITAN_API bool FitToTarget(State& state,
        const FitToTargetOptions& options,
        const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> InVertices, av::ConstArrayView<float> jointRotations = {}, IterationFunc iterationFunc = {}) const;

    TITAN_API bool FitToTarget(State& state,
        const FitToTargetOptions& options,
        const dna::Reader* InDnaReader) const;
    
    TITAN_API bool FitToArbitraryTarget(State& state,
       const ArbitraryFitSolveOptions& options,
       const TITAN_NAMESPACE::BodyShapeEditorTarget& solveTarget,
       IterationFunc iterationFunc = {}) const;

    TITAN_API bool AlignToTargetMesh(State& state,
       const TITAN_NAMESPACE::BodyShapeEditorTarget& solveTarget) const;
    
    TITAN_API bool FitFaceToArbitraryTarget(State& state,
      const ArbitraryFitSolveOptions& options,
      const TITAN_NAMESPACE::BodyShapeEditorTarget& solveTarget,
      IterationFunc iterationFunc = {}) const;
	
    TITAN_API bool RefineVertices(State& state,
        const TITAN_NAMESPACE::BodySolveConfiguration& options,
        const TITAN_NAMESPACE::BodyShapeEditorTarget& solveTarget,
        const std::string& overrideVertexMaskName = "") const;
    
    TITAN_API bool PipelineFitToArbitraryTarget(State& state,
        const std::string& pipelineName,
        const ArbitraryFitSolveOptions& options,
        const TITAN_NAMESPACE::BodyShapeEditorTarget& solveTarget,
        IterationFunc iterationFunc = {}) const;

    //! Calculate measurements on the combined body vertices
    TITAN_API bool GetMeasurements(Eigen::Ref<const Eigen::Matrix<float, 3, -1>> combinedBodyAndFaceVertices, Eigen::VectorXf& measurements, std::vector<std::string>& measurementNames) const;

    //! Calculate measurements on the body and face vertices
    TITAN_API bool GetMeasurements(Eigen::Ref<const Eigen::Matrix<float, 3, -1>> faceVertices, Eigen::Ref<const Eigen::Matrix<float, 3, -1>> bodyVertices, Eigen::VectorXf& measurements, std::vector<std::string>& measurementNames) const;

	//! Get the number of mesh vertices for LOD 0, either for the standalone body, or the combined body
	TITAN_API bool GetNumLOD0MeshVertices(int& OutNumMeshVertices, bool bInCombined) const;

    TITAN_API av::ConstArrayView<int> GetBodyToCombinedMapping(int lod) const;

    TITAN_API int SelectVertex(const State& State, const Eigen::Vector3f& Origin, const Eigen::Vector3f& Direction, Eigen::Vector3f& OutVertex, Eigen::Vector3f& OutNormal) const;

    TITAN_API void SetFacePatchBlendModel(std::shared_ptr<TITAN_NAMESPACE::PatchBlendModel<float>> facePatchModel);
	TITAN_API bool SetFacePatchBlendModelParameters(State& state, const Eigen::VectorXf& parameters) const;
    TITAN_API bool ResetFacePatchBlendModel(State& state) const;
	TITAN_API bool AreFacePatchBlendModelParametersDefault(const State& state) const;

    struct Keypoint
    {
        int index = 0;
        std::string name;
    };

    TITAN_API std::vector<Keypoint> GetDefaultKeypoints() const;

private:
    MetaHumanCreatorBodyAPI();

    struct Private;
    Private* m {};
};

class TITAN_API MetaHumanCreatorBodyAPI::State
{
public:
    ~State();
    State(State&&) = delete;
    State& operator=(State&&) = delete;
    State& operator=(const State&) = delete;

    std::shared_ptr<State> Clone() const;

    bool Reset();

    av::ConstArrayView<float> GetMesh(int lod) const;
    av::ConstArrayView<float> GetMeshNormals(int lod) const;
    av::ConstArrayView<float> GetBindPose() const;
    av::ConstArrayView<float> GetWorldPose() const;
    av::ConstArrayView<float> GetMeasurements() const;
    av::ConstArrayView<float> GetGuiControls() const;
    bool AreGuiControlsZero() const;
    bool GetFacePatchBlendModelParameters(Eigen::VectorXf& OutParameters) const;
    av::ConstArrayView<float> GetRawControls() const;

    void SetCustomVertexInfluenceWeightsLOD0(const TITAN_NAMESPACE::SparseMatrix<float>& vertexInfluenceWeights); 
    
    Eigen::Matrix3Xf GetContourVertices(int ConstraintIndex) const;
    Eigen::Matrix3Xf GetContourDebugVertices(int ConstraintIndex) const;

    int GetConstraintNum() const;
    const std::string& GetConstraintName(int ConstraintIndex) const;
    bool GetConstraintTarget(int ConstraintIndex, float& OutTarget) const;
    bool SetConstraintTarget(int ConstraintIndex, float Target);
    bool RemoveConstraintTarget(int ConstraintIndex);

    bool SetApplyFloorOffset(bool floorOffset);
    bool GetApplyFloorOffset() const;

    // Use MetaHumanCreatorBodyAPI::UpdateEvaluatePose(state, value) — the
    // toggle re-derives VertexDeltas to preserve the mesh and needs the
    // owning API instance.
    bool GetEvaluatePose() const;

    float GetFaceBlend() const;
    float GetSeamBlend() const;

    float VertexDeltaScale() const;

private:
    State();
    State(const State&);

    struct Private;
    Private* m {};

private:
    friend class MetaHumanCreatorBodyAPI;
};

} // namespace TITAN_API_NAMESPACE
