// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorMeshImportTool.h"
#include "MetaHumanCharacterEditorMeshImportContextObject.h"
#include "MetaHumanCharacterAnalytics.h"

#include "Components/DynamicMeshComponent.h"
#include "ContextObjectStore.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "InteractiveToolObjects.h"
#include "InteractiveToolManager.h"
#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanCharacterEditorToolTargetUtil.h"
#include "MetaHumanCharacterEditorViewportClient.h"
#include "MetaHumanSDKEditor.h"
#include "MetaHumanRigEvaluatedState.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "MetaHumanConformTargetParams.h"
#include "Misc/ITransaction.h"
#include "ScopedTransaction.h"
#include "Misc/ScopedSlowTask.h"
#include "StaticMeshAttributes.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ToolTargetManager.h"
#include "Utility/ScriptableToolContextObjects.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "Utility/ScriptableToolContextObjects.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ITransaction.h"
#include "MetaHumanCharacterEditorToolCommandChange.h"
#include "Engine/AssetManager.h"
#include "Async/Async.h"
#include "Framework/Notifications/NotificationManager.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "DNA.h"
#include "DNAUtils.h"
#include "MetaHumanCharacterExportBlueprintLibrary.h"
#include "MetaHumanCharacterSkelMeshUtils.h"
#include "MetaHumanCommonDataUtils.h"
#include "Logging/StructuredLog.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "MetaHumanCharacterEditorLog.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorMeshImportTool"

class FMeshImportToolInputProcessor : public IInputProcessor
{
public:
	FMeshImportToolInputProcessor(UMetaHumanCharacterEditorMeshImportTool* InTool)
		: WeakTool(InTool)
	{}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		if (UMetaHumanCharacterEditorMeshImportTool* Tool = WeakTool.Get())
		{
			if (InKeyEvent.GetKey() == EKeys::Escape)
			{
				UMetaHumanCharacterEditorSubsystem* MetaHumanSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
				if (MetaHumanSubsystem && MetaHumanSubsystem->IsAsyncConformPending(Tool->MetaHumanCharacter))
				{
					MetaHumanSubsystem->CancelMeshAsyncProcess(Tool->MetaHumanCharacter);
					return true;
				}
				
				if (Tool->KeyPointsMechanic3D)
	            {
	                Tool->KeyPointsMechanic3D->DeselectAll();
	                return true;
	            }
			}
			else if (InKeyEvent.GetKey() == EKeys::Delete)
			{
				if (Tool->KeyPointsMechanic3D)
				{
					Tool->KeyPointsMechanic3D->DeleteSelectedKeyPoint();
					return true;
				}
			}
		}
		return false;
	}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

private:
	TWeakObjectPtr<UMetaHumanCharacterEditorMeshImportTool> WeakTool;
};

namespace UE::MetaHuman
{
	static FMeshDescription* GetMeshDescription(UObject* InMesh)
	{
		FMeshDescription* MeshDescription = nullptr;
		
		constexpr int32 LODIndex = 0;	
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(InMesh))
		{
			MeshDescription = StaticMesh->GetMeshDescription(LODIndex);
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(InMesh))
		{
			MeshDescription = SkeletalMesh->GetMeshDescription(LODIndex);
		}

		if (MeshDescription && MeshDescription->NeedsCompact())
		{
			FElementIDRemappings IDRemappings;
			MeshDescription->Compact(IDRemappings);
		}

		return MeshDescription;
	}

	static TMap<FString, FTrackingPoints> GetSolverTrackingPoints(const TMap<FString, FMetaHumanCharacterCurveTrackingPoints>& InCharacterCurveTrackingPoints)
	{
		TMap<FString, FTrackingPoints> CurveTrackingPoints;
		for (const TPair<FString, FMetaHumanCharacterCurveTrackingPoints>& Pair : InCharacterCurveTrackingPoints)
		{
			FTrackingPoints Points;
			Points.TrackingPoints = Pair.Value.Points;
			CurveTrackingPoints.Add(Pair.Key, Points);
		}
		return CurveTrackingPoints;
	}

	FDynamicMesh3 BuildDynamicMesh(UObject* InMesh)
	{
		FDynamicMesh3 OutMesh;

		FMeshDescription* MeshDescription = nullptr;
		constexpr int32 LODIndex = 0;

		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(InMesh))
		{
			MeshDescription = StaticMesh->GetMeshDescription(LODIndex);
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(InMesh))
		{
			MeshDescription = SkeletalMesh->GetMeshDescription(LODIndex);
		}

		if (MeshDescription)
		{
			FMeshDescriptionToDynamicMesh Converter;
			Converter.Convert(MeshDescription, OutMesh);
		}

		return OutMesh;
	}
}

// -------------------------------------------------------------------------
// UMetaHumanCharacterEditorMeshImportToolBuilder implementation -----------
// -------------------------------------------------------------------------

bool UMetaHumanCharacterEditorMeshImportToolBuilder::CanBuildTool(const FToolBuilderState& InSceneState) const
{
	return Super::CanBuildTool(InSceneState) && !IsCharacterRigged(InSceneState);
}

UInteractiveTool* UMetaHumanCharacterEditorMeshImportToolBuilder::BuildTool(const FToolBuilderState& InSceneState) const
{
	UToolTarget* Target = InSceneState.TargetManager->BuildFirstSelectedTargetable(InSceneState, GetTargetRequirements());
	check(Target);

	// Check if the body state of the character matches the posed state of the target mesh. If not, it means the user has done some mesh edits outside of From Custom Mesh, and we should show a warning.
	if (UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(Target))
	{
		if (!UMetaHumanCharacterEditorSubsystem::Get()->IsBodyStateMatchingTargetPosedState(MetaHumanCharacter, MetaHumanCharacter->LastTargetMeshKey))
		{
			EAppReturnType::Type Result =
				FMessageDialog::Open
				(
					EAppMsgType::OkCancel,
					FText::FromString(TEXT("Warning! We detect that you have done mesh edits outside of From Custom Mesh. If you proceed further, you will loose those mesh edits. Please make sure to save your preset or it will be lost. Proceed?"))
				);

			if (Result == EAppReturnType::Cancel)
			{
				return nullptr;
			}
		}
	}

	UMetaHumanCharacterEditorMeshImportTool* MeshImportTool = NewObject<UMetaHumanCharacterEditorMeshImportTool>(InSceneState.ToolManager);
	MeshImportTool->SetTarget(Target);
	MeshImportTool->SetWorld(InSceneState.World);

	return MeshImportTool;
}

// -----------------------------------------------------------------------------
// UMetaHumanCharacterEditorMeshImportToolProperties implementation ------------
// -----------------------------------------------------------------------------

void UMetaHumanCharacterEditorMeshImportToolProperties::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCharacterEditorMeshImportTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorMeshImportTool>();
	UMetaHumanCharacter* MetaHumanCharacter = OwnerTool ? UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget()) : nullptr;
	if (!MetaHumanCharacter)
	{
		return;
	}

	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	const bool bRebuildDynamicMeshes = PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, BodyDelta))
	{
		OwnerTool->SetBodyDelta(BodyDelta, bRebuildDynamicMeshes);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, HeadDelta))
	{
		OwnerTool->SetHeadDelta(HeadDelta, bRebuildDynamicMeshes);
	}
}

void UMetaHumanCharacterEditorMeshImportToolProperties::StartBodyStepMeshConform()
{
	constexpr bool bIsAutoSolve = false;
	constexpr bool bDisableBodySolve = false;
	constexpr bool bDisableFaceSolve = true;
	StartMeshConform(bIsAutoSolve, bDisableBodySolve, bDisableFaceSolve);
}

void UMetaHumanCharacterEditorMeshImportToolProperties::StartAlignHeadMeshConform()
{
	UMetaHumanCharacterEditorMeshImportTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorMeshImportTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	check(MetaHumanCharacter);
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

	// Clear errors before starting
	OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);

	FMetaHumanCharacterTargetTrackingResults TrackingResults = GetFaceTrackingFromCharacter();

	FConformTargetParams ConformTargetParams;
	ConformTargetParams.ConformTargetMesh = GetConformTargetMesh();
	ConformTargetParams.KeyPointTargets = GetKeyPointCorrespondencesFromCharacter();
	ConformTargetParams.CurveTrackingPoints = UE::MetaHuman::GetSolverTrackingPoints(TrackingResults.CurveTrackingPoints);
	ConformTargetParams.CameraViewInfo = TrackingResults.CameraViewInfo;
	ConformTargetParams.ImageSize = TrackingResults.ImageSize;

	// Capture body and face states before for undo
	OwnerTool->PreConformBodyState = Subsystem->CopyBodyState(MetaHumanCharacter);
	OwnerTool->PreConformFaceState = Subsystem->CopyFaceState(MetaHumanCharacter);

	const FMetaHumanCharacterTargetMeshKey TargetMeshKey = GetTargetMeshKey();
	const bool bAlignStarted = Subsystem->AlignToTargetMeshesAsync(MetaHumanCharacter, TargetMeshKey, ConformTargetParams);
	if (bAlignStarted)
	{
		UpdateClothVisibility(MetaHumanCharacter, true);
	}
	else
	{
		OwnerTool->DisplayError(LOCTEXT("FailedToStartAlignHead", "Failed to Start Align Head"));
	}
}

void UMetaHumanCharacterEditorMeshImportToolProperties::StartFaceStepMeshConform()
{
	constexpr bool bIsAutoSolve = false;
	constexpr bool bDisableBodySolve = true;
	constexpr bool bDisableFaceSolve = false;
	StartMeshConform(bIsAutoSolve, bDisableBodySolve, bDisableFaceSolve);
}

void UMetaHumanCharacterEditorMeshImportToolProperties::StartAutoMeshConform()
{
	constexpr bool bIsAutoSolve = true;
	constexpr bool bDisableBodySolve = false;
	constexpr bool bDisableFaceSolve = false;
	StartMeshConform(bIsAutoSolve, bDisableBodySolve, bDisableFaceSolve);
}

bool UMetaHumanCharacterEditorMeshImportToolProperties::CanProcess() const
{
	UMetaHumanCharacterEditorMeshImportTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorMeshImportTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());

	if (OwnerTool->bIsLoadingTargetMesh)
	{
		return false;
	}

	bool bMeshIsValid = true;
	
	auto IsMeshValid = [](TSoftObjectPtr<UObject> Mesh)-> bool
	{
		if (Mesh.IsNull()) 
		{
			return false;
		}
		else if (!Mesh.IsValid())
		{
			return Mesh.LoadSynchronous() != nullptr;
		}
		return true;
	};
	
	if (bUseCharacterParts)
	{
		if (!IsMeshValid(BodyMesh) && !IsMeshValid(HeadMesh))
		{
			bMeshIsValid = false;
		}
	}
	else
	{
		bMeshIsValid = IsMeshValid(CombinedMesh);
	}
	
	bool bIsProcessing = UMetaHumanCharacterEditorSubsystem::Get()->GetRiggingState(MetaHumanCharacter) == EMetaHumanCharacterRigState::RigPending;
	return bMeshIsValid && !bIsProcessing;
}

bool UMetaHumanCharacterEditorMeshImportToolProperties::CanReset() const
{
	UMetaHumanCharacterEditorMeshImportTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorMeshImportTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());

	bool bIsProcessing = UMetaHumanCharacterEditorSubsystem::Get()->GetRiggingState(MetaHumanCharacter) == EMetaHumanCharacterRigState::RigPending;
	return !bIsProcessing;
}

void UMetaHumanCharacterEditorMeshImportTool::ExportStateToDNA()
{
	if (!IsValid(MetaHumanCharacter))
	{
		DisplayError(LOCTEXT("NoCharacterForExport", "No MetaHuman character found"));
		return;
	}

	const FMetaHumanCharacterTargetMeshKey TargetMeshKey = MeshImportProperties->GetTargetMeshKey();

	if (MetaHumanCharacter->GetBodyTargetPoseStateData(TargetMeshKey).GetSize() == 0)
	{
		DisplayError(LOCTEXT("NoPosedStateForExport", "No posed state found. Please conform to target mesh first before exporting."));
		return;
	}

	// Show modal save dialog to get the project content path. The library implementation
	// is headless and takes the path as a parameter — the dialog is the only piece of
	// this flow that genuinely needs the tool's UI context.
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	FString OutPackageName;
	FString OutAssetName;
	AssetTools.CreateUniqueAssetName(
		FPackageName::GetLongPackagePath(MetaHumanCharacter->GetPackage()->GetPathName()) / MetaHumanCharacter->GetName(),
		TEXT("_PosedBody_DNA"),
		OutPackageName,
		OutAssetName);

	FSaveAssetDialogConfig SaveAssetDialogConfig;
	SaveAssetDialogConfig.DefaultPath = FPackageName::GetLongPackagePath(OutPackageName);
	SaveAssetDialogConfig.DefaultAssetName = OutAssetName;
	SaveAssetDialogConfig.AssetClassNames.Add(UDNA::StaticClass()->GetClassPathName());
	SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
	SaveAssetDialogConfig.DialogTitleOverride = LOCTEXT("ExportPosedDNATitle", "Export Posed Body DNA");

	const FString AssetPathAndName = IContentBrowserSingleton::Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);
	if (AssetPathAndName.IsEmpty())
	{
		// Cancelled
		return;
	}

	// CreateModalSaveAssetDialog returns a full object path of the form
	// "/Game/Folder/AssetName.AssetName". ExportPosedDNA expects ProjectPath to be the
	// containing folder and AssetName to be the asset name (see FMetaHumanPosedDNAExportParams).
	// Split the dialog result so the library's validation (FPackageName::IsValidLongPackageName)
	// accepts the folder and the user's chosen name is preserved.
	const FString PackageName = FPackageName::ObjectPathToPackageName(AssetPathAndName);

	FMetaHumanPosedDNAExportParams ExportParams;
	ExportParams.TargetMeshKey = TargetMeshKey;
	ExportParams.ProjectPath = FPackageName::GetLongPackagePath(PackageName);
	ExportParams.AssetName = FPackageName::GetLongPackageAssetName(PackageName);
	ExportParams.bOverwriteExistingAssets = true;

	UMetaHumanCharacterExportBlueprintLibrary::ExportPosedDNA(MetaHumanCharacter, ExportParams);
}


void UMetaHumanCharacterEditorMeshImportToolProperties::RefineVertices()
{
	UMetaHumanCharacterEditorMeshImportTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorMeshImportTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	check(MetaHumanCharacter);
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	
	TMap<int32, FVector3f> KeyPointCorrespondences = GetKeyPointCorrespondencesFromCharacter();
	FMetaHumanCharacterTargetTrackingResults TrackingResults = GetFaceTrackingFromCharacter();
	
	FRefinementTargetParams RefinementParams;
	RefinementParams.ConformTargetMesh = GetConformTargetMesh();
	RefinementParams.KeyPointTargets = KeyPointCorrespondences;
	RefinementParams.CurveTrackingPoints = UE::MetaHuman::GetSolverTrackingPoints(TrackingResults.CurveTrackingPoints);
	RefinementParams.CameraViewInfo = TrackingResults.CameraViewInfo;
	RefinementParams.ImageSize = TrackingResults.ImageSize;
	RefinementParams.RefinementSettings = RefinementSettings;

	// Capture body and face states before refinement for undo
	OwnerTool->PreConformBodyState = Subsystem->CopyBodyState(MetaHumanCharacter);
	OwnerTool->PreConformFaceState = Subsystem->CopyFaceState(MetaHumanCharacter);

	FMetaHumanCharacterTargetMeshKey TargetMeshKey = GetTargetMeshKey();
	const bool bRefinementStarted = Subsystem->RefineVerticesToTargeAsync(MetaHumanCharacter, TargetMeshKey, RefinementParams);
	if (bRefinementStarted)
	{
		UpdateClothVisibility(MetaHumanCharacter, true);
	}
	else
	{
		OwnerTool->DisplayError( LOCTEXT("FailedToStartVertexRefinement", "Failed to Start Vertex Refinement"));
	}
}

void UMetaHumanCharacterEditorMeshImportToolProperties::OnAsyncConformComplete(bool bInSuccess, bool bWasCancelled)
{
	UMetaHumanCharacterEditorMeshImportTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorMeshImportTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	
	// Restore cloth visibility once finished
	UpdateClothVisibility(MetaHumanCharacter, false);

	if (bInSuccess)
	{
		// Update skel mesh fully on tool exit using the key actually conformed
		OwnerTool->SetNeedsFullUpdate(GetTargetMeshKey());
	}

	if (bInSuccess && OwnerTool->PreConformBodyState.IsValid() && OwnerTool->PreConformFaceState.IsValid())
	{
		UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

		TUniquePtr<FMetaHumanCharacterEditorStateCommandChange> CommandChange = MakeUnique<FMetaHumanCharacterEditorStateCommandChange>(
			OwnerTool->GetToolManager(),
			OwnerTool->PreConformBodyState.ToSharedRef(),
			OwnerTool->PreConformFaceState.ToSharedRef(),
			Subsystem->CopyBodyState(MetaHumanCharacter),
			Subsystem->CopyFaceState(MetaHumanCharacter),
			LOCTEXT("MeshConformChange", "Mesh Conform").ToString());

		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(
			MetaHumanCharacter, MoveTemp(CommandChange),
			LOCTEXT("MeshConformChange", "Mesh Conform"));
	}

	OwnerTool->PreConformBodyState.Reset();
	OwnerTool->PreConformFaceState.Reset();

	if (!bInSuccess && !bWasCancelled)
	{
		OwnerTool->DisplayError(LOCTEXT("FailedToConformBodyComplete", "Error Conforming"));
	}
}

TMap<int32, FVector3f> UMetaHumanCharacterEditorMeshImportToolProperties::GetKeyPointCorrespondencesFromCharacter() const
{
	UMetaHumanCharacterEditorMeshImportTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorMeshImportTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());

	TMap<int32, FVector3f> KeyPointCorrespondences;
	FMetaHumanCharacterTargetMeshKey TargetMeshKey = GetTargetMeshKey();

	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

	// Add user defined custom keypoints
	if (FMetaHumanCharacterTargetKeyPoints* KeyPoints = MetaHumanCharacter->TargetMeshKeyPointsCollection.PerMeshTargetKeyPoints.Find(TargetMeshKey))
	{
		for (const TPair<FName, int32>& NameVertexID : KeyPoints->CharacterBodyVertexIndexes)
		{
			if (const FVector3f* TargetBodyPosition = KeyPoints->TargetBodyPositions.Find(NameVertexID.Key))
			{
				KeyPointCorrespondences.Add(NameVertexID.Value, *TargetBodyPosition);
			}
			else if (const FVector3f* TargetHeadPosition = KeyPoints->TargetHeadPositions.Find(NameVertexID.Key))
			{
				KeyPointCorrespondences.Add(NameVertexID.Value, *TargetHeadPosition);
			}
		}

		for (const TPair<FName, int32>& NameVertexID : KeyPoints->CharacterHeadVertexIndexes)
		{
			if (const FVector3f* TargetBodyPosition = KeyPoints->TargetBodyPositions.Find(NameVertexID.Key))
			{
				KeyPointCorrespondences.Add(NameVertexID.Value, *TargetBodyPosition);
			}
			else if (const FVector3f* TargetHeadPosition = KeyPoints->TargetHeadPositions.Find(NameVertexID.Key))
			{
				KeyPointCorrespondences.Add(NameVertexID.Value, *TargetHeadPosition);
			}
		}
	}
	
	// Pair presets
	// TODO: Maybe refactor this function to use same helper we already have UE::MetaHuman::GetCharacterTargetKeyPoints
	if (Subsystem)
	{
		TMap<FName, int32> PresetBodyKeyPoints = Subsystem->GetPresetBodyKeyPoints(MetaHumanCharacter);

		if (FMetaHumanCharacterTargetKeyPoints* KeyPoints = MetaHumanCharacter->TargetMeshKeyPointsCollection.PerMeshTargetKeyPoints.Find(TargetMeshKey))
		{
			for (const TPair<FName, int32>& NameVertexID : PresetBodyKeyPoints)
			{
				if (const FVector3f* TargetBodyPosition = KeyPoints->TargetBodyPositions.Find(NameVertexID.Key))
				{
					if (!KeyPointCorrespondences.Contains(NameVertexID.Value))
					{
						KeyPointCorrespondences.Add(NameVertexID.Value, *TargetBodyPosition);
					}
				}
				else if (const FVector3f* TargetHeadPosition = KeyPoints->TargetHeadPositions.Find(NameVertexID.Key))
				{
					if (!KeyPointCorrespondences.Contains(NameVertexID.Value))
					{
						KeyPointCorrespondences.Add(NameVertexID.Value, *TargetHeadPosition);
					}
				}
			}
		}
	}

	return KeyPointCorrespondences;
}

FMetaHumanCharacterTargetTrackingResults UMetaHumanCharacterEditorMeshImportToolProperties::GetFaceTrackingFromCharacter() const
{
	FMetaHumanCharacterTargetTrackingResults OutTargetTrackingResults;
	if (UMetaHumanCharacterEditorMeshImportTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorMeshImportTool>())
	{
		if (UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget()))
		{
			FMetaHumanCharacterTargetMeshKey TrackingMeshKey = GetTargetMeshKey();
			if (const FMetaHumanCharacterTargetTrackingResults* TrackingResults = MetaHumanCharacter->TargetMeshTrackingResultsCollection.PerMeshTrackingResults.Find(TrackingMeshKey))
			{
				OutTargetTrackingResults = *TrackingResults;
			}
		}
	}

	return OutTargetTrackingResults;
}


FMetaHumanCharacterTargetMeshKey UMetaHumanCharacterEditorMeshImportToolProperties::GetTargetMeshKey() const
{
	FMetaHumanCharacterTargetMeshKey TargetMeshKey;
	if (bUseCharacterParts)
	{
		TargetMeshKey.BodyMesh = BodyMesh;
		TargetMeshKey.HeadMesh = HeadMesh;
	}
	else
	{
		TargetMeshKey.CombinedMesh = CombinedMesh;
	}
	return TargetMeshKey;
}

void UMetaHumanCharacterEditorMeshImportToolProperties::TrackFace()
{
	UMetaHumanCharacterEditorMeshImportTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorMeshImportTool>();
	if (OwnerTool->ContourMechanic->TrackFaceInCurrentView(MeshOffset))
	{
		OwnerTool->OnUpdateFacialTrackingCurvesDelegate.Broadcast();
	}
	else
	{
		OwnerTool->DisplayError(LOCTEXT("FailedToTrackFaceLandmarks", "Failed to Track Facial Features. See documentation for best practices."));
	}
}

bool UMetaHumanCharacterEditorMeshImportToolProperties::CanTrackFace() const
{
	if (!CanProcess())
	{
		return false;
	}
	return true;
}

void UMetaHumanCharacterEditorMeshImportToolProperties::ResetBody()
{
	UMetaHumanCharacterEditorMeshImportTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorMeshImportTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	check(MetaHumanCharacter);
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> OldBodyState = Subsystem->CopyBodyState(MetaHumanCharacter);
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyState = Subsystem->CopyBodyState(MetaHumanCharacter);
	TSharedRef<FMetaHumanCharacterIdentity::FState> OldFaceState = Subsystem->CopyFaceState(MetaHumanCharacter);
	BodyState->ResetBodyOnly();
	BodyState->ClearVertexDeltas();

	Subsystem->CommitBodyState(MetaHumanCharacter, BodyState, UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Full);
	
	TSharedRef<FMetaHumanCharacterIdentity::FState> FaceState = Subsystem->CopyFaceState(MetaHumanCharacter);
	FFitToTargetOptions Options;
	Options.AlignmentOptions = EAlignmentOptions::None;
	FaceState->FitFromBodyVertices(BodyState->GetVerticesAndVertexNormals().Vertices, Options);
	Subsystem->CommitFaceState(MetaHumanCharacter, FaceState);
	
	
	
	TUniquePtr<FMetaHumanCharacterEditorStateCommandChange> CommandChange = MakeUnique<FMetaHumanCharacterEditorStateCommandChange>(
			OwnerTool->GetToolManager(),
			OldBodyState,
			OldFaceState,
			BodyState,
			FaceState,
			LOCTEXT("MeshImportResetBodyChange", "Reset Body").ToString());

	OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(
		MetaHumanCharacter, MoveTemp(CommandChange),
		LOCTEXT("MeshImportResetBodyChange", "Reset Body"));

	constexpr bool bRebuildKeyPoints = true;
	OwnerTool->OnCharacterStateChanged(bRebuildKeyPoints);
	OwnerTool->SetNeedsFullUpdate(GetTargetMeshKey());
}

void UMetaHumanCharacterEditorMeshImportToolProperties::ResetHead()
{
	UMetaHumanCharacterEditorMeshImportTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorMeshImportTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	check(MetaHumanCharacter);
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	
	TSharedRef<FMetaHumanCharacterIdentity::FState> OldFaceState = Subsystem->CopyFaceState(MetaHumanCharacter);
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> OldBodyState = Subsystem->CopyBodyState(MetaHumanCharacter);
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyState = Subsystem->CopyBodyState(MetaHumanCharacter);
	BodyState->ResetFaceModel();
	Subsystem->CommitBodyState(MetaHumanCharacter, BodyState, UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Minimal);
	
	TSharedRef<FMetaHumanCharacterIdentity::FState> FaceState = Subsystem->CopyFaceState(MetaHumanCharacter);
	FFitToTargetOptions Options;
	Options.AlignmentOptions = EAlignmentOptions::None;
	FaceState->ClearFromBodyVertexDeltas();
	
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyStateForFaceFit = Subsystem->CopyBodyState(MetaHumanCharacter);
	BodyStateForFaceFit->ResetFaceModel();
	BodyStateForFaceFit->ClearVertexDeltas();	
	FaceState->FitFromBodyVertices(BodyStateForFaceFit->GetVerticesAndVertexNormals().Vertices, Options);
	Subsystem->CommitFaceState(MetaHumanCharacter, FaceState);

	TUniquePtr<FMetaHumanCharacterEditorStateCommandChange> CommandChange = MakeUnique<FMetaHumanCharacterEditorStateCommandChange>(
			OwnerTool->GetToolManager(),
			OldBodyState,
			OldFaceState,
			BodyState,
			FaceState,
			LOCTEXT("MeshImportResetHeadChange", "Reset Head").ToString());

	OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(
		MetaHumanCharacter, MoveTemp(CommandChange),
		LOCTEXT("MeshImportResetHeadChange", "Reset Head"));
	
	constexpr bool bRebuildKeyPoints = true;
	OwnerTool->OnCharacterStateChanged(bRebuildKeyPoints);
	OwnerTool->SetNeedsFullUpdate(GetTargetMeshKey());
}


void UMetaHumanCharacterEditorMeshImportToolProperties::UpdateKeyPointVisibility()
{
	UMetaHumanCharacterEditorMeshImportTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorMeshImportTool>();
	if (OwnerTool && OwnerTool->KeyPointsMechanic3D)
	{
		OwnerTool->KeyPointsMechanic3D->bShowCustomKeyPoints = bShowCustomKeyPoints;
		OwnerTool->KeyPointsMechanic3D->bShowPresetBodyKeyPoints = bShowPresetKeyPoints;
		OwnerTool->KeyPointsMechanic3D->UpdateKeyPointManipulatorsVisibility();
	}
}

bool UMetaHumanCharacterEditorMeshImportToolProperties::GetErrorMessageText(EImportErrorCode InErrorCode, FText& OutErrorMessage) const
{
	switch (InErrorCode)
	{
	case EImportErrorCode::InvalidInputData:
		OutErrorMessage = LOCTEXT("FailedToConformInvalidInputData", "Failed to Conform: input mesh is not consistent with MetaHuman topology");
		break;
	case EImportErrorCode::InvalidInputBones:
		OutErrorMessage = LOCTEXT("FailedToConformInvalidInputBones", "Failed to Conform: input mesh bones are not consistent with MetaHuman topology");
		break;
	case EImportErrorCode::InvalidHeadMesh:
		OutErrorMessage = LOCTEXT("FailedToConformInvalidHeadMesh", "Failed to Conform: input mesh is not consistent with MetaHuman topology");
		break;
	default:
		return false;
	}
	
	return true;
}

void UMetaHumanCharacterEditorMeshImportTool::DisplayError(const FText& InErrorMessage) const
{
	GetToolManager()->DisplayMessage(InErrorMessage, EToolMessageLevel::UserError);
	FMessageLog(UE::MetaHuman::MessageLogName).Error(InErrorMessage);
	FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, false);
}

void UMetaHumanCharacterEditorMeshImportToolProperties::StartMeshConform(bool bIsAutoSolve, bool bDisableBodySolve, bool bDisableFaceSolve)
{
	UMetaHumanCharacterEditorMeshImportTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorMeshImportTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	check(MetaHumanCharacter);
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	
	// Clear errors before starting
	OwnerTool->GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
	
	FConformTargetMesh ConformTargetMesh = GetConformTargetMesh();

	// Auto-track face if no manual tracking has been done yet
	if (bIsAutoSolve && ConformTargetMesh.TargetPartsType != ETargetPartsType::BodyOnly && !OwnerTool->ContourMechanic->HasTrackingResults())
	{
		if (!OwnerTool->ContourMechanic->TrackFaceWithAutoFraming(MeshOffset))
		{
			OwnerTool->DisplayError(LOCTEXT("FailedToAutoTrackFaceLandmarks", "Failed to Auto-Track Face Contour Landmarks.\nUse \"Trace Facial Features\" in \"Manual Solve Actions\". See documentation for best practices."));
		}
	}
	
	constexpr bool bEstimateJointsFromMesh = false;
	
	TMap<int32, FVector3f> KeyPointCorrespondences = GetKeyPointCorrespondencesFromCharacter();
	FMetaHumanCharacterTargetTrackingResults TrackingResults = GetFaceTrackingFromCharacter();
	
	FConformTargetParams ConformTargetParams{
		.ConformTargetMesh = ConformTargetMesh,
		.KeyPointTargets = KeyPointCorrespondences,
		.CurveTrackingPoints = UE::MetaHuman::GetSolverTrackingPoints(TrackingResults.CurveTrackingPoints),
		.CameraViewInfo = TrackingResults.CameraViewInfo,
		.ImageSize = TrackingResults.ImageSize,
		.bEstimateBodyJointsFromMesh = bEstimateJointsFromMesh,
		.bAutoSolve = bIsAutoSolve,
		.BodyConformSolveSettings = BodySolveSettings
	};

	if (bDisableBodySolve)
	{
		ConformTargetParams.BodyConformSolveSettings.Iterations = 0;
	}
	
	if (bDisableFaceSolve)
	{
		ConformTargetParams.BodyConformSolveSettings.FaceIterations = 0;
	}
	
	// Set default pipeline names if empty
	if (bIsAutoSolve)
	{
		ETargetPartsType TargetPartsType = ConformTargetMesh.TargetPartsType;
		if (ConformTargetParams.BodyConformSolveSettings.PipelineName.IsEmpty())
		{
			if (TargetPartsType == ETargetPartsType::Combined)
			{
				ConformTargetParams.BodyConformSolveSettings.PipelineName = "combined";
			}
			else if (TargetPartsType == ETargetPartsType::BodyOnly)
			{
				ConformTargetParams.BodyConformSolveSettings.PipelineName = "body_only";
			}
			else if (TargetPartsType == ETargetPartsType::HeadOnly)
			{
				ConformTargetParams.BodyConformSolveSettings.PipelineName = "head_only";
			}
			else if (TargetPartsType == ETargetPartsType::HeadAndBody)
			{
				ConformTargetParams.BodyConformSolveSettings.PipelineName = "head_body";
			}
		}
	}
	
	// Capture body and face states before any conform operations for undo
	OwnerTool->PreConformBodyState = Subsystem->CopyBodyState(MetaHumanCharacter);
	OwnerTool->PreConformFaceState = Subsystem->CopyFaceState(MetaHumanCharacter);

	FMetaHumanCharacterTargetMeshKey TargetMeshKey = GetTargetMeshKey();
	const bool bConformStarted = Subsystem->ConformTargetMeshesAsync(MetaHumanCharacter, TargetMeshKey, ConformTargetParams);
	if (bConformStarted)
	{
		UpdateClothVisibility(MetaHumanCharacter, true);
	}
	else
	{
		OwnerTool->DisplayError( LOCTEXT("FailedToStartBodyConform", "Failed to Start Body Conform"));
	}

	{
		UE::MetaHuman::Analytics::FMeshConformEventExtras Extras;
		if (bIsAutoSolve)
		{
			Extras.Step = TEXT("Auto");
		}
		else if (bDisableBodySolve && !bDisableFaceSolve)
		{
			Extras.Step = TEXT("FaceOnly");
		}
		else if (bDisableFaceSolve && !bDisableBodySolve)
		{
			Extras.Step = TEXT("BodyOnly");
		}
		else
		{
			Extras.Step = TEXT("Unknown");
		}
		switch (ConformTargetMesh.TargetPartsType)
		{
			case ETargetPartsType::Combined:    Extras.TargetPartsType = TEXT("Combined");    break;
			case ETargetPartsType::BodyOnly:    Extras.TargetPartsType = TEXT("BodyOnly");    break;
			case ETargetPartsType::HeadOnly:    Extras.TargetPartsType = TEXT("HeadOnly");    break;
			case ETargetPartsType::HeadAndBody: Extras.TargetPartsType = TEXT("HeadAndBody"); break;
			default:                            Extras.TargetPartsType = TEXT("Unknown");     break;
		}
		Extras.bSuccess      = bConformStarted;
		Extras.bHasKeyPoints = KeyPointCorrespondences.Num() > 0;
		UE::MetaHuman::Analytics::RecordMeshConformEvent(MetaHumanCharacter, Extras);
	}

	OwnerTool->OnUpdateFacialTrackingCurvesDelegate.Broadcast();
}

FConformTargetMesh UMetaHumanCharacterEditorMeshImportToolProperties::GetConformTargetMesh() const
{
	TArray<FVector3f> TargetBodyVertices;
	TArray<int32> TargetBodyVertexIndices;

	ETargetPartsType TargetPartsType = ETargetPartsType::Combined;
	TArray<FVector3f> TargetHeadVertices;
	TArray<int32> TargetHeadVertexIndices;

	if (bUseCharacterParts)
	{
		if (BodyMesh.IsValid() && !HeadMesh.IsValid())
		{
			TObjectPtr<UObject> BodyMeshObjectPtr = BodyMesh.LoadSynchronous();
			UMetaHumanCharacterEditorSubsystem::GetMeshDataForConforming(BodyMeshObjectPtr, TargetBodyVertices, TargetBodyVertexIndices);

			TargetPartsType = ETargetPartsType::BodyOnly;
		}
		else if (!BodyMesh.IsValid() && HeadMesh.IsValid())
		{
			TObjectPtr<UObject> HeadMeshObjectPtr = HeadMesh.LoadSynchronous();
			UMetaHumanCharacterEditorSubsystem::GetMeshDataForConforming(HeadMeshObjectPtr, TargetHeadVertices, TargetHeadVertexIndices);

			TargetPartsType = ETargetPartsType::HeadOnly;
		}
		else
		{
			TObjectPtr<UObject> BodyMeshObjectPtr = BodyMesh.LoadSynchronous();
			UMetaHumanCharacterEditorSubsystem::GetMeshDataForConforming(BodyMeshObjectPtr, TargetBodyVertices, TargetBodyVertexIndices);
			TObjectPtr<UObject> HeadMeshObjectPtr = HeadMesh.LoadSynchronous();
			UMetaHumanCharacterEditorSubsystem::GetMeshDataForConforming(HeadMeshObjectPtr, TargetHeadVertices, TargetHeadVertexIndices);

			TargetPartsType = ETargetPartsType::HeadAndBody;
		}
	}
	else
	{
		TObjectPtr<UObject> CombinedStaticOrSkelMesh = CombinedMesh.LoadSynchronous();
		UMetaHumanCharacterEditorSubsystem::GetMeshDataForConforming(CombinedStaticOrSkelMesh, TargetBodyVertices, TargetBodyVertexIndices);
	}

	FConformTargetMesh TargetMesh{
		.TargetPartsType = TargetPartsType,
		.BodyVertices = TargetBodyVertices,
		.BodyVertexIndices = TargetBodyVertexIndices,
		.HeadVertices = TargetHeadVertices,
		.HeadVertexIndices = TargetHeadVertexIndices
	};

	return TargetMesh;
}

// -------------------------------------------------------------------
// UMetaHumanCharacterEditorMeshImportTool implementation ------------
// -------------------------------------------------------------------

void UMetaHumanCharacterEditorMeshImportTool::Setup()
{
	Super::Setup();
	SetToolDisplayName(LOCTEXT("MeshImportToolName", "Mesh Import"));

	MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	UMetaHumanCharacterEditorSubsystem* MetaHumanSubsystem = UMetaHumanCharacterEditorSubsystem::Get();

	SetupBodyState = MetaHumanSubsystem->CopyBodyState(MetaHumanCharacter);
	SetupFaceState = MetaHumanSubsystem->CopyFaceState(MetaHumanCharacter);
	
	MeshImportProperties = NewObject<UMetaHumanCharacterEditorMeshImportToolProperties>(this);
	AddToolPropertySource(MeshImportProperties);
	FString CharacterPathCacheString = FSoftObjectPath(MetaHumanCharacter).ToString();
	MeshImportProperties->RestoreProperties(this, CharacterPathCacheString);

	constexpr bool bEvaluatePose = true;
	MetaHumanSubsystem->SetEvaluateBodyPose(MetaHumanCharacter, bEvaluatePose);
	MeshImportProperties->BodyDelta = MetaHumanSubsystem->GetBodyState(MetaHumanCharacter)->GetGlobalDeltaScale();
	
	KeyPointsMechanic3D = NewObject<UMeshTarget3DKeyPointMechanic>(this);
	KeyPointsMechanic3D->Setup(this);
	KeyPointsMechanic3D->SetManipulatorsScale(MeshImportProperties->KeyPointsScale);

	ContourMechanic = NewObject<UMeshTargetContourMechanic>(this);
	ContourMechanic->Setup(this);

	OnAsyncConformIterationDelegateHandle = MetaHumanSubsystem->OnAsyncMeshConformIteration(MetaHumanCharacter).AddWeakLambda(this, [this] (const FMetaHumanRigEvaluatedState& InBodyVerticesAndNormals, const FMetaHumanRigEvaluatedState& InFaceVerticesAndNormals)
	{
		constexpr bool bRebuildKeyPoints = false;
		OnCharacterVerticesChanged(InBodyVerticesAndNormals.Vertices, InFaceVerticesAndNormals.Vertices, bRebuildKeyPoints);
	});

	OnAsyncConformCompleteDelegateHandle = MetaHumanSubsystem->OnAsyncMeshConformCompleted(MetaHumanCharacter).AddWeakLambda(this, [this] (bool bSuccess, bool bWasCancelled)
	{
		MeshImportProperties->OnAsyncConformComplete(bSuccess, bWasCancelled);
		constexpr bool bRebuildKeyPoints = false;
		OnCharacterStateChanged(bRebuildKeyPoints);
	});

	OnBodyStateChangedDelegateHandle = MetaHumanSubsystem->OnBodyStateChanged(MetaHumanCharacter).AddWeakLambda(this, [this]
	{
		constexpr bool bRebuildKeyPoints = true;
		OnCharacterStateChanged(bRebuildKeyPoints);
	});

	OnTargetMeshKeyPointsChangedDelegateHandle = MetaHumanSubsystem->OnTargetMeshKeyPointsChanged(MetaHumanCharacter).AddWeakLambda(this, [this]
	{
		if (!bIsRebuildingKeyPoints && KeyPointsMechanic3D && KeyPointsMechanic3D->IsInitialized())
		{
			TGuardValue<bool> RebuildGuard(bIsRebuildingKeyPoints, true);
			Initialize3DKeyPoints();
		}
	});

	OnPreviewMaterialChangedFromGrayDelagateHandle = MetaHumanSubsystem->OnPreviewMaterialChanged(MetaHumanCharacter).AddWeakLambda(this, [this] 
	{
		MeshImportProperties->bUseGrayMaterialOnMetaHuman = false;
	});
	
	MeshTargetScene = NewObject<UMetaHumanCharacterEditorMeshImportTargetScene>(this);
	if (TStrongObjectPtr<UWorld> TargetWorldPtr = TargetWorld.Pin())
	{
		MeshTargetScene->Initialize(MetaHumanCharacter, TargetWorldPtr.Get());
		
		TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> BodyState = UMetaHumanCharacterEditorSubsystem::Get()->GetBodyState(MetaHumanCharacter);
		TSharedRef<const FMetaHumanCharacterIdentity::FState> FaceState = UMetaHumanCharacterEditorSubsystem::Get()->GetFaceState(MetaHumanCharacter);
		MeshTargetScene->BuildCharacterDynamicMeshes(BodyState->GetVerticesAndVertexNormals().Vertices, FaceState->Evaluate().Vertices, MeshImportProperties->bShowBodyModelMesh);
	}

	
	// Restore the last used target mesh so the import tool opens in the same state as when it was last used
	const FMetaHumanCharacterTargetMeshKey& LastKey = MetaHumanCharacter->LastTargetMeshKey;
	if (!LastKey.BodyMesh.IsNull() || !LastKey.HeadMesh.IsNull())
	{
		MeshImportProperties->bUseCharacterParts = true;
		MeshImportProperties->Mode = EMetaHumanMeshImportMode::MeshParts;
		MeshImportProperties->BodyMesh = LastKey.BodyMesh;
		MeshImportProperties->HeadMesh = LastKey.HeadMesh;
	}
	else if (!LastKey.CombinedMesh.IsNull())
	{
		MeshImportProperties->bUseCharacterParts = false;
		MeshImportProperties->Mode = EMetaHumanMeshImportMode::Single;
		MeshImportProperties->CombinedMesh = LastKey.CombinedMesh;
	}

	OnTargetMeshChange();

	MeshImportProperties->UpdateKeyPointVisibility();

	// Update MH Materials
	UpdateTargetMeshMaterial();

	if(MeshImportProperties->bUseGrayMaterialOnMetaHuman)
	{
		UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

		LastUsedPreviewMaterial = MetaHumanCharacter->PreviewMaterialType;
		// Transient: do NOT persist Gray to the asset's PreviewMaterialType UPROPERTY.
		Subsystem->UpdateCharacterPreviewMaterial(MetaHumanCharacter, EMetaHumanCharacterSkinPreviewMaterial::Gray, /*bWritePersistentState=*/false);

		Subsystem->UpdateTranslucencyOnActor(MetaHumanCharacter, 1.0f - MeshImportProperties->MetaHumanOpacity);
		Subsystem->UpdateMatcapMaterialColorOnActor(MetaHumanCharacter, MeshImportProperties->MetaHumanMeshColor);
		Subsystem->SetGuideTexturesOnActor(MetaHumanCharacter, MeshImportProperties->bUseGuidesTextureOnMetaHuman);
	}

	ApplyOverlayState();

	// Register right-click drag behavior for keypoint interaction.
    // Left-click (from base class) handles character mesh; both require Ctrl to create/remove keypoints.
    ULocalClickDragInputBehavior* RightClickBehavior = NewObject<ULocalClickDragInputBehavior>(this);
    RightClickBehavior->SetUseRightMouseButton();

	RightClickBehavior->CanBeginClickDragFunc = [this](const FInputDeviceRay& PressPos) -> FInputRayHit
	{
		FHitResult OutHit;

		// Check manipulators first
		if (KeyPointsMechanic3D->HitTest(PressPos.WorldRay, OutHit))
		{
			return FInputRayHit(OutHit.Distance);
		}

		// Mesh hit is only used for Ctrl+RMB keypoint operations; without Ctrl let the
		// viewport handle RMB for zooming even when the cursor is over the mesh
		if (!FSlateApplication::Get().GetModifierKeys().IsControlDown())
		{
			return FInputRayHit();
		}

		constexpr bool bTestTarget = true;
		constexpr bool bTestChar = false;
		constexpr bool bSelectVertexId = true;

		FMetaHumanTargetHitResult HitResult;
		if (MeshTargetScene->HitTestMesh(PressPos.WorldRay, bTestTarget, bTestChar, bSelectVertexId, HitResult))
		{
			LastHitResult = HitResult;
			return FInputRayHit(HitResult.HitResult.Distance);
		}

		return FInputRayHit();
	};

    RightClickBehavior->OnClickPressFunc = [this](const FInputDeviceRay& PressPos)
    {
		bRightClickActive = true;
        LastWorldRay = PressPos.WorldRay;
        KeyPointsMechanic3D->OnBeginDrag(GetCtrlToggle(), /*bRightClick=*/true, GetShiftToggle());
    };

    RightClickBehavior->OnClickDragFunc = [this](const FInputDeviceRay& DragPos)
    {
        LastWorldRay = DragPos.WorldRay;
    };

    RightClickBehavior->OnClickReleaseFunc = [this](const FInputDeviceRay& ReleasePos)
    {
        LastWorldRay = ReleasePos.WorldRay;
        KeyPointsMechanic3D->OnEndDrag(LastHitResult);
		bRightClickActive = false;
		UpdateSelectedPointFromMechanic();
    };

    RightClickBehavior->Initialize();
    AddInputBehavior(RightClickBehavior);

	InputProcessor = MakeShared<FMeshImportToolInputProcessor>(this);
	FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
}


void UMetaHumanCharacterEditorMeshImportTool::Shutdown(EToolShutdownType InShutdownType)
{
	UMetaHumanCharacterEditorSubsystem* MetaHumanSubsystem = UMetaHumanCharacterEditorSubsystem::Get();

	if (OnAsyncConformIterationDelegateHandle.IsValid())
	{
		MetaHumanSubsystem->OnAsyncMeshConformIteration(MetaHumanCharacter).Remove(OnAsyncConformIterationDelegateHandle);
	}

	if (OnAsyncConformCompleteDelegateHandle.IsValid())
	{
		MetaHumanSubsystem->OnAsyncMeshConformCompleted(MetaHumanCharacter).Remove(OnAsyncConformCompleteDelegateHandle);
	}

	if (OnBodyStateChangedDelegateHandle.IsValid())
	{
		MetaHumanSubsystem->OnBodyStateChanged(MetaHumanCharacter).Remove(OnBodyStateChangedDelegateHandle);
	}

	if (OnTargetMeshKeyPointsChangedDelegateHandle.IsValid())
	{
		MetaHumanSubsystem->OnTargetMeshKeyPointsChanged(MetaHumanCharacter).Remove(OnTargetMeshKeyPointsChangedDelegateHandle);
	}

	if (OnPreviewMaterialChangedFromGrayDelagateHandle.IsValid())
	{
		MetaHumanSubsystem->OnPreviewMaterialChanged(MetaHumanCharacter).Remove(OnPreviewMaterialChangedFromGrayDelagateHandle);
	}

	// Cancel any in-flight target mesh load
	if (TargetMeshStreamableHandle.IsValid())
	{
		TargetMeshStreamableHandle->CancelHandle();
		TargetMeshStreamableHandle.Reset();
	}
	bIsLoadingTargetMesh = false;
	if (TSharedPtr<SNotificationItem> Notification = TargetMeshLoadNotification.Pin())
	{
		Notification->SetCompletionState(SNotificationItem::CS_None);
		Notification->ExpireAndFadeout();
	}

	// Cancel async process if running
	MetaHumanSubsystem->CancelMeshAsyncProcess(MetaHumanCharacter);

	if (InputProcessor.IsValid())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
		InputProcessor.Reset();
	}
	
	constexpr bool bEvaluatePose = false;
	MetaHumanSubsystem->SetEvaluateBodyPose(MetaHumanCharacter, bEvaluatePose);

	if (bNeedsFullUpdate)
	{
		MetaHumanSubsystem->CommitPosedStateAsAPose(MetaHumanCharacter, ConformedTargetMeshKey);
	}
	else
	{
		MetaHumanSubsystem->ApplyBodyState(MetaHumanCharacter, SetupBodyState.ToSharedRef(), UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Minimal);
		MetaHumanSubsystem->ApplyFaceState(MetaHumanCharacter, SetupFaceState.ToSharedRef());
	}

	KeyPointsMechanic3D->Shutdown();
	ContourMechanic->Shutdown();
	MeshTargetScene->Shutdown();
	
	FString CharacterPathCacheString = FSoftObjectPath(MetaHumanCharacter).ToString();
	MeshImportProperties->SaveProperties(this, CharacterPathCacheString);
	
	GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserError);
	
	SetViewportClient(nullptr);
	
	// Restore the live MIDs back to whatever was active when the tool opened. The asset's
	// PreviewMaterialType UPROPERTY was never mutated by the matcap toggle (bWritePersistentState=false),
	// so this path also runs transient — Shutdown never dirties the asset post-Save.
	// We must NOT inequality-guard on PreviewMaterialType here: with bWritePersistentState=false
	// the UPROPERTY is unchanged, so an `!=` check is always false and the restore would be
	// skipped, leaving the live MIDs stuck on Gray after the tool closes.
	if(LastUsedPreviewMaterial.IsSet())
	{
		UMetaHumanCharacterEditorSubsystem::Get()->UpdateCharacterPreviewMaterial(MetaHumanCharacter, LastUsedPreviewMaterial.GetValue(), /*bWritePersistentState=*/false);
		LastUsedPreviewMaterial.Reset();
	}

	bHasBeenShutdown = true;

	Super::Shutdown(InShutdownType);
}

void UMetaHumanCharacterEditorMeshImportTool::UpdateMeshLocationFromOffset() const
{
	MeshTargetScene->SetMeshLocation(MeshImportProperties->MeshOffset);		
	KeyPointsMechanic3D->UpdateComponentTransform();
	ContourMechanic->UpdateComponentTransform();
}

void UMetaHumanCharacterEditorMeshImportTool::Initialize3DKeyPoints()
{
	FMetaHumanCharacterTargetMeshKey TargetMeshKey = MeshImportProperties->GetTargetMeshKey();

	KeyPointsMechanic3D->Initialize(MetaHumanCharacter,
		TargetMeshKey,
		UMetaHumanCharacterEditorSubsystem::Get()->GetBodyState(MetaHumanCharacter),
		UMetaHumanCharacterEditorSubsystem::Get()->GetFaceState(MetaHumanCharacter),
		MeshTargetScene);
}

void UMetaHumanCharacterEditorMeshImportTool::OnCharacterStateChanged(bool bRebuildKeyPoints, bool bRebuildDynamicMeshes)
{
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyState = UMetaHumanCharacterEditorSubsystem::Get()->CopyBodyState(MetaHumanCharacter);
	TSharedRef<const FMetaHumanCharacterIdentity::FState> FaceState = UMetaHumanCharacterEditorSubsystem::Get()->GetFaceState(MetaHumanCharacter);
	
	constexpr bool bEvaluatePose = true;
	BodyState->SetEvaluatePose(bEvaluatePose);
	
	TArray<FVector3f> BodyVertices = BodyState->GetVerticesAndVertexNormals().Vertices;
	TArray<FVector3f> FaceVertices = FaceState->Evaluate().Vertices;
	
	OnCharacterVerticesChanged(BodyVertices, FaceVertices, bRebuildKeyPoints, bRebuildDynamicMeshes);
}

void UMetaHumanCharacterEditorMeshImportTool::OnCharacterVerticesChanged(const TArray<FVector3f>& InBodyVertices, const TArray<FVector3f>& InFaceVertices, bool bRebuildKeyPoints, bool bRebuildDynamicMeshes)
{
	if (bRebuildDynamicMeshes)
	{
		MeshTargetScene->BuildCharacterDynamicMeshes(InBodyVertices, InFaceVertices, MeshImportProperties->bShowBodyModelMesh);
	}

	if (bRebuildKeyPoints)
	{
		Initialize3DKeyPoints();
	}
	else
	{
		KeyPointsMechanic3D->UpdateCharacterManipulatorPositions(InBodyVertices, InFaceVertices, UMetaHumanCharacterEditorSubsystem::Get()->GetFaceState(MetaHumanCharacter));	
	}
}

bool UMetaHumanCharacterEditorMeshImportTool::HitTest(const FRay& InRay, FHitResult& OutHit)
{
	LastHitResult.HitResult = FHitResult();
	LastHitResult.HitMeshType = EHitMeshType::None;
	LastHitResult.HitVertexID = INDEX_NONE;
	
	if (KeyPointsMechanic3D->HitTest(InRay, OutHit))
	{
		return true;
	}

	bool bTestTarget = true;
	bool bTestChar = !bRightClickActive;
	constexpr bool bSelectVertexId = true;
	FMetaHumanTargetHitResult HitResult;
	if (MeshTargetScene->HitTestMesh(InRay, bTestTarget, bTestChar, bSelectVertexId,HitResult))
	{
		LastHitResult = HitResult;
		OutHit = LastHitResult.HitResult;	
		return true;
	}
	
	return false;
}

FInputRayHit UMetaHumanCharacterEditorMeshImportTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	FHitResult OutHit;

	if (KeyPointsMechanic3D->HitTest(PressPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}

	// Mesh hit is only used for Ctrl+LMB keypoint operations; without Ctrl let the
	// viewport handle LMB for orbiting even when the cursor is over the mesh
	if (!FSlateApplication::Get().GetModifierKeys().IsControlDown())
	{
		// If a keypoint is selected, claim input so we can deselect on empty click
		if (KeyPointsMechanic3D && KeyPointsMechanic3D->GetSelectedManipulator() != INDEX_NONE)
		{
			return FInputRayHit(TNumericLimits<float>::Max());  // Claim with low priority
		}

		return FInputRayHit();
	}

	constexpr bool bTestTarget = false;
	constexpr bool bTestChar = true;
	constexpr bool bSelectVertexId = true;

	FMetaHumanTargetHitResult HitResult;
	if (MeshTargetScene->HitTestMesh(PressPos.WorldRay, bTestTarget, bTestChar, bSelectVertexId, HitResult))
	{
		LastHitResult = HitResult;
		return FInputRayHit(HitResult.HitResult.Distance);
	}

	return FInputRayHit();
}

void UMetaHumanCharacterEditorMeshImportTool::OnBeginDrag(const FRay& InRay)
{
	// This function handles the left click only by default behavior
	KeyPointsMechanic3D->OnBeginDrag(GetCtrlToggle(), /*bIsRightClick*/false, GetShiftToggle());
}

void UMetaHumanCharacterEditorMeshImportTool::OnUpdateDrag(const FRay& InRay)
{
}

void UMetaHumanCharacterEditorMeshImportTool::OnEndDrag(const FRay& InRay)
{
	if (!FSlateApplication::Get().GetModifierKeys().IsAltDown())
	{
		KeyPointsMechanic3D->OnEndDrag(LastHitResult);
		UpdateSelectedPointFromMechanic();
	}
}

void UMetaHumanCharacterEditorMeshImportTool::SetShowAPose(bool bSetToAPose)
{
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

	Subsystem->SetEvaluateBodyPose(MetaHumanCharacter, !bSetToAPose);
	
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyState = Subsystem->CopyBodyState(MetaHumanCharacter);
	BodyState->SetEvaluatePose(!bSetToAPose);
	TSharedRef<FMetaHumanCharacterIdentity::FState> FaceState = Subsystem->CopyFaceState(MetaHumanCharacter);
	
	const FMetaHumanRigEvaluatedState VerticesAndVertexNormals = BodyState->GetVerticesAndVertexNormals();	
	FaceState->SetBodyJointsAndBodyFaceVertices(BodyState->CopyBindPose(), VerticesAndVertexNormals.Vertices);
	FaceState->SetBodyVertexNormals(VerticesAndVertexNormals.VertexNormals, BodyState->GetNumVerticesPerLOD());
				
	FFitToTargetOptions Options;
	Options.AlignmentOptions = EAlignmentOptions::None;
	FaceState->FitFromBodyVertices(VerticesAndVertexNormals.Vertices, Options);

	Subsystem->ApplyBodyState(MetaHumanCharacter, BodyState, UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Minimal);
	Subsystem->ApplyFaceState(MetaHumanCharacter, FaceState);
}

void UMetaHumanCharacterEditorMeshImportTool::OnPropertyModified(UObject* InPropertySet, FProperty* InProperty)
{
	if (InPropertySet != MeshImportProperties || !InProperty)
	{
		return;
	}
	
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, CombinedMesh) ||
		InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, BodyMesh) ||
		InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, HeadMesh) ||
		InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bUseCharacterParts))
	{
		bTargetMeshChangePending = true;
	}
	else if(InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bUseGrayMaterialOnMesh))
	{
		UpdateTargetMeshMaterial();
	}
	else if(InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bUseGrayMaterialOnMetaHuman))
	{
		if(MeshImportProperties->bUseGrayMaterialOnMetaHuman)
		{
			LastUsedPreviewMaterial = MetaHumanCharacter->PreviewMaterialType;
			// Transient: do NOT persist Gray to the asset's PreviewMaterialType UPROPERTY.
			Subsystem->UpdateCharacterPreviewMaterial(MetaHumanCharacter, EMetaHumanCharacterSkinPreviewMaterial::Gray, /*bWritePersistentState=*/false);

			Subsystem->UpdateTranslucencyOnActor(MetaHumanCharacter, 1.0f - MeshImportProperties->MetaHumanOpacity);
			Subsystem->UpdateMatcapMaterialColorOnActor(MetaHumanCharacter, MeshImportProperties->MetaHumanMeshColor);
			Subsystem->SetGuideTexturesOnActor(MetaHumanCharacter, MeshImportProperties->bUseGuidesTextureOnMetaHuman);
		}
		else
		{
			// Restore mirrors the transient apply: because bWritePersistentState=false never
			// mutated PreviewMaterialType, the old inequality guard (PreviewMaterialType !=
			// LastUsedPreviewMaterial) is always false and would skip the restore — leaving
			// the live MIDs stuck on Gray. We always refresh the live MIDs to clear the override.
			if(LastUsedPreviewMaterial.IsSet())
			{
				Subsystem->UpdateCharacterPreviewMaterial(MetaHumanCharacter, LastUsedPreviewMaterial.GetValue(), /*bWritePersistentState=*/false);
				LastUsedPreviewMaterial.Reset();
			}
		}

		// Enabling/disabling MH matcap changes whether the overlay should be active
		ApplyOverlayState();
	}
	else if(InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, MeshOpacity))
	{
		UpdateTargetMeshMaterial();
	}
	else if(InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, MetaHumanOpacity))
	{
		UMetaHumanCharacterEditorSubsystem::Get()->UpdateTranslucencyOnActor(MetaHumanCharacter, 1.0f - MeshImportProperties->MetaHumanOpacity);
	}
	else if(InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, MeshColor))
	{
		UpdateTargetMeshMaterial();
	}
	else if(InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, MetaHumanMeshColor))
	{
		Subsystem->UpdateMatcapMaterialColorOnActor(MetaHumanCharacter, MeshImportProperties->MetaHumanMeshColor);
	}
	else if(InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, Overlay))
	{
		ApplyOverlayState();
	}
	else if(InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bUseGuidesTextureOnMetaHuman))
	{
		Subsystem->SetGuideTexturesOnActor(MetaHumanCharacter, MeshImportProperties->bUseGuidesTextureOnMetaHuman);
	}
	else if(InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bShowBodyModelMesh) ||
		InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bShowBodyModelAPose))
	{
		TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyState = Subsystem->CopyBodyState(MetaHumanCharacter);
		const bool bEvaluatePose = !MeshImportProperties->bShowBodyModelAPose;
		BodyState->SetEvaluatePose(bEvaluatePose);
		TSharedRef<const FMetaHumanCharacterIdentity::FState> FaceState = Subsystem->GetFaceState(MetaHumanCharacter);
		MeshTargetScene->BuildCharacterDynamicMeshes(BodyState->GetVerticesAndVertexNormals().Vertices, FaceState->Evaluate().Vertices, MeshImportProperties->bShowBodyModelMesh);
	}
	else if(InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bShowMetaHumanAPose))
	{
		SetShowAPose(MeshImportProperties->bShowMetaHumanAPose);
	}
	else if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, KeyPointsScale))
	{
		KeyPointsMechanic3D->SetManipulatorsScale(MeshImportProperties->KeyPointsScale);
	}
	else if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bShowFacialTracking) ||
			 InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bEditFacialCurves) ||
			 InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, TrackingCurvesColor)||
			 InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, TrackingPointsColor) ||
			 InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, TrackingPointsSize))
	{
		OnUpdateFacialTrackingCurvesDelegate.Broadcast();
	}
	else if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, CustomKeyPointsColor))
	{
		KeyPointsMechanic3D->SetTargetManipulatorsColor(MeshImportProperties->CustomKeyPointsColor);
	}
	else if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, MHKeyPointsColor))
	{
		KeyPointsMechanic3D->SetCharacterManipulatorsColor(MeshImportProperties->MHKeyPointsColor);
	}
	else if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bShowCustomKeyPoints) ||
			 InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bShowPresetKeyPoints))
	{
		MeshImportProperties->UpdateKeyPointVisibility();
	}

	UpdateMeshLocationFromOffset();
}

TSharedPtr<FMetaHumanCurveDataController> UMetaHumanCharacterEditorMeshImportTool::GetCurveDataController() const
{
	return ContourMechanic ? ContourMechanic->GetCurveDataController() : nullptr;
}

UTexture* UMetaHumanCharacterEditorMeshImportTool::GetTrackingImageTexture() const
{
	return ContourMechanic ? ContourMechanic->GetTrackingImageTexture() : nullptr;
}

FIntPoint UMetaHumanCharacterEditorMeshImportTool::GetTrackingImageSize() const
{
	FIntPoint OutImageSize = FIntPoint::ZeroValue;
	if (ContourMechanic)
	{
		OutImageSize = ContourMechanic->GetTrackingImageSize();
	}
	return OutImageSize;
}

void UMetaHumanCharacterEditorMeshImportTool::RemoveAllKeyPoints()
{
	if(KeyPointsMechanic3D)
	{ 
		KeyPointsMechanic3D->RemoveAllKeyPoints();
	}
}

void UMetaHumanCharacterEditorMeshImportTool::DeleteFaceCurves()
{
	if (ContourMechanic)
	{
		ContourMechanic->DeleteFaceCurves();
	}
}

bool UMetaHumanCharacterEditorMeshImportTool::HasFaceCurves() const
{
	return ContourMechanic && ContourMechanic->HasTrackingResults();
}

void UMetaHumanCharacterEditorMeshImportTool::UpdateSelectedPointFromMechanic()
{
	FVector SelectedPos;
	if (KeyPointsMechanic3D->GetSelectedManipulatorWorldPosition(SelectedPos))
	{
		if (TSharedPtr<FMetaHumanCharacterViewportClient> Client = ViewportClient.Pin())
		{
			Client->SetSelectedPoint(SelectedPos);
		}
	}
}

void UMetaHumanCharacterEditorMeshImportTool::SetBodyDelta(float InBodyDelta, bool bRebuildDynamicMeshes)
{
	UMetaHumanCharacterEditorSubsystem::Get()->SetBodyGlobalDeltaScale(MetaHumanCharacter, InBodyDelta);

	constexpr bool bRebuildKeyPoints = false;
	OnCharacterStateChanged(bRebuildKeyPoints, bRebuildDynamicMeshes);
	SetNeedsFullUpdate(MeshImportProperties->GetTargetMeshKey());
}

void UMetaHumanCharacterEditorMeshImportTool::SetHeadDelta(float InHeadDelta, bool bRebuildDynamicMeshes)
{
	TSharedRef<FMetaHumanCharacterIdentity::FState> FaceState = UMetaHumanCharacterEditorSubsystem::Get()->CopyFaceState(MetaHumanCharacter);
	FaceState->SetFromBodyVertexDeltaScale(InHeadDelta);
	UMetaHumanCharacterEditorSubsystem::Get()->ApplyFaceState(MetaHumanCharacter, FaceState);

	constexpr bool bRebuildKeyPoints = false;
	OnCharacterStateChanged(bRebuildKeyPoints, bRebuildDynamicMeshes);
	SetNeedsFullUpdate(MeshImportProperties->GetTargetMeshKey());
}

void UMetaHumanCharacterEditorMeshImportTool::SetNeedsFullUpdate(const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey)
{
	bNeedsFullUpdate = true;
	ConformedTargetMeshKey = InTargetMeshKey;
}

void UMetaHumanCharacterEditorMeshImportTool::DeselectAllKeyPoints()
{
	if (KeyPointsMechanic3D)
	{
		KeyPointsMechanic3D->DeselectAll();
	}
}

void UMetaHumanCharacterEditorMeshImportTool::SetViewportClient(const TSharedPtr<FMetaHumanCharacterViewportClient>& InViewportClient)
{
	if (ViewportClient.IsValid())
	{
		TSharedPtr<FMetaHumanCharacterViewportClient> OldClient = ViewportClient.Pin();
		OldClient->SelectPointHitProvider = nullptr;
		OldClient->OnSelectPointOrbitStarted.RemoveAll(this);
		OldClient->ClearShortcuts();
	}

	ViewportClient = InViewportClient;

	if (ViewportClient.IsValid())
	{
		TSharedPtr<FMetaHumanCharacterViewportClient> Client = ViewportClient.Pin();

		Client->OnSelectPointOrbitStarted.AddUObject(this, &UMetaHumanCharacterEditorMeshImportTool::DeselectAllKeyPoints);

		TWeakObjectPtr<UMetaHumanCharacterEditorMeshImportTool> WeakThis = this;
		Client->SelectPointHitProvider = [WeakThis](const FRay& InRay, FVector& OutPoint) -> bool
		{
			UMetaHumanCharacterEditorMeshImportTool* Tool = WeakThis.Get();
			if (!Tool)
			{
				return false;
			}

			// Try MetaHuman mesh first
			if (const UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get())
			{
				FVector HitNormal;
				if (Subsystem->SelectBodyVertex(Tool->MetaHumanCharacter, InRay, OutPoint, HitNormal) != INDEX_NONE)
				{
					return true;
				}
			}

			// Fall back to custom target mesh
			FMetaHumanTargetHitResult HitResult;
			if (Tool->MeshTargetScene && Tool->MeshTargetScene->HitTestMesh(InRay, /*bTestTarget*/true, /*bTestChar*/false, /*bSelectVertexId*/false, HitResult))
			{
				OutPoint = HitResult.HitResult.Location;
				return true;
			}

			return false;
		};
		// Update shortcuts in viewport client

		ViewportClient.Pin()->SetShortcuts
		(
			{
				{ LOCTEXT("SelectDragKeyPointMH_ShortcutKey", "Select + Drag Key Point (MetaHuman)"), LOCTEXT("SelectDragKeyPointMH_ShortcutValue", "LMB Drag") },
				{ LOCTEXT("SelectDragKeyPointMesh_ShortcutKey", "Select + Drag Key Point (Target Mesh)"), LOCTEXT("SelectDragKeyPointMesh_ShortcutValue", "RMB Drag") },
				{ LOCTEXT("AddKeyPointMH_ShortcutKey", "Add Key Point (MetaHuman)"), LOCTEXT("AddKeyPointMH_ShortcutValue", "CTRL + LMB Click") },
				{ LOCTEXT("AddKeyPointMesh_ShortcutKey", "Add Key Point (TargetMesh)"), LOCTEXT("AddKeyPointMesh_ShortcutValue", "CTRL + RMB Click") },
				{ LOCTEXT("RemoveKeyPointMH_ShortcutKey", "Remove Key Point (MetaHuman)"), LOCTEXT("RemoveKeyPointMH_ShortcutValue", "CTRL + SHIFT + LMB Click") },
				{ LOCTEXT("RemoveKeyPointMesh_ShortcutKey", "Remove Key Point (TargetMesh)"), LOCTEXT("RemoveKeyPointMesh_ShortcutValue", "CTRL + SHIFT + RMB Click") }
			}
		);
	}
}

void UMetaHumanCharacterEditorMeshImportTool::OnTick(float InDeltaTime)
{
	if (bTargetMeshChangePending)
	{
		bTargetMeshChangePending = false;
		OnTargetMeshChange();
	}

	if (KeyPointsMechanic3D->IsDraggingManipulator())
	{
		EHitMeshType ManipulatorHitType = KeyPointsMechanic3D->GetDraggingManipulatorHitMeshType();
		bool bHitTestTarget = ManipulatorHitType == EHitMeshType::TargetBody || ManipulatorHitType == EHitMeshType::TargetHead;
		bool bHitTestChar = !bHitTestTarget;
		constexpr bool bSelectVertexId = true;
		FMetaHumanTargetHitResult HitResult;
		if (MeshTargetScene->HitTestMesh(LastWorldRay, bHitTestTarget, bHitTestChar, bSelectVertexId, HitResult))
		{
			LastHitResult = HitResult;
			KeyPointsMechanic3D->UpdateDraggingManipulatorPosition(LastHitResult);
			if (TSharedPtr<FMetaHumanCharacterViewportClient> Client = ViewportClient.Pin())
			{
				Client->SetSelectedPoint(LastHitResult.HitResult.Location);
			}
		}
	}
}

void UMetaHumanCharacterEditorMeshImportTool::Render(IToolsContextRenderAPI* InRenderAPI)
{
	Super::Render(InRenderAPI);
	ContourMechanic->Render(InRenderAPI);
	
	if (!MeshImportProperties->bShowKeyPoints)
	{
		return;
	}
	
	KeyPointsMechanic3D->Render(InRenderAPI);

	if (FPrimitiveDrawInterface* PDI = InRenderAPI->GetPrimitiveDrawInterface())
	{
		// See if there is a better way to check if FHitResult is valid
		if (GetCtrlToggle() && LastHitResult.HitResult.IsValidBlockingHit())
		{
			const FVector StartPoint = LastHitResult.HitResult.Location;
			const FVector EndPoint = LastHitResult.HitResult.Location + LastHitResult.HitResult.Normal * 5.0;
			const FLinearColor Color = (LastHitResult.HitMeshType == EHitMeshType::TargetBody || LastHitResult.HitMeshType == EHitMeshType::TargetHead) ? FLinearColor::Blue : FLinearColor::Red;
			constexpr uint8 DepthPriorityGroup = 0;
			constexpr float DepthBias = 0.0f;
			constexpr float Thickness = 0.0f;
			constexpr bool bScreenSpace = false;
			PDI->DrawLine(StartPoint,
				EndPoint,
				Color,
				DepthPriorityGroup,
				Thickness,
				DepthBias,
				bScreenSpace);
		}
	}
}

void UMetaHumanCharacterEditorMeshImportTool::DrawHUD(FCanvas* InCanvas, IToolsContextRenderAPI* InRenderAPI)
{
	Super::DrawHUD(InCanvas, InRenderAPI);
	if (!MeshImportProperties->bShowKeyPoints)
	{
		return;
	}
	
	KeyPointsMechanic3D->DrawHUD(InCanvas, InRenderAPI);
}

void UMetaHumanCharacterEditorMeshImportTool::OnTargetMeshChange()
{
	// Cancel any in-flight load
	if (TargetMeshStreamableHandle.IsValid())
	{
		TargetMeshStreamableHandle->CancelHandle();
		TargetMeshStreamableHandle.Reset();
	}

	// Dismiss any existing loading notification
	if (TSharedPtr<SNotificationItem> Notification = TargetMeshLoadNotification.Pin())
	{
		Notification->SetCompletionState(SNotificationItem::CS_None);
		Notification->ExpireAndFadeout();
	}

	// Collect soft paths to load
	TArray<FSoftObjectPath> PathsToLoad;
	if (MeshImportProperties->bUseCharacterParts)
	{
		if (!MeshImportProperties->BodyMesh.IsNull())
		{
			PathsToLoad.Add(MeshImportProperties->BodyMesh.ToSoftObjectPath());
		}
		if (!MeshImportProperties->HeadMesh.IsNull())
		{
			PathsToLoad.Add(MeshImportProperties->HeadMesh.ToSoftObjectPath());
		}
	}
	else
	{
		if (!MeshImportProperties->CombinedMesh.IsNull())
		{
			PathsToLoad.Add(MeshImportProperties->CombinedMesh.ToSoftObjectPath());
		}
	}

	// Persist the current mesh selection so the tool can restore it next time it opens.
	// Equality-guard the write so re-entering the tool with the same mesh selection does not
	// gratuitously dirty the asset (Setup() calls this even when nothing changed).
	{
		const FMetaHumanCharacterTargetMeshKey NewTargetMeshKey = MeshImportProperties->GetTargetMeshKey();
		if (MetaHumanCharacter->LastTargetMeshKey != NewTargetMeshKey)
		{
			MetaHumanCharacter->LastTargetMeshKey = NewTargetMeshKey;
			MetaHumanCharacter->MarkPackageDirty();
		}
	}

	// Clear existing target mesh before loading the new one
	MeshTargetScene->ClearTargetMesh();
	KeyPointsMechanic3D->Clear();
	ContourMechanic->Clear();
	OnUpdateFacialTrackingCurvesDelegate.Broadcast();

	if (!PathsToLoad.IsEmpty())
	{
		// Check whether the context object store already holds dynamic meshes for this exact
		// target-mesh key.  If so, skip the async asset load + conversion and apply the cached
		// meshes directly on the game thread.
		const FMetaHumanCharacterTargetMeshKey CurrentKey = MeshImportProperties->GetTargetMeshKey();
		UMetaHumanCharacterEditorMeshImportContextObject* ContextObject =
			GetToolManager()->GetContextObjectStore()->FindContext<UMetaHumanCharacterEditorMeshImportContextObject>();

		if (ContextObject && ContextObject->HasValidMeshesForKey(CurrentKey))
		{
			UObject* BodyMeshObj     = MeshImportProperties->bUseCharacterParts  ? MeshImportProperties->BodyMesh.Get()     : nullptr;
			UObject* HeadMeshObj     = MeshImportProperties->bUseCharacterParts  ? MeshImportProperties->HeadMesh.Get()     : nullptr;
			UObject* CombinedMeshObj = !MeshImportProperties->bUseCharacterParts ? MeshImportProperties->CombinedMesh.Get() : nullptr;

			const bool bTargetMeshesBuilt = MeshTargetScene->ApplyBuiltDynamicMeshesFromContextObject(
				*ContextObject,
				MeshImportProperties->bUseCharacterParts,
				BodyMeshObj, HeadMeshObj, CombinedMeshObj,
				MeshImportProperties->MeshOffset);

			if (bTargetMeshesBuilt)
			{
				UpdateTargetMeshMaterial();

				ContourMechanic->Initialize(MetaHumanCharacter, MeshTargetScene, CurrentKey);
				Initialize3DKeyPoints();

				UMetaHumanCharacterEditorSubsystem::Get()->SetToTargetPosedState(MetaHumanCharacter, CurrentKey);
				SetBodyDelta(MeshImportProperties->BodyDelta, false /*bRebuildDynamicMeshes*/);
				SetHeadDelta(MeshImportProperties->HeadDelta, true /*bRebuildDynamicMeshes*/); // Rebuild dynamic mesh once

				OnUpdateFacialTrackingCurvesDelegate.Broadcast();
				return;
			}
		}

		bIsLoadingTargetMesh = true;

		FNotificationInfo Info(LOCTEXT("LoadingTargetMesh", "Loading Target Mesh..."));
		Info.bFireAndForget = false;
		TargetMeshLoadNotification = FSlateNotificationManager::Get().AddNotification(Info);
		if (TSharedPtr<SNotificationItem> Notification = TargetMeshLoadNotification.Pin())
		{
			Notification->SetCompletionState(SNotificationItem::CS_Pending);
		}

		// Request async load
		FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
		TargetMeshStreamableHandle = StreamableManager.RequestAsyncLoad(
			PathsToLoad,
			FStreamableDelegate::CreateUObject(this, &UMetaHumanCharacterEditorMeshImportTool::OnTargetMeshLoaded));
	}
}


void UMetaHumanCharacterEditorMeshImportTool::OnTargetMeshLoaded()
{
	TargetMeshStreamableHandle.Reset();

	// Resolve loaded UObjects and extract mesh descriptions (game thread)
	bool bUseCharacterParts = MeshImportProperties->bUseCharacterParts;
	UObject* BodyMeshObj = nullptr;
	UObject* HeadMeshObj = nullptr;
	UObject* CombinedMeshObj = nullptr;
	FMeshDescription* BodyMeshDesc = nullptr;
	FMeshDescription* HeadMeshDesc = nullptr;
	
	if (bUseCharacterParts)
	{
		BodyMeshObj = MeshImportProperties->BodyMesh.Get();
		HeadMeshObj = MeshImportProperties->HeadMesh.Get();
		BodyMeshDesc = UE::MetaHuman::GetMeshDescription(BodyMeshObj);
		HeadMeshDesc = UE::MetaHuman::GetMeshDescription(HeadMeshObj);
	}
	else
	{
		CombinedMeshObj = MeshImportProperties->CombinedMesh.Get();
		BodyMeshDesc = UE::MetaHuman::GetMeshDescription(CombinedMeshObj);
	}

	TWeakObjectPtr<UMetaHumanCharacterEditorMeshImportTool> WeakThis(this);

	// Convert mesh descriptions to dynamic meshes on worker thread (the expensive part)
	Async(EAsyncExecution::ThreadPool,
		[WeakThis, bUseCharacterParts, BodyMeshDesc, HeadMeshDesc,
		 BodyMeshObj, HeadMeshObj, CombinedMeshObj]()
	{
		TSharedPtr<FDynamicMesh3> BodyDynMesh;
		TSharedPtr<FDynamicMesh3> HeadDynMesh;

		if (BodyMeshDesc)
		{
			BodyDynMesh = MakeShared<FDynamicMesh3>();
			FMeshDescriptionToDynamicMesh Converter;
			Converter.Convert(BodyMeshDesc, *BodyDynMesh);
		}
		if (HeadMeshDesc)
		{
			HeadDynMesh = MakeShared<FDynamicMesh3>();
			FMeshDescriptionToDynamicMesh Converter;
			Converter.Convert(HeadMeshDesc, *HeadDynMesh);
		}

		// Dispatch back to game thread for component setup
		Async(EAsyncExecution::TaskGraphMainTick,
			[WeakThis, bUseCharacterParts, BodyDynMesh, HeadDynMesh,
			 BodyMeshObj, HeadMeshObj, CombinedMeshObj]()
		{
			UMetaHumanCharacterEditorMeshImportTool* This = WeakThis.Get();
			if (!This || This->bHasBeenShutdown)
			{
				return;
			}
					
			FVector MeshOffset = This->MeshImportProperties->MeshOffset;

			const FMetaHumanCharacterTargetMeshKey TargetMeshKey = This->MeshImportProperties->GetTargetMeshKey();
			UMetaHumanCharacterEditorMeshImportContextObject* ContextObject =
				This->GetToolManager()->GetContextObjectStore()->FindContext<UMetaHumanCharacterEditorMeshImportContextObject>();

			bool bTargetMeshesBuilt = This->MeshTargetScene->ApplyBuiltDynamicMeshes(
				bUseCharacterParts, BodyDynMesh, HeadDynMesh,
				BodyMeshObj, HeadMeshObj, CombinedMeshObj,
				MeshOffset);

			// Populate the context object store so the meshes can be re-used on the next
			// tool restart without triggering another async load. Only written on success
			// so the store never holds a key that maps to degenerate/empty meshes.
			if (bTargetMeshesBuilt && ContextObject)
			{
				TSharedPtr<UE::Geometry::FDynamicMesh3> StoredBodyMesh;
				TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> StoredBodyAABBTree;
				if (BodyDynMesh)
				{
					StoredBodyMesh = MakeShared<UE::Geometry::FDynamicMesh3>(*BodyDynMesh);
					StoredBodyAABBTree = MakeShared<UE::Geometry::FDynamicMeshAABBTree3>(StoredBodyMesh.Get());
				}
				TSharedPtr<UE::Geometry::FDynamicMesh3> StoredHeadMesh;
				TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> StoredHeadAABBTree;
				if (HeadDynMesh)
				{
					StoredHeadMesh = MakeShared<UE::Geometry::FDynamicMesh3>(*HeadDynMesh);
					StoredHeadAABBTree = MakeShared<UE::Geometry::FDynamicMeshAABBTree3>(StoredHeadMesh.Get());
				}
				ContextObject->SetCachedMeshes(TargetMeshKey,
					MoveTemp(StoredBodyMesh), MoveTemp(StoredHeadMesh),
					MoveTemp(StoredBodyAABBTree), MoveTemp(StoredHeadAABBTree));
			}

			if (bTargetMeshesBuilt)
			{
				// Apply material settings when mesh is changed
				This->UpdateTargetMeshMaterial();
				
				This->ContourMechanic->Initialize(This->MetaHumanCharacter, This->MeshTargetScene, TargetMeshKey);
				This->Initialize3DKeyPoints();

				UMetaHumanCharacterEditorSubsystem::Get()->SetToTargetPosedState(This->MetaHumanCharacter, TargetMeshKey);
				This->SetBodyDelta(This->MeshImportProperties->BodyDelta, false /*bRebuildDynamicMeshes*/);
				This->SetHeadDelta(This->MeshImportProperties->HeadDelta, true /*bRebuildDynamicMeshes*/);
				
				This->OnUpdateFacialTrackingCurvesDelegate.Broadcast();
			}

			This->bIsLoadingTargetMesh = false;

			if (TSharedPtr<SNotificationItem> Notification = This->TargetMeshLoadNotification.Pin())
			{
				Notification->SetCompletionState(SNotificationItem::CS_None);
				Notification->ExpireAndFadeout();
			}
		});
	});
}

void UMetaHumanCharacterEditorMeshImportTool::UpdateTargetMeshMaterial()
{
	if (MeshImportProperties->bUseGrayMaterialOnMesh)
	{
		MeshTargetScene->SetTargetTranslucentMaterial();
		MeshTargetScene->SetMaterialTranslucency(1.0f - MeshImportProperties->MeshOpacity);
		MeshTargetScene->SetMaterialColor(MeshImportProperties->MeshColor);
	}
	else
	{
		if (MeshImportProperties->bUseCharacterParts)
		{
			UObject* BodyMesh = MeshImportProperties->BodyMesh.LoadSynchronous();
			UObject* HeadMesh = MeshImportProperties->HeadMesh.LoadSynchronous();
			MeshTargetScene->SetTargetMaterialsFromMeshes(BodyMesh, HeadMesh);
		}
		else
		{
			UObject* Mesh = MeshImportProperties->CombinedMesh.LoadSynchronous();
			MeshTargetScene->SetTargetMaterialFromMesh(Mesh);
		}
	}

	ApplyOverlayState();
}

void UMetaHumanCharacterEditorMeshImportTool::ApplyOverlayState()
{
	if (!MeshTargetScene || !MeshImportProperties)
	{
		return;
	}

	// Overlay is meaningful only when BOTH the target mesh and the MH character are rendering with the matcap/Gray preview material.
	const bool bOverlayActive = MeshImportProperties->bUseGrayMaterialOnMesh
	                         && MeshImportProperties->bUseGrayMaterialOnMetaHuman;
	if (!bOverlayActive)
	{
		MeshTargetScene->SetMeshDepthOffset(0.f);
		return;
	}

	constexpr float TargetMeshOverlayOffset = 100.f;
	switch (MeshImportProperties->Overlay)
	{
		case EMetaHumanCharacterOverlay::OverlayMesh:
			// Pull target toward camera so it renders in front of the MH.
			MeshTargetScene->SetMeshDepthOffset(-TargetMeshOverlayOffset);
			break;
		case EMetaHumanCharacterOverlay::OverlayMetaHuman:
			// Push target away from camera so the MH renders in front of it.
			MeshTargetScene->SetMeshDepthOffset(+TargetMeshOverlayOffset);
			break;
		case EMetaHumanCharacterOverlay::NoOverlay:
		default:
			MeshTargetScene->SetMeshDepthOffset(0.f);
			break;
	}
}

#undef LOCTEXT_NAMESPACE
