// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterBodyIdentity.h"
#include "MetaHumanCharacterIdentity.h"
#include "MetaHumanCoreTechLibGlobals.h"
#include "MetaHumanCreatorBodyAPI.h"
#include "MetaHumanCreatorAPI.h"
#include "MetaHumanConformTargetParams.h"
#include "MetaHumanCommonDataUtils.h"
#include "terse/archives/binary/InputArchive.h"
#include "terse/archives/binary/OutputArchive.h"
#include <nls/math/Math.h>
#include <nls/geometry/MetaShapeCamera.h>
#include <rig/BodyGeometry.h>
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "DNAUtils.h"
#include "DNAReaderAdapter.h"
#include "LegacyDNAReaderAdapter.h"
#include "Serialization/JsonSerializer.h"
#include "dna/Reader.h"
#include "MetaHumanRigEvaluatedState.h"
#include "Kismet/GameplayStatics.h"

#include <string>
#include <vector>
#include <bodyshapeeditor/BodySolveConfiguration.h>
#include <bodyshapeeditor/WeightSchedule.h>

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanCharacterBodyIdentity)

namespace UE::MetaHuman
{
	static TAutoConsoleVariable<bool> CVarMHCharacterReloadSolvePipelineConfig
	{
		TEXT("mh.Character.LoadSolvePipelineConfigFromDisk"),
		false,
		TEXT("Set to true to load the solve pipline config from dish each solve"),
		ECVF_Default
	};
	
	TArray<FVector3f> GetVerticesDNASpace(const TArray<FVector3f>& InVertices)
	{
		TArray<FVector3f> VerticesDNASpace;
		VerticesDNASpace.AddUninitialized(InVertices.Num());
		for (int32 I = 0; I < InVertices.Num(); ++I)
		{
			VerticesDNASpace[I] = FVector3f{ InVertices[I].X, InVertices[I].Z, InVertices[I].Y };
		}
		return VerticesDNASpace;
	}
	
	Eigen::Map<const Eigen::Matrix3Xf> GetEigenVertices(const TArray<FVector3f>& InVertices)
	{
		return Eigen::Map<const Eigen::Matrix3Xf>((const float*)InVertices.GetData(), 3, InVertices.Num());
	}

	Eigen::Map<const Eigen::Matrix3Xi> GetEigenVertexIndices(const TArray<int32>& InVertexIndices)
	{
		return Eigen::Map<const Eigen::Matrix3Xi>((const int*)InVertexIndices.GetData(), 3, InVertexIndices.Num() / 3);
	}

	TITAN_NAMESPACE::MetaShapeCamera<float> GetMetaShapeCamera(
	  const FMinimalViewInfo& InViewInfo,
	  const FIntPoint& InImageSize,
	  const FString& InCameraName)
	{
		using namespace TITAN_NAMESPACE;

		MetaShapeCamera<float> MetaShapeCamera;

		if (InImageSize.X <= 0 || InImageSize.Y <= 0)
		{
			UE_LOGF(LogMetaHumanCoreTechLib, Warning, "Error in setting viewport camera. Invalid image size");
			return MetaShapeCamera;
		}
		
		if (InViewInfo.FOV <= 0)
		{
			UE_LOGF(LogMetaHumanCoreTechLib, Warning, "Error in setting viewport camera. Invalid FOV");
			return MetaShapeCamera;
		}

		MetaShapeCamera.SetLabel(TCHAR_TO_UTF8(*InCameraName));
		MetaShapeCamera.SetWidth(InImageSize.X);
		MetaShapeCamera.SetHeight(InImageSize.Y);
		
		const float ImageWidth = static_cast<float>(InImageSize.X);
		const float ImageHeight = static_cast<float>(InImageSize.Y);

		const float HorizontalFOVRadians = FMath::DegreesToRadians(InViewInfo.FOV);
		const float AspectRatio = ImageWidth / ImageHeight;
		const float FxNormalized = 1.0f / FMath::Tan(HorizontalFOVRadians * 0.5f);
		const float FyNormalized = FxNormalized * AspectRatio;
	
		const float Cx = ImageWidth * 0.5f;
		const float Cy = ImageHeight * 0.5f;	
		const float Fx = FxNormalized * Cx;
		const float Fy = FyNormalized * Cy;

		Eigen::Matrix3f Intrinsics = Eigen::Matrix3f::Identity();
		Intrinsics(0, 0) = Fx;
		Intrinsics(1, 1) = -Fy;
		Intrinsics(0, 2) = Cx;
		Intrinsics(1, 2) = Cy;
		MetaShapeCamera.SetIntrinsics(Intrinsics);


		FMatrix UEViewMatrix, UEProjectionMatrix, ViewProjectionMatrix;
		UGameplayStatics::GetViewProjectionMatrix(InViewInfo, UEViewMatrix, UEProjectionMatrix, ViewProjectionMatrix);
	
		Affine<float, 3, 3> Extrinsics;
		Eigen::Vector3f CameraTranslation(UEViewMatrix.M[3][0], UEViewMatrix.M[3][1], UEViewMatrix.M[3][2]);	
		Extrinsics.SetTranslation(CameraTranslation);
	
		Eigen::Matrix3f UERotation;
		for (int r = 0; r < 3; r++)
		{
			for (int c = 0; c < 3; c++)
			{
				UERotation(r, c) = UEViewMatrix.M[c][r]; // transpose row to column
			}
		}
	
		Eigen::Matrix3f CoordTransformMatrix;
		CoordTransformMatrix <<	1, 0, 0,   // X_ue = X_dna
								0, 0, 1,   // Y_ue = Z_dna
								0, 1, 0;   // Z_ue = Y_dna
	
		Eigen::Matrix3f CameraRotation = UERotation * CoordTransformMatrix;
		Extrinsics.SetLinear(CameraRotation);
		MetaShapeCamera.SetExtrinsics(Extrinsics);

		return MetaShapeCamera;
	}
	
	std::map<std::string, TITAN_API_NAMESPACE::FaceTrackingLandmarkData> GetFaceTrackingLandmarkData(const TMap<FString, FTrackingPoints>& InTrackingContours)
	{
		std::map<std::string, TITAN_API_NAMESPACE::FaceTrackingLandmarkData> FaceLandmarkData;
		
		for (const TPair<FString, FTrackingPoints>& CurveTrackingPoints : InTrackingContours)
		{
			std::string curveName = TCHAR_TO_UTF8(*CurveTrackingPoints.Key);
			const TArray<FVector2D>& TrackingPoints = CurveTrackingPoints.Value.TrackingPoints;
		
			std::vector<float> ComponentValues;
			ComponentValues.reserve(TrackingPoints.Num() * 2);

			for (const FVector2D& Point : TrackingPoints)
			{
				ComponentValues.push_back(Point.X);
				ComponentValues.push_back(Point.Y);
			}

			FaceLandmarkData[curveName] = TITAN_API_NAMESPACE::FaceTrackingLandmarkData::Create(ComponentValues.data(), nullptr, TrackingPoints.Num(), 2);
		}
		
		return FaceLandmarkData;
	}
	
	std::vector<std::pair<int, Eigen::Vector3f>> GetKeypointCorrespondences(const TMap<int32, FVector3f>& InKeypointCorrespondences)
	{
		std::vector<std::pair<int, Eigen::Vector3f>> KeypointCorrespondences;
		for (const TPair<int32, FVector3f>& KeyTargetPair : InKeypointCorrespondences)
		{
			int Index = KeyTargetPair.Key;
			Eigen::Vector3f TargetPos(KeyTargetPair.Value[0], KeyTargetPair.Value[2], KeyTargetPair.Value[1]);
			KeypointCorrespondences.push_back({Index, TargetPos});
		}
		return KeypointCorrespondences;
	}
	
	TITAN_NAMESPACE::ScheduleCurve ToScheduleCurve(EWeightScheduleCurve InCurve)
	{
		switch (InCurve)
		{
		case EWeightScheduleCurve::Linear:    return TITAN_NAMESPACE::ScheduleCurve::Linear;
		case EWeightScheduleCurve::Quadratic: return TITAN_NAMESPACE::ScheduleCurve::Quadratic;
		case EWeightScheduleCurve::Log:       return TITAN_NAMESPACE::ScheduleCurve::Log;
		default:                              return TITAN_NAMESPACE::ScheduleCurve::Static;
		}
	}

	void SetWeightSchedule(TITAN_NAMESPACE::Configuration& Config, const FString& ParamName, const FWeightSchedule& Schedule)
	{
		Config[TCHAR_TO_UTF8(*ParamName)].Set(Schedule.Start);
		Config[TCHAR_TO_UTF8(*(ParamName + TEXT("End")))].Set(Schedule.End);
		Config[TCHAR_TO_UTF8(*(ParamName + TEXT("Curve")))].Set(static_cast<int>(ToScheduleCurve(Schedule.Curve)));
	}

	TITAN_NAMESPACE::BodySolveConfiguration GetBSESolveConfiguration(const FBodyConformSolveSettings& SolveSettings)
	{
		TITAN_NAMESPACE::BodySolveConfiguration c;
		c.body = TITAN_NAMESPACE::CreateBodySolveConfig();
		c.face = TITAN_NAMESPACE::CreateFaceSolveConfig();

		// Body solve parameters
		c.body["iterations"].Set(SolveSettings.Iterations);
		SetWeightSchedule(c.body, TEXT("icp"), SolveSettings.IcpGeometryWeight);
		SetWeightSchedule(c.body, TEXT("icpTol"), SolveSettings.IcpSearchTolerance);
		SetWeightSchedule(c.body, TEXT("normalCompat"), SolveSettings.IcpNormalCompatibility);
		SetWeightSchedule(c.body, TEXT("keypoint"), SolveSettings.IcpKeyPointWeight);
		SetWeightSchedule(c.body, TEXT("landmark2D"), SolveSettings.IcpLandmarksWeight);
		SetWeightSchedule(c.body, TEXT("regGlobal"), SolveSettings.RegularizationGlobalControls);
		SetWeightSchedule(c.body, TEXT("regLocal"), SolveSettings.RegularizationLocalControls);
		SetWeightSchedule(c.body, TEXT("regProportions"), SolveSettings.RegularizationProportions);
		SetWeightSchedule(c.body, TEXT("regPose"), SolveSettings.RegularizationPose);
		c.body["symmetry"].Set(SolveSettings.bSymmetricalSolve);
		c.body["curveResampling"].Set(SolveSettings.CurveResampling);

		// Face solve parameters
		c.face["iterations"].Set(SolveSettings.FaceIterations);
		SetWeightSchedule(c.face, TEXT("icp"), SolveSettings.FaceIcpWeight);
		SetWeightSchedule(c.face, TEXT("icpTol"), SolveSettings.FaceIcpSearchTolerance);
		SetWeightSchedule(c.face, TEXT("normalCompat"), SolveSettings.FaceNormalCompatibility);
		SetWeightSchedule(c.face, TEXT("keypoint"), SolveSettings.FaceKeypointWeight);
		SetWeightSchedule(c.face, TEXT("landmark2D"), SolveSettings.FaceLandmark2DWeight);
		SetWeightSchedule(c.face, TEXT("modelRegularization"), SolveSettings.ModelRegularization);
		SetWeightSchedule(c.face, TEXT("patchSmoothness"), SolveSettings.PatchSmoothness);
		c.face["lmDamping"].Set(SolveSettings.LandmarkDamping);

		return c;
	}
	
	TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::ArbitraryFitSolveOptions GetFitSolveOptions(const FBodyConformSolveSettings& SolveSettings)
	{
		TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::ArbitraryFitSolveOptions FitSolveOptions;
		FitSolveOptions.bodySolveConfiguration = GetBSESolveConfiguration(SolveSettings);
		FitSolveOptions.bReloadSolveConfigurations = CVarMHCharacterReloadSolvePipelineConfig.GetValueOnAnyThread();
		FitSolveOptions.bSolveForPose = SolveSettings.bSolvePose;
		return FitSolveOptions;
	}

	bool ValidateConformTargetMesh(const FConformTargetMesh& InConformTargetMesh)
	{
		if (InConformTargetMesh.TargetPartsType == ETargetPartsType::HeadOnly || InConformTargetMesh.TargetPartsType == ETargetPartsType::HeadAndBody)
		{
			if (InConformTargetMesh.HeadVertices.IsEmpty() || InConformTargetMesh.HeadVertexIndices.IsEmpty())
			{
				UE_LOGF(LogMetaHumanCoreTechLib, Error, "Conform failed. Target params must contain vertices and vertex indices");
				return false;	
			}
		}

		if (InConformTargetMesh.TargetPartsType != ETargetPartsType::HeadOnly)
		{
			if (InConformTargetMesh.BodyVertices.IsEmpty() || InConformTargetMesh.BodyVertexIndices.IsEmpty())
			{
				UE_LOGF(LogMetaHumanCoreTechLib, Error, "Conform failed. Target params must contain vertices and vertex indices");
				return false;	
			}
		}

		return true;
	}

	bool BuildSolveTarget(
		const FConformTargetParams& InConformTargetParams,
		std::shared_ptr<const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI> InMHCBodyAPI,
		TITAN_NAMESPACE::BodyShapeEditorTarget& OutSolveTarget)
	{
		const FConformTargetMesh& TargetMesh = InConformTargetParams.ConformTargetMesh;

		const TArray<FVector3f> HeadVerticesDNASpace = GetVerticesDNASpace(TargetMesh.HeadVertices);
		const TArray<FVector3f> BodyVerticesDNASpace = GetVerticesDNASpace(TargetMesh.BodyVertices);

		const auto FaceLandmarks = GetFaceTrackingLandmarkData(InConformTargetParams.CurveTrackingPoints);
		const auto ViewportCamera = GetMetaShapeCamera(InConformTargetParams.CameraViewInfo, InConformTargetParams.ImageSize, "Front");
		const auto KeyPointCorrespondences = GetKeypointCorrespondences(InConformTargetParams.KeyPointTargets);
		auto LandmarkConstraints = InMHCBodyAPI->CreateLandmarkConstraints(FaceLandmarks, ViewportCamera);

		bool bBuildOk = false;
		switch (TargetMesh.TargetPartsType)
		{
		case ETargetPartsType::HeadOnly:
			bBuildOk = TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::BuildHeadOnlySolveTarget(
				GetEigenVertices(HeadVerticesDNASpace),
				GetEigenVertexIndices(TargetMesh.HeadVertexIndices),
				KeyPointCorrespondences, LandmarkConstraints,
				OutSolveTarget);
			break;
		case ETargetPartsType::BodyOnly:
			bBuildOk = TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::BuildBodyOnlySolveTarget(
				GetEigenVertices(BodyVerticesDNASpace),
				GetEigenVertexIndices(TargetMesh.BodyVertexIndices),
				KeyPointCorrespondences, LandmarkConstraints,
				OutSolveTarget);
			break;
		case ETargetPartsType::HeadAndBody:
			{
				bBuildOk = TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::BuildHeadAndBodySolveTarget(
					GetEigenVertices(HeadVerticesDNASpace),
					GetEigenVertexIndices(TargetMesh.HeadVertexIndices),
					GetEigenVertices(BodyVerticesDNASpace),
					GetEigenVertexIndices(TargetMesh.BodyVertexIndices),
					KeyPointCorrespondences, LandmarkConstraints,
					OutSolveTarget);
				break;
			}
		case ETargetPartsType::Combined:
		default:
			bBuildOk = TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::BuildCombinedSolveTarget(
				GetEigenVertices(BodyVerticesDNASpace),
				GetEigenVertexIndices(TargetMesh.BodyVertexIndices),
				KeyPointCorrespondences, LandmarkConstraints,
				OutSolveTarget);
			break;
		}

		if (!bBuildOk)
		{
			return false;
		}

		TArray<float> JointRotationsView;
		if (TargetMesh.BodyJointRotations.Num() > 0)
		{
			JointRotationsView.SetNumUninitialized(TargetMesh.BodyJointRotations.Num() * 3);
			for (int32 i = 0; i < TargetMesh.BodyJointRotations.Num(); ++i)
			{
				JointRotationsView[i * 3 + 0] = TargetMesh.BodyJointRotations[i].X;
				JointRotationsView[i * 3 + 1] = -TargetMesh.BodyJointRotations[i].Y;
				JointRotationsView[i * 3 + 2] = -TargetMesh.BodyJointRotations[i].Z;
			}
		}
		OutSolveTarget.SetJointRotations({JointRotationsView.GetData(), static_cast<std::size_t>(JointRotationsView.Num())});

		return true;
	}

	bool BuildSolveTarget(
		const FRefinementTargetParams& InRefinementTargetParams,
		std::shared_ptr<const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI> InMHCBodyAPI,
		TITAN_NAMESPACE::BodyShapeEditorTarget& OutSolveTarget)
	{
		const FConformTargetMesh& TargetMesh = InRefinementTargetParams.ConformTargetMesh;

		const TArray<FVector3f> HeadVerticesDNASpace = GetVerticesDNASpace(TargetMesh.HeadVertices);
		const TArray<FVector3f> BodyVerticesDNASpace = GetVerticesDNASpace(TargetMesh.BodyVertices);

		const auto FaceLandmarks = GetFaceTrackingLandmarkData(InRefinementTargetParams.CurveTrackingPoints);
		const auto ViewportCamera = GetMetaShapeCamera(InRefinementTargetParams.CameraViewInfo, InRefinementTargetParams.ImageSize, "Front");
		const auto KeyPointCorrespondences = GetKeypointCorrespondences(InRefinementTargetParams.KeyPointTargets);
		auto LandmarkConstraints = InMHCBodyAPI->CreateLandmarkConstraints(FaceLandmarks, ViewportCamera);

		bool bBuildOk = false;
		switch (TargetMesh.TargetPartsType)
		{
		case ETargetPartsType::HeadOnly:
			bBuildOk = TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::BuildHeadOnlySolveTarget(
				GetEigenVertices(HeadVerticesDNASpace),
				GetEigenVertexIndices(TargetMesh.HeadVertexIndices),
				KeyPointCorrespondences, LandmarkConstraints,
				OutSolveTarget);
			break;
		case ETargetPartsType::BodyOnly:
			bBuildOk = TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::BuildBodyOnlySolveTarget(
				GetEigenVertices(BodyVerticesDNASpace),
				GetEigenVertexIndices(TargetMesh.BodyVertexIndices),
				KeyPointCorrespondences, LandmarkConstraints,
				OutSolveTarget);
			break;
		case ETargetPartsType::HeadAndBody:
		{
			Eigen::VectorXf HeadTargetVertexWeights;
			bBuildOk = TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::BuildHeadAndBodySolveTarget(
				GetEigenVertices(HeadVerticesDNASpace),
				GetEigenVertexIndices(TargetMesh.HeadVertexIndices),
				GetEigenVertices(BodyVerticesDNASpace),
				GetEigenVertexIndices(TargetMesh.BodyVertexIndices),
				KeyPointCorrespondences, LandmarkConstraints,
				OutSolveTarget);
			break;
		}
		case ETargetPartsType::Combined:
		default:
			bBuildOk = TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::BuildCombinedSolveTarget(
				GetEigenVertices(BodyVerticesDNASpace),
				GetEigenVertexIndices(TargetMesh.BodyVertexIndices),
				KeyPointCorrespondences, LandmarkConstraints,
				OutSolveTarget);
			break;
		}

		if (!bBuildOk)
		{
			return false;
		}

		TArray<float> JointRotationsView;
		if (TargetMesh.BodyJointRotations.Num() > 0)
		{
			JointRotationsView.SetNumUninitialized(TargetMesh.BodyJointRotations.Num() * 3);
			for (int32 i = 0; i < TargetMesh.BodyJointRotations.Num(); ++i)
			{
				JointRotationsView[i * 3 + 0] = TargetMesh.BodyJointRotations[i].X;
				JointRotationsView[i * 3 + 1] = -TargetMesh.BodyJointRotations[i].Y;
				JointRotationsView[i * 3 + 2] = -TargetMesh.BodyJointRotations[i].Z;
			}
		}
		OutSolveTarget.SetJointRotations({JointRotationsView.GetData(), static_cast<std::size_t>(JointRotationsView.Num())});

		return true;
	}
}

struct FMetaHumanCharacterBodyIdentity::FImpl
{
	void Initialize(std::shared_ptr<const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI> InMHCBodyAPI, 
		std::shared_ptr<const TMap<EMetaHumanBodyType, int32>> InBodyTypeLegacyIndexMap,
		const TArray<int32>& InRegionIndice)
	{
		MHCBodyAPI = InMHCBodyAPI;
		BodyTypeLegacyIndexMap = InBodyTypeLegacyIndexMap;
		RegionIndices = InRegionIndice;
		bIsInitialized = true;
	}
	
	std::shared_ptr<const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI> MHCBodyAPI;
	std::shared_ptr<const TMap<EMetaHumanBodyType, int32>> BodyTypeLegacyIndexMap;
	TArray<int32> RegionIndices;
	
	bool bIsInitialized = false;
};

struct FMetaHumanCharacterBodyIdentity::FState::FImpl
{
	std::shared_ptr<const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI> MHCBodyAPI;
	std::shared_ptr<const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> MHCBodyState;
	std::shared_ptr<const TMap<EMetaHumanBodyType, int32>> BodyTypeLegacyIndexMap;
	TArray<int32> RegionIndices;
	EMetaHumanBodyType MetaHumanBodyType = EMetaHumanBodyType::BlendableBody;
	
	FMeshConformIteration MeshConformIterationDelegate;
};

FMetaHumanCharacterBodyIdentity::FMetaHumanCharacterBodyIdentity()
	: Impl(MakePimpl<FImpl>())
{
}

bool FMetaHumanCharacterBodyIdentity::Init(const FString& InModelPath, const FString& InLegacyBodiesPath,  const TSharedPtr<FMetaHumanCharacterIdentity>& InFaceIdentity)
{
#if WITH_EDITORONLY_DATA

	FString BodyPCAModelPath = InModelPath + TEXT("/body_model.dna");
	FString BodySkinModelPath = InModelPath + TEXT("/skin_model.binary");
	FString BodyRBFModelPath = InModelPath + TEXT("/rbf_model.binary");
	
	TArray<uint8> PCAModelBuffer;
	if (!FFileHelper::LoadFileToArray(PCAModelBuffer, *BodyPCAModelPath))
	{
		UE_LOGF(LogMetaHumanCoreTechLib, Error, "failed to load MHC body model");
		return false;
	}
	TSharedPtr<IDNAReader> PCAModelReader = ReadDNAFromBuffer(&PCAModelBuffer, EDNADataLayer::All);
	
	FString CombinedBodyArchetypeFilename = FMetaHumanCommonDataUtils::GetCombinedDNAFilesystemPath();
	TArray<uint8> CombinedDNABuffer;
	if (!FFileHelper::LoadFileToArray(CombinedDNABuffer, *CombinedBodyArchetypeFilename))
	{
		UE_LOGF(LogMetaHumanCoreTechLib, Error, "failed to load MHC body model");
		return false;
	}
	TSharedPtr<IDNAReader> CombinedBodyArchetypeReader = ReadDNAFromBuffer(&CombinedDNABuffer, EDNADataLayer::All);

	FString PhysicsBodiesConfigPath = InModelPath + TEXT("/physics_bodies.json");
	FString PhysicsBodiesMaskPath = InModelPath + TEXT("/bodies_mask.json");
	FString SkinningWeightGenerationConfigPath = InModelPath + TEXT("/body_joint_mapping.json");
	FString LodGenerationDataPath = InModelPath + TEXT("/combined_lod_generation.binary");
	FString RegionsLandmarksPath = InModelPath + TEXT("/region_landmarks.json");
	FString SolvePipelinesPath = InModelPath + TEXT("/pipeline_presets.json");

	std::shared_ptr<TITAN_NAMESPACE::PatchBlendModel<float>> FacePatchBlendModel = nullptr;
	std::shared_ptr<TITAN_NAMESPACE::MeshLandmarks<float>> FaceTrackingLandmarks = nullptr;
	if (InFaceIdentity)
	{
		if (const TITAN_API_NAMESPACE::MetaHumanCreatorAPI* MHCAPI = InFaceIdentity->GetMHCAPIPtr())
		{
			FacePatchBlendModel = MHCAPI->GetFacePatchBlendModel();
			FaceTrackingLandmarks = MHCAPI->GetFaceTrackingLandmarks();
		}
	}

	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI> MHCBodyAPI = TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::CreateMHCBodyApi(
		PCAModelReader->Unwrap(), 
		CombinedBodyArchetypeReader->Unwrap(),
		TCHAR_TO_UTF8(*BodyRBFModelPath),
		TCHAR_TO_UTF8(*BodySkinModelPath),
		TCHAR_TO_UTF8(*SkinningWeightGenerationConfigPath),
		TCHAR_TO_UTF8(*LodGenerationDataPath),
		TCHAR_TO_UTF8(*PhysicsBodiesConfigPath),
		TCHAR_TO_UTF8(*PhysicsBodiesMaskPath),
		TCHAR_TO_UTF8(*RegionsLandmarksPath),
		TCHAR_TO_UTF8(*SolvePipelinesPath),
		FacePatchBlendModel,
		FaceTrackingLandmarks,
		-1);

	TMap<EMetaHumanBodyType, int32> BodyTypeLegacyIndexMap;

	if (!MHCBodyAPI)
	{
		UE_LOGF(LogMetaHumanCoreTechLib, Error, "failed to initialize MHC body API");
		return false;
	}

	// Get indices of regions used to create gizmos
	TArray<int32> RegionIndices;
	const std::vector<std::string>& RegionNames = MHCBodyAPI->GetRegionNames();
	for (int32 RegionIndex = 0; RegionIndex < RegionNames.size(); ++RegionIndex)
	{
		if (RegionNames[RegionIndex].substr(0, 5) != std::string("joint"))
		{
			RegionIndices.Add(RegionIndex);
		}
	}

	// Add legacy bodies
	if (FPaths::DirectoryExists(InLegacyBodiesPath))
	{
		for (uint8 BodyTypeIndex = 0; BodyTypeIndex < uint8(EMetaHumanBodyType::BlendableBody); BodyTypeIndex++)
		{
			EMetaHumanBodyType BodyType = EMetaHumanBodyType(BodyTypeIndex);
			FString BodyTypeName = StaticEnum<EMetaHumanBodyType>()->GetAuthoredNameStringByValue(static_cast<int64>(BodyTypeIndex));
			FString LegacyCombinedDNAPath = InLegacyBodiesPath / BodyTypeName + TEXT(".dna");
			
			TArray<uint8> LegacyDNABuffer;
			FFileHelper::LoadFileToArray(LegacyDNABuffer, *LegacyCombinedDNAPath);

			if (!LegacyDNABuffer.IsEmpty())
			{
				TSharedPtr<IDNAReader> LegacyCombinedDNAReader = ReadDNAFromBuffer(&LegacyDNABuffer, EDNADataLayer::All);

				std::string StdStringBodyTypeName(TCHAR_TO_UTF8(*BodyTypeName));
				if (LegacyCombinedDNAReader)
				{
					MHCBodyAPI->AddLegacyBody(LegacyCombinedDNAReader->Unwrap(), StdStringBodyTypeName);
					BodyTypeLegacyIndexMap.Add(BodyType, MHCBodyAPI->NumLegacyBodies() - 1);
				}
				else
				{
					UE_LOGF(LogMetaHumanCoreTechLib, Error, "failed to initialize MHC legacy body type %ls", *BodyTypeName);
				}
			}
			else
			{
				UE_LOGF(LogMetaHumanCoreTechLib, Error, "Failed to load MHC legacy body type %ls dna file. Please make sure the file exists.", *BodyTypeName);
			}
		}
	}

	std::shared_ptr<const TMap<EMetaHumanBodyType, int32>> BodyTypeMap = std::make_shared<const TMap<EMetaHumanBodyType, int32>>(BodyTypeLegacyIndexMap);
	std::shared_ptr<const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI> ConstBodyAPI = 
		std::static_pointer_cast<const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI>(MHCBodyAPI);
	Impl->Initialize(ConstBodyAPI, BodyTypeMap, RegionIndices);

	return true;
#else
	UE_LOGF(LogMetaHumanCoreTechLib, Error, "body shape editor API only works with EditorOnly Data ");
	return false;
#endif
}

FMetaHumanCharacterBodyIdentity::~FMetaHumanCharacterBodyIdentity() = default;

TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> FMetaHumanCharacterBodyIdentity::CreateState() const
{
	if (!Impl->bIsInitialized) return nullptr;

	TSharedPtr<FState, ESPMode::ThreadSafe> State = MakeShared<FState>();
	State->Impl->MHCBodyState = Impl->MHCBodyAPI->CreateState();
	State->Impl->MHCBodyAPI = Impl->MHCBodyAPI;
	State->Impl->BodyTypeLegacyIndexMap = Impl->BodyTypeLegacyIndexMap;
	State->Impl->RegionIndices = Impl->RegionIndices;

	return State;
}

FMetaHumanCharacterBodyIdentity::FState::FState()
{
	Impl = MakePimpl<FImpl>();
}

FMetaHumanCharacterBodyIdentity::FState::~FState() = default;

FMetaHumanCharacterBodyIdentity::FState::FState(const FState& InOther)
{
	Impl = MakePimpl<FImpl>(*InOther.Impl);
}

TArray<FMetaHumanCharacterBodyConstraint> FMetaHumanCharacterBodyIdentity::FState::GetBodyConstraints(bool bScaleMeasurementRangesWithHeight) const
{
	check(Impl->MHCBodyAPI);
	check(Impl->MHCBodyState);

	TArray<FMetaHumanCharacterBodyConstraint> BodyConstraints;

	int ConstraintsNum = Impl->MHCBodyState->GetConstraintNum();
	BodyConstraints.AddUninitialized( StaticCast<int32>(ConstraintsNum));
	av::ConstArrayView<float> Measurements =  Impl->MHCBodyState->GetMeasurements();

	std::vector<float> MinValues;
	std::vector<float> MaxValues;
	MinValues.resize(ConstraintsNum);
	MaxValues.resize(ConstraintsNum);
	Impl->MHCBodyAPI->EvaluateConstraintRange(*(Impl->MHCBodyState), MinValues, MaxValues, bScaleMeasurementRangesWithHeight);

	for (int ConstraintIndex = 0; ConstraintIndex < ConstraintsNum; ConstraintIndex++)
	{
		FMetaHumanCharacterBodyConstraint BodyConstraint;
		std::string StdConstraintName(Impl->MHCBodyState->GetConstraintName(ConstraintIndex));
		BodyConstraint.Name = UTF8_TO_TCHAR(StdConstraintName.c_str());

		float TargetMeasurement = 0.f;
		bool bIsActive = Impl->MHCBodyState->GetConstraintTarget(ConstraintIndex, TargetMeasurement);
		BodyConstraint.bIsActive = bIsActive;

		if (bIsActive)
		{
			BodyConstraint.TargetMeasurement = TargetMeasurement;
		}
		else
		{
			BodyConstraint.TargetMeasurement = Measurements[ConstraintIndex];
		}

		BodyConstraint.MinMeasurement = MinValues[ConstraintIndex];
		BodyConstraint.MaxMeasurement = MaxValues[ConstraintIndex];
		BodyConstraints[ConstraintIndex] = BodyConstraint;
	}

	return BodyConstraints;
}

void FMetaHumanCharacterBodyIdentity::FState::EvaluateBodyConstraints(const TArray<FMetaHumanCharacterBodyConstraint>& BodyConstraints)
{
	check(Impl->MHCBodyAPI);
	check(Impl->MHCBodyState);

	TArray<FVector3f> Out;

	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState;
	NewBodyShapeState = Impl->MHCBodyState->Clone();

	for (int32 ConstraintIndex = 0; ConstraintIndex < BodyConstraints.Num(); ConstraintIndex++)
	{
		if (BodyConstraints[ConstraintIndex].bIsActive)
		{
			NewBodyShapeState->SetConstraintTarget(StaticCast<int>(ConstraintIndex), BodyConstraints[ConstraintIndex].TargetMeasurement);
		}
		else
		{
			NewBodyShapeState->RemoveConstraintTarget(StaticCast<int>(ConstraintIndex));
		}
	}
	
	Impl->MHCBodyAPI->Evaluate(*NewBodyShapeState);
	Impl->MHCBodyState = NewBodyShapeState;

}

FMetaHumanRigEvaluatedState FMetaHumanCharacterBodyIdentity::FState::GetVerticesAndVertexNormals() const
{
	check(Impl->MHCBodyState);

	FMetaHumanRigEvaluatedState Out;

	int32 NumVertices = 0;
	for (int32 Lod = 0; Lod < Impl->MHCBodyAPI->NumLODs(); ++Lod)
	{
		NumVertices += Impl->MHCBodyState->GetMesh(Lod).size() / 3;
	}
	Out.Vertices.AddUninitialized(NumVertices);
	Out.VertexNormals.AddUninitialized(NumVertices);

	// concatenate the vertices from all lods
	float* VerticesDataPtr = (float*)(Out.Vertices.GetData());
	float* VertexNormalsDataPtr = (float*)(Out.VertexNormals.GetData());
	size_t Count = 0;
	for (int Lod = 0; Lod < Impl->MHCBodyAPI->NumLODs(); ++Lod) 
	{
		av::ConstArrayView<float> CurMesh = Impl->MHCBodyState->GetMesh(Lod);
		FMemory::Memcpy(VerticesDataPtr, CurMesh.data(), CurMesh.size() * sizeof(float));
		VerticesDataPtr += CurMesh.size();
		av::ConstArrayView<float> CurMeshVertexNormals = Impl->MHCBodyState->GetMeshNormals(Lod);
		FMemory::Memcpy(VertexNormalsDataPtr, CurMeshVertexNormals.data(), CurMeshVertexNormals.size() * sizeof(float));
		VertexNormalsDataPtr += CurMeshVertexNormals.size();
	}

	return Out;
}

void FMetaHumanCharacterBodyIdentity::FState::GetVerticesWithAndWithoutDeltas(FMetaHumanRigEvaluatedState& OutNoDelta, FMetaHumanRigEvaluatedState& OutWithDelta) const
{
	check(Impl->MHCBodyState);

	const TSharedRef<FMetaHumanCharacterBodyIdentity::FState> TempState = MakeShared<FMetaHumanCharacterBodyIdentity::FState>(*this);

	TempState->SetGlobalDeltaScale(0.0f);
	OutNoDelta = TempState->GetVerticesAndVertexNormals();

	TempState->SetGlobalDeltaScale(1.0f);
	OutWithDelta = TempState->GetVerticesAndVertexNormals();
}

TArray<int32> FMetaHumanCharacterBodyIdentity::FState::GetNumVerticesPerLOD() const
{
	TArray<int32> NumVerticesPerLOD;
	check(Impl->MHCBodyAPI);
	NumVerticesPerLOD.SetNumUninitialized(Impl->MHCBodyAPI->NumLODs());

	for (int32 Lod = 0; Lod < Impl->MHCBodyAPI->NumLODs(); ++Lod)
	{
		NumVerticesPerLOD[Lod] = Impl->MHCBodyState->GetMesh(Lod).size() / 3;
	}

	return NumVerticesPerLOD;
}


TArray<int32> FMetaHumanCharacterBodyIdentity::FState::GetTrianglesIndices() const
{
	check(Impl);
	check(Impl->MHCBodyAPI);
	
	TArray<int32> TrianglesIndices;
	
	constexpr int32 LodIndex = 0;
	av::ConstArrayView<int> Triangles = Impl->MHCBodyAPI->GetTriangles(LodIndex);
	
	TrianglesIndices.AddUninitialized(Triangles.size());
	int32* TrianglesIndicesPtr = (int32*)(TrianglesIndices.GetData());
	
	FMemory::Memcpy(TrianglesIndicesPtr, Triangles.data(), Triangles.size() * sizeof(int));
	
	return TrianglesIndices;
}


FVector3f FMetaHumanCharacterBodyIdentity::FState::GetVertex(const TArray<FVector3f>& InVertices, int32 InDNAMeshIndex, int32 InDNAVertexIndex) const
{
	check(Impl->MHCBodyAPI);

	float Out[3];
	const float* DataPtr = (const float*)(InVertices.GetData());

	for (int32 Lod = 0; Lod < InDNAMeshIndex; ++Lod)
	{
		DataPtr += Impl->MHCBodyState->GetMesh(Lod).size();
	}
	ensure(Impl->MHCBodyAPI->GetVertex(InDNAMeshIndex, DataPtr, InDNAVertexIndex, Out));
	return FVector3f{ Out[0], Out[2], Out[1] };
}

TArray<FVector3f> FMetaHumanCharacterBodyIdentity::FState::GetRegionGizmos() const
{
	TArray<FVector3f> Out;
	Out.AddUninitialized(Impl->MHCBodyAPI->NumGizmos());
	ensure(Impl->MHCBodyAPI->EvaluateGizmos(*(Impl->MHCBodyState), (float*)Out.GetData()));

	for (int32 I = 0; I < Out.Num(); ++I)
	{
		Out[I] = FVector3f{ Out[I].X, Out[I].Z, Out[I].Y };
	}

	return Out;
}

static coretechlib::titan::api::MetaHumanCreatorBodyAPI::BodyAttribute UEBodyBlendOptionsToTitanBodyAttribute(EBodyBlendOptions InBodyBlendOptions)
{
	switch (InBodyBlendOptions)
	{
	case EBodyBlendOptions::Skeleton:
			return coretechlib::titan::api::MetaHumanCreatorBodyAPI::BodyAttribute::Skeleton;
	case EBodyBlendOptions::Shape:
		return coretechlib::titan::api::MetaHumanCreatorBodyAPI::BodyAttribute::Shape;
	case EBodyBlendOptions::Both:
		return coretechlib::titan::api::MetaHumanCreatorBodyAPI::BodyAttribute::Both;
	default:
		check(false);
	}

	return coretechlib::titan::api::MetaHumanCreatorBodyAPI::BodyAttribute::Both;
}

void FMetaHumanCharacterBodyIdentity::FState::BlendPresets(int32 InGizmoIndex, const TArray<TPair<float, const FState*>>& InStates, EBodyBlendOptions InBodyBlendOptions)
{
	check(Impl->MHCBodyState);

	if (InStates.Num() > 0)
	{
		std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyState->Clone();

		std::vector<std::pair<float, const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State*>> InnerStates;
		for (int32 PresetIndex = 0; PresetIndex < InStates.Num(); PresetIndex++)
		{
			std::pair<float, const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State*> Preset { InStates[PresetIndex].Key, InStates[PresetIndex].Value->Impl->MHCBodyState.get() };
			InnerStates.emplace_back(Preset);
		}
		int32 RegionIndex = (InGizmoIndex == INDEX_NONE) ? -1 : Impl->RegionIndices[InGizmoIndex]; 
		ensure(Impl->MHCBodyAPI->Blend(*NewBodyShapeState, RegionIndex, InnerStates,UEBodyBlendOptionsToTitanBodyAttribute(InBodyBlendOptions)));
		Impl->MHCBodyState = NewBodyShapeState;
	}
}

int32 FMetaHumanCharacterBodyIdentity::FState::SelectVertex(FVector3f InOrigin, FVector3f InDirection, FVector3f& OutVertex, FVector3f& OutNormal) const
{
	check(Impl->MHCBodyState);

	Eigen::Vector3f Origin(InOrigin[0], InOrigin[2], InOrigin[1]);
	Eigen::Vector3f Direction(InDirection[0], InDirection[2], InDirection[1]);
	Eigen::Vector3f Vertex;
	Eigen::Vector3f Normal;

	int32 VertexID = static_cast<int32>(Impl->MHCBodyAPI->SelectVertex(*Impl->MHCBodyState, Origin, Direction, Vertex, Normal));
	if (VertexID != INDEX_NONE)
	{
		OutVertex = FVector3f{ Vertex[0], Vertex[2], Vertex[1] };
		OutNormal = FVector3f{ Normal[0], Normal[2], Normal[1] };
	}
	return VertexID;
}

int32 FMetaHumanCharacterBodyIdentity::FState::GetNumberOfConstraints() const
{
	check(Impl->MHCBodyState);

	return StaticCast<int32>(Impl->MHCBodyState->GetConstraintNum());
}

float FMetaHumanCharacterBodyIdentity::FState::GetMeasurement(int32 ConstraintIndex) const
{
	check(Impl->MHCBodyState);

	return Impl->MHCBodyState->GetMeasurements()[ConstraintIndex];
}

void FMetaHumanCharacterBodyIdentity::FState::GetMeasurementsForFaceAndBody(TSharedRef<IDNAReader> InFaceDNA, TSharedRef<IDNAReader> InBodyDNA, TMap<FString, float>& OutMeasurements) const
{
	auto GetVerticesFromDNA = [](TSharedRef<IDNAReader> InDNA, uint16 InMeshIndex)
	{
		const uint32 VertexCount = InDNA->GetVertexPositionCount(InMeshIndex);
		TConstArrayView<float> Xs = InDNA->GetVertexPositionXs(InMeshIndex);
		TConstArrayView<float> Ys = InDNA->GetVertexPositionYs(InMeshIndex);
		TConstArrayView<float> Zs = InDNA->GetVertexPositionZs(InMeshIndex);

		// API expects Y up, but DNA reader swaps Y and Z, so we need to swap coordinates back
		Eigen::Matrix3Xf Result(3, VertexCount);
		Result.row(0) = Eigen::Map<const Eigen::RowVectorXf>(Xs.GetData(), Xs.Num());
		Result.row(1) = Eigen::Map<const Eigen::RowVectorXf>(Zs.GetData(), Zs.Num());
		Result.row(2) = Eigen::Map<const Eigen::RowVectorXf>(Ys.GetData(), Ys.Num());

		return Result;
	};

	constexpr uint16 MeshIndex = 0;
	const Eigen::Matrix3Xf FaceVertices = GetVerticesFromDNA(InFaceDNA, MeshIndex);
	const Eigen::Matrix3Xf BodyVertices = GetVerticesFromDNA(InBodyDNA, MeshIndex);

	Eigen::VectorXf Measurements;
	std::vector<std::string> MeasurementNames;

	Impl->MHCBodyAPI->GetMeasurements(FaceVertices, BodyVertices, Measurements, MeasurementNames);
	check(Measurements.size() == MeasurementNames.size());

	OutMeasurements.Reserve(static_cast<int32>(MeasurementNames.size()));

	for (size_t i = 0; i < MeasurementNames.size(); ++i)
	{
		OutMeasurements.Emplace(UTF8_TO_TCHAR(MeasurementNames[i].c_str()), Measurements[i]);
	}
}

void FMetaHumanCharacterBodyIdentity::FState::GetMeasurementsForFaceAndBody(const TArray<FVector3f>& InFaceRawVertices, TMap<FString, float>& OutMeasurements) const
{
	check(Impl->MHCBodyState);
	
	constexpr uint16 MeshIndex = 0;
	av::ConstArrayView<float> CombinedMesh = Impl->MHCBodyState->GetMesh(MeshIndex);
	Eigen::Matrix<float, 3, -1> CombinedVertices = Eigen::Map<const Eigen::Matrix<float, 3, -1>>((const float*)CombinedMesh.data(), 3, CombinedMesh.size() / 3u);
	
	if (InFaceRawVertices.Num() > 0 && InFaceRawVertices.Num() <= CombinedVertices.cols())
	{
		const Eigen::Map<const Eigen::Matrix<float, 3, -1>> FaceVertices = UE::MetaHuman::GetEigenVertices(InFaceRawVertices);
		CombinedVertices.leftCols(FaceVertices.cols()) = FaceVertices;
	}

	Eigen::VectorXf Measurements;
	std::vector<std::string> MeasurementNames;

	Impl->MHCBodyAPI->GetMeasurements(CombinedVertices, Measurements, MeasurementNames);
	check(Measurements.size() == MeasurementNames.size());

	OutMeasurements.Reserve(static_cast<int32>(MeasurementNames.size()));

	for (size_t i = 0; i < MeasurementNames.size(); ++i)
	{
		OutMeasurements.Emplace(UTF8_TO_TCHAR(MeasurementNames[i].c_str()), Measurements[i]);
	}
}

TArray<FVector> FMetaHumanCharacterBodyIdentity::FState::GetContourVertices(int32 ConstraintIndex) const
{
	check(Impl->MHCBodyState);

	TArray<FVector> Out;

	const Eigen::Matrix3Xf ContourVertices = Impl->MHCBodyState->GetContourVertices(ConstraintIndex);;

	for (int32 ContourValueIndex = 0; ContourValueIndex < (int32)ContourVertices.cols(); ContourValueIndex++)
	{
		Out.Add({ ContourVertices(0, ContourValueIndex), ContourVertices(2, ContourValueIndex), ContourVertices(1, ContourValueIndex) });
	}
	
	return Out;
}

TArray<FMatrix44f> FMetaHumanCharacterBodyIdentity::FState::CopyBindPose() const
{
	check(Impl->MHCBodyState);

	TArray<FMatrix44f> Out;

	av::ConstArrayView<float> BindPose = Impl->MHCBodyState->GetBindPose();
	Out.AddUninitialized(BindPose.size() / 16);
	FMemory::Memcpy((float*)Out.GetData(), BindPose.data(), BindPose.size()*sizeof(float));

	return Out;
}

TArray<FMatrix44f> FMetaHumanCharacterBodyIdentity::FState::CopyComponentPose() const
{
	check(Impl->MHCBodyState);

	TArray<FMatrix44f> Out;

	av::ConstArrayView<float> ComponentPose = Impl->MHCBodyState->GetWorldPose();
	Out.AddUninitialized(ComponentPose.size() / 16);
	FMemory::Memcpy((float*)Out.GetData(), ComponentPose.data(), ComponentPose.size()*sizeof(float));

	return Out;
}

void FMetaHumanCharacterBodyIdentity::FState::SetEvaluatePose(bool bEvaluatePose)
{
	if (Impl->MetaHumanBodyType == EMetaHumanBodyType::BlendableBody)
	{
		std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyState->Clone();
		Impl->MHCBodyAPI->UpdateEvaluatePose(*NewBodyShapeState, bEvaluatePose);
		Impl->MHCBodyState = NewBodyShapeState;
	}
}

void FMetaHumanCharacterBodyIdentity::FState::SetApplyFloorOffset(bool bApplyFloorOffset)
{
	if (Impl->MetaHumanBodyType == EMetaHumanBodyType::BlendableBody)
	{
		std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyState->Clone();
		Impl->MHCBodyAPI->UpdateApplyFloorOffset(*NewBodyShapeState, bApplyFloorOffset);
		Impl->MHCBodyState = NewBodyShapeState;
	}
}

int32 FMetaHumanCharacterBodyIdentity::FState::GetNumberOfJoints() const
{
	check(Impl->MHCBodyAPI);

	return Impl->MHCBodyAPI->NumJoints();
}

void FMetaHumanCharacterBodyIdentity::FState::GetNeutralJointTransform(int32 JointIndex, FVector3f& OutJointTranslation, FRotator3f& OutJointRotation) const
{
	check(Impl->MHCBodyState);
	check(JointIndex == FMath::Clamp(JointIndex, 0, MAX_uint16));

	Eigen::Vector3f Translation;
	Eigen::Vector3f Rotation;
	Impl->MHCBodyAPI->GetNeutralJointTransform(*Impl->MHCBodyState, static_cast<uint16>(JointIndex), Translation, Rotation);

	OutJointTranslation = FVector3f(Translation.x(), Translation.y(), Translation.z());
	OutJointRotation = FRotator3f(Rotation.x(), Rotation.y(), Rotation.z());
}

void FMetaHumanCharacterBodyIdentity::FState::GetNeutralJointTransforms(const TArray<FMatrix44f>& InBindPoseMatrices, TArray<FVector3f>& OutJointTranslations, TArray<FRotator3f>& OutJointRotations) const
{
	OutJointTranslations.SetNumUninitialized(InBindPoseMatrices.Num());
	OutJointRotations.SetNumUninitialized(InBindPoseMatrices.Num());
	
	std::vector<Eigen::Transform<float, 3, Eigen::Affine>> BindPoseTransforms;	
	BindPoseTransforms.resize(InBindPoseMatrices.Num());
	FMemory::Memcpy((float*)BindPoseTransforms.data(), InBindPoseMatrices.GetData(), InBindPoseMatrices.Num() * 16 * sizeof(float));
	
	for (int JointIndex = 0; JointIndex < InBindPoseMatrices.Num(); ++JointIndex)
	{
		Eigen::Vector3f Translation;
		Eigen::Vector3f Rotation;
		Impl->MHCBodyAPI->GetNeutralJointTransform(BindPoseTransforms, JointIndex, Translation, Rotation);
		
		OutJointTranslations[JointIndex] = FVector3f(Translation.x(), Translation.y(), Translation.z());
		OutJointRotations[JointIndex] = FRotator3f(Rotation.x(), Rotation.y(), Rotation.z());
	}
}

void FMetaHumanCharacterBodyIdentity::FState::CopyCombinedModelVertexInfluenceWeights(TArray<TPair<int32, TArray<FFloatTriplet>>>& OutCombinedModelVertexInfluenceWeights) const
{
	check(Impl->MHCBodyState);

	std::vector<TITAN_NAMESPACE::SparseMatrix<float>> VertexInfluenceWeights;

	Impl->MHCBodyAPI->GetVertexInfluenceWeights(*Impl->MHCBodyState, VertexInfluenceWeights);
	OutCombinedModelVertexInfluenceWeights.SetNum(int32(VertexInfluenceWeights.size()));

	for (int32 Lod = 0; Lod < int32(VertexInfluenceWeights.size()); ++Lod)
	{
		OutCombinedModelVertexInfluenceWeights[Lod] = TPair<int32, TArray<FFloatTriplet>>(int32(VertexInfluenceWeights[size_t(Lod)].rows()), TArray<FFloatTriplet>());

		for (int k = 0; k < VertexInfluenceWeights[size_t(Lod)].outerSize(); ++k)
		{
			for (TITAN_NAMESPACE::SparseMatrix<float>::InnerIterator it(VertexInfluenceWeights[size_t(Lod)], k); it; ++it) 
			{
				OutCombinedModelVertexInfluenceWeights[Lod].Value.Add(FFloatTriplet(it.row(), it.col(), it.value()));
			}
		}
	}
}

void FMetaHumanCharacterBodyIdentity::FState::SetCombinedModelVertexInfluenceWeightsLOD0(TArray<FFloatTriplet> InCombinedModelVertexInfluenceWeightsLOD0) 
{
	check(Impl->MHCBodyState);
	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyState->Clone();
	int32 MaxRow = 0, MaxCol = 0;

	std::vector<Eigen::Triplet<float>> Triplets;
	Triplets.reserve(InCombinedModelVertexInfluenceWeightsLOD0.Num());

	for (const FFloatTriplet& T : InCombinedModelVertexInfluenceWeightsLOD0)
	{
		MaxRow = FMath::Max(MaxRow, T.Row);
		MaxCol = FMath::Max(MaxCol, T.Col);
		Triplets.emplace_back(T.Row, T.Col, T.Value);
	}

	TITAN_NAMESPACE::SparseMatrix<float> SparseMat(MaxRow + 1, MaxCol + 1);
	SparseMat.setFromTriplets(Triplets.begin(), Triplets.end());
	NewBodyShapeState->SetCustomVertexInfluenceWeightsLOD0(SparseMat);
	Impl->MHCBodyState = NewBodyShapeState;
}

void FMetaHumanCharacterBodyIdentity::FState::GetCombinedModelVertexInfluenceWeightsLOD0(TArray<FFloatTriplet> & OutCombinedModelVertexInfluenceWeightsLOD0) const
{
	check(Impl->MHCBodyState);
	std::vector<TITAN_NAMESPACE::SparseMatrix<float>> VertexInfluenceWeights;
	Impl->MHCBodyAPI->GetVertexInfluenceWeights(*Impl->MHCBodyState, VertexInfluenceWeights);
	OutCombinedModelVertexInfluenceWeightsLOD0.Reset();
	OutCombinedModelVertexInfluenceWeightsLOD0.Reserve(VertexInfluenceWeights.size());

	for (int k = 0; k < VertexInfluenceWeights[size_t(0)].outerSize(); ++k)
	{
		for (TITAN_NAMESPACE::SparseMatrix<float>::InnerIterator it(VertexInfluenceWeights[0], k); it; ++it) 
		{
			OutCombinedModelVertexInfluenceWeightsLOD0.Add(FFloatTriplet(it.row(), it.col(), it.value()));
		}
	}
}

void FMetaHumanCharacterBodyIdentity::FState::Reset()
{
	check(Impl->MHCBodyState);

	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyAPI->CreateState();
	Impl->MHCBodyState = NewBodyShapeState;
	Impl->MetaHumanBodyType = EMetaHumanBodyType::BlendableBody;
}

void FMetaHumanCharacterBodyIdentity::FState::ResetFaceModel()
{
	check(Impl->MHCBodyState);
	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyState->Clone();
	Impl->MHCBodyAPI->ResetFacePatchBlendModel(*NewBodyShapeState);
	
	if (NewBodyShapeState->AreGuiControlsZero())
	{
		Impl->MHCBodyAPI->ResetNeckSeam(*NewBodyShapeState);
	}
	else
	{
		TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::AdaptNeckSeamParams NeckSeamParams;
		NeckSeamParams.seamLockSide = TITAN_NAMESPACE::SeamLockSide::Body;
		Impl->MHCBodyAPI->AdaptNeckSeam(*NewBodyShapeState, NeckSeamParams);
	}
	
	Impl->MHCBodyState = NewBodyShapeState;
}

void FMetaHumanCharacterBodyIdentity::FState::ResetBodyOnly()
{
	check(Impl->MHCBodyState);
	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyState->Clone();
	Impl->MHCBodyAPI->ResetGuiControls(*NewBodyShapeState);
	Impl->MHCBodyAPI->ResetScale(*NewBodyShapeState);
	Impl->MHCBodyAPI->ClearJointDeltas(*NewBodyShapeState);

	if (Impl->MHCBodyAPI->AreFacePatchBlendModelParametersDefault(*NewBodyShapeState))
	{
		Impl->MHCBodyAPI->ResetNeckSeam(*NewBodyShapeState);
	}
	else
	{
		TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::AdaptNeckSeamParams NeckSeamParams;
		NeckSeamParams.seamLockSide = TITAN_NAMESPACE::SeamLockSide::Face;
		Impl->MHCBodyAPI->AdaptNeckSeam(*NewBodyShapeState, NeckSeamParams);
	}
	 Impl->MHCBodyState = NewBodyShapeState;
}

void FMetaHumanCharacterBodyIdentity::FState::ClearVertexDeltas()
{
	check(Impl->MHCBodyState);
	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyState->Clone();
	Impl->MHCBodyAPI->ClearVertexDeltas(*NewBodyShapeState);
	Impl->MHCBodyState = NewBodyShapeState;
}

EMetaHumanBodyType FMetaHumanCharacterBodyIdentity::FState::GetMetaHumanBodyType() const
{
	return Impl->MetaHumanBodyType;
}

void FMetaHumanCharacterBodyIdentity::FState::SetMetaHumanBodyType(EMetaHumanBodyType InMetaHumanBodyType, bool bFitFromLegacy)
{
	EMetaHumanBodyType PreviousBodyType = Impl->MetaHumanBodyType;
	Impl->MetaHumanBodyType = InMetaHumanBodyType;

	if (InMetaHumanBodyType != EMetaHumanBodyType::BlendableBody)
	{
		if (const int32* LegacyBodyTypeIndex = Impl->BodyTypeLegacyIndexMap->Find(InMetaHumanBodyType))
		{
			std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyState->Clone();
			Impl->MHCBodyAPI->SelectLegacyBody(*NewBodyShapeState, static_cast<int>(*LegacyBodyTypeIndex), false);
			Impl->MHCBodyState = NewBodyShapeState;
		}
		else
		{
			FString BodyTypeName;
			UEnum::GetValueAsString(InMetaHumanBodyType).Split("::", nullptr, &BodyTypeName);
			UE_LOGF(LogMetaHumanCoreTechLib, Warning, "failed to find legacy dna body type %ls", *BodyTypeName);
		}
	}
	else if (InMetaHumanBodyType == EMetaHumanBodyType::BlendableBody && bFitFromLegacy)
	{
		if (const int32* LegacyBodyTypeIndex = Impl->BodyTypeLegacyIndexMap->Find(PreviousBodyType))
		{
			std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyState->Clone();
			Impl->MHCBodyAPI->SelectLegacyBody(*NewBodyShapeState, static_cast<int>(*LegacyBodyTypeIndex), true);
			Impl->MHCBodyState = NewBodyShapeState;
		}
	}
}

#if WITH_EDITORONLY_DATA
bool FMetaHumanCharacterBodyIdentity::FState::FitToBodyDna(TSharedRef<class IDNAReader> InBodyDna, EMetaHumanCharacterBodyFitOptions InBodyFitOptions)
{
	check(Impl->MHCBodyState);

	TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::FitToTargetOptions FitToTargetOptions;
	FitToTargetOptions.enforceAnatomicalPose = false;

	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState = Impl->MHCBodyState->Clone();

	if (!Impl->MHCBodyAPI->FitToTarget(*NewBodyState, FitToTargetOptions, InBodyDna->Unwrap()))
	{
		return false;
	}
	if (InBodyFitOptions == EMetaHumanCharacterBodyFitOptions::FitFromMeshAndSkeleton)
	{
		Eigen::Matrix<float, 3, -1> JointsEigen;
		JointsEigen.conservativeResize(3, InBodyDna->GetJointCount());
		for (int32 I = 0; I < JointsEigen.cols(); ++I)
		{
			auto dnaJoint= InBodyDna->Unwrap()->getNeutralJointTranslation(I); 
			JointsEigen(0, I) = dnaJoint.x;
			JointsEigen(1, I) = dnaJoint.y;
			JointsEigen(2, I) = dnaJoint.z;
		}
		Impl->MHCBodyAPI->SetNeutralJointsTranslations(*NewBodyState, JointsEigen);
	}
	else if (InBodyFitOptions == EMetaHumanCharacterBodyFitOptions::FitFromMeshToFixedSkeleton)
	{
		auto JointsFromState =  Impl->MHCBodyState->GetBindPose();
		const Eigen::Map<const Eigen::Matrix<float, 3, -1>> JointsEigen((const float*)JointsFromState.data(), 3, JointsFromState.size() / 3u);
		Impl->MHCBodyAPI->SetNeutralJointsTranslations(*NewBodyState, JointsEigen);
	}

	Impl->MHCBodyAPI->SetVertexDeltaScale(*NewBodyState, 1.0f);
	Impl->MHCBodyState = NewBodyState;

	return true;
}

bool FMetaHumanCharacterBodyIdentity::FState::FitToTarget(const TArray<FVector3f>& InVertices, const TArray<FVector3f>& InComponentJointTranslations, EMetaHumanCharacterBodyFitOptions InBodyFitOptions)
{
	check(Impl->MHCBodyState);

	TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::FitToTargetOptions FitToTargetOptions;
	FitToTargetOptions.enforceAnatomicalPose = false;

	TArray<FVector3f> VerticesDNASpace = UE::MetaHuman::GetVerticesDNASpace(InVertices);
	const Eigen::Map<const Eigen::Matrix<float, 3, -1>> VerticesEigen = UE::MetaHuman::GetEigenVertices(VerticesDNASpace);
	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState = Impl->MHCBodyState->Clone();
	if (!Impl->MHCBodyAPI->FitToTarget(*NewBodyState, FitToTargetOptions, VerticesEigen))
	{
		return false;
	}
	if (InBodyFitOptions == EMetaHumanCharacterBodyFitOptions::FitFromMeshAndSkeleton)
	{
		TArray<FVector3f> JointTranslationsDNASpace;
		JointTranslationsDNASpace.AddUninitialized(InComponentJointTranslations.Num());
		for (int32 I = 0; I < InComponentJointTranslations.Num(); ++I)
		{
			JointTranslationsDNASpace[I] = FVector3f{ InComponentJointTranslations[I].X, InComponentJointTranslations[I].Z, InComponentJointTranslations[I].Y };
		}
		const Eigen::Map<const Eigen::Matrix<float, 3, -1>> JointsEigen((const float*)JointTranslationsDNASpace.GetData(), 3, JointTranslationsDNASpace.Num());
		Impl->MHCBodyAPI->SetNeutralJointsTranslations(*NewBodyState, JointsEigen);
	}
	else if (InBodyFitOptions == EMetaHumanCharacterBodyFitOptions::FitFromMeshToFixedSkeleton)
	{
		auto JointsFromState =  Impl->MHCBodyState->GetBindPose();
		const Eigen::Map<const Eigen::Matrix<float, 3, -1>> JointsEigen((const float*)JointsFromState.data(), 3, JointsFromState.size() / 3u);
		Impl->MHCBodyAPI->SetNeutralJointsTranslations(*NewBodyState, JointsEigen);
	}
	Impl->MHCBodyAPI->SetVertexDeltaScale(*NewBodyState, 1.0f);
	Impl->MHCBodyState = NewBodyState;

	return true;
}
#endif // WITH_EDITORONLY_DATA


bool FMetaHumanCharacterBodyIdentity::FState::Conform(const TArray<FVector3f>& InVertices, const TArray<FVector3f>& InJointRotations, bool bTargetIsInAPose, bool  bEstimateJointsFromMesh)
{
	if (InVertices.IsEmpty())
	{
		UE_LOGF(LogMetaHumanCoreTechLib, Error, "Conform failed. Input vertices is empty");
		return false;	
	}

	TArray<FVector3f> VerticesDNASpace = UE::MetaHuman::GetVerticesDNASpace(InVertices);
	const Eigen::Map<const Eigen::Matrix<float, 3, -1>> VerticesEigen = UE::MetaHuman::GetEigenVertices(VerticesDNASpace);
	TArray<float> JointRotationsView;
	if (InJointRotations.Num() > 0)
	{
		JointRotationsView.SetNumUninitialized(InJointRotations.Num() * 3);
		for (int32 i = 0; i < InJointRotations.Num(); ++i)
		{
			JointRotationsView[i * 3 + 0] = InJointRotations[i].X;
			JointRotationsView[i * 3 + 1] = -InJointRotations[i].Y;
			JointRotationsView[i * 3 + 2] = -InJointRotations[i].Z;
		}	
	}
	TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::FitToTargetOptions options;
	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState =Impl->MHCBodyAPI->CreateState();
	options.isAPose = bTargetIsInAPose;
	const bool bApplyFloorOffset = !bTargetIsInAPose; // apply floor offset only when reposing to A Pose
	NewBodyState->SetApplyFloorOffset(bApplyFloorOffset);
	Impl->MHCBodyAPI->ClearVertexDeltas(*NewBodyState);

	auto IterationUpdate = [this] (const av::ConstArrayView<float> InVerticesArray, const av::ConstArrayView<float> InNormalsArray, const av::ConstArrayView<float> InBindPose, int InIterationCount, TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::ESolveStepType InSolveStepType) -> bool
	{
		FMetaHumanRigEvaluatedState IterationState;

		IterationState.Vertices.AddUninitialized(InVerticesArray.size() / 3);
		float* VerticesDataPtr = (float*)(IterationState.Vertices.GetData());
		FMemory::Memcpy(VerticesDataPtr, InVerticesArray.data(), InVerticesArray.size() * sizeof(float));

		IterationState.VertexNormals.AddUninitialized(InNormalsArray.size() / 3);
		float* NormalsDataPtr = (float*)(IterationState.VertexNormals.GetData());
		FMemory::Memcpy(NormalsDataPtr, InNormalsArray.data(), InNormalsArray.size() * sizeof(float));

		TArray<FMatrix44f> BindPoseMatrices;
		BindPoseMatrices.AddUninitialized(InBindPose.size() / 16);
		FMemory::Memcpy((float*)BindPoseMatrices.GetData(), InBindPose.data(), InBindPose.size()*sizeof(float));

		if (Impl->MeshConformIterationDelegate.IsBound())
		{
			return Impl->MeshConformIterationDelegate.Execute(IterationState, BindPoseMatrices, InIterationCount, static_cast<ESolveStepType>(InSolveStepType));
		}
		
		return true;
	};
	
	if (!Impl->MHCBodyAPI->FitToTarget(*NewBodyState, options, VerticesEigen, {JointRotationsView.GetData(), static_cast<std::size_t>(JointRotationsView.Num())}, IterationUpdate))
	{
		return false;
	}
	if (bEstimateJointsFromMesh)
	{
		Impl->MHCBodyAPI->VolumetricallyFitHandAndFeetJoints(*NewBodyState);
	}
	Impl->MHCBodyState = NewBodyState;
	return true;
}

bool FMetaHumanCharacterBodyIdentity::FState::AlignToTargetMesh(const FConformTargetParams& InConformTargetParams)
{
	if (!UE::MetaHuman::ValidateConformTargetMesh(InConformTargetParams.ConformTargetMesh))
	{
		return false;
	}

	const FConformTargetMesh& TargetMesh = InConformTargetParams.ConformTargetMesh;
	const bool bUseHeadVertices = TargetMesh.TargetPartsType == ETargetPartsType::HeadOnly || TargetMesh.TargetPartsType == ETargetPartsType::HeadAndBody;
	TArray<FVector3f> VerticesDNASpace = UE::MetaHuman::GetVerticesDNASpace(bUseHeadVertices ? TargetMesh.HeadVertices : TargetMesh.BodyVertices);

	TITAN_NAMESPACE::BodyShapeEditorTarget SolveTarget;
	if (!UE::MetaHuman::BuildSolveTarget(InConformTargetParams, Impl->MHCBodyAPI, SolveTarget))
	{
		return false;
	}

	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState = Impl->MHCBodyState->Clone();
	NewBodyState->SetApplyFloorOffset(false);

	if (!Impl->MHCBodyAPI->AlignToTargetMesh(*NewBodyState, SolveTarget))
	{
		return false;
	}

	Impl->MHCBodyState = NewBodyState;
	return true;
}

bool FMetaHumanCharacterBodyIdentity::FState::ConformTarget(const FConformTargetParams& InConformTargetParams)
{
	if (!UE::MetaHuman::ValidateConformTargetMesh(InConformTargetParams.ConformTargetMesh))
	{
		return false;
	}

	// Build a solve target using the appropriate function for the target parts type
	TITAN_NAMESPACE::BodyShapeEditorTarget SolveTarget;
	Eigen::VectorXf TargetVertexWeights;
	if (!UE::MetaHuman::BuildSolveTarget(InConformTargetParams, Impl->MHCBodyAPI, SolveTarget))
	{
		return false;
	}

	const FConformTargetMesh& TargetMesh = InConformTargetParams.ConformTargetMesh;

	const FBodyConformSolveSettings& SolveSettings = InConformTargetParams.BodyConformSolveSettings;
	TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::ArbitraryFitSolveOptions SolveOptions = UE::MetaHuman::GetFitSolveOptions(SolveSettings);

	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState = Impl->MHCBodyState->Clone();
	NewBodyState->SetApplyFloorOffset(false);
	Impl->MHCBodyAPI->ClearVertexDeltas(*NewBodyState);

	auto IterationUpdate = [this] (const av::ConstArrayView<float> InVerticesArray, const av::ConstArrayView<float> InNormalsArray, const av::ConstArrayView<float> InBindPose, int InIterationCount, TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::ESolveStepType InSolveStepType)
	{
		FMetaHumanRigEvaluatedState IterationState;

		IterationState.Vertices.AddUninitialized(InVerticesArray.size() / 3);
		float* VerticesDataPtr = (float*)(IterationState.Vertices.GetData());
		FMemory::Memcpy(VerticesDataPtr, InVerticesArray.data(), InVerticesArray.size() * sizeof(float));

		IterationState.VertexNormals.AddUninitialized(InNormalsArray.size() / 3);
		float* NormalsDataPtr = (float*)(IterationState.VertexNormals.GetData());
		FMemory::Memcpy(NormalsDataPtr, InNormalsArray.data(), InNormalsArray.size() * sizeof(float));

		TArray<FMatrix44f> BindPoseMatrices;
		BindPoseMatrices.AddUninitialized(InBindPose.size() / 16);
		FMemory::Memcpy((float*)BindPoseMatrices.GetData(), InBindPose.data(), InBindPose.size()*sizeof(float));

		return !Impl->MeshConformIterationDelegate.IsBound() || Impl->MeshConformIterationDelegate.Execute(IterationState, BindPoseMatrices, InIterationCount, static_cast<ESolveStepType>(InSolveStepType));
	};
	
	if (InConformTargetParams.bAutoSolve)
	{
		std::string PipelineName = TCHAR_TO_UTF8(*SolveSettings.PipelineName);	
		if (!Impl->MHCBodyAPI->PipelineFitToArbitraryTarget(*NewBodyState, PipelineName, SolveOptions, SolveTarget,IterationUpdate))
		{
			return false;
		}
	}
	else
	{
		// if doing body solve step
		if (SolveSettings.Iterations > 0)
		{
			if (!Impl->MHCBodyAPI->FitToArbitraryTarget(*NewBodyState, SolveOptions, SolveTarget,IterationUpdate))
			{
				return false;
			}

			if (SolveSettings.bApplyNeckSeamSmoothing)
			{
				TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::AdaptNeckSeamParams NeckSeamParams;
				NeckSeamParams.iterations = SolveSettings.SeamIterations;
				NeckSeamParams.laplacianWeight = SolveSettings.SeamLaplacian;
				NeckSeamParams.rings = SolveSettings.SeamRings;
				if (!Impl->MHCBodyAPI->AdaptNeckSeam(*NewBodyState, NeckSeamParams))
				{
					return false;
				}
			}
		}
		
		// if doing face solve step
		if (SolveSettings.FaceIterations > 0 && TargetMesh.TargetPartsType != ETargetPartsType::BodyOnly)
		{
			if (!Impl->MHCBodyAPI->FitFaceToArbitraryTarget(*NewBodyState, SolveOptions, SolveTarget,IterationUpdate))
			{
				return false;
			}
			
			if (SolveSettings.bApplyNeckSeamSmoothing)
			{
				TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::AdaptNeckSeamParams NeckSeamParams;
				NeckSeamParams.iterations = SolveSettings.SeamIterations;
				NeckSeamParams.laplacianWeight = SolveSettings.SeamLaplacian;
				NeckSeamParams.rings = SolveSettings.SeamRings;
				if (!Impl->MHCBodyAPI->AdaptNeckSeam(*NewBodyState, NeckSeamParams))
				{
					return false;
				}
			}
		}
	}

	if (InConformTargetParams.bEstimateBodyJointsFromMesh)
	{
		Impl->MHCBodyAPI->VolumetricallyFitHandAndFeetJoints(*NewBodyState);
	}
	Impl->MHCBodyState = NewBodyState;
	return true;
}

FMeshConformIteration& FMetaHumanCharacterBodyIdentity::FState::OnMeshConformIteration() const
{
	return Impl->MeshConformIterationDelegate;
}

bool FMetaHumanCharacterBodyIdentity::FState::RefineVerticesToTarget(const FRefinementTargetParams& InRefinementTargetParams)
{
	if (!UE::MetaHuman::ValidateConformTargetMesh(InRefinementTargetParams.ConformTargetMesh))
	{
		return false;
	}

	TITAN_NAMESPACE::BodyShapeEditorTarget SolveTarget;
	if (!UE::MetaHuman::BuildSolveTarget(InRefinementTargetParams, Impl->MHCBodyAPI, SolveTarget))
	{
		return false;
	}
	
	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState = Impl->MHCBodyState->Clone();
	Impl->MHCBodyAPI->ClearVertexDeltas(*NewBodyState);
	
	const FRefinementSettings& RS = InRefinementTargetParams.RefinementSettings;
	TITAN_NAMESPACE::BodySolveConfiguration solveConfig;
	solveConfig.refinement["iterations"].Set(RS.Iterations);
	solveConfig.refinement["vertexWeight"].Set(RS.VertexWeight);
	solveConfig.refinement["keypoint"].Set(RS.KeypointWeight);
	solveConfig.refinement["landmark2D"].Set(RS.Landmark2DWeight);
	solveConfig.refinement["laplacian"].Set(RS.Laplacian);
	solveConfig.refinement["bending"].Set(RS.Bending);
	solveConfig.refinement["strain"].Set(RS.Strain);
	solveConfig.refinement["vertexOffsetReg"].Set(RS.VertexOffsetReg);
	solveConfig.refinement["vertexReg"].Set(RS.VertexRegularization);
	solveConfig.refinement["icpTol"].Set(RS.DistanceTolerance);

	std::string refinementMaskName = InRefinementTargetParams.ConformTargetMesh.TargetPartsType == ETargetPartsType::BodyOnly ? "body_only" : "full_refinement";
	if (!Impl->MHCBodyAPI->RefineVertices(*NewBodyState, solveConfig, SolveTarget, refinementMaskName))
	{
		return false;
	}

	Impl->MHCBodyState = NewBodyState;
	return true;
}

TArray<FVector3f> FMetaHumanCharacterBodyIdentity::FState::GetJointTranslations() const
{
	auto extractTranslations = [](const av::ConstArrayView<float>& view) -> Eigen::Matrix<float, 3, Eigen::Dynamic> {
			using Matrix4f = Eigen::Matrix<float, 4, 4>; 
			constexpr int floats_per_transform = 16;

			size_t num_transforms = view.size() / floats_per_transform;
			Eigen::Matrix<float, 3, Eigen::Dynamic> translations(3, num_transforms);

			for (size_t i = 0; i < num_transforms; ++i) {
				const float* base_ptr = view.data() + i * floats_per_transform;
				Eigen::Map<const Matrix4f> mat(base_ptr);
				translations.col(i) = mat.block<3, 1>(0, 3);
			}
			return translations;
	};
	Eigen::Matrix<float, 3, -1> BindPose = extractTranslations(Impl->MHCBodyState->GetBindPose());
	TArray<FVector3f> JointTranslationsUESpace;
	JointTranslationsUESpace.AddUninitialized(BindPose.cols());
	for (int32 I = 0; I < BindPose.cols(); ++I)
	{
		JointTranslationsUESpace[I] = FVector3f{ BindPose(0, I), BindPose(1, I),  BindPose(2,I)};
	}
	return JointTranslationsUESpace;
}

bool FMetaHumanCharacterBodyIdentity::FState::SetJointTranslations(const TArray<FVector3f>& InComponentJointTranslations, bool bImportHelperJoints)
{
	if (InComponentJointTranslations.Num() != Impl->MHCBodyAPI->NumJoints())
	{
		return false;
	}
	auto extractTranslations = [](const av::ConstArrayView<float>& view) -> Eigen::Matrix<float, 3, Eigen::Dynamic> {
			using Matrix4f = Eigen::Matrix<float, 4, 4>; 
			constexpr int floats_per_transform = 16;

			size_t num_transforms = view.size() / floats_per_transform;
			Eigen::Matrix<float, 3, Eigen::Dynamic> translations(3, num_transforms);

			for (size_t i = 0; i < num_transforms; ++i) {
				const float* base_ptr = view.data() + i * floats_per_transform;
				Eigen::Map<const Matrix4f> mat(base_ptr);
				translations.col(i) = mat.block<3, 1>(0, 3);
			}
			return translations;
	};
	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState = Impl->MHCBodyState->Clone();
	Eigen::Matrix<float, 3, -1> bindPose = extractTranslations(NewBodyState->GetBindPose());
	TArray<FVector3f> JointTranslationsDNASpace;
	// Fill joint translations if fitting from skeleton
	JointTranslationsDNASpace.AddUninitialized(InComponentJointTranslations.Num());
	for (int32 I = 0; I < InComponentJointTranslations.Num(); ++I)
	{
		JointTranslationsDNASpace[I] = FVector3f{ InComponentJointTranslations[I].X, InComponentJointTranslations[I].Z, InComponentJointTranslations[I].Y };
	}
	const Eigen::Map<const Eigen::Matrix<float, 3, -1>> JointsEigen((const float*)JointTranslationsDNASpace.GetData(), 3, JointTranslationsDNASpace.Num());
	if(JointTranslationsDNASpace.Num() != bindPose.cols())
	{
		return false;
	} 
	for(int ji : Impl->MHCBodyAPI->CoreJoints())
	{
		bindPose.col(ji) = JointsEigen.col(ji);
	}
	if(bImportHelperJoints)
	{
		for(int ji : Impl->MHCBodyAPI->HelperJoints())
		{
			bindPose.col(ji) = JointsEigen.col(ji);
		}
	}
	Impl->MHCBodyAPI->SetNeutralJointsTranslations(*NewBodyState, bindPose);
	Impl->MHCBodyState = NewBodyState;
	return true;
}

bool FMetaHumanCharacterBodyIdentity::FState::SetJointRotations(const TArray<FVector3f>& InJointRotations, bool bImportHelperJoints)
{
	if (InJointRotations.Num() != Impl->MHCBodyAPI->NumJoints())
	{
		return false;
	}

	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState = Impl->MHCBodyState->Clone();
	TArray<float> JointRotationsView;
	JointRotationsView.SetNumUninitialized(InJointRotations.Num() * 3);
	for (int32 i = 0; i < InJointRotations.Num(); ++i)
	{
		Eigen::Vector3f JointRotation;
		Eigen::Vector3f JointTranslation;
		constexpr float ToRadians = PI / 180.0f;	
		Impl->MHCBodyAPI->GetNeutralJointTransform(*NewBodyState, static_cast<uint16_t>(i), JointTranslation, JointRotation);
		JointRotation *= ToRadians;
		JointRotationsView[i * 3 + 0] = JointRotation(0);
		JointRotationsView[i * 3 + 1] = JointRotation(1);
		JointRotationsView[i * 3 + 2] = JointRotation(2);
	
	}	
	for (int i : Impl->MHCBodyAPI->CoreJoints())
	{
		if (i == 0)
		{
			//This is to account for baked dna root rotation in skel mesh
			continue;
		}
		JointRotationsView[i * 3 + 0] = InJointRotations[i].X;
		JointRotationsView[i * 3 + 1] = -InJointRotations[i].Y;
		JointRotationsView[i * 3 + 2] = -InJointRotations[i].Z;	
	}
	if ( bImportHelperJoints )
	{
		for (int i : Impl->MHCBodyAPI->HelperJoints())
		{
			JointRotationsView[i * 3 + 0] = InJointRotations[i].X;  	
			JointRotationsView[i * 3 + 1] = -InJointRotations[i].Y;
			JointRotationsView[i * 3 + 2] = -InJointRotations[i].Z;
		}
	}
	Impl->MHCBodyAPI->SetNeutralJointRotations(*NewBodyState, {JointRotationsView.GetData(), static_cast<std::size_t>(JointRotationsView.Num())});
	Impl->MHCBodyState = NewBodyState;
	return true;
}

bool FMetaHumanCharacterBodyIdentity::FState::SetMesh(const TArray<FVector3f>& InVertices, bool bRepositionHelperJoint)
{
	TArray<FVector3f> VerticesDNASpace = UE::MetaHuman::GetVerticesDNASpace(InVertices);
	const Eigen::Map<const Eigen::Matrix<float, 3, -1>> VerticesEigen = UE::MetaHuman::GetEigenVertices(VerticesDNASpace);
	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState = Impl->MHCBodyState->Clone();
	auto extractTranslations = [](const av::ConstArrayView<float>& view) -> Eigen::Matrix<float, 3, Eigen::Dynamic> {
			using Matrix4f = Eigen::Matrix<float, 4, 4>; 
			constexpr int floats_per_transform = 16;

			size_t num_transforms = view.size() / floats_per_transform;
			Eigen::Matrix<float, 3, Eigen::Dynamic> translations(3, num_transforms);

			for (size_t i = 0; i < num_transforms; ++i) {
				const float* base_ptr = view.data() + i * floats_per_transform;
				Eigen::Map<const Matrix4f> mat(base_ptr);
				translations.col(i) = mat.block<3, 1>(0, 3);
			}
			return translations;
	};		
	Eigen::Matrix<float, 3, -1> bindPose = extractTranslations(NewBodyState->GetBindPose());
	NewBodyState->SetApplyFloorOffset(false);
	if (!Impl->MHCBodyAPI->SetNeutralMesh(*NewBodyState, VerticesEigen))
	{
		return false;
	}
	
	if (bRepositionHelperJoint)
	{
		Impl->MHCBodyAPI->FixJoints(*NewBodyState);
		Eigen::Matrix<float, 3, -1> bindPoseFixed = extractTranslations(NewBodyState->GetBindPose());
		for (int32 i : Impl->MHCBodyAPI->HelperJoints())
		{
			bindPose.col(i) = bindPoseFixed.col(i);
		}
	}
	Impl->MHCBodyAPI->SetNeutralJointsTranslations(*NewBodyState, bindPose);
	Impl->MHCBodyState = NewBodyState;
	return true;
}

void FMetaHumanCharacterBodyIdentity::FState::SetGlobalDeltaScale(float InVertexDelta)
{
	check(Impl->MHCBodyState);

	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState = Impl->MHCBodyState->Clone();
	Impl->MHCBodyAPI->SetVertexDeltaScale(*NewBodyState, InVertexDelta);
	Impl->MHCBodyState = NewBodyState;
}


float FMetaHumanCharacterBodyIdentity::FState::GetGlobalDeltaScale() const
{
	check(Impl->MHCBodyState);
	return Impl->MHCBodyState->VertexDeltaScale();
}

bool FMetaHumanCharacterBodyIdentity::FState::Serialize(FSharedBuffer& OutArchive) const
{
	check(Impl->MHCBodyState);

	pma::ScopedPtr<dna::MemoryStream> MemStream = pma::makeScoped<dna::MemoryStream>();
	Impl->MHCBodyAPI->DumpState(*(Impl->MHCBodyState), MemStream.get());
	terse::BinaryOutputArchive<trio::BoundedIOStream> archive{MemStream.get()};
	archive(static_cast<uint8>(Impl->MetaHumanBodyType));

	MemStream->seek(0);

	FUniqueBuffer UniqueBuffer = FUniqueBuffer::Alloc(MemStream->size());
	MemStream->read((char*)UniqueBuffer.GetData(), MemStream->size());
	OutArchive = UniqueBuffer.MoveToShared();

	return true;
}

bool FMetaHumanCharacterBodyIdentity::FState::Deserialize(const FSharedBuffer& InArchive)
{
	check(Impl->MHCBodyState);

	if (!InArchive.IsNull())
	{
		pma::ScopedPtr<dna::MemoryStream> MemStream = pma::makeScoped<dna::MemoryStream>();
		MemStream->write((char*)InArchive.GetData(), InArchive.GetSize());
		MemStream->seek(0);

		std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyAPI->CreateState();
		if (Impl->MHCBodyAPI->RestoreState(MemStream.get(), NewBodyShapeState))
		{
			terse::BinaryInputArchive<trio::BoundedIOStream> archive{MemStream.get()};
			uint8 BodyType = 0;
			archive(BodyType);

			Impl->MHCBodyState = NewBodyShapeState;
			EMetaHumanBodyType MetaHumanBodyType = static_cast<EMetaHumanBodyType>(BodyType);
			SetMetaHumanBodyType(MetaHumanBodyType);

			return true;
		}
	}

	return false;
}

TSharedRef<IDNAReader> FMetaHumanCharacterBodyIdentity::FState::StateToDna(dna::Reader* InDnaReader, bool bIsCombine, bool bUsePosedJoints) const
{
	check(Impl->MHCBodyState);

	pma::ScopedPtr<dna::MemoryStream> OutputStream = pma::makeScoped<dna::MemoryStream>();
	pma::ScopedPtr<dna::BinaryStreamWriter> DnaWriter = pma::makeScoped<dna::BinaryStreamWriter>(OutputStream.get());
	DnaWriter.get()->setFrom(InDnaReader);

	Impl->MHCBodyAPI->StateToDna(*(Impl->MHCBodyState), DnaWriter.get(), bIsCombine, bUsePosedJoints);
	DnaWriter->write();

	pma::ScopedPtr<dna::BinaryStreamReader> StateDnaReader = pma::makeScoped<dna::BinaryStreamReader>(OutputStream.get());
	StateDnaReader->read();

	return MakeShared<FLegacyDNAReader<dna::BinaryStreamReader>>(StateDnaReader.release(), FDNAConfig::Legacy());
}

TSharedRef<IDNAReader> FMetaHumanCharacterBodyIdentity::FState::StateToDna(UDNAAsset* InBodyDna) const
{
	pma::ScopedPtr<dna::MemoryStream> MemoryStream = pma::makeScoped<dna::MemoryStream>();
	pma::ScopedPtr<dna::BinaryStreamWriter> DnaWriter = pma::makeScoped<dna::BinaryStreamWriter>(MemoryStream.get());

	DnaWriter->setFrom(InBodyDna->GetDNAReader()->Unwrap(), dna::DataLayer::All);
	DnaWriter->write();

	pma::ScopedPtr<dna::BinaryStreamReader> BinaryDnaReader = pma::makeScoped<dna::BinaryStreamReader>(MemoryStream.get());
	BinaryDnaReader->read();

	return StateToDna(BinaryDnaReader.get());
}

TArray<PhysicsBodyVolume> FMetaHumanCharacterBodyIdentity::FState::GetPhysicsBodyVolumes(const FName& InJointName) const
{
	TArray<PhysicsBodyVolume> OutPhysicsBodyVolumes;

	std::string jointName = TCHAR_TO_UTF8(*InJointName.ToString());

	for (int VolumeIndex = 0; VolumeIndex < Impl->MHCBodyAPI->NumPhysicsBodyVolumes(jointName); VolumeIndex++)
	{
		Eigen::Vector3f BoundingBoxCenter;
		Eigen::Vector3f BoundingBoxExtents;
		Impl->MHCBodyAPI->GetPhysicsBodyBoundingBox(*Impl->MHCBodyState, jointName, VolumeIndex, BoundingBoxCenter, BoundingBoxExtents);

		// Extract transform and extents in UE coordinates
		PhysicsBodyVolume OutPhysicsVolume;
		OutPhysicsVolume.Center = FVector{BoundingBoxCenter[0], -BoundingBoxCenter[1], BoundingBoxCenter[2]};
		OutPhysicsVolume.Extent = FVector{BoundingBoxExtents[0], BoundingBoxExtents[1], BoundingBoxExtents[2]};

		OutPhysicsBodyVolumes.Add(OutPhysicsVolume);
	}

	return OutPhysicsBodyVolumes;
}

TMap<FName, int32> FMetaHumanCharacterBodyIdentity::FState::GetPresetBodyKeyPoints() const
{
	check(Impl);
	check(Impl->MHCBodyAPI);
	
	TMap<FName, int32> PresetKeyPoints;

	// Get preset keypoints from the BodyShapeEditor API
	const std::vector<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::Keypoint>& ApiKeypoints = Impl->MHCBodyAPI->GetDefaultKeypoints();

	// Convert to UE format, and use new names for UE purposes
	int32 KeypointCounter = 1;
	for (const auto& Keypoint : ApiKeypoints)
	{
		FName KeypointName = FName(*FString::Printf(TEXT("P%d"), KeypointCounter++));
		PresetKeyPoints.Add(KeypointName, Keypoint.index);
	}

	return PresetKeyPoints;
}

TArray<float> FMetaHumanCharacterBodyIdentity::FState::GetBodyModelCoefficients() const
{
	check(Impl);
	check(Impl->MHCBodyState);

	av::ConstArrayView<float> GuiControls = Impl->MHCBodyState->GetGuiControls();
	return TArray<float>(GuiControls.data(), GuiControls.size());
}

TArray<FVector3f> FMetaHumanCharacterBodyIdentity::FState::TransformTargetVerticesToBindPose(const TArray<FVector3f>& InVertices) const
{
	check(Impl);
	check(Impl->MHCBodyAPI);
	check(Impl->MHCBodyState);

	if (InVertices.IsEmpty())
	{
		return {};
	}

	// Convert vertices from UE space (Z-up, left-handed) to DNA/Eigen space (Y-up).
	// The convention throughout this file: DNA = {ue.X, ue.Z, ue.Y}.
	const TArray<FVector3f> VerticesDNASpace = UE::MetaHuman::GetVerticesDNASpace(InVertices);
	const Eigen::Map<const Eigen::Matrix3Xf> PosedEigen = UE::MetaHuman::GetEigenVertices(VerticesDNASpace);
		//reinterpret_cast<const float*>(VerticesDNASpace.GetData()), 3, VerticesDNASpace.Num());

	// Ask the body API to rigidly snap the posed vertices back to the bind frame of the
	// head joint, taking the body's current shape into account.
	const Eigen::Matrix3Xf BindEigen = Impl->MHCBodyAPI->RigidAttachmentToBind(*Impl->MHCBodyState, "head", PosedEigen);

	if (BindEigen.cols() == 0)
	{
		// Joint could not be resolved or joint matrices are not populated.
		return {};
	}

	// Convert the result back from DNA space to UE space: {dna.X, dna.Z, dna.Y}.
	TArray<FVector3f> OutVertices;
	OutVertices.SetNumUninitialized(static_cast<int32>(BindEigen.cols()));
	for (int32 I = 0; I < OutVertices.Num(); ++I)
	{
		OutVertices[I] = FVector3f{ BindEigen(0, I), BindEigen(2, I), BindEigen(1, I) };
	}
	return OutVertices;
}

int32 FMetaHumanCharacterBodyIdentity::GetNumLOD0MeshVertices(bool bInCombined) const
{
	check(Impl);
	check(Impl->MHCBodyAPI);

	int NumMeshVertices;
	bool bRes = Impl->MHCBodyAPI->GetNumLOD0MeshVertices(NumMeshVertices, bInCombined);

	if (!bRes)
	{
		return -1;
	}
	return static_cast<int32>(NumMeshVertices);
}

TArray<int32> FMetaHumanCharacterBodyIdentity::GetBodyToCombinedMapping() const
{
	check(Impl);
	check(Impl->MHCBodyAPI);
	TArray<int32> BodyToCombinedMapping;
	auto MappingView = Impl->MHCBodyAPI->GetBodyToCombinedMapping(0);
	BodyToCombinedMapping.Append(MappingView.data(), MappingView.size());
	return BodyToCombinedMapping;
}
