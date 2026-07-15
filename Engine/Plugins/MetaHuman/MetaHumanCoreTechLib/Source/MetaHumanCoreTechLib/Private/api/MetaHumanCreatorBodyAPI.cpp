// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCreatorBodyAPI.h"
#include "resourceloader/MetaHumanFileResourceLoader.h"
#include "resourceloader/MetaHumanResourceLoaderHub.h"
#include "Internals/LandmarkInstanceUtils.h"
#include <Common.h>

#include <bodyshapeeditor/BodyShapeEditor.h>
#include <bodyshapeeditor/PipelineBundle.h>
#include <rig/BodyGeometry.h>
#include <rig/RigGeometry.h>
#include <rig/CombinedBodyJointLodMapping.h>
#include <dna/BinaryStreamWriter.h>
#include <nls/geometry/MetaShapeCamera.h>
#include <nrr/MeshLandmarks.h>
#include <nrr/PatchBlendModel.h>
#include <nrr/VertexWeights.h>
#include <nrr/landmarks/LandmarkConstraints2D.h>
#include <nrr/landmarks/LandmarkInstance.h>
#include <carbon/io/JsonIO.h>
#include <carbon/io/Utils.h>
#include <carbon/utils/StringUtils.h>
#include <trio/Stream.h>
#include "trio/streams/MemoryStream.h"

#include <algorithm>
#include <map>
#include <memory>

using namespace TITAN_NAMESPACE;

namespace TITAN_API_NAMESPACE
{

struct PhysicsVolumeDefinition
{
    std::vector<int> vertexIndices;
    std::vector<std::pair<std::string, float>> extentJointsAndScale;
};

struct MetaHumanCreatorBodyAPI::Private
{
    std::shared_ptr<TaskThreadPool> threadPool;
    BodyShapeEditor ptr;
    std::vector<std::shared_ptr<const BodyGeometry<float>>> legacyBodies;
    std::vector<std::string> legacyBodiesNames;
    std::vector<int> regionVertexIndices;
    std::vector<std::string> presetNames;
    std::map<std::string, std::shared_ptr<const State>> presetStates;
	
	// Arbitrary solve pipeline configuration
	std::string pipelinePresetsJsonFilepath;
	std::map<std::string, BodySolveConfiguration> solverConfigPresets;
	std::map<std::string, std::vector<int>> controlGroups;
	std::map<std::string, std::vector<SolveStep>> pipelines;
	std::map<std::string, VertexWeights<float>> maskPresets;

    std::map<std::string, std::vector<PhysicsVolumeDefinition>> physicsBodiesVolumes;
	
	std::shared_ptr<MeshLandmarks<float>> faceTrackingLandmarks;
	std::optional<Eigen::VectorXf> defaultFacePatchParams;
    
    mutable struct MeshQuery
    {
        std::mutex mutex;
        std::shared_ptr<AABBTree<float>> aabbTree;
        Eigen::Matrix<int, 3, -1> triangles;
    } meshQuery;
};

static std::map<std::string, std::vector<PhysicsVolumeDefinition>> LoadPhysicsVolumeDefinitions(const JsonElement& bodiesJson, const JsonElement& physicsVertexMasksJson, int topologyNumVertices)
{
    std::map<std::string, std::vector<PhysicsVolumeDefinition>> physicsVolumeDefinitions;
    std::map<std::string, VertexWeights<float>> physicsVertexMasks = VertexWeights<float>::LoadAllVertexWeights(physicsVertexMasksJson, topologyNumVertices);

    if (bodiesJson.Contains("physics_body_volumes"))
    {
        for (const JsonElement& bodyDef : bodiesJson["physics_body_volumes"].Array())
        {
            PhysicsVolumeDefinition volumeDefinition;

            const std::string jointName = bodyDef["joint_name"].String();

            const std::string vertexMaskName = bodyDef["vertex_mask"].String();
            if (auto vertexWeightItr = physicsVertexMasks.find(vertexMaskName); vertexWeightItr != physicsVertexMasks.end())
            {
                volumeDefinition.vertexIndices = vertexWeightItr->second.NonzeroVertices();
            }

            if (bodyDef.Contains("extent_joints"))
            {
                for (const JsonElement& extentJoints : bodyDef["extent_joints"].Array())
                {
                    const std::string extentJointName = extentJoints["extent_joint"].String();
                    float extentJointScale = extentJoints["scale"].Get<float>();
                    volumeDefinition.extentJointsAndScale.push_back({ extentJointName, extentJointScale });
                }
            }

            physicsVolumeDefinitions[jointName].push_back(volumeDefinition);
        }
    }

    return physicsVolumeDefinitions;
}

static std::map<std::string, int> LoadRegionVertexIndices(const std::string& RegionLandmarksPath, dna::Reader* CombinedBodyArchetypeDnaReader)
{
    std::map<std::string, int> nameToVertexIndex;

    if (std::filesystem::exists(Utf8Path(RegionLandmarksPath)))
    {
        const int meshIndex = 0;
        auto rigGeometry = std::make_shared<RigGeometry<float>>();
        TITAN_CHECK_OR_RETURN(rigGeometry->Init(CombinedBodyArchetypeDnaReader, true), {}, "cannot load rig geometry");
        const std::string meshName = rigGeometry->GetMeshName(meshIndex);

        auto meshLandmarks = std::make_shared<MeshLandmarks<float>>();
        meshLandmarks->Load(RegionLandmarksPath, rigGeometry->GetMesh(meshName), meshName);

        for (const auto& [name, baryCoord] : meshLandmarks->LandmarksBarycentricCoordinates())
        {
            nameToVertexIndex[name] = baryCoord.Index(0);
        }
    }

    return nameToVertexIndex;
}
	
static bool LoadJsonMask(VertexWeights<float>& output, const JsonElement& masksJson, const std::string& maskName, int numVertices)
{
    if (masksJson.Contains(maskName))
    {
        output.Load(masksJson, maskName, numVertices);
        return true;
    }

    return false;
}
	
static bool BuildSolveTargetMesh(
									const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> vertices,
									const Eigen::Ref<const Eigen::Matrix<int, 3, -1>> triangleIndices,
									std::shared_ptr<Mesh<float>>& outMesh)
{
	const int numVertices = static_cast<int>(vertices.cols());
	const int numTriangles = static_cast<int>(triangleIndices.cols());

	for (int i = 0; i < numTriangles; ++i)
	{
		for (int k = 0; k < 3; ++k)
		{
			const int vertexIndex = triangleIndices(k, i);
			if (vertexIndex < 0 || vertexIndex >= numVertices)
			{
				LOG_ERROR("Invalid triangle index: triangle {} vertex {} has index {} but vertex count is {}",
					i, k, vertexIndex, numVertices);
				return false;
			}
		}
	}

	auto targetMesh = std::make_shared<Mesh<float>>();
	targetMesh->SetTriangles(triangleIndices);
	targetMesh->SetVertices(vertices);
	targetMesh->CalculateVertexNormals();

	outMesh = targetMesh;
	return true;
}

bool MetaHumanCreatorBodyAPI::BuildCombinedSolveTarget(const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> vertices,
											const Eigen::Ref<const Eigen::Matrix<int, 3, -1>> triangleIndices,
											const std::vector<std::pair<int, Eigen::Vector3f>>& keyPointCorrespondences,
											const std::shared_ptr<LandmarkConstraints2D<float>>& landmarkConstraints2D,
											BodyShapeEditorTarget& outSolveTarget)
{
	std::shared_ptr<Mesh<float>> mesh;
	if (!BuildSolveTargetMesh(vertices, triangleIndices, mesh))
		return false;

	outSolveTarget.SetMesh(BodyShapeEditorTarget::MeshSlot::Combined, mesh);
	for (const auto& [vIdx, pos] : keyPointCorrespondences)
		outSolveTarget.AddKeypoint(vIdx, pos);
	outSolveTarget.SetLandmarks2D(landmarkConstraints2D);
	return true;
}

bool MetaHumanCreatorBodyAPI::BuildHeadOnlySolveTarget(const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> vertices,
											const Eigen::Ref<const Eigen::Matrix<int, 3, -1>> triangleIndices,
											const std::vector<std::pair<int, Eigen::Vector3f>>& keyPointCorrespondences,
											const std::shared_ptr<LandmarkConstraints2D<float>>& landmarkConstraints2D,
											BodyShapeEditorTarget& outSolveTarget)
{
	std::shared_ptr<Mesh<float>> mesh;
	if (!BuildSolveTargetMesh(vertices, triangleIndices, mesh))
		return false;

	outSolveTarget.SetMesh(BodyShapeEditorTarget::MeshSlot::Head, mesh);
	for (const auto& [vIdx, pos] : keyPointCorrespondences)
		outSolveTarget.AddKeypoint(vIdx, pos);
	outSolveTarget.SetLandmarks2D(landmarkConstraints2D);
	return true;
}

bool MetaHumanCreatorBodyAPI::BuildBodyOnlySolveTarget(const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> vertices,
											const Eigen::Ref<const Eigen::Matrix<int, 3, -1>> triangleIndices,
											const std::vector<std::pair<int, Eigen::Vector3f>>& keyPointCorrespondences,
											const std::shared_ptr<LandmarkConstraints2D<float>>& landmarkConstraints2D,
											BodyShapeEditorTarget& outSolveTarget)
{
	std::shared_ptr<Mesh<float>> mesh;
	if (!BuildSolveTargetMesh(vertices, triangleIndices, mesh))
		return false;

	outSolveTarget.SetMesh(BodyShapeEditorTarget::MeshSlot::Body, mesh);
	for (const auto& [vIdx, pos] : keyPointCorrespondences)
		outSolveTarget.AddKeypoint(vIdx, pos);
	outSolveTarget.SetLandmarks2D(landmarkConstraints2D);
	return true;
}

bool MetaHumanCreatorBodyAPI::BuildHeadAndBodySolveTarget(const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> headVertices,
											const Eigen::Ref<const Eigen::Matrix<int, 3, -1>> headTriangleIndices,
											const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> bodyVertices,
											const Eigen::Ref<const Eigen::Matrix<int, 3, -1>> bodyTriangleIndices,
											const std::vector<std::pair<int, Eigen::Vector3f>>& keyPointCorrespondences,
											const std::shared_ptr<LandmarkConstraints2D<float>>& landmarkConstraints2D,
											BodyShapeEditorTarget& outSolveTarget)
{
	std::shared_ptr<Mesh<float>> headMesh;
	if (!BuildSolveTargetMesh(headVertices, headTriangleIndices, headMesh))
		return false;

	std::shared_ptr<Mesh<float>> bodyMesh;
	if (!BuildSolveTargetMesh(bodyVertices, bodyTriangleIndices, bodyMesh))
		return false;

	outSolveTarget.SetMesh(BodyShapeEditorTarget::MeshSlot::Head, headMesh);
	outSolveTarget.SetMesh(BodyShapeEditorTarget::MeshSlot::Body, bodyMesh);
	for (const auto& [vIdx, pos] : keyPointCorrespondences)
		outSolveTarget.AddKeypoint(vIdx, pos);
	outSolveTarget.SetLandmarks2D(landmarkConstraints2D);
	return true;
}

MetaHumanCreatorBodyAPI::MetaHumanCreatorBodyAPI()
    : m(new Private())
{
}

MetaHumanCreatorBodyAPI::~MetaHumanCreatorBodyAPI()
{
    delete m;
}

struct MetaHumanCreatorBodyAPI::State::Private
{
    std::shared_ptr<BodyShapeEditor::State> ptr;
    int legacyBodyIndex { -1 };
};

MetaHumanCreatorBodyAPI::State::State()
    : m(new Private())
{
}

MetaHumanCreatorBodyAPI::State::~State()
{
    delete m;
}

MetaHumanCreatorBodyAPI::State::State(const State& other)
    : m(new Private(*other.m))
{
}

std::shared_ptr<MetaHumanCreatorBodyAPI> MetaHumanCreatorBodyAPI::CreateMHCBodyApi(const dna::Reader* PCABodyModel,
    dna::Reader* InCombinedBodyArchetypeDnaReader,
    const std::string& RBFModelPath,
    const std::string& SkinModelPath,
    const std::string& CombinedSkinningWeightGenerationConfigPath,
    const std::string& CombinedLodGenerationConfigPath,
    const std::string& PhysicsBodiesConfigPath,
    const std::string& BodyMasksPath,
    const std::string& RegionLandmarksPath,
    const std::string& PipelinePresetsPath,
    std::shared_ptr<PatchBlendModel<float>> FacePatchBlendModel,
    std::shared_ptr<MeshLandmarks<float>> FaceTrackingLandmarks,
    int numThreads)
{
    try
    {
        TITAN_RESET_ERROR;
        std::shared_ptr<MetaHumanCreatorBodyAPI> APIInstance(new MetaHumanCreatorBodyAPI);
        if (numThreads != 0)
        {
            APIInstance->m->threadPool = std::make_shared<TaskThreadPool>(numThreads);
        }
        APIInstance->m->ptr.SetThreadPool(APIInstance->m->threadPool);
        std::shared_ptr<LodGeneration<float>> CombinedLodGenerationData;
        if (FMetaHumanFileResourceLoader::ResourceExists(CombinedLodGenerationConfigPath))
        {
            CombinedLodGenerationData = std::make_shared<LodGeneration<float>>();
            if (!CombinedLodGenerationData->LoadModelBinary(CombinedLodGenerationConfigPath))
            {
                LOG_ERROR("Failed to load combined body model lod generation data");
                return nullptr;
            }
        }
        else
        {
            LOG_WARNING("No lod generation data supplied; only lod 0 will be available");
        }

        CombinedLodGenerationData->SetThreadPool(APIInstance->m->threadPool);

        // load in the configuration which defines which how joint weights are distributed to higher lods
        JsonElement json = FMetaHumanFileResourceLoader::GetJsonElementForFile(CombinedSkinningWeightGenerationConfigPath);
        CombinedBodyJointLodMapping<float> jointMapping;
        bool bLoadMapping = jointMapping.ReadJson(json);
        if (!bLoadMapping)
        {
            LOG_ERROR("Failed to parse skinning weight generation config for body model");
            return nullptr;
        }

        const std::vector<int> MaxSkinWeightsPerLod = { 12, 8, 8, 4 };

        auto skinModelMemoryStream = FMetaHumanFileResourceLoader::GetBoundedIOStreamFromFile(SkinModelPath);
        auto RBFModelMemoryStream = FMetaHumanFileResourceLoader::GetBoundedIOStreamFromFile(RBFModelPath);
        
        skinModelMemoryStream->open();
        RBFModelMemoryStream->open();
        APIInstance->m->ptr.Init(PCABodyModel, RBFModelMemoryStream.get(), skinModelMemoryStream.get(), InCombinedBodyArchetypeDnaReader, jointMapping.GetJointMapping(), MaxSkinWeightsPerLod, CombinedLodGenerationData);
        skinModelMemoryStream.release();
        RBFModelMemoryStream.release();

        const JsonElement PhysicsBodiesConfigJson = FMetaHumanFileResourceLoader::GetJsonElementForFile(PhysicsBodiesConfigPath);
        const JsonElement physicsVertexMasksJson = FMetaHumanFileResourceLoader::GetJsonElementForFile(BodyMasksPath);

        APIInstance->m->physicsBodiesVolumes = LoadPhysicsVolumeDefinitions(PhysicsBodiesConfigJson, physicsVertexMasksJson, InCombinedBodyArchetypeDnaReader->getVertexPositionCount(0));
        {
            const auto landmarkMap = LoadRegionVertexIndices(RegionLandmarksPath, InCombinedBodyArchetypeDnaReader);
            for (const auto& regionName : APIInstance->m->ptr.GetRegionNames())
            {
                if (regionName.substr(0, 5) != "joint")
                {
                    auto it = landmarkMap.find(regionName);
                    APIInstance->m->regionVertexIndices.push_back(it != landmarkMap.end() ? it->second : -1);
                }
            }
        }

        VertexWeights<float> weights;
        if (!LoadJsonMask(weights, physicsVertexMasksJson, "FitToTarget", InCombinedBodyArchetypeDnaReader->getVertexPositionCount(0)))
        {
            weights = VertexWeights<float>(Eigen::VectorXf::Ones(InCombinedBodyArchetypeDnaReader->getVertexPositionCount(0)));
        }
        APIInstance->m->ptr.SetFittingVertexIDs(weights.NonzeroVertices());

        const int neckSeamLoopsCount = 3;
        std::vector<std::vector<int>> neckSeamLoops;
        for (int i = 0; i < neckSeamLoopsCount; ++i)
        {
            if (LoadJsonMask(weights, physicsVertexMasksJson, "neck_seam_" + std::to_string(i), InCombinedBodyArchetypeDnaReader->getVertexPositionCount(0)))
            {
                neckSeamLoops.push_back(weights.NonzeroVertices());
            }
        }
        APIInstance->m->ptr.SetNeckSeamVertexIDs(neckSeamLoops);

        APIInstance->m->presetNames.clear();
        APIInstance->m->presetStates.clear();
        std::sort(APIInstance->m->presetNames.begin(), APIInstance->m->presetNames.end());

        APIInstance->SetFacePatchBlendModel(FacePatchBlendModel);
        if (FacePatchBlendModel)
        {
            const auto defaultFaceState = FacePatchBlendModel->CreateState();
            APIInstance->m->defaultFacePatchParams = defaultFaceState.SerializeToVector();
        }
    	APIInstance->m->faceTrackingLandmarks = FaceTrackingLandmarks;
    	
    	APIInstance->m->pipelinePresetsJsonFilepath = PipelinePresetsPath;
    	{
    		PipelineBundle bundle;
    		if (!LoadPipelineBundle(PipelinePresetsPath, &APIInstance->m->ptr, bundle))
    		{
    			LOG_ERROR("Failed to parse pipeline bundle '{}' for body solve", PipelinePresetsPath);
    			return nullptr;
    		}
    		APIInstance->m->pipelines     = std::move(bundle.pipelines);
    		APIInstance->m->controlGroups = std::move(bundle.controlGroups);
    		APIInstance->m->maskPresets   = std::move(bundle.masks);
    	}


        return APIInstance;
    }
    catch (const std::exception& e)
    {
        TITAN_SET_ERROR(-1, TITAN_NAMESPACE::fmt::format("failure to initialize body: {}", e.what()).c_str());
        return nullptr;
    }
}

void MetaHumanCreatorBodyAPI::SetNumThreads(int numThreads) { m->threadPool->SetNumThreads(numThreads); }

int MetaHumanCreatorBodyAPI::GetNumThreads() const
{
    return (int)m->threadPool->NumThreads();
}

void MetaHumanCreatorBodyAPI::AddLegacyBody(const dna::Reader* LegacyBody, const av::StringView& LegacyBodyName)
{
    m->legacyBodiesNames.push_back(LegacyBodyName.c_str());
    std::shared_ptr<BodyGeometry<float>> body = std::make_shared<BodyGeometry<float>>(m->threadPool);
    body->Init(LegacyBody, /*computeMeshNormals=*/false);
    m->legacyBodies.push_back(body);
}

std::shared_ptr<MetaHumanCreatorBodyAPI::State> MetaHumanCreatorBodyAPI::CreateState() const
{
    try
    {
        TITAN_RESET_ERROR;
        std::shared_ptr<MetaHumanCreatorBodyAPI::State> StateInstance { new MetaHumanCreatorBodyAPI::State() };
        StateInstance->m->ptr = m->ptr.CreateState();
        return StateInstance;
    }
    catch (const std::exception& e)
    {
        TITAN_SET_ERROR(-1, TITAN_NAMESPACE::fmt::format("failure to create state: {}", e.what()).c_str());
        return nullptr;
    }
}

std::shared_ptr<MetaHumanCreatorBodyAPI::State> MetaHumanCreatorBodyAPI::State::Clone() const
{
    try
    {
        TITAN_RESET_ERROR;
        return std::shared_ptr<MetaHumanCreatorBodyAPI::State>(new MetaHumanCreatorBodyAPI::State(*this));
    }
    catch (const std::exception& e)
    {
        TITAN_SET_ERROR(-1, TITAN_NAMESPACE::fmt::format("failure to clone state: {}", e.what()).c_str());
        return nullptr;
    }
}

bool MetaHumanCreatorBodyAPI::State::Reset()
{
    try
    {
        TITAN_RESET_ERROR;
        auto newState = std::make_shared<BodyShapeEditor::State>(*m->ptr);
        newState->Reset();
        m->ptr = newState;
        m->legacyBodyIndex = -1;
        return true;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to reset: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::GetVertex(int lod, const float* InVertices, int DNAVertexIndex, float OutVertexXYZ[3]) const
{
    try
    {
        TITAN_RESET_ERROR;
        const std::vector<int>& BodyToCombinedMapping = m->ptr.GetBodyToCombinedMapping(lod);
        if (BodyToCombinedMapping.size() == 0)
        {
            return false;
        }
        int CombinedIndex = BodyToCombinedMapping[DNAVertexIndex];
        for (int k = 0; k < 3; ++k)
        {
            OutVertexXYZ[k] = InVertices[3 * CombinedIndex + k];
        }
        return true;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to get vertex: {}", e.what());
    }
}

int MetaHumanCreatorBodyAPI::SelectVertex(const State& State, const Eigen::Vector3f& Origin, const Eigen::Vector3f& Direction, Eigen::Vector3f& OutVertex, Eigen::Vector3f& OutNormal) const
{
    try
    {
        TITAN_RESET_ERROR;
        int VertexID = -1;
        constexpr int LodIndex = 0;
        
        std::lock_guard<std::mutex> lock(m->meshQuery.mutex);
        
        float* VertexDataPtr = (float*)State.GetMesh(LodIndex).data();
        Eigen::Matrix<float, 3, -1> Vertices = Eigen::Map<const Eigen::Matrix<float, 3, -1>>(VertexDataPtr, 3, State.GetMesh(LodIndex).size() / 3);
        
        if (!m->meshQuery.aabbTree.get())
        {	
            av::ConstArrayView<int> TrianglesView = GetTriangles(LodIndex);
            Eigen::Matrix<int, 3, -1> Triangles = Eigen::Map<const Eigen::Matrix<int, 3, -1>>( (int*)TrianglesView.data(), 3, TrianglesView.size() / 3);
            
            m->meshQuery.triangles = Triangles;
            m->meshQuery.aabbTree = std::make_shared<AABBTree<float>>(Vertices.transpose(), m->meshQuery.triangles.transpose());
        }
        else
        {
            m->meshQuery.aabbTree->Update((const float*)Vertices.data(), m->threadPool.get());
        }
        
        auto [tID, bc, dist] = m->meshQuery.aabbTree->intersectRay(Origin.transpose(), Direction.transpose());
        if (tID >= 0)
        {
            int bestK = 0;
            const Eigen::Matrix3f triangleVertices = Vertices(Eigen::all, m->meshQuery.triangles.col(tID).transpose());
            const Eigen::Vector3f intersection = triangleVertices * bc.transpose();
            const Eigen::RowVector3f distPerVertex = (triangleVertices.colwise() - intersection).colwise().norm();
            distPerVertex.minCoeff(&bestK);
            VertexID = m->meshQuery.triangles.col(tID)[bestK];
            
            OutVertex = Vertices.col(VertexID);
            
            Eigen::Matrix<float, 3, -1> Normals = Eigen::Map<const Eigen::Matrix<float, 3, -1>>( (float*)State.GetMeshNormals(LodIndex).data(), 3, State.GetMeshNormals(LodIndex).size() / 3);
            OutNormal = Normals.col(VertexID);
        }
        
        return VertexID;
    }
    catch (const std::exception& e)
    {
        TITAN_CHECK_OR_RETURN(false, -1, "failure to select body vertex: {}", e.what());
    }
}

void MetaHumanCreatorBodyAPI::SetFacePatchBlendModel(std::shared_ptr<PatchBlendModel<float>> facePatchModel)
{
    m->ptr.SetFacePatchBlendModel(facePatchModel);
}

bool MetaHumanCreatorBodyAPI::SetFacePatchBlendModelParameters(State& state, const Eigen::VectorXf& parameters) const
{
	try
	{
		TITAN_RESET_ERROR;
		auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*state.m->ptr);
		PatchBlendModel<float>::State* pbmState = newBodyShapeState->GetFaceState();
		if (!pbmState)
		{
			return false;
		}
		pbmState->DeserializeFromVector(parameters);
		m->ptr.EvaluateState(*newBodyShapeState);
		state.m->ptr = newBodyShapeState;
		return true;
	}
	catch (const std::exception& e)
	{
		TITAN_HANDLE_EXCEPTION("failure to set state parameters: {}", e.what());
	}
}

bool MetaHumanCreatorBodyAPI::State::GetFacePatchBlendModelParameters(Eigen::VectorXf& OutParameters) const
{
	try
	{
		TITAN_RESET_ERROR;
		const PatchBlendModel<float>::State* pbmState = m->ptr->GetFaceState();
		if (!pbmState)
		{
			return false;
		}
		OutParameters = pbmState->SerializeToVector();
		return true;
	}
	catch (const std::exception& e)
	{
		TITAN_HANDLE_EXCEPTION("failure to get face patch blend model parameters: {}", e.what());
	}
}

bool MetaHumanCreatorBodyAPI::AreFacePatchBlendModelParametersDefault(const State& state) const
{
    if (!m->defaultFacePatchParams.has_value())
    {
        return false;
    }
    const PatchBlendModel<float>::State* pbmState = state.m->ptr->GetFaceState();
    if (!pbmState)
    {
        return false;
    }
    return pbmState->SerializeToVector() == m->defaultFacePatchParams.value();
}

bool MetaHumanCreatorBodyAPI::ResetFacePatchBlendModel(State& state) const
{
    try
    {
        TITAN_RESET_ERROR;
    	auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*state.m->ptr);
        m->ptr.ResetFaceState(*newBodyShapeState);
    	state.m->ptr = newBodyShapeState;
        return true;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to reset body shape face state: {}", e.what());
    }
}

std::vector<MetaHumanCreatorBodyAPI::Keypoint> MetaHumanCreatorBodyAPI::GetDefaultKeypoints() const
{
	const auto& bseKeypoints = m->ptr.GetKeypoints();
	std::vector<Keypoint> result;
	result.reserve(bseKeypoints.size());

	for (const auto& kp : bseKeypoints)
	{
		result.push_back({kp.index, kp.name});
	}

	return result;
}

void MetaHumanCreatorBodyAPI::GetVertexInfluenceWeights(const State& State, std::vector<TITAN_NAMESPACE::SparseMatrix<float>>& vertexInfluenceWeights) const
{
    m->ptr.GetVertexInfluenceWeights(*State.m->ptr, vertexInfluenceWeights);
}

void MetaHumanCreatorBodyAPI::Evaluate(State& State) const
{
    m->ptr.Solve(*State.m->ptr);
    State.m->legacyBodyIndex = -1;
}

void MetaHumanCreatorBodyAPI::EvaluateConstraintRange(const State& State, av::ArrayView<float> MinValues, av::ArrayView<float> MaxValues, bool bScaleWithHeight) const
{
    m->ptr.EvaluateConstraintRange(*State.m->ptr, MinValues, MaxValues, bScaleWithHeight);
}

void MetaHumanCreatorBodyAPI::StateToDna(const State& State, dna::Writer* InOutDnaWriter, bool combinedBodyAndFace, bool usePosedJoints) const
{
    m->ptr.StateToDna(*State.m->ptr, InOutDnaWriter, combinedBodyAndFace, usePosedJoints);
}

void MetaHumanCreatorBodyAPI::DumpState(const State& State, trio::BoundedIOStream* Stream) const
{
    m->ptr.DumpState(*State.m->ptr, Stream);
}

bool MetaHumanCreatorBodyAPI::RestoreState(trio::BoundedIOStream* Stream, std::shared_ptr<MetaHumanCreatorBodyAPI::State> OutState) const
{
    try
    {
        TITAN_RESET_ERROR;
    	auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*OutState->m->ptr);
    	newBodyShapeState = m->ptr.RestoreState(Stream);
    	OutState->m->ptr = newBodyShapeState;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to restore body state: {}", e.what());
    }

    return true;
}

int MetaHumanCreatorBodyAPI::NumLegacyBodies() const
{
    return (int)m->legacyBodies.size();
}

int MetaHumanCreatorBodyAPI::NumLODs() const
{
    return m->ptr.NumLODs();
}
    
av::ConstArrayView<int> MetaHumanCreatorBodyAPI::GetTriangles(int lod) const
{
    return m->ptr.GetMeshTriangles(lod);
}

int MetaHumanCreatorBodyAPI::NumPhysicsBodyVolumes(const ::std::string& JointName) const
{
    return static_cast<int>(m->physicsBodiesVolumes[JointName].size());
}

bool MetaHumanCreatorBodyAPI::GetPhysicsBodyBoundingBox(const State& State, const ::std::string& JointName, int BodyVolumeIndex, Eigen::Vector3f& OutCenter, Eigen::Vector3f& OutExtents) const
{
    try
    {
        TITAN_RESET_ERROR;
        PhysicsVolumeDefinition physicsVolumeDefinition = m->physicsBodiesVolumes[JointName][BodyVolumeIndex];

        int jointIndex = m->ptr.GetJointIndex(JointName);
        const Eigen::Transform<float, 3, Eigen::Affine>& jointTransform = State.m->ptr->GetJointBindMatrices().at(jointIndex);

        const size_t numVertexExtents = physicsVolumeDefinition.vertexIndices.size();
        const size_t extentSize = numVertexExtents + physicsVolumeDefinition.extentJointsAndScale.size();
        Eigen::Matrix<float, 3, -1> BodyVertexExtents(3, extentSize);

        constexpr int lodIndex = 0;
        float* VertexDataPtr = (float*)State.GetMesh(lodIndex).data();
        Eigen::Matrix<float, 3, -1> Vertices = Eigen::Map<const Eigen::Matrix<float, 3, -1>>(VertexDataPtr, 3, State.GetMesh(lodIndex).size() / 3);
        for (size_t i = 0; i < numVertexExtents; i++)
        {
            int VertexIndex = physicsVolumeDefinition.vertexIndices[i];
            if (VertexIndex < 0 || VertexIndex >= Vertices.cols())
                return false;
            BodyVertexExtents.col(i) = jointTransform.inverse() * Vertices.col(VertexIndex);
        }

        auto GetJointExtentLocalPos = [&State, &jointTransform](const int extentJointIndex, const float scaleFactor)
        {
            const Eigen::Transform<float, 3, Eigen::Affine>& extentJointTransform = State.m->ptr->GetJointBindMatrices()[extentJointIndex];
            Eigen::Vector3f extentLocalPos = (jointTransform.inverse() * extentJointTransform.translation()) * scaleFactor;
            return extentLocalPos;
        };

        for (size_t i = 0; i < physicsVolumeDefinition.extentJointsAndScale.size(); i++)
        {
            const int extentJointIndex = m->ptr.GetJointIndex(physicsVolumeDefinition.extentJointsAndScale[i].first);
            const float scaleFactor = physicsVolumeDefinition.extentJointsAndScale[i].second;
            BodyVertexExtents.col(numVertexExtents + i) = GetJointExtentLocalPos(extentJointIndex, scaleFactor);
        }

        Eigen::Vector3f boxMin = BodyVertexExtents.rowwise().minCoeff();
        Eigen::Vector3f boxMax = BodyVertexExtents.rowwise().maxCoeff();

        OutCenter = (boxMax + boxMin) * 0.5f;
        OutExtents = boxMax - boxMin;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to bounding box: {}", e.what());
    }

    return true;
}

int MetaHumanCreatorBodyAPI::NumJoints() const
{
    return m->ptr.NumJoints();
}

void MetaHumanCreatorBodyAPI::GetNeutralJointTransform(const State& State, std::uint16_t JointIndex, Eigen::Vector3f& OutJointTranslation, Eigen::Vector3f& OutJointRotation) const
{
    m->ptr.GetNeutralJointTransform(*State.m->ptr, JointIndex, OutJointTranslation, OutJointRotation);
}

void MetaHumanCreatorBodyAPI::GetNeutralJointTransform(const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& BindPoseMatrices, std::uint16_t JointIndex, Eigen::Vector3f& OutJointTranslation, Eigen::Vector3f& OutJointRotation) const
{
    m->ptr.GetNeutralJointTransform(BindPoseMatrices, JointIndex, OutJointTranslation, OutJointRotation);
}

void MetaHumanCreatorBodyAPI::SetNeutralJointsTranslations(State& State, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> InJoints) const
{
	auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*State.m->ptr);
    m->ptr.SetNeutralJointsTranslations(*newBodyShapeState, InJoints);
	State.m->ptr = newBodyShapeState;
}

void MetaHumanCreatorBodyAPI::SetNeutralJointRotations(State& State, av::ConstArrayView<float> inJointRotations) const
{
	auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*State.m->ptr);
    m->ptr.SetNeutralJointRotations(*newBodyShapeState, inJointRotations);
	State.m->ptr = newBodyShapeState;
}

void MetaHumanCreatorBodyAPI::ResetGuiControls(State& State) const
{
    const Eigen::VectorXf& guiControls = State.m->ptr->GetGuiControls();
	auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*State.m->ptr);
    newBodyShapeState->SetGuiControls(Eigen::VectorXf::Zero(guiControls.size()));
    m->ptr.EvaluateState(*newBodyShapeState);
	State.m->ptr = newBodyShapeState;
}

void MetaHumanCreatorBodyAPI::SetGuiControls(State& State, av::ConstArrayView<float> guiControls) const
{
    const Eigen::VectorXf& current = State.m->ptr->GetGuiControls();
    if (static_cast<int>(guiControls.size()) != current.size())
    {
        LOG_ERROR("SetGuiControls: expected {} controls, got {}", current.size(), guiControls.size());
        return;
    }
    Eigen::VectorXf next(current.size());
    for (int i = 0; i < current.size(); ++i) next[i] = guiControls[i];
    auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*State.m->ptr);
    newBodyShapeState->SetGuiControls(next);
    m->ptr.EvaluateState(*newBodyShapeState);
    State.m->ptr = newBodyShapeState;
}

void MetaHumanCreatorBodyAPI::ResetScale(State& State) const
{
    auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*State.m->ptr);
    newBodyShapeState->SetUniformScale(1.0f);
    m->ptr.EvaluateState(*newBodyShapeState);
    State.m->ptr = newBodyShapeState;
}

void MetaHumanCreatorBodyAPI::UpdateEvaluatePose(State& State, bool bEvaluatePose) const
{
    if (State.GetEvaluatePose() != bEvaluatePose)
    {
        auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*State.m->ptr);
        // BSE SetEvaluatePose flips the flag, re-derives VertexDeltas to
        // preserve the mesh across the toggle, and evaluates internally.
        m->ptr.SetEvaluatePose(*newBodyShapeState, bEvaluatePose);
        State.m->ptr = newBodyShapeState;
    }
}

void MetaHumanCreatorBodyAPI::UpdateApplyFloorOffset(State& State, bool bApplyFloorOffset) const
{
	if (State.GetApplyFloorOffset() != bApplyFloorOffset)
	{
		auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*State.m->ptr);
		newBodyShapeState->SetApplyFloorOffset(bApplyFloorOffset);
		m->ptr.EvaluateState(*newBodyShapeState);
		State.m->ptr = newBodyShapeState;
	}
}

bool MetaHumanCreatorBodyAPI::SetNeutralMesh(State& State, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> inMesh) const
{
	auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*State.m->ptr);
    const bool success = m->ptr.SetNeutralMesh(*newBodyShapeState, inMesh);
    if (success)
    {
        State.m->ptr = newBodyShapeState;
    }
    return success;
}

Eigen::Matrix3Xf MetaHumanCreatorBodyAPI::RigidAttachmentToBind(
    const State& state,
    const std::string& jointName,
    const Eigen::Ref<const Eigen::Matrix3Xf>& posedVertices) const
{
    if (!state.m || !state.m->ptr) return {};
    return m->ptr.RigidAttachmentToBind(*state.m->ptr, jointName, posedVertices);
}

av::ConstArrayView<int> MetaHumanCreatorBodyAPI::CoreJoints() const
{
    return m->ptr.JointEstimator().CoreJoints();
}

av::ConstArrayView<int> MetaHumanCreatorBodyAPI::HelperJoints() const
{
    return m->ptr.JointEstimator().SurfaceJoints();
}

void MetaHumanCreatorBodyAPI::FixJoints(State& State) const
{
    auto meshView = State.GetMesh(0);
    auto jointEstimate = m->ptr.JointEstimator().EstimateJointWorldTranslations(Eigen::Map<const Eigen::Matrix<float, 3, Eigen::Dynamic>> (meshView.data(), 3, meshView.size() / 3u));
    SetNeutralJointsTranslations(State, jointEstimate);
}

void MetaHumanCreatorBodyAPI::VolumetricallyFitHandAndFeetJoints(State& State) const
{
	auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*State.m->ptr);
    m->ptr.VolumetricallyFitHandAndFeetJoints(*newBodyShapeState);
	State.m->ptr = newBodyShapeState;
}

const std::string& MetaHumanCreatorBodyAPI::LegacyBodyName(int LegacyBodyIndex) const
{
    if ((LegacyBodyIndex >= 0) && (LegacyBodyIndex < NumLegacyBodies()))
    {
        return m->legacyBodiesNames[LegacyBodyIndex];
    }
    static const std::string tmp = "";
    return tmp;
}

void MetaHumanCreatorBodyAPI::SelectLegacyBody(State& State, int LegacyBodyIndex, bool Fit) const
{
    if ((LegacyBodyIndex >= 0) && (LegacyBodyIndex < NumLegacyBodies()))
    {
        auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*State.m->ptr);
        m->ptr.SetCustomGeometryToState(*newBodyShapeState, m->legacyBodies[LegacyBodyIndex], Fit);

        for (int constraintIndex = 0; constraintIndex < newBodyShapeState->GetConstraintNum(); constraintIndex++)
        {
            newBodyShapeState->RemoveConstraintTarget(constraintIndex);
        }

        State.m->ptr = newBodyShapeState;
    }
}

int MetaHumanCreatorBodyAPI::NumPresetBodies() const
{
    return (int)m->presetNames.size();
}

const std::vector<std::string>& MetaHumanCreatorBodyAPI::GetPresetNames() const
{
    return m->presetNames;
}

const std::string& MetaHumanCreatorBodyAPI::PresetBodyName(int PresetBodyIndex) const
{
    return m->presetNames[PresetBodyIndex];
}

int MetaHumanCreatorBodyAPI::NumGizmos() const
{
    return static_cast<int>(m->regionVertexIndices.size());
}

bool MetaHumanCreatorBodyAPI::EvaluateGizmos(const State& State, float* OutGizmos) const
{
    try
    {
        TITAN_RESET_ERROR;
        Eigen::Map<const Eigen::Matrix3Xf> vertices(State.GetMesh(0).data(), 3, State.GetMesh(0).size());
        Eigen::Map<Eigen::Matrix3Xf> outGizmos(OutGizmos, 3, NumGizmos());
        for (int gizmoIndex = 0; gizmoIndex < (int)m->regionVertexIndices.size(); ++gizmoIndex)
        {
            const int vID = m->regionVertexIndices[gizmoIndex];
            if (vID >= 0)
            {
                outGizmos.col(gizmoIndex) = vertices.col(vID);
            }
            else
            {
                outGizmos.col(gizmoIndex).setZero();
            }
        }
        return true;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to evaluate gizmos: {}", e.what());
    }
}

const std::vector<std::string>& MetaHumanCreatorBodyAPI::GetRegionNames() const
{
    return m->ptr.GetRegionNames();
}

const std::vector<std::string>& MetaHumanCreatorBodyAPI::GetGuiControlNames() const
{
    return m->ptr.GetGuiControlNames();
}

const std::vector<std::string>& MetaHumanCreatorBodyAPI::GetRawControlNames() const
{
    return m->ptr.GetRawControlNames();
}

bool MetaHumanCreatorBodyAPI::SelectPreset(State& state, int RegionIndex, const std::string& PresetName, BodyAttribute Type) const
{
    try
    {
        TITAN_RESET_ERROR;
        return BlendPresets(state, RegionIndex, { { 1.0f, PresetName } }, Type);
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to select preset: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::BlendPresets(State& state, int RegionIndex, const std::vector<std::pair<float, std::string>>& alphaAndPresetNames, BodyAttribute Type) const
{
    try
    {
        TITAN_RESET_ERROR;
        std::vector<std::pair<float, const State*>> alphaAndStates;
        for (const auto& [alpha, name] : alphaAndPresetNames)
        {
            auto it = m->presetStates.find(name);
            if (it != m->presetStates.end())
            {
                alphaAndStates.push_back({ alpha, it->second.get() });
            }
        }
        return Blend(state, RegionIndex, alphaAndStates, Type);
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to reset state: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::Blend(State& state, int RegionIndex, const std::vector<std::pair<float, const State*>>& states, BodyAttribute Type) const
{
    try
    {
        TITAN_RESET_ERROR;

        auto newState = std::make_shared<BodyShapeEditor::State>(*state.m->ptr);
        std::vector<std::pair<float, const BodyShapeEditor::State*>> bodyStates;
        for (const auto& [alpha, s] : states)
        {
            bodyStates.push_back({ alpha, s->m->ptr.get() });
        }

        if (m->ptr.Blend(*newState, RegionIndex, bodyStates, (BodyShapeEditor::BodyAttribute)Type))
        {
            state.m->ptr = newState;
            return true;
        }
        return false;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to blend body states: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::SetVertexDeltaScale(State& state, float VertexDeltaScale) const
{
    try
    {
        TITAN_RESET_ERROR;
        auto newState = std::make_shared<BodyShapeEditor::State>(*state.m->ptr);
        newState->SetVertexDeltaScale(VertexDeltaScale);
        m->ptr.EvaluateState(*newState);
        state.m->ptr = newState;
        return true;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to set vertex delta scale: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::ClearVertexDeltas(State& State) const
{
    try
    {
        TITAN_RESET_ERROR;
    	auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*State.m->ptr);
        const Eigen::Index numVerts = newBodyShapeState->GetVertexDeltas().cols();
        newBodyShapeState->SetVertexDeltas(Eigen::Matrix<float, 3, -1>::Zero(3, numVerts));
        m->ptr.EvaluateState(*newBodyShapeState);
    	State.m->ptr = newBodyShapeState;
        return true;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to clear vertex deltas: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::SetFaceBlend(State& state, float faceBlend) const
{
    try
    {
        TITAN_RESET_ERROR;
        auto newState = std::make_shared<BodyShapeEditor::State>(*state.m->ptr);
        newState->SetFaceBlend(faceBlend);
        m->ptr.EvaluateState(*newState);
        state.m->ptr = newState;
        return true;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to set face blend: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::SetSeamBlend(State& state, float seamBlend) const
{
    try
    {
        TITAN_RESET_ERROR;
        auto newState = std::make_shared<BodyShapeEditor::State>(*state.m->ptr);
        newState->SetSeamBlend(seamBlend);
        m->ptr.EvaluateState(*newState);
        state.m->ptr = newState;
        return true;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to set seam blend: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::ClearJointDeltas(State& State) const
{
	try
	{
		TITAN_RESET_ERROR;
		auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*State.m->ptr);
		m->ptr.ClearJointDeltas(*newBodyShapeState);
		State.m->ptr = newBodyShapeState;
		return true;
	}
	catch (const std::exception& e)
	{
		TITAN_HANDLE_EXCEPTION("failure to clear joint deltas: {}", e.what());
	}
}

bool MetaHumanCreatorBodyAPI::AdaptNeckSeam(State& State, const AdaptNeckSeamParams& Params) const
{
    try
    {
        TITAN_RESET_ERROR;
    	auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*State.m->ptr);
        m->ptr.AdaptNeckSeam(*newBodyShapeState, Params.laplacianWeight, Params.rings, Params.iterations, Params.seamLockSide);
    	State.m->ptr = newBodyShapeState;
    	return true;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to adapt neck seam: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::ResetNeckSeam(State& State) const
{
	try
	{
		TITAN_RESET_ERROR;
		auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*State.m->ptr);
		const Eigen::Index numVerts = newBodyShapeState->GetBodySeamDelta().cols();
		newBodyShapeState->SetBodySeamDelta(Eigen::Matrix<float, 3, -1>::Zero(3, numVerts));		
		State.m->ptr = newBodyShapeState;
		return true;
	}
	catch (const std::exception& e)
	{
		TITAN_HANDLE_EXCEPTION("failure to reset neck seam: {}", e.what());
	}
}

bool MetaHumanCreatorBodyAPI::FitToTarget(State& state, const FitToTargetOptions& options, const Eigen::Ref<const Eigen::Matrix<float, 3, -1>> InVertices, av::ConstArrayView<float> jointRotations, IterationFunc iterationFunc) const
{
	struct FCancelled {};
    try
    {
        TITAN_RESET_ERROR;
        auto newState = std::make_shared<BodyShapeEditor::State>(*state.m->ptr);

        // Solve in the skeletal/pose-evaluated frame even when the caller
        // had pose evaluation off — the template solver expects posed world
        // matrices to drive joint placement, and downstream rigid-attachment
        // workflows depend on the head joint actually being posed. The
        // original flag is restored on the clone before write-back so the
        // caller sees no change in `state.GetEvaluatePose()`.
        const bool prevEvaluatePose = newState->GetEvaluatePose();
        if (!prevEvaluatePose) m->ptr.SetEvaluatePose(*newState, true);

        // Same snapshot/restore for FloorOffsetApplied. With the flag on,
        // the final EvaluateState inside SolveForTemplateMesh shifts the
        // mesh by Δ before SetNeutralMesh, so the (target − currentMesh)
        // delta absorbs Δ and the subsequent re-eval re-applies it —
        // leaves a (Δ_old − Δ_new) uniform offset in the fitted result.
        // Force off for the duration of the solve so the math runs in
        // unfloor'd space; restore + re-eval at the end so the caller
        // sees the floor shift back on their cached mesh if they had it.
        const bool prevFloorOffsetApplied = newState->GetApplyFloorOffset();
        if (prevFloorOffsetApplied) newState->SetApplyFloorOffset(false);

        BodyShapeEditor::FitToTargetOptions bseOptions;
        bseOptions.iterations = options.iterations;

        newState->ClearLockedControls();
        newState->SetUniformScale(1.0f);
        if (options.isAPose)
        {
            const auto& controlNames = m->ptr.GetGuiControlNames();
            std::vector<int> poseIndices;
            for (int i = 0; i < static_cast<int>(controlNames.size()); ++i)
            {
                // A-pose: lock all pose_driver shape correctives AND the
                // pelvis rotation channels — the target is already in
                // A-pose, the body shouldn't rotate to match it.
                if (controlNames[i].find("pose_driver") == 0
                 || controlNames[i] == "pose_rigid_pelvis.rx"
                 || controlNames[i] == "pose_rigid_pelvis.ry"
                 || controlNames[i] == "pose_rigid_pelvis.rz")
                {
                    poseIndices.push_back(i);
                }
            }
            newState->SetLockedControlIndices(poseIndices);
        }

        auto iterationCallback = [iterationFunc](const Eigen::Matrix<float, 3, -1>& vertices, const Eigen::Matrix<float, 3, -1>& normals, int iterationCount, float, const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& jointMatrices)
        {
            if (iterationFunc)
            {
                av::ConstArrayView<float> bindPose((const float*)jointMatrices.data(), sizeof(Eigen::Transform<float, 3, Eigen::Affine>) / sizeof(float) *jointMatrices.size());
            	if (!iterationFunc(vertices, normals, bindPose, iterationCount, ESolveStepType::BodySolve))
            	{
            		throw FCancelled{};
            	}
            }	
        };
        m->ptr.SolveForTemplateMesh(*newState, InVertices, bseOptions, jointRotations, {}, iterationCallback);
        

        for (int constraintIndex = 0; constraintIndex < newState->GetConstraintNum(); constraintIndex++)
        {
            newState->RemoveConstraintTarget(constraintIndex);
        }
        newState->ClearLockedControls();
        // Restore the caller's original EvaluatePose flag on the clone before
        // writing it back. SetEvaluatePose preserves the fitted mesh by
        // re-deriving VertexDeltas, so the visual result is identical.
        if (!prevEvaluatePose) m->ptr.SetEvaluatePose(*newState, false);
        if (prevFloorOffsetApplied)
        {
            newState->SetApplyFloorOffset(true);
            m->ptr.EvaluateState(*newState);
        }
        state.m->ptr = newState;
        return true;
    }
	catch (const FCancelled&)
	{
		return false;
	}
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to fit body to target: {}", e.what());
    }
}

TITAN_API bool MetaHumanCreatorBodyAPI::FitToTarget(State& state,
        const FitToTargetOptions& options,
        const dna::Reader* InDnaReader) const
{
    try
    {
        TITAN_RESET_ERROR;

        auto xs = InDnaReader->getVertexPositionXs(0);
        auto ys = InDnaReader->getVertexPositionYs(0);
        auto zs = InDnaReader->getVertexPositionZs(0);
        Eigen::Matrix<float, 3, -1> vertices;
        vertices.conservativeResize(3, xs.size());
        for(int i = 0; i < static_cast<int>(xs.size()); ++i)
        {
            vertices(0, i) = xs[i];
            vertices(1, i) = ys[i];
            vertices(2, i) = zs[i];
        }
        return FitToTarget(state, options, vertices);
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to fit to target: {}", e.what());
    }
}

std::shared_ptr<LandmarkConstraints2D<float>> MetaHumanCreatorBodyAPI::CreateLandmarkConstraints(const std::map<std::string, FaceTrackingLandmarkData>& faceLandmarks, const MetaShapeCamera<float>& camera) const
{
    if (m->faceTrackingLandmarks)
    {
        std::shared_ptr<LandmarkConstraints2D<float>> landmarkConstraint = std::make_shared<LandmarkConstraints2D<float>>();

        std::shared_ptr<const LandmarkInstance<float, 2>> landmarkInstance = CreateLandmarkInstanceForCamera(faceLandmarks, {}, camera);
        std::pair<TITAN_NAMESPACE::LandmarkInstance<float, 2>, Camera<float>> targetLandmarks;
        targetLandmarks.first = *landmarkInstance;
        targetLandmarks.second = Camera(camera);

        landmarkConstraint->SetMeshLandmarks(*m->faceTrackingLandmarks);
        landmarkConstraint->SetTargetLandmarks({targetLandmarks});
        return landmarkConstraint;
    }
    return nullptr;
}

bool MetaHumanCreatorBodyAPI::FitToArbitraryTarget(State& state,
       const ArbitraryFitSolveOptions& options,
       const BodyShapeEditorTarget& solveTarget,
       IterationFunc iterationFunc) const
{
	struct FCancelled {};
    try
    {
        TITAN_RESET_ERROR;
        auto newState = std::make_shared<BodyShapeEditor::State>(*state.m->ptr);
    
        auto iterationBodyCallback = [iterationFunc](const Eigen::Matrix<float, 3, -1>& vertices, const Eigen::Matrix<float, 3, -1>& normals, int iterationCount, float, const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& jointMatrices)
        {
            if (iterationFunc)
            {
                av::ConstArrayView<float> bindPose((const float*)jointMatrices.data(), sizeof(Eigen::Transform<float, 3, Eigen::Affine>) / sizeof(float) *jointMatrices.size());
                if (!iterationFunc(vertices, normals, bindPose, iterationCount, ESolveStepType::BodySolve))
                {
                	throw FCancelled{};
                }
            }
        };

	    BodySolveConfiguration bseSolveConfiguration = options.bodySolveConfiguration;

        if (!options.bSolveForPose)
        {
            const auto& cn = m->ptr.GetGuiControlNames();
            std::vector<int> pi;
            for (int i = 0; i < static_cast<int>(cn.size()); ++i)
                if (cn[i].find("pose_driver") == 0) pi.push_back(i);
            // Locks must go on newState (the solver's working copy) — writing them
            // to state.m->ptr is a no-op against the solve because the solver never
            // reads the caller's state. bSolveForPose=false was silently broken here.
            auto ex = newState->GetLockedControlIndices();
            ex.insert(ex.end(), pi.begin(), pi.end());
            newState->SetLockedControlIndices(ex);
        }

    	if (options.bReloadSolveConfigurations)
    	{
    		// Shared PipelineBundle loader — reads the JSON config + its companion
    		// <name>.masks.bin sidecar (if present), ingests BSE part weights so any
    		// step.maskNames that reference "body" / "head" / etc. resolve. Populates
    		// the legacy m->pipelines / m->controlGroups / m->maskPresets maps so
    		// the downstream name-resolution loop below needs no changes.
    		PipelineBundle bundle;
    		if (!LoadPipelineBundle(m->pipelinePresetsJsonFilepath, &m->ptr, bundle))
    		{
    			LOG_ERROR("Failed to parse pipeline bundle '{}' for body solve", m->pipelinePresetsJsonFilepath);
    			return false;
    		}
    		m->pipelines     = std::move(bundle.pipelines);
    		m->controlGroups = std::move(bundle.controlGroups);
    		m->maskPresets   = std::move(bundle.masks);
    	}

		m->ptr.SolveForArbitraryMeshWithICP(*newState,
			solveTarget,
			bseSolveConfiguration,
			{},
			iterationBodyCallback);
    
		for (int constraintIndex = 0; constraintIndex < newState->GetConstraintNum(); constraintIndex++)
		{
			newState->RemoveConstraintTarget(constraintIndex);
		}

        newState->ClearLockedControls();
        state.m->ptr = newState;
        return true;
    }
    catch (const FCancelled&)
    {
    	return false;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to fit body to target: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::AlignToTargetMesh(State& state,
       const BodyShapeEditorTarget& solveTarget) const
{
    try
    {
        TITAN_RESET_ERROR;
        auto newState = std::make_shared<BodyShapeEditor::State>(*state.m->ptr);

        m->ptr.AlignToTargetMesh(*newState, solveTarget);
        state.m->ptr = newState;
        return true;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to align body to target mesh: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::FitFaceToArbitraryTarget(State& state,
       const ArbitraryFitSolveOptions& options,
       const BodyShapeEditorTarget& solveTarget,
       IterationFunc iterationFunc) const
{
	struct FCancelled {};
    try
    {
        TITAN_RESET_ERROR;
        auto newState = std::make_shared<BodyShapeEditor::State>(*state.m->ptr);
    	
    	auto iterationFaceCallback = [iterationFunc](const Eigen::Matrix<float, 3, -1>& vertices, const Eigen::Matrix<float, 3, -1>& normals, int iterationCount, float, const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& jointMatrices)
    	{
    		if (iterationFunc)
    		{
    			av::ConstArrayView<float> bindPose((const float*)jointMatrices.data(), sizeof(Eigen::Transform<float, 3, Eigen::Affine>) / sizeof(float) *jointMatrices.size());
    			if (!iterationFunc(vertices, normals, bindPose, iterationCount, ESolveStepType::FaceSolve))
    			{
    				throw FCancelled{};
    			}
    		}
    	};

	    BodySolveConfiguration bseSolveConfiguration = options.bodySolveConfiguration;

        if (!options.bSolveForPose)
        {
            const auto& cn = m->ptr.GetGuiControlNames();
            std::vector<int> pi;
            for (int i = 0; i < static_cast<int>(cn.size()); ++i)
                if (cn[i].find("pose_driver") == 0) pi.push_back(i);
            // Locks must go on newState (the solver's working copy) — writing them
            // to state.m->ptr is a no-op against the solve because the solver never
            // reads the caller's state. bSolveForPose=false was silently broken here.
            auto ex = newState->GetLockedControlIndices();
            ex.insert(ex.end(), pi.begin(), pi.end());
            newState->SetLockedControlIndices(ex);
        }

    	if (options.bReloadSolveConfigurations)
    	{
    		// Shared PipelineBundle loader — reads the JSON config + its companion
    		// <name>.masks.bin sidecar (if present), ingests BSE part weights so any
    		// step.maskNames that reference "body" / "head" / etc. resolve. Populates
    		// the legacy m->pipelines / m->controlGroups / m->maskPresets maps so
    		// the downstream name-resolution loop below needs no changes.
    		PipelineBundle bundle;
    		if (!LoadPipelineBundle(m->pipelinePresetsJsonFilepath, &m->ptr, bundle))
    		{
    			LOG_ERROR("Failed to parse pipeline bundle '{}' for body solve", m->pipelinePresetsJsonFilepath);
    			return false;
    		}
    		m->pipelines     = std::move(bundle.pipelines);
    		m->controlGroups = std::move(bundle.controlGroups);
    		m->maskPresets   = std::move(bundle.masks);
    	}
    
        m->ptr.SolveFace(*newState,
            solveTarget,
            bseSolveConfiguration,
            iterationFaceCallback);
    
		for (int constraintIndex = 0; constraintIndex < newState->GetConstraintNum(); constraintIndex++)
		{
			newState->RemoveConstraintTarget(constraintIndex);
		}

        newState->ClearLockedControls();
        state.m->ptr = newState;
        return true;
    }
	catch (const FCancelled&)
	{
		return false;
	}
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to fit body to target: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::RefineVertices(State& state,
    const TITAN_NAMESPACE::BodySolveConfiguration& options,
    const BodyShapeEditorTarget& solveTarget,
    const std::string& overrideVertexMaskName) const
{
    try
    {
        TITAN_RESET_ERROR;
        
        BodySolveConfiguration solveConfig = options;
        if (!overrideVertexMaskName.empty())
        {
        	if (auto maskItr = m->maskPresets.find(overrideVertexMaskName); maskItr != m->maskPresets.end())
        	{
        		solveConfig.vertexMask = maskItr->second.Weights();
        	}
        }

        auto newState = std::make_shared<BodyShapeEditor::State>(*state.m->ptr);
        m->ptr.RefineVertices(*newState, solveTarget, solveConfig);
        m->ptr.EvaluateState(*newState);

        state.m->ptr = newState;
        return true;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to refine vertices: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::PipelineFitToArbitraryTarget(State& state,
       const std::string& pipelineName,
       const ArbitraryFitSolveOptions& options,
       const BodyShapeEditorTarget& solveTarget,
       IterationFunc iterationFunc) const
{
	struct FCancelled {};
    try
    {
        TITAN_RESET_ERROR;
        auto newState = std::make_shared<BodyShapeEditor::State>(*state.m->ptr);
        ESolveStepType solveStepType = ESolveStepType::BodySolve;
    
        auto iterationCallback = [iterationFunc, &solveStepType](const Eigen::Matrix<float, 3, -1>& vertices, const Eigen::Matrix<float, 3, -1>& normals, int iterationCount, float, const std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& jointMatrices)
        {
            if (iterationFunc)
            {
                av::ConstArrayView<float> bindPose((const float*)jointMatrices.data(), sizeof(Eigen::Transform<float, 3, Eigen::Affine>) / sizeof(float) *jointMatrices.size());
            	if (!iterationFunc(vertices, normals, bindPose, iterationCount, solveStepType))
            	{
            		throw FCancelled{};
            	}
            }
        };
    
        auto progressCallback = [&solveStepType](int, int, const std::string& stepMessage)
        {
            if (stepMessage == "Scale Solve")
                solveStepType = ESolveStepType::ScaleSolve;
            else if (stepMessage == "Face Solve")
                solveStepType = ESolveStepType::FaceSolve;
            else if (stepMessage == "Refine Vertices")
                solveStepType = ESolveStepType::RefineVerticesSolve;
            else
                solveStepType = ESolveStepType::BodySolve;
        };

	    BodySolveConfiguration bseSolveConfiguration = options.bodySolveConfiguration;

        if (!options.bSolveForPose)
        {
            const auto& cn = m->ptr.GetGuiControlNames();
            std::vector<int> pi;
            for (int i = 0; i < static_cast<int>(cn.size()); ++i)
                if (cn[i].find("pose_driver") == 0) pi.push_back(i);
            // Locks must go on newState (the solver's working copy) — writing them
            // to state.m->ptr is a no-op against the solve because the solver never
            // reads the caller's state. bSolveForPose=false was silently broken here.
            auto ex = newState->GetLockedControlIndices();
            ex.insert(ex.end(), pi.begin(), pi.end());
            newState->SetLockedControlIndices(ex);
        }

    	if (options.bReloadSolveConfigurations)
    	{
    		// Shared PipelineBundle loader — reads the JSON config + its companion
    		// <name>.masks.bin sidecar (if present), ingests BSE part weights so any
    		// step.maskNames that reference "body" / "head" / etc. resolve. Populates
    		// the legacy m->pipelines / m->controlGroups / m->maskPresets maps so
    		// the downstream name-resolution loop below needs no changes.
    		PipelineBundle bundle;
    		if (!LoadPipelineBundle(m->pipelinePresetsJsonFilepath, &m->ptr, bundle))
    		{
    			LOG_ERROR("Failed to parse pipeline bundle '{}' for body solve", m->pipelinePresetsJsonFilepath);
    			return false;
    		}
    		m->pipelines     = std::move(bundle.pipelines);
    		m->controlGroups = std::move(bundle.controlGroups);
    		m->maskPresets   = std::move(bundle.masks);
    	}
    	
    	std::vector<SolveStep> pipelineToRun;    	
    	if (m->pipelines.contains(pipelineName))
    	{
    		pipelineToRun = m->pipelines[pipelineName];
    	}
    	else
    	{
    		LOG_ERROR("Failed to find pipeline {}", pipelineName);
    		return false;
    	}

    	// Resolve per-step config + masks + locked controls before handoff. The BSE
    	// RunPipeline API no longer resolves preset / mask / controlGroup name refs.
    	{
    		const int numControlsMhc = (int)m->ptr.GetGuiControlNames().size();
    		for (auto& step : pipelineToRun)
    		{
    			// Config: prefer inline; fall back to the matching sub-config from
    			// bseSolveConfiguration (legacy bundle) when the step's Configuration
    			// is empty. Align / AdaptNeck don't use a Configuration.
    			if (step.config.Parameters().empty())
    			{
    				switch (step.kind) {
    					case StepKind::BodySolve: step.config = bseSolveConfiguration.body;       break;
    					case StepKind::FaceSolve: step.config = bseSolveConfiguration.face;       break;
    					case StepKind::Refine:    step.config = bseSolveConfiguration.refinement; break;
    					default: break;
    				}
    			}
    			// Masks: union maskNames -> step.vertexMask.
    			if (!step.maskNames.empty())
    			{
    				bool initialized = false;
    				for (const auto& n : step.maskNames)
    				{
    					auto it = m->maskPresets.find(n);
    					if (it == m->maskPresets.end()) continue;
    					if (!initialized) { step.vertexMask = it->second.Weights(); initialized = true; }
    					else step.vertexMask += it->second.Weights();
    				}
    				if (initialized)
    					step.vertexMask = step.vertexMask.cwiseMin(1.0f).cwiseMax(0.0f);
    			}
    			// Locked controls: union group members, invert -> locked.
    			if (!step.controlGroupNames.empty())
    			{
    				std::vector<bool> active(numControlsMhc, false);
    				for (const auto& g : step.controlGroupNames)
    				{
    					auto it = m->controlGroups.find(g);
    					if (it == m->controlGroups.end()) continue;
    					for (int idx : it->second)
    						if (idx >= 0 && idx < numControlsMhc) active[idx] = true;
    				}
    				step.lockedControls.clear();
    				for (int i = 0; i < numControlsMhc; ++i) if (!active[i]) step.lockedControls.push_back(i);
    			}
    		}
    	}

    	m->ptr.RunPipeline(*newState,
			solveTarget,
			bseSolveConfiguration,
			pipelineToRun,
			iterationCallback,
			progressCallback);
        
        for (int constraintIndex = 0; constraintIndex < newState->GetConstraintNum(); constraintIndex++)
		{
			newState->RemoveConstraintTarget(constraintIndex);
		}

        newState->ClearLockedControls();
        state.m->ptr = newState;
        return true;
    }
	catch (const FCancelled&)
	{
		return false;
	}
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to fit body to target: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::GetMeasurements(Eigen::Ref<const Eigen::Matrix<float, 3, -1>> combinedBodyAndFaceVertices, Eigen::VectorXf& measurements, std::vector<std::string>& measurementNames) const
{
    try
    {
        return m->ptr.GetMeasurements(combinedBodyAndFaceVertices, measurements, measurementNames);
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to get measurements: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::GetMeasurements(Eigen::Ref<const Eigen::Matrix<float, 3, -1>> faceVertices, Eigen::Ref<const Eigen::Matrix<float, 3, -1>> bodyVertices, Eigen::VectorXf& measurements, std::vector<std::string>& measurementNames) const
{
    try
    {
        TITAN_RESET_ERROR;
        return m->ptr.GetMeasurements(faceVertices, bodyVertices, measurements, measurementNames);
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to get measurements: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::GetNumLOD0MeshVertices(int& OutNumMeshVertices, bool bInCombined) const
{
    try
    {
        TITAN_RESET_ERROR;
        OutNumMeshVertices = m->ptr.GetNumLOD0MeshVertices(bInCombined);
        return true;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to get number of body LOD0 mesh vertices: {}", e.what());
    }
}
    
av::ConstArrayView<int> MetaHumanCreatorBodyAPI::GetBodyToCombinedMapping(int lod) const
{
    return m->ptr.GetBodyToCombinedMapping(lod);
}

av::ConstArrayView<float> MetaHumanCreatorBodyAPI::State::GetMesh(int lod) const
{
    return m->ptr->GetMesh(lod).Vertices();
}

av::ConstArrayView<float> MetaHumanCreatorBodyAPI::State::GetMeshNormals(int lod) const
{
    return m->ptr->GetMesh(lod).VertexNormals();
}

av::ConstArrayView<float> MetaHumanCreatorBodyAPI::State::GetBindPose() const
{
    av::ConstArrayView<float> view((const float*)m->ptr->GetJointBindMatrices().data(),
        sizeof(Eigen::Transform<float, 3, Eigen::Affine>) / sizeof(float) * m->ptr->GetJointBindMatrices().size());
    return view;
}

av::ConstArrayView<float> MetaHumanCreatorBodyAPI::State::GetWorldPose() const
{
    av::ConstArrayView<float> view((const float*)m->ptr->GetWorldMatrices().data(),
        sizeof(Eigen::Transform<float, 3, Eigen::Affine>) / sizeof(float) * m->ptr->GetWorldMatrices().size());
    return view;
}

av::ConstArrayView<float> MetaHumanCreatorBodyAPI::State::GetMeasurements() const
{
    return m->ptr->GetNamedConstraintMeasurements();
}

av::ConstArrayView<float> MetaHumanCreatorBodyAPI::State::GetGuiControls() const
{
    const Eigen::VectorXf& guiControls = m->ptr->GetGuiControls();
    return av::ConstArrayView<float>(guiControls.data(), guiControls.size());
}

bool MetaHumanCreatorBodyAPI::State::AreGuiControlsZero() const
{
    const Eigen::VectorXf& guiControls = m->ptr->GetGuiControls();
    return guiControls.isZero();
}

av::ConstArrayView<float> MetaHumanCreatorBodyAPI::State::GetRawControls() const
{
    const Eigen::VectorXf& rawControls = m->ptr->GetRawControls();
    return av::ConstArrayView<float>(rawControls.data(), rawControls.size());
}

void MetaHumanCreatorBodyAPI::State::SetCustomVertexInfluenceWeightsLOD0( const SparseMatrix<float>& vertexInfluenceWeights)
{	
    auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*m->ptr);
    newBodyShapeState->SetVertexInfluenceWeights(vertexInfluenceWeights);
	m->ptr = newBodyShapeState;
}

Eigen::Matrix3Xf MetaHumanCreatorBodyAPI::State::GetContourVertices(int ConstraintIndex) const
{
    return m->ptr->GetContourVertices(ConstraintIndex);
}

Eigen::Matrix3Xf MetaHumanCreatorBodyAPI::State::GetContourDebugVertices(int ConstraintIndex) const
{
    return m->ptr->GetContourDebugVertices(ConstraintIndex);
}

int MetaHumanCreatorBodyAPI::State::GetConstraintNum() const
{
    return m->ptr->GetConstraintNum();
}

const std::string& MetaHumanCreatorBodyAPI::State::GetConstraintName(int ConstraintIndex) const
{
    return m->ptr->GetConstraintName(ConstraintIndex);
}

bool MetaHumanCreatorBodyAPI::State::GetConstraintTarget(int ConstraintIndex, float& OutTarget) const
{
    return m->ptr->GetConstraintTarget(ConstraintIndex, OutTarget);
}

bool MetaHumanCreatorBodyAPI::State::SetConstraintTarget(int ConstraintIndex, float Target)
{
    try
    {
        TITAN_RESET_ERROR;
        auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*m->ptr);

        newBodyShapeState->SetConstraintTarget(ConstraintIndex, Target);

        m->ptr = newBodyShapeState;
        return true;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to set target constraint: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::State::RemoveConstraintTarget(int ConstraintIndex)
{
    try
    {
        TITAN_RESET_ERROR;
        auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*m->ptr);

        newBodyShapeState->RemoveConstraintTarget(ConstraintIndex);

        m->ptr = newBodyShapeState;
        return true;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to remove target constraint: {}", e.what());
    }
}


bool MetaHumanCreatorBodyAPI::State::SetApplyFloorOffset(bool bFloorOffset)
{
    try
    {
        TITAN_RESET_ERROR;
        auto newBodyShapeState = std::make_shared<BodyShapeEditor::State>(*m->ptr);
        newBodyShapeState->SetApplyFloorOffset(bFloorOffset);
        m->ptr = newBodyShapeState;
        return true;
    }
    catch (const std::exception& e)
    {
        TITAN_HANDLE_EXCEPTION("failure to set target constraint: {}", e.what());
    }
}

bool MetaHumanCreatorBodyAPI::State::GetApplyFloorOffset() const
{
    return m->ptr->GetApplyFloorOffset();
}

// State::SetEvaluatePose removed — use MetaHumanCreatorBodyAPI::UpdateEvaluatePose
// instead, which calls BSE's SetEvaluatePose (flag + VertexDelta re-derive
// + EvaluateState).

bool MetaHumanCreatorBodyAPI::State::GetEvaluatePose() const
{
    return m->ptr->GetEvaluatePose();
}

float MetaHumanCreatorBodyAPI::State::GetFaceBlend() const
{
    return m->ptr->GetFaceBlend();
}

float MetaHumanCreatorBodyAPI::State::GetSeamBlend() const
{
    return m->ptr->GetSeamBlend();
}

float MetaHumanCreatorBodyAPI::State::VertexDeltaScale() const
{
    return m->ptr->VertexDeltaScale();
}

} // namespace TITAN_API_NAMESPACE

