// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorBodyEditingTools.h"
#include "MetaHumanCharacterEditorViewportClient.h"
#include "Async/Async.h"
#include "Components/StaticMeshComponent.h"
#include "DNAUtils.h"
#include "Editor/EditorEngine.h"
#include "InteractiveToolManager.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorActorInterface.h"
#include "MetaHumanCharacterEditorCommands.h"
#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanRigEvaluatedState.h"
#include "Misc/ScopedSlowTask.h"
#include "SceneManagement.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ToolTargetManager.h"
#include "Tools/MetaHumanCharacterEditorToolCommandChange.h"
#include "Tools/MetaHumanCharacterEditorToolTargetUtil.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditor"

class FBodyParametricFitDNACommandChange : public FToolCommandChange
{
public:
	FBodyParametricFitDNACommandChange(
		const TArray<uint8>& InOldDNABuffer,
		const TSharedRef<FMetaHumanCharacterBodyIdentity::FState>& InNewState,
		TNotNull<UInteractiveToolManager*> InToolManager)
		: OldDNABuffer{ InOldDNABuffer }
		, NewState{ InNewState }
		, ToolManager{ InToolManager }
	{
	}

	//~Begin FToolCommandChange interface
	virtual void Apply(UObject* InObject) override
	{
		UMetaHumanCharacter* MetaHumanCharacter = CastChecked<UMetaHumanCharacter>(InObject);
		UMetaHumanCharacterEditorSubsystem::Get()->RemoveBodyRig(MetaHumanCharacter);
		UMetaHumanCharacterEditorSubsystem::Get()->CommitBodyState(MetaHumanCharacter, NewState);
	}

	virtual void Revert(UObject* InObject) override
	{
		UMetaHumanCharacter* MetaHumanCharacter = CastChecked<UMetaHumanCharacter>(InObject);

		TArray<uint8> BufferCopy;
		BufferCopy.SetNumUninitialized(OldDNABuffer.Num());
		FMemory::Memcpy(BufferCopy.GetData(), OldDNABuffer.GetData(), OldDNABuffer.Num());
		constexpr bool bImportingAsFixedBodyType = true;
		UMetaHumanCharacterEditorSubsystem::Get()->CommitBodyDNA(MetaHumanCharacter, ReadDNAFromBuffer(&BufferCopy, EDNADataLayer::All).ToSharedRef(), bImportingAsFixedBodyType);
	}

	virtual bool HasExpired(UObject* InObject) const override
	{
		// If the ToolManager is not valid anymore it means the asset editor was closed so mark the transaction as expired
		return !ToolManager.IsValid();
	}
	//~End FToolCommandChange interface

private:
	TArray<uint8> OldDNABuffer;
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> NewState;
	TWeakObjectPtr<UInteractiveToolManager> ToolManager;
};

bool UMetaHumanCharacterEditorBodyToolBuilder::CanBuildTool(const FToolBuilderState& InSceneState) const
{
	return Super::CanBuildTool(InSceneState) && !IsCharacterRigged(InSceneState);
}

UInteractiveTool* UMetaHumanCharacterEditorBodyToolBuilder::BuildTool(const FToolBuilderState& InSceneState) const
{
	UToolTarget* Target = InSceneState.TargetManager->BuildFirstSelectedTargetable(InSceneState, GetTargetRequirements());
	check(Target);

	switch (ToolType)
	{
	case EMetaHumanCharacterBodyEditingTool::Model:
		{
			UMetaHumanCharacterEditorBodyModelTool* BodyModelTool = NewObject<UMetaHumanCharacterEditorBodyModelTool>(InSceneState.ToolManager);
			BodyModelTool->SetTarget(Target);
			return BodyModelTool;
		}
		case EMetaHumanCharacterBodyEditingTool::Blend:
		{
			UMetaHumanCharacterEditorHeadAndBodyBlendTool* BlendTool = NewObject<UMetaHumanCharacterEditorHeadAndBodyBlendTool>(InSceneState.ToolManager);
			BlendTool->SetTarget(Target);
			BlendTool->SetWorld(InSceneState.World);
			return BlendTool;
		}
	}

	return nullptr;
}

void FMetaHumanCharacterClothVisibilityBase::UpdateClothVisibility(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, bool bStartBodyModeling, bool bUpdateMaterialHiddenFaces /*= true*/)
{
	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();

	if (MetaHumanCharacterSubsystem->IsCharacterOutfitSelected(InMetaHumanCharacter))
	{

		// Body is/was already hidden when modeling started so no need to hide it and update it later
		if(!(MetaHumanCharacterSubsystem->GetClothingVisibilityState(InMetaHumanCharacter) == EMetaHumanClothingVisibilityState::Hidden && bStartBodyModeling))
		{
			if (!bStartBodyModeling)
			{
				// Reset the stored preview material
				if (SavedPreviewMaterial.IsSet())
				{
					MetaHumanCharacterSubsystem->UpdateCharacterPreviewMaterial(InMetaHumanCharacter, SavedPreviewMaterial.GetValue());
					SavedPreviewMaterial.Reset();
				}

				// Reset the stored visibility state
				if(SavedClothingVisibilityState.IsSet())
				{
					MetaHumanCharacterSubsystem->SetClothingVisibilityState(InMetaHumanCharacter, SavedClothingVisibilityState.GetValue(), bUpdateMaterialHiddenFaces);
					SavedClothingVisibilityState.Reset();
				}
			}
			else
			{
				// Hide any outfit and revert to clay mode if the character has selected outfits
				SavedPreviewMaterial = InMetaHumanCharacter->PreviewMaterialType;
				SavedClothingVisibilityState = MetaHumanCharacterSubsystem->GetClothingVisibilityState(InMetaHumanCharacter);
				MetaHumanCharacterSubsystem->UpdateCharacterPreviewMaterial(InMetaHumanCharacter, EMetaHumanCharacterSkinPreviewMaterial::Clay);

				MetaHumanCharacterSubsystem->SetClothingVisibilityState(InMetaHumanCharacter, EMetaHumanClothingVisibilityState::Hidden, bUpdateMaterialHiddenFaces);
			}
		}
		
	}
}

void UMetaHumanCharacterParametricBodyProperties::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UMetaHumanCharacterEditorBodyModelTool* BodyModelTool = GetTypedOuter<UMetaHumanCharacterEditorBodyModelTool>();

	const FName PropertyName = PropertyChangedEvent.GetMemberPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterParametricBodyProperties, bScaleRangesByHeight) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterParametricBodyProperties, bUnlockBodyRanges) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterParametricBodyProperties, ParametersRangeMultiplier))
	{
		BodyModelTool->ParametricBodyProperties->OnBodyStateChanged();
	}
	else
	{
		// Forward to body parameter properties
		BodyModelTool->BodyParameterProperties->OnPostEditChangeProperty(PropertyChangedEvent);
	}
}

bool UMetaHumanCharacterParametricBodyProperties::IsFixedBodyType() const
{
	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCharacterEditorBodyModelTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorBodyModelTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());

	return MetaHumanCharacter->bFixedBodyType;
}

static void UpdateConstraintItem(const FMetaHumanCharacterBodyConstraint& InBodyConstraint, FMetaHumanCharacterBodyConstraintItemPtr& OutConstraintItem, bool bUnlockBodyRanges, float Multiplier = 1.f)
{
	OutConstraintItem->Name = InBodyConstraint.Name;
	OutConstraintItem->bIsActive = InBodyConstraint.bIsActive;
	OutConstraintItem->TargetMeasurement = InBodyConstraint.TargetMeasurement;
	OutConstraintItem->ActualMeasurement = InBodyConstraint.TargetMeasurement;

	float MinMeasurement = InBodyConstraint.MinMeasurement;
	float MaxMeasurement = InBodyConstraint.MaxMeasurement;
	if(bUnlockBodyRanges)
	{
		const float Center = (MinMeasurement + MaxMeasurement) * .5f;
		const float HalfRange = (MaxMeasurement - MinMeasurement) * .5f;
		const float NewHalfRange = HalfRange * Multiplier;

		MinMeasurement = Center - NewHalfRange;
		MaxMeasurement = Center + NewHalfRange;
	}

	OutConstraintItem->MinMeasurement = MinMeasurement;
	OutConstraintItem->MaxMeasurement = MaxMeasurement;
}

static TArray<FMetaHumanCharacterBodyConstraintItemPtr> BodyConstraintsToConstraintItems(const TArray<FMetaHumanCharacterBodyConstraint>& InBodyConstraints, bool bUnlockBodyRanges, float Multiplier = 1.f)
{
	TArray<FMetaHumanCharacterBodyConstraintItemPtr> BodyConstraintItems;
	for (const FMetaHumanCharacterBodyConstraint& BodyConstraint : InBodyConstraints)
	{
		FMetaHumanCharacterBodyConstraintItemPtr BodyConstraintItem = MakeShared<FMetaHumanCharacterBodyConstraintItem>();
		UpdateConstraintItem(BodyConstraint, BodyConstraintItem, bUnlockBodyRanges, Multiplier);

		BodyConstraintItems.Add(BodyConstraintItem);
	}
	return BodyConstraintItems;
}

static TArray<FMetaHumanCharacterBodyConstraint> BodyConstraintItemsToConstraints(const TArray<FMetaHumanCharacterBodyConstraintItemPtr>& InBodyConstraintItems)
{
	TArray<FMetaHumanCharacterBodyConstraint> BodyConstraints;
	for (const FMetaHumanCharacterBodyConstraintItemPtr& BodyConstraintItem : InBodyConstraintItems)
	{
		FMetaHumanCharacterBodyConstraint BodyConstraint;
		BodyConstraint.Name = BodyConstraintItem->Name;
		BodyConstraint.bIsActive = BodyConstraintItem->bIsActive;
		BodyConstraint.TargetMeasurement = BodyConstraintItem->TargetMeasurement;
		BodyConstraints.Add(BodyConstraint);
	}
	return BodyConstraints;
}

void UMetaHumanCharacterParametricBodyProperties::OnBeginConstraintEditing()
{
	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCharacterEditorBodyModelTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorBodyModelTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	UMaterialInterface* TranslucentMaterial = MetaHumanCharacterSubsystem->GetTranslucentClothingMaterial();

	UpdateClothVisibility(MetaHumanCharacter, true);
}

void UMetaHumanCharacterParametricBodyProperties::OnConstraintItemsChanged(bool bInCommitChange)
{
	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCharacterEditorBodyModelTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorBodyModelTool>(); 
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());

	TArray<FMetaHumanCharacterBodyConstraint> BodyConstraints = BodyConstraintItemsToConstraints(BodyConstraintItems);
	MetaHumanCharacterSubsystem->SetBodyConstraints(MetaHumanCharacter, BodyConstraints);

	if (bInCommitChange && PreviousBodyState.IsValid())
	{
		TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> BodyState = MetaHumanCharacterSubsystem->CopyBodyState(MetaHumanCharacter);

		UpdateClothVisibility(MetaHumanCharacter, false);

		MetaHumanCharacterSubsystem->CommitBodyState(MetaHumanCharacter, BodyState.ToSharedRef(), UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Minimal);
		OwnerTool->bNeedsFullUpdate = true;

		const FText CommandChangeDescription = LOCTEXT("BodyParametricCommandChange", "Adjust Parametric Body");
	
		// Creates a command change that allows the user to revert back the state
		TUniquePtr<FMetaHumanCharacterEditorBodyToolCommandChange> CommandChange = MakeUnique<FMetaHumanCharacterEditorBodyToolCommandChange>(PreviousBodyState.ToSharedRef(), BodyState.ToSharedRef(), OwnerTool->GetToolManager());
		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(MetaHumanCharacter, MoveTemp(CommandChange), CommandChangeDescription);
	
		PreviousBodyState = BodyState;
	}
	else
	{
		// Skip Min/Max range refresh on live update
		UpdateMeasurements(/*bUpdateRanges=*/false);
	}
}

void UMetaHumanCharacterParametricBodyProperties::ResetConstraints()
{
	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCharacterEditorBodyModelTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorBodyModelTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());

	MetaHumanCharacterSubsystem->ResetParametricBody(MetaHumanCharacter);

	TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> BodyState = MetaHumanCharacterSubsystem->CopyBodyState(MetaHumanCharacter);
	MetaHumanCharacterSubsystem->CommitBodyState(MetaHumanCharacter, BodyState.ToSharedRef(), UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Full);

	if (PreviousBodyState.IsValid())
	{
		const FText CommandChangeDescription = LOCTEXT("BodyParametricResetCommandChange", "Reset Parametric Body");
	
		// Creates a command change that allows the user to revert back the stateZ
		TUniquePtr<FMetaHumanCharacterEditorBodyToolCommandChange> CommandChange = MakeUnique<FMetaHumanCharacterEditorBodyToolCommandChange>(PreviousBodyState.ToSharedRef(), BodyState.ToSharedRef(), OwnerTool->GetToolManager());
		OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(MetaHumanCharacter, MoveTemp(CommandChange), CommandChangeDescription);

		PreviousBodyState = BodyState;
	}
}

void UMetaHumanCharacterParametricBodyProperties::PerformParametricFit()
{
	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCharacterEditorBodyModelTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorBodyModelTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());

	if (MetaHumanCharacter->HasBodyDNA())
	{
		TArray<uint8> OldDNABuffer = MetaHumanCharacter->GetBodyDNABuffer();
		if (MetaHumanCharacterSubsystem->ParametricFitToDnaBody(MetaHumanCharacter))
		{
			// Creates a command change that allows the user to revert back the body dna
			const FText CommandChangeDescription = LOCTEXT("BodyParametricFitDnaCommandChange", "Parametric Fit From Body Dna");
			TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> NewBodyState = MetaHumanCharacterSubsystem->CopyBodyState(MetaHumanCharacter);
			TUniquePtr<FBodyParametricFitDNACommandChange> CommandChange = MakeUnique<FBodyParametricFitDNACommandChange>(OldDNABuffer, NewBodyState.ToSharedRef(), OwnerTool->GetToolManager());
			OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(MetaHumanCharacter, MoveTemp(CommandChange), CommandChangeDescription);
			PreviousBodyState = NewBodyState;
		}
	}
	else
	{
		TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> OldBodyState = MetaHumanCharacterSubsystem->CopyBodyState(MetaHumanCharacter);
		if (MetaHumanCharacterSubsystem->ParametricFitToCompatibilityBody(MetaHumanCharacter))
		{
			// Creates a command change that allows the user to revert back the state
			const FText CommandChangeDescription = LOCTEXT("BodyParametricFitCompatibilityCommandChange", "Parametric Fit From Fixed Compatibility Body");
			TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> NewBodyState = MetaHumanCharacterSubsystem->CopyBodyState(MetaHumanCharacter);
			TUniquePtr<FMetaHumanCharacterEditorBodyToolCommandChange> CommandChange = MakeUnique<FMetaHumanCharacterEditorBodyToolCommandChange>(OldBodyState.ToSharedRef(), NewBodyState.ToSharedRef(), OwnerTool->GetToolManager());
			OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(MetaHumanCharacter, MoveTemp(CommandChange), CommandChangeDescription);
			PreviousBodyState = NewBodyState;
		}
	}
}

TArray<FMetaHumanCharacterBodyConstraintItemPtr> UMetaHumanCharacterParametricBodyProperties::GetConstraintItems(const TArray<FName>& ConstraintNames)
{
	TArray<FMetaHumanCharacterBodyConstraintItemPtr> OutConstraintItems;
	OutConstraintItems.Init(MakeShared<FMetaHumanCharacterBodyConstraintItem>(), ConstraintNames.Num());
	
	for (int32 NameIndex = 0; NameIndex < ConstraintNames.Num(); NameIndex++)
	{
		for (int32 ConstraintIndex = 0; ConstraintIndex < BodyConstraintItems.Num(); ConstraintIndex++)
		{
			if (BodyConstraintItems[ConstraintIndex]->Name == ConstraintNames[NameIndex])
			{
				OutConstraintItems[NameIndex] = BodyConstraintItems[ConstraintIndex];
				break;
			}
		}
	}
	return OutConstraintItems;
}

void UMetaHumanCharacterParametricBodyProperties::OnBodyStateChanged()
{
	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCharacterEditorBodyModelTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorBodyModelTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyState = MetaHumanCharacterSubsystem->CopyBodyState(MetaHumanCharacter);

	TArray<FMetaHumanCharacterBodyConstraint> BodyConstraints = BodyState->GetBodyConstraints(bScaleRangesByHeight);
	
	int32 NumConstraints = BodyConstraintItems.Num();
	ActiveContours.Init({}, NumConstraints);
	
	for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints; ConstraintIndex++)
	{
		// Update constraint item
		UpdateConstraintItem(BodyConstraints[ConstraintIndex], BodyConstraintItems[ConstraintIndex], bUnlockBodyRanges, ParametersRangeMultiplier);
		
		// Update measurements
		BodyConstraintItems[ConstraintIndex]->ActualMeasurement = BodyState->GetMeasurement(ConstraintIndex);
		
		// Update active contour vertices
		if (BodyConstraintItems[ConstraintIndex]->bIsActive)
		{
			ActiveContours.Add(BodyState->GetContourVertices(ConstraintIndex));
		}
	}
}

void UMetaHumanCharacterParametricBodyProperties::UpdateMeasurements(bool bUpdateRanges)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterParametricBodyProperties::UpdateMeasurements");
	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCharacterEditorBodyModelTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorBodyModelTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> BodyState = MetaHumanCharacterSubsystem->GetBodyState(MetaHumanCharacter);

	TArray<FMetaHumanCharacterBodyConstraint> BodyConstraints;
	int32 NumConstraints = BodyConstraintItems.Num();
	if (bUpdateRanges)
	{
		BodyConstraints = BodyState->GetBodyConstraints(bScaleRangesByHeight);
		NumConstraints = BodyConstraints.Num();
	}
	ActiveContours.Init({}, NumConstraints);

	for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints; ConstraintIndex++)
	{
		// Update measurements
		BodyConstraintItems[ConstraintIndex]->ActualMeasurement = BodyState->GetMeasurement(ConstraintIndex);

		// Update active contour vertices
		if (BodyConstraintItems[ConstraintIndex]->bIsActive)
		{
			ActiveContours.Add(BodyState->GetContourVertices(ConstraintIndex));
		}

		if (bUpdateRanges)
		{
			float MinMeasurement = BodyConstraints[ConstraintIndex].MinMeasurement;
			float MaxMeasurement = BodyConstraints[ConstraintIndex].MaxMeasurement;
			if (bUnlockBodyRanges)
			{
				const float Center = (MinMeasurement + MaxMeasurement) * .5f;
				const float HalfRange = (MaxMeasurement - MinMeasurement) * .5f;
				const float NewHalfRange = HalfRange * ParametersRangeMultiplier;

				MinMeasurement = Center - NewHalfRange;
				MaxMeasurement = Center + NewHalfRange;
			}

			BodyConstraintItems[ConstraintIndex]->MinMeasurement = MinMeasurement;
			BodyConstraintItems[ConstraintIndex]->MaxMeasurement = MaxMeasurement;
		}
	}
}

void UMetaHumanCharacterFixedCompatibilityBodyProperties::UpdateHeightFromBodyType()
{
	const FString FixedBodyName = StaticEnum<EMetaHumanBodyType>()->GetAuthoredNameStringByValue(static_cast<int32>(MetaHumanBodyType));
	if (FixedBodyName.Contains(TEXT("srt")))
	{
		Height = EMetaHumanCharacterFixedBodyToolHeight::Short;
	}
	else if (FixedBodyName.Contains(TEXT("tal")))
	{
		Height = EMetaHumanCharacterFixedBodyToolHeight::Tall;
	}
	else
	{
		Height = EMetaHumanCharacterFixedBodyToolHeight::Average;
	}
}


void UMetaHumanCharacterFixedCompatibilityBodyProperties::OnBodyStateChanged()
{
	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCharacterEditorBodyModelTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorBodyModelTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());
	MetaHumanBodyType = MetaHumanCharacterSubsystem->GetBodyState(MetaHumanCharacter)->GetMetaHumanBodyType();
}

void UMetaHumanCharacterFixedCompatibilityBodyProperties::OnMetaHumanBodyTypeChanged()
{
	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCharacterEditorBodyModelTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorBodyModelTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());

	TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> PreviousBodyState = MetaHumanCharacterSubsystem->CopyBodyState(MetaHumanCharacter);
	MetaHumanCharacterSubsystem->SetMetaHumanBodyType(MetaHumanCharacter, MetaHumanBodyType, UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Minimal);
	OwnerTool->bNeedsFullUpdate = true;
	TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> BodyState = MetaHumanCharacterSubsystem->CopyBodyState(MetaHumanCharacter);
	
	MetaHumanCharacterSubsystem->CommitBodyState(MetaHumanCharacter, BodyState.ToSharedRef(), UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Minimal);
	OwnerTool->bNeedsFullUpdate = true;

	const FText CommandChangeDescription = LOCTEXT("BodyFixedCompatibilityCommandChange", "Set Fixed Compatibility Body");
	
	// Creates a command change that allows the user to revert back the state
	TUniquePtr<FMetaHumanCharacterEditorBodyToolCommandChange> CommandChange = MakeUnique<FMetaHumanCharacterEditorBodyToolCommandChange>(PreviousBodyState.ToSharedRef(), BodyState.ToSharedRef(), OwnerTool->GetToolManager());
	OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(MetaHumanCharacter, MoveTemp(CommandChange), CommandChangeDescription);
}

void UMetaHumanCharacterEditorBodyModelTool::Setup()
{
	Super::Setup();

	SetToolDisplayName(LOCTEXT("BodyModelToolName", "Model"));
	
	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	// Take a copy of the editing state
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	check(MetaHumanCharacter);
	TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> BodyState = MetaHumanCharacterSubsystem->CopyBodyState(MetaHumanCharacter);

	ParametricBodyProperties = NewObject<UMetaHumanCharacterParametricBodyProperties>(this);
	ParametricBodyProperties->RestoreProperties(this, TEXT("BodyModelToolParametric"));
	ParametricBodyProperties->BodyConstraintItems = BodyConstraintsToConstraintItems(BodyState->GetBodyConstraints(ParametricBodyProperties->bScaleRangesByHeight), ParametricBodyProperties->bUnlockBodyRanges, ParametricBodyProperties->ParametersRangeMultiplier);
	ParametricBodyProperties->UpdateMeasurements();	
	ParametricBodyProperties->PreviousBodyState = BodyState;

	FixedCompatibilityBodyProperties = NewObject<UMetaHumanCharacterFixedCompatibilityBodyProperties>(this);
	FixedCompatibilityBodyProperties->MetaHumanBodyType = BodyState->GetMetaHumanBodyType();
	FixedCompatibilityBodyProperties->UpdateHeightFromBodyType();

	BodyParameterProperties = NewObject<UMetaHumanCharacterEditorHeadAndBodyParameterProperties>(this); // Body model tool's own instance (not inherited from FaceTool)
	BodyParameterProperties->BodyDelta = BodyState->GetGlobalDeltaScale();
	BodyParameterProperties->PreviousBodyState = BodyState;
	AddToolPropertySource(BodyParameterProperties);

	MetaHumanCharacterSubsystem->OnBodyStateChanged(MetaHumanCharacter).	AddWeakLambda(this, [this]
	{
		ParametricBodyProperties->OnBodyStateChanged();
		FixedCompatibilityBodyProperties->OnBodyStateChanged();
		BodyParameterProperties->OnBodyStateChanged();
	});

	const FMetaHumanCharacterEditorToolCommands& Commands = FMetaHumanCharacterEditorToolCommands::Get();

	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	if (Settings->bShowCompatibilityModeBodies)
	{
		SubTools->RegisterSubTools(
		{
			{ Commands.BeginBodyModelParametricTool, ParametricBodyProperties },
			{ Commands.BeginBodyFixedCompatibilityTool, FixedCompatibilityBodyProperties },
		});
	}
	else
	{
		SubTools->RegisterSubTools(
		{
			{ Commands.BeginBodyModelParametricTool, ParametricBodyProperties },
		});
	}

}

void UMetaHumanCharacterEditorBodyModelTool::Shutdown(EToolShutdownType InShutdownType)
{
	Super::Shutdown(InShutdownType);

	ParametricBodyProperties->SaveProperties(this, TEXT("BodyModelToolParametric"));

	if (bNeedsFullUpdate)
	{
		UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
		UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
		MetaHumanCharacterSubsystem->CommitBodyState(MetaHumanCharacter, MetaHumanCharacterSubsystem->GetBodyState(MetaHumanCharacter), UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Full);
	}
}

void UMetaHumanCharacterEditorBodyModelTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (ParametricBodyProperties->bSubToolActive && ParametricBodyProperties->bShowMeasurements)
	{
		FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
		check(PDI);

		for (const TArray<FVector>& Contour : ParametricBodyProperties->ActiveContours)
		{
			for (int32 PointIndex = 0; PointIndex + 1 < Contour.Num(); PointIndex++)
			{
				PDI->DrawLine(Contour[PointIndex], Contour[PointIndex + 1], FLinearColor(0.0, 1.0, 1.0), SDPG_MAX, 0.0f);
			}
		}
	}
}

void UMetaHumanCharacterEditorBodyModelTool::SetEnabledSubTool(UMetaHumanCharacterBodyModelSubToolBase* InSubTool, bool bInEnabled)
{
	if (InSubTool)
	{
		InSubTool->SetEnabled(bInEnabled);
	}
}


// -----------------------------------------------------
// BodyStateChangeTransactor implementation ------------
// -----------------------------------------------------

FSimpleMulticastDelegate& UBodyStateChangeTransactor::GetStateChangedDelegate(UMetaHumanCharacter* InMetaHumanCharacter)
{
	return UMetaHumanCharacterEditorSubsystem::Get()->OnBodyStateChanged(InMetaHumanCharacter);
}

void UBodyStateChangeTransactor::CommitShutdownState(UInteractiveToolManager* InToolManager, UMetaHumanCharacter* InMetaHumanCharacter, EToolShutdownType InShutdownType, const FText& InCommandChangeDescription)
{
	// If BeginDragState is valid it means the user has made some changes so we create a transaction
	// that can be reversed
	if (BeginDragState.IsValid())
	{
		UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

		Subsystem->CommitBodyState(InMetaHumanCharacter, Subsystem->GetBodyState(InMetaHumanCharacter));

		const FText CommandChangeDescription = FText::Format(LOCTEXT("BodyEditingCommandChangeTransaction", "{0} {1}"),
															 UEnum::GetDisplayValueAsText(InShutdownType),
															 InCommandChangeDescription);

		// Creates a command change that allows the user to revert back the state
		TUniquePtr<FMetaHumanCharacterEditorBodyToolCommandChange> CommandChange = MakeUnique<FMetaHumanCharacterEditorBodyToolCommandChange>(BeginDragState.ToSharedRef(), Subsystem->CopyBodyState(InMetaHumanCharacter), InToolManager);
		InToolManager->GetContextTransactionsAPI()->AppendChange(InMetaHumanCharacter, MoveTemp(CommandChange), CommandChangeDescription);

		UpdateClothVisibility(InMetaHumanCharacter, false);
	}
}
	
void UBodyStateChangeTransactor::StoreBeginDragState(UMetaHumanCharacter* InMetaHumanCharacter)
{
	// Stores the face state when the drag start to allow it to be undone while the tool is active
	BeginDragState = UMetaHumanCharacterEditorSubsystem::Get()->CopyBodyState(InMetaHumanCharacter);

	UpdateClothVisibility(InMetaHumanCharacter, true);
}

void UBodyStateChangeTransactor::CommitEndDragState(UInteractiveToolManager* InToolManager, UMetaHumanCharacter* InMetaHumanCharacter, const FText& InCommandChangeDescription)
{
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	TUniquePtr<FMetaHumanCharacterEditorBodyToolCommandChange> CommandChange = MakeUnique<FMetaHumanCharacterEditorBodyToolCommandChange>(
		BeginDragState.ToSharedRef(), 
		Subsystem->CopyBodyState(InMetaHumanCharacter), 
		InToolManager);

	InToolManager->GetContextTransactionsAPI()->AppendChange(InMetaHumanCharacter, MoveTemp(CommandChange), InCommandChangeDescription);

	// We cannot simply update the cloth visibility here since the body state is not committed and we need to explicitly update the body
	// This code should be in sync with UMetaHumanCharacterEditorSubsystem::CommitBodyState
	//UpdateClothVisibility(InMetaHumanCharacter, EMetaHumanClothingVisibilityState::Shown);
	
	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();

	if (MetaHumanCharacterSubsystem->IsCharacterOutfitSelected(InMetaHumanCharacter))
	{
		FScopedSlowTask RefitClothingSlowTask{ 2.0f, LOCTEXT("RefitClothingSlowTask", "Fitting outfit to body mesh") };

		// Update body state so that outfit resizing will see the updated mesh
		MetaHumanCharacterSubsystem->ApplyBodyState(InMetaHumanCharacter, Subsystem->CopyBodyState(InMetaHumanCharacter), UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Minimal);

		RefitClothingSlowTask.EnterProgressFrame();

		if (SavedClothingVisibilityState.IsSet())
		{
			UpdateClothVisibility(InMetaHumanCharacter, false);
		}

		MetaHumanCharacterSubsystem->RunCharacterEditorPipelineForPreview(InMetaHumanCharacter);

		if (SavedPreviewMaterial.IsSet())
		{
			// Reset the stored preview material
			MetaHumanCharacterSubsystem->UpdateCharacterPreviewMaterial(InMetaHumanCharacter, SavedPreviewMaterial.GetValue());
			SavedPreviewMaterial.Reset();
		}
	}
}

TSharedRef<FMetaHumanCharacterBodyIdentity::FState> UBodyStateChangeTransactor::GetBeginDragState() const
{
	return BeginDragState.ToSharedRef();
}

// -----------------------------------------------------
// HeadAndBodyBlendTool implementation ------------------------
// -----------------------------------------------------

bool UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties::IsFixedBodyType() const
{
	UMetaHumanCharacterEditorMeshBlendTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorMeshBlendTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());

	return MetaHumanCharacter->bFixedBodyType;
}

void UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties::PerformParametricFit() const
{
	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCharacterEditorMeshBlendTool* OwnerTool = GetTypedOuter<UMetaHumanCharacterEditorMeshBlendTool>();
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(OwnerTool->GetTarget());

	if (MetaHumanCharacter->HasBodyDNA())
	{
		TArray<uint8> OldDNABuffer = MetaHumanCharacter->GetBodyDNABuffer();
		if (MetaHumanCharacterSubsystem->ParametricFitToDnaBody(MetaHumanCharacter))
		{
			// Creates a command change that allows the user to revert back the body dna
			const FText CommandChangeDescription = LOCTEXT("BodyParametricFitDnaCommandChange", "Parametric Fit From Body Dna");
			TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> NewBodyState = MetaHumanCharacterSubsystem->CopyBodyState(MetaHumanCharacter);
			TUniquePtr<FBodyParametricFitDNACommandChange> CommandChange = MakeUnique<FBodyParametricFitDNACommandChange>(OldDNABuffer, NewBodyState.ToSharedRef(), OwnerTool->GetToolManager());
			OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(MetaHumanCharacter, MoveTemp(CommandChange), CommandChangeDescription);
		}
	}
	else
	{
		TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> OldBodyState = MetaHumanCharacterSubsystem->CopyBodyState(MetaHumanCharacter);
		if (MetaHumanCharacterSubsystem->ParametricFitToCompatibilityBody(MetaHumanCharacter))
		{
			// Creates a command change that allows the user to revert back the state
			const FText CommandChangeDescription = LOCTEXT("BodyParametricFitCompatibilityCommandChange", "Parametric Fit From Fixed Compatibility Body");
			TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> NewBodyState = MetaHumanCharacterSubsystem->CopyBodyState(MetaHumanCharacter);
			TUniquePtr<FMetaHumanCharacterEditorBodyToolCommandChange> CommandChange = MakeUnique<FMetaHumanCharacterEditorBodyToolCommandChange>(OldBodyState.ToSharedRef(), NewBodyState.ToSharedRef(), OwnerTool->GetToolManager());
			OwnerTool->GetToolManager()->GetContextTransactionsAPI()->AppendChange(MetaHumanCharacter, MoveTemp(CommandChange), CommandChangeDescription);
		}
	}
}

// -----------------------------------------------------
// DualStateChangeTransactor implementation ------------
// -----------------------------------------------------

FSimpleMulticastDelegate& UDualStateChangeTransactor::GetStateChangedDelegate(UMetaHumanCharacter* InMetaHumanCharacter)
{
	if (!bDelegatesBound)
	{
		UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
		Subsystem->OnBodyStateChanged(InMetaHumanCharacter).AddWeakLambda(this, [this] { CombinedStateChangedDelegate.Broadcast(); });
		Subsystem->OnFaceStateChanged(InMetaHumanCharacter).AddWeakLambda(this, [this] { CombinedStateChangedDelegate.Broadcast(); });
		bDelegatesBound = true;
	}
	return CombinedStateChangedDelegate;
}

void UDualStateChangeTransactor::StoreBeginDragState(UMetaHumanCharacter* InMetaHumanCharacter)
{
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	BodyBeginDragState = Subsystem->CopyBodyState(InMetaHumanCharacter);
	FaceBeginDragState = Subsystem->CopyFaceState(InMetaHumanCharacter);

	if (bBlendBothTypes || IsBodyManipulator(ActiveManipulatorIndex))
	{
		UpdateClothVisibility(InMetaHumanCharacter, true);
	}
}

void UDualStateChangeTransactor::CommitEndDragState(UInteractiveToolManager* InToolManager, UMetaHumanCharacter* InMetaHumanCharacter, const FText& InCommandChangeDescription)
{
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

	const bool bBodyChanged = bBlendBothTypes || IsBodyManipulator(ActiveManipulatorIndex);
	const bool bFaceChanged = bBlendBothTypes || !IsBodyManipulator(ActiveManipulatorIndex);

	if (bBodyChanged && bFaceChanged)
	{
		// Both body and face changed — emit a single combined undo transaction
		TUniquePtr<FMetaHumanCharacterEditorStateCommandChange> CombinedChange = MakeUnique<FMetaHumanCharacterEditorStateCommandChange>(
			InToolManager,
			BodyBeginDragState.ToSharedRef(),
			FaceBeginDragState.ToSharedRef(),
			Subsystem->CopyBodyState(InMetaHumanCharacter),
			Subsystem->CopyFaceState(InMetaHumanCharacter),
			InCommandChangeDescription.ToString());
		InToolManager->GetContextTransactionsAPI()->AppendChange(InMetaHumanCharacter, MoveTemp(CombinedChange), InCommandChangeDescription);
	}
	else if (bBodyChanged)
	{
		// Body-only undo change
		TUniquePtr<FMetaHumanCharacterEditorBodyToolCommandChange> BodyChange = MakeUnique<FMetaHumanCharacterEditorBodyToolCommandChange>(
			BodyBeginDragState.ToSharedRef(),
			Subsystem->CopyBodyState(InMetaHumanCharacter),
			InToolManager);
		InToolManager->GetContextTransactionsAPI()->AppendChange(InMetaHumanCharacter, MoveTemp(BodyChange), InCommandChangeDescription);
	}
	else if (bFaceChanged)
	{
		// Face-only undo change
		TUniquePtr<FMetaHumanCharacterEditorFaceToolCommandChange> FaceChange = MakeUnique<FMetaHumanCharacterEditorFaceToolCommandChange>(
			FaceBeginDragState.ToSharedRef(),
			InMetaHumanCharacter,
			InToolManager);
		InToolManager->GetContextTransactionsAPI()->AppendChange(InMetaHumanCharacter, MoveTemp(FaceChange), InCommandChangeDescription);
	}

	if (bBodyChanged)
	{
		// Handle cloth visibility and outfit refit for body changes
		if (Subsystem->IsCharacterOutfitSelected(InMetaHumanCharacter))
		{
			FScopedSlowTask RefitClothingSlowTask{ 2.0f, LOCTEXT("RefitClothingSlowTask", "Fitting outfit to body mesh") };
			Subsystem->ApplyBodyState(InMetaHumanCharacter, Subsystem->CopyBodyState(InMetaHumanCharacter), UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Minimal);
			RefitClothingSlowTask.EnterProgressFrame();

			if (SavedClothingVisibilityState.IsSet())
			{
				UpdateClothVisibility(InMetaHumanCharacter, false);
			}

			Subsystem->RunCharacterEditorPipelineForPreview(InMetaHumanCharacter);

			if (SavedPreviewMaterial.IsSet())
			{
				Subsystem->UpdateCharacterPreviewMaterial(InMetaHumanCharacter, SavedPreviewMaterial.GetValue());
				SavedPreviewMaterial.Reset();
			}
		}
	}
}

void UDualStateChangeTransactor::CommitShutdownState(UInteractiveToolManager* InToolManager, UMetaHumanCharacter* InMetaHumanCharacter, EToolShutdownType InShutdownType, const FText& InCommandChangeDescription)
{
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

	// Handle body shutdown state
	if (BodyBeginDragState.IsValid())
	{
		Subsystem->CommitBodyState(InMetaHumanCharacter, Subsystem->GetBodyState(InMetaHumanCharacter));

		const FText CommandChangeDescription = FText::Format(LOCTEXT("DualBodyEditingCommandChangeTransaction", "{0} {1}"),
			UEnum::GetDisplayValueAsText(InShutdownType),
			InCommandChangeDescription);

		TUniquePtr<FMetaHumanCharacterEditorBodyToolCommandChange> BodyChange = MakeUnique<FMetaHumanCharacterEditorBodyToolCommandChange>(
			BodyBeginDragState.ToSharedRef(), Subsystem->CopyBodyState(InMetaHumanCharacter), InToolManager);
		InToolManager->GetContextTransactionsAPI()->AppendChange(InMetaHumanCharacter, MoveTemp(BodyChange), CommandChangeDescription);

		UpdateClothVisibility(InMetaHumanCharacter, false);
	}

	// Handle face shutdown state
	if (FaceBeginDragState.IsValid())
	{
		const FText CommandChangeDescription = FText::Format(LOCTEXT("DualFaceEditingCommandChangeTransaction", "{0} {1}"),
			UEnum::GetDisplayValueAsText(InShutdownType),
			InCommandChangeDescription);

		TUniquePtr<FMetaHumanCharacterEditorFaceToolCommandChange> FaceChange = MakeUnique<FMetaHumanCharacterEditorFaceToolCommandChange>(
			FaceBeginDragState.ToSharedRef(), InMetaHumanCharacter, InToolManager);
		InToolManager->GetContextTransactionsAPI()->AppendChange(InMetaHumanCharacter, MoveTemp(FaceChange), CommandChangeDescription);
	}

	// Always commit current face state on shutdown
	if (TSharedPtr<FMetaHumanCharacterIdentity::FState> NewFaceState = Subsystem->CopyFaceState(InMetaHumanCharacter))
	{
		Subsystem->CommitFaceState(InMetaHumanCharacter, NewFaceState.ToSharedRef());
	}
}

// -----------------------------------------------------
// HeadAndBodyBlendTool implementation ------------------------
// -----------------------------------------------------

void UMetaHumanCharacterEditorHeadAndBodyBlendTool::InitStateChangeTransactor()
{
	UDualStateChangeTransactor* DualTransactor = NewObject<UDualStateChangeTransactor>(this);
	if (DualTransactor && DualTransactor->GetClass()->ImplementsInterface(UMeshStateChangeTransactorInterface::StaticClass()))
	{
		MeshStateChangeTransactor.SetInterface(Cast<IMeshStateChangeTransactorInterface>(DualTransactor));
		MeshStateChangeTransactor.SetObject(DualTransactor);
	}
}

void UMetaHumanCharacterEditorHeadAndBodyBlendTool::Setup()
{
	Super::Setup();

	// Extend the shortcuts set by the base class to also advertise Shift+Ctrl for blending all features on both head and body
	FViewport* Viewport = GetToolManager()->GetContextQueriesAPI()->GetFocusedViewport();
	FMetaHumanCharacterViewportClient* MetaHumanCharacterViewportClient = static_cast<FMetaHumanCharacterViewportClient*>(Viewport->GetClient());
	if (MetaHumanCharacterViewportClient)
	{
		MetaHumanCharacterViewportClient->SetShortcuts({
			{LOCTEXT("HeadBodyBlendToolShortcutShiftCtrlKey", "SHIFT+CTRL"), LOCTEXT("HeadBodyBlendToolShortcutShiftCtrlValue", "blend all head and body features")}
		});
	}

	BlendProperties = NewObject<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>(this);
	BlendProperties->RestoreProperties(this, GetCommandChangeDescription().ToString());
	AddToolPropertySource(BlendProperties);

	UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties* HeadBodyBlendProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>(BlendProperties);
	HeadBodyBlendProperties->WatchProperty(HeadBodyBlendProperties->bShowManipulators, [this](bool bVisible)
	{
		for (UStaticMeshComponent* Component : ManipulatorComponents)
		{
			if (IsValid(Component))
			{
				// Pending a correction of the landmark dataset, hide manipulators that evaluate to world origin.
				Component->SetVisibility(bVisible && !Component->GetComponentLocation().IsZero());
			}
		}
	});

	// Initialize the body parts of the shared HeadAndBodyParameterProperties created by FaceTool::Setup()
	TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> OriginalState = UMetaHumanCharacterEditorSubsystem::Get()->CopyBodyState(MetaHumanCharacter);
	GetHeadAndBodyParameterProperties()->BodyDelta = OriginalState->GetGlobalDeltaScale();
	GetHeadAndBodyParameterProperties()->PreviousBodyState = OriginalState;

	GetHeadAndBodyParameterProperties()->OnBodyParameterChangedDelegate.AddWeakLambda(this, [this]()
	{
		UpdateManipulatorPositions();
	});

	UMetaHumanCharacterEditorSubsystem::Get()->OnBodyStateChanged(MetaHumanCharacter).AddWeakLambda(this, [this]
	{
		GetHeadAndBodyParameterProperties()->OnBodyStateChanged();
	});
}

const FText UMetaHumanCharacterEditorHeadAndBodyBlendTool::GetDescription() const
{
	return LOCTEXT("HeadAndBodyBlendToolName", "Blend");
}

const FText UMetaHumanCharacterEditorHeadAndBodyBlendTool::GetCommandChangeDescription() const
{
	return LOCTEXT("HeadAndBodyBlendToolCommandChange", "Head And Body Blend Tool");
}

const FText UMetaHumanCharacterEditorHeadAndBodyBlendTool::GetCommandChangeIntermediateDescription() const
{
	return LOCTEXT("HeadBodyBlendToolIntermediateCommandChange", "Move Blend Manipulator");
}

float UMetaHumanCharacterEditorHeadAndBodyBlendTool::GetManipulatorScale() const
{
	return 0.006f;
}

float UMetaHumanCharacterEditorHeadAndBodyBlendTool::GetAncestryCircleRadius() const
{
	return 9.f;
}

TArray<FVector3f> UMetaHumanCharacterEditorHeadAndBodyBlendTool::GetManipulatorPositions() const
{
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	TArray<FVector3f> BodyGizmos = Subsystem->GetBodyGizmos(MetaHumanCharacter);
	TArray<FVector3f> FaceGizmos = Subsystem->GetFaceGizmos(MetaHumanCharacter);

	NumBodyManipulators = BodyGizmos.Num();

	TArray<FVector3f> Combined;
	Combined.Append(BodyGizmos);
	Combined.Append(FaceGizmos);
	return Combined;
}

void UMetaHumanCharacterEditorHeadAndBodyBlendTool::OnBeginDrag(const FRay& InRay)
{
	if (UDualStateChangeTransactor* DualTransactor = Cast<UDualStateChangeTransactor>(MeshStateChangeTransactor.GetObject()))
	{
		DualTransactor->SetNumBodyManipulators(NumBodyManipulators);
		if (GetShiftToggle() && GetCtrlToggle())
		{
			// Shift+Ctrl: blend all regions of both head and body — signal the transactor so it
			// captures both states for undo/redo and shows cloth visibility correctly.
			DualTransactor->SetBlendBothTypes();
		}
		else
		{
			DualTransactor->SetActiveManipulatorIndex(SelectedManipulator);
		}
	}

	Super::OnBeginDrag(InRay);
}

TArray<FVector3f> UMetaHumanCharacterEditorHeadAndBodyBlendTool::BlendPresets(int32 InManipulatorIndex, const TArray<float>& Weights)
{
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties* BlendToolProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>(BlendProperties);
	UDualStateChangeTransactor* DualTransactor = Cast<UDualStateChangeTransactor>(MeshStateChangeTransactor.GetObject());

	TArray<FVector3f> BodyPositions;
	TArray<FVector3f> FacePositions;

	// Shift+Ctrl held: blend all regions of both head and body simultaneously.
	// Face must be blended first so that UpdateFaceFromBodyInternal (called inside BlendBodyRegion)
	// adjusts the already-blended face state rather than the raw begin-drag state.
	if (InManipulatorIndex == INDEX_NONE && GetCtrlToggle())
	{
		TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyBeginDragState = DualTransactor->GetBodyBeginDragState();
		TSharedRef<FMetaHumanCharacterIdentity::FState> FaceBeginDragState = MakeShared<FMetaHumanCharacterIdentity::FState>(*DualTransactor->GetFaceBeginDragState());
		BodyPositions = Subsystem->BlendBodyRegion(MetaHumanCharacter, INDEX_NONE, BlendToolProperties->BodyBlendOptions, BodyBeginDragState, BodyPresetStates, Weights);
		// Update starting face state from body before blending
		FaceBeginDragState->SetBodyJointsAndBodyFaceVertices(Subsystem->GetBodyState(MetaHumanCharacter)->CopyBindPose(), Subsystem->GetBodyState(MetaHumanCharacter)->GetVerticesAndVertexNormals().Vertices);
		FacePositions = Subsystem->BlendFaceRegion(MetaHumanCharacter, INDEX_NONE, FaceBeginDragState, FacePresetStates, Weights, BlendToolProperties->FaceBlendOptions, MeshEditingToolProperties->bSymmetricModeling);
	}
	else
	{
		// Determine if this is a body or face manipulator (or INDEX_NONE for shift-blend-all of one type)
		bool bIsBodyManipulator;
		if (InManipulatorIndex == INDEX_NONE)
		{
			// Shift held: blend all regions of whichever type is selected
			bIsBodyManipulator = (SelectedManipulator >= 0 && SelectedManipulator < NumBodyManipulators);
		}
		else
		{
			bIsBodyManipulator = (InManipulatorIndex < NumBodyManipulators);
		}

		if (bIsBodyManipulator)
		{
			TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyBeginDragState = DualTransactor->GetBodyBeginDragState();
			int32 BodyRegionIndex = (InManipulatorIndex == INDEX_NONE) ? INDEX_NONE : InManipulatorIndex;
			BodyPositions = Subsystem->BlendBodyRegion(MetaHumanCharacter, BodyRegionIndex, BlendToolProperties->BodyBlendOptions, BodyBeginDragState, BodyPresetStates, Weights);
			FacePositions = Subsystem->GetFaceGizmos(MetaHumanCharacter);
		}
		else
		{
			TSharedRef<const FMetaHumanCharacterIdentity::FState> FaceBeginDragState = DualTransactor->GetFaceBeginDragState();
			int32 FaceRegionIndex = (InManipulatorIndex == INDEX_NONE) ? INDEX_NONE : (InManipulatorIndex - NumBodyManipulators);
			BodyPositions = Subsystem->GetBodyGizmos(MetaHumanCharacter);
			FacePositions = Subsystem->BlendFaceRegion(MetaHumanCharacter, FaceRegionIndex, FaceBeginDragState, FacePresetStates, Weights, BlendToolProperties->FaceBlendOptions, MeshEditingToolProperties->bSymmetricModeling);
		}
	}

	TArray<FVector3f> Combined;
	Combined.Append(BodyPositions);
	Combined.Append(FacePositions);
	return Combined;
}

void UMetaHumanCharacterEditorHeadAndBodyBlendTool::Shutdown(EToolShutdownType InShutdownType)
{
	Super::Shutdown(InShutdownType);
	BlendProperties->SaveProperties(this, GetCommandChangeDescription().ToString());
}

void UMetaHumanCharacterEditorHeadAndBodyBlendTool::AddMetaHumanCharacterPreset(UMetaHumanCharacter* InCharacterPreset, int32 InItemIndex)
{
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

	// Face preset state
	TSharedPtr<FMetaHumanCharacterIdentity::FState> FacePresetState = Subsystem->CopyFaceState(MetaHumanCharacter);
	FacePresetState->Deserialize(InCharacterPreset->GetFaceStateData());
	FMetaHumanCharacterIdentity::FSettings Settings = FacePresetState->GetSettings();
	Settings.SetGlobalVertexDeltaScale(MetaHumanCharacter->FaceEvaluationSettings.GlobalDelta);

	// Body preset state
	TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> BodyPresetState = Subsystem->CopyBodyState(MetaHumanCharacter);
	BodyPresetState->Deserialize(InCharacterPreset->GetBodyStateData());

	// Grow both arrays to fit
	if (FacePresetStates.Num() <= InItemIndex)
	{
		FacePresetStates.AddDefaulted(InItemIndex - FacePresetStates.Num() + 1);
	}
	if (BodyPresetStates.Num() <= InItemIndex)
	{
		BodyPresetStates.AddDefaulted(InItemIndex - BodyPresetStates.Num() + 1);
	}

	FacePresetStates[InItemIndex] = FacePresetState;
	BodyPresetStates[InItemIndex] = BodyPresetState;
}

void UMetaHumanCharacterEditorHeadAndBodyBlendTool::RemoveMetaHumanCharacterPreset(int32 InItemIndex)
{
	if (InItemIndex < FacePresetStates.Num())
	{
		FacePresetStates[InItemIndex].Reset();
	}
	if (InItemIndex < BodyPresetStates.Num())
	{
		BodyPresetStates[InItemIndex].Reset();
	}
}

void UMetaHumanCharacterEditorHeadAndBodyBlendTool::BlendToMetaHumanCharacterPreset(UMetaHumanCharacter* InCharacterPreset)
{
	// Signal that this blends both face and body
	if (UDualStateChangeTransactor* DualTransactor = Cast<UDualStateChangeTransactor>(MeshStateChangeTransactor.GetObject()))
	{
		DualTransactor->SetBlendBothTypes();
		DualTransactor->SetNumBodyManipulators(NumBodyManipulators);
	}

	MeshStateChangeTransactor->StoreBeginDragState(MetaHumanCharacter);

	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties* BlendToolProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>(BlendProperties);
	TArray<float> Weights = { 1.0f };

	// Blend body
	TSharedPtr<const FMetaHumanCharacterBodyIdentity::FState> BodyInitState = Subsystem->GetBodyState(MetaHumanCharacter);
	TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> BodyState = Subsystem->CopyBodyState(MetaHumanCharacter);
	BodyState->Deserialize(InCharacterPreset->GetBodyStateData());
	TArray<TSharedPtr<const FMetaHumanCharacterBodyIdentity::FState>> BodyStates = { BodyState };
	TArray<FVector3f> BodyPositions = Subsystem->BlendBodyRegion(MetaHumanCharacter, INDEX_NONE, BlendToolProperties->BodyBlendOptions, BodyInitState, BodyStates, Weights);

	// Blend face
	TSharedPtr<const FMetaHumanCharacterIdentity::FState> FaceInitState = Subsystem->CopyFaceState(MetaHumanCharacter);
	TSharedPtr<FMetaHumanCharacterIdentity::FState> FaceState = Subsystem->CopyFaceState(MetaHumanCharacter);
	FaceState->Deserialize(InCharacterPreset->GetFaceStateData());
	TArray<TSharedPtr<const FMetaHumanCharacterIdentity::FState>> FaceStates = { FaceState };
	TArray<FVector3f> FacePositions = Subsystem->BlendFaceRegion(MetaHumanCharacter, INDEX_NONE, FaceInitState, FaceStates, Weights, BlendToolProperties->FaceBlendOptions, /*bInBlendSymmetrically*/ true);

	// Combine positions
	TArray<FVector3f> Combined;
	Combined.Append(BodyPositions);
	Combined.Append(FacePositions);
	UpdateManipulatorPositions(Combined);

	MeshStateChangeTransactor->CommitEndDragState(GetToolManager(), MetaHumanCharacter, GetCommandChangeIntermediateDescription());
}

void UMetaHumanCharacterEditorHeadAndBodyBlendTool::BlendToMetaHumanCharacterPresetHeadOnly(UMetaHumanCharacter* InCharacterPreset)
{
	if (UDualStateChangeTransactor* DualTransactor = Cast<UDualStateChangeTransactor>(MeshStateChangeTransactor.GetObject()))
	{
		DualTransactor->SetActiveManipulatorIndex(INDEX_NONE);
		DualTransactor->SetNumBodyManipulators(NumBodyManipulators);
	}
	MeshStateChangeTransactor->StoreBeginDragState(MetaHumanCharacter);

	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties* BlendToolProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>(BlendProperties);
	TArray<float> Weights = { 1.0f };

	// Body stays unchanged: just get gizmos
	TArray<FVector3f> BodyPositions = Subsystem->GetBodyGizmos(MetaHumanCharacter);

	// Blend face from preset
	TSharedPtr<const FMetaHumanCharacterIdentity::FState> FaceInitState = Subsystem->CopyFaceState(MetaHumanCharacter);
	TSharedPtr<FMetaHumanCharacterIdentity::FState> FaceState = Subsystem->CopyFaceState(MetaHumanCharacter);
	FaceState->Deserialize(InCharacterPreset->GetFaceStateData());
	TArray<TSharedPtr<const FMetaHumanCharacterIdentity::FState>> FaceStates = { FaceState };
	TArray<FVector3f> FacePositions = Subsystem->BlendFaceRegion(MetaHumanCharacter, INDEX_NONE, FaceInitState, FaceStates, Weights, BlendToolProperties->FaceBlendOptions, /*bInBlendSymmetrically*/ true);

	TArray<FVector3f> Combined;
	Combined.Append(BodyPositions);
	Combined.Append(FacePositions);
	UpdateManipulatorPositions(Combined);

	MeshStateChangeTransactor->CommitEndDragState(GetToolManager(), MetaHumanCharacter, GetCommandChangeIntermediateDescription());
}

void UMetaHumanCharacterEditorHeadAndBodyBlendTool::BlendToMetaHumanCharacterPresetBodyOnly(UMetaHumanCharacter* InCharacterPreset)
{
	// Body-only: use a valid body manipulator index so only body changes are recorded
	if (UDualStateChangeTransactor* DualTransactor = Cast<UDualStateChangeTransactor>(MeshStateChangeTransactor.GetObject()))
	{
		DualTransactor->SetActiveManipulatorIndex(0);
		DualTransactor->SetNumBodyManipulators(NumBodyManipulators);
	}
	MeshStateChangeTransactor->StoreBeginDragState(MetaHumanCharacter);

	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties* BlendToolProperties = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties>(BlendProperties);
	TArray<float> Weights = { 1.0f };

	// Blend body from preset
	TSharedPtr<const FMetaHumanCharacterBodyIdentity::FState> BodyInitState = Subsystem->GetBodyState(MetaHumanCharacter);
	TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> BodyState = Subsystem->CopyBodyState(MetaHumanCharacter);
	BodyState->Deserialize(InCharacterPreset->GetBodyStateData());
	TArray<TSharedPtr<const FMetaHumanCharacterBodyIdentity::FState>> BodyStates = { BodyState };
	TArray<FVector3f> BodyPositions = Subsystem->BlendBodyRegion(MetaHumanCharacter, INDEX_NONE, BlendToolProperties->BodyBlendOptions, BodyInitState, BodyStates, Weights);

	// Face stays unchanged: just get face gizmos
	TArray<FVector3f> FacePositions = Subsystem->GetFaceGizmos(MetaHumanCharacter);

	TArray<FVector3f> Combined;
	Combined.Append(BodyPositions);
	Combined.Append(FacePositions);
	UpdateManipulatorPositions(Combined);

	MeshStateChangeTransactor->CommitEndDragState(GetToolManager(), MetaHumanCharacter, GetCommandChangeIntermediateDescription());
}

#undef LOCTEXT_NAMESPACE