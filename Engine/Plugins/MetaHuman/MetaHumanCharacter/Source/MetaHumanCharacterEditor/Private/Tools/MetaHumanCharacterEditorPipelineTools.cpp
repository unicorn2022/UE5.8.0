// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorPipelineTools.h"

#include "InteractiveToolManager.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorActorInterface.h"
#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionPipeline.h"
#include "MetaHumanCollectionEditorPipeline.h"
#include "MetaHumanCharacterPaletteProjectSettings.h"
#include "MetaHumanSDKSettings.h"
#include "Subsystem/MetaHumanCharacterSkinMaterials.h"

#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "SceneManagement.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "Tools/MetaHumanCharacterEditorToolTargetUtil.h"
#include "ToolTargetManager.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/SoftObjectPtr.h"
#include "Dialogs/Dialogs.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditor"

namespace UE::MetaHuman::Private
{
	/**
	 * Returns the names of override materials whose parent (walking the parent chain) is not one of
	 * the default MetaHuman base materials returned by FMetaHumanCharacterSkinMaterials::GetHeadPreviewMaterialInstance
	 * or GetBodyPreviewMaterialInstance.
	 *
	 * Only the override materials that participate in baking are checked: face skin (all LOD slots),
	 * body skin, teeth and the left/right eyes. Non-baked slots (eye shell, lacrimal fluid and
	 * eyelashes) are ignored — those can use arbitrary parents without affecting the bake.
	 */
	static TArray<FString> GetNonMetaHumanOverrideMaterialNames(const UMetaHumanCharacter& InCharacter)
	{
		TArray<FString> NonMHMaterialNames;

		const FMetaHumanCharacterTextureMaterialOverrides& Overrides = InCharacter.SkinSettings.TextureMaterialOverrides;
		if (!Overrides.bEnableMaterialOverrides)
		{
			return NonMHMaterialNames;
		}

		constexpr bool bWithVTSupport = false;

		TSet<const UMaterialInterface*> ValidParents;

		auto AddParentOf = [&ValidParents](const UMaterialInstance* InMI)
		{
			if (InMI && InMI->Parent)
			{
				ValidParents.Add(InMI->Parent);
			}
		};

		auto CollectFromHead = [&AddParentOf](EMetaHumanCharacterSkinPreviewMaterial InType)
		{
			const FMetaHumanCharacterFaceMaterialSet HeadSet = FMetaHumanCharacterSkinMaterials::GetHeadPreviewMaterialInstance(InType, bWithVTSupport);
			HeadSet.ForEachSkinMaterial<UMaterialInstance>(
				[&AddParentOf](EMetaHumanCharacterSkinMaterialSlot, UMaterialInstance* InMaterial)
				{
					AddParentOf(InMaterial);
				});
			AddParentOf(HeadSet.EyeLeft);
			AddParentOf(HeadSet.EyeRight);
			AddParentOf(HeadSet.EyeShell);
			AddParentOf(HeadSet.LacrimalFluid);
			AddParentOf(HeadSet.Teeth);
			AddParentOf(HeadSet.Eyelashes);
			AddParentOf(HeadSet.EyelashesHiLods);
		};

		const EMetaHumanCharacterSkinPreviewMaterial PreviewTypes[] =
		{
			EMetaHumanCharacterSkinPreviewMaterial::Default,
			EMetaHumanCharacterSkinPreviewMaterial::Editable,
			EMetaHumanCharacterSkinPreviewMaterial::Clay,
			EMetaHumanCharacterSkinPreviewMaterial::Gray,
		};

		for (EMetaHumanCharacterSkinPreviewMaterial PreviewType : PreviewTypes)
		{
			CollectFromHead(PreviewType);
			AddParentOf(FMetaHumanCharacterSkinMaterials::GetBodyPreviewMaterialInstance(PreviewType, bWithVTSupport));
		}

		auto IsParentedToMHMaterial = [&ValidParents](const UMaterialInstance* InMIC) -> bool
		{
			const UMaterialInterface* Cur = InMIC ? InMIC->Parent.Get() : nullptr;
			while (Cur)
			{
				if (ValidParents.Contains(Cur))
				{
					return true;
				}
				if (const UMaterialInstance* AsMI = Cast<UMaterialInstance>(Cur))
				{
					Cur = AsMI->Parent;
				}
				else
				{
					break;
				}
			}
			return false;
		};

		auto CheckOverride = [&IsParentedToMHMaterial, &NonMHMaterialNames](const FString& InSlotLabel, const TSoftObjectPtr<UMaterialInstanceConstant>& InSoftRef)
		{
			if (InSoftRef.IsNull())
			{
				return;
			}
			UMaterialInstanceConstant* MIC = InSoftRef.LoadSynchronous();
			if (!MIC)
			{
				return;
			}
			if (!IsParentedToMHMaterial(MIC))
			{
				NonMHMaterialNames.Add(FString::Printf(TEXT("%s: %s"), *InSlotLabel, *MIC->GetName()));
			}
		};

		const UEnum* SkinSlotEnum = StaticEnum<EMetaHumanCharacterSkinMaterialSlot>();
		for (const TPair<EMetaHumanCharacterSkinMaterialSlot, TSoftObjectPtr<UMaterialInstanceConstant>>& Pair : Overrides.MaterialOverrides.Skin)
		{
			const FString SlotLabel = FString::Printf(TEXT("Face Skin %s"), *SkinSlotEnum->GetNameStringByValue(static_cast<int64>(Pair.Key)));
			CheckOverride(SlotLabel, Pair.Value);
		}

		const UEnum* TeethAndEyesSlotEnum = StaticEnum<EMetaHumanCharacterTeethAndEyesSlot>();
		for (const TPair<EMetaHumanCharacterTeethAndEyesSlot, TSoftObjectPtr<UMaterialInstanceConstant>>& Pair : Overrides.MaterialOverrides.TeethAndEyes)
		{
			// Only teeth and the left/right eyes get baked. Eye shell, lacrimal fluid and
			// eyelashes are not, so their overrides are free to use any parent material.
			const EMetaHumanCharacterTeethAndEyesSlot Slot = Pair.Key;
			const bool bIsBakedSlot =
				Slot == EMetaHumanCharacterTeethAndEyesSlot::Teeth ||
				Slot == EMetaHumanCharacterTeethAndEyesSlot::EyeLeft ||
				Slot == EMetaHumanCharacterTeethAndEyesSlot::EyeRight;

			if (!bIsBakedSlot)
			{
				continue;
			}

			const FString SlotLabel = TeethAndEyesSlotEnum->GetNameStringByValue(static_cast<int64>(Slot));
			CheckOverride(SlotLabel, Pair.Value);
		}

		CheckOverride(TEXT("Body"), Overrides.MaterialOverrides.Body);

		return NonMHMaterialNames;
	}
}

class FPipelineToolCommandChange : public FToolCommandChange
{
public:

	FPipelineToolCommandChange(const FMetaHumanCharacterAssemblySettings& InOldAssemblySettings,
		const FMetaHumanCharacterAssemblySettings& InNewAssemblySettings,
		TNotNull<UInteractiveToolManager*> InToolManager)
		: OldAssemblySettings{ InOldAssemblySettings }
		, NewAssemblySettings{ InNewAssemblySettings }
		, ToolManager{ InToolManager }
	{
	}

	//~Begin FToolCommandChange interface
	virtual FString ToString() const override
	{
		return TEXT("MetaHuman Character Edit Assembly");
	}

	virtual bool HasExpired(UObject* InObject) const override
	{
		return !ToolManager.IsValid();
	}

	void Apply(UObject* InObject) override
	{
		UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(InObject);

		Character->Modify();
		Character->AssemblySettings = NewAssemblySettings;

		UpdatePipelineToolProperties(NewAssemblySettings);
	}

	void Revert(UObject* InObject) override
	{
		UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(InObject);

		Character->Modify();
		Character->AssemblySettings = OldAssemblySettings;

		UpdatePipelineToolProperties(OldAssemblySettings);
	}
	//~End FToolCommandChange interface

protected:

	void UpdatePipelineToolProperties(const FMetaHumanCharacterAssemblySettings& InAssemblySettings)
	{
		if (ToolManager.IsValid())
		{
			if (UMetaHumanCharacterEditorPipelineTool* PipelineTool = Cast<UMetaHumanCharacterEditorPipelineTool>(ToolManager->GetActiveTool(EToolSide::Left)))
			{
				UMetaHumanCharacterEditorPipelineToolProperties* PipelineToolProperties = nullptr;
				if (PipelineTool->GetToolProperties().FindItemByClass<UMetaHumanCharacterEditorPipelineToolProperties>(&PipelineToolProperties))
				{
					PipelineToolProperties->CopyFrom(InAssemblySettings);
					PipelineToolProperties->SilentUpdateWatched();

					PipelineTool->PreviousAssemblySettings = InAssemblySettings;
				}
			}
		}
	}

protected:

	FMetaHumanCharacterAssemblySettings OldAssemblySettings;
	FMetaHumanCharacterAssemblySettings NewAssemblySettings;

	TWeakObjectPtr<UInteractiveToolManager> ToolManager;

};

UInteractiveTool* UMetaHumanCharacterEditorPipelineToolBuilder::BuildTool(const FToolBuilderState& InSceneState) const
{
	UToolTarget* Target = InSceneState.TargetManager->BuildFirstSelectedTargetable(InSceneState, GetTargetRequirements());
	check(Target);

	switch (ToolType)
	{
		case EMetaHumanCharacterPipelineEditingTool::Pipeline:
		{
			UMetaHumanCharacterEditorPipelineTool* PipelineTool = NewObject<UMetaHumanCharacterEditorPipelineTool>(InSceneState.ToolManager);
			PipelineTool->SetTarget(Target);
			PipelineTool->SetTargetWorld(InSceneState.World);
			return PipelineTool;
		}

		default:
			checkNoEntry();
	}

	return nullptr;
}

void UMetaHumanCharacterEditorPipelineToolProperties::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorPipelineToolProperties, PipelineType)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorPipelineToolProperties, PipelineQuality))
	{
		if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ValueSet)
		{
			UpdateSelectedPipeline();
		}
	}
}

void UMetaHumanCharacterEditorPipelineToolProperties::CopyFrom(const FMetaHumanCharacterAssemblySettings& InAssemblySettings)
{
	PipelineQuality = InAssemblySettings.PipelineQuality;
	PipelineType = InAssemblySettings.PipelineType;

	DefaultAnimationSystemNameAnimBP = InAssemblySettings.AnimationSystemNameAnimBP;
	AnimationSystemName = InAssemblySettings.AnimationSystemName;
	NameOverride = InAssemblySettings.NameOverride;

	CommonDirectory = InAssemblySettings.CommonDirectory;
	RootDirectory = InAssemblySettings.RootDirectory;
}

void UMetaHumanCharacterEditorPipelineToolProperties::CopyTo(FMetaHumanCharacterAssemblySettings& OutAssemblySettings)
{
	OutAssemblySettings.PipelineQuality = PipelineQuality;
	OutAssemblySettings.PipelineType = PipelineType;

	OutAssemblySettings.AnimationSystemName = AnimationSystemName;
	OutAssemblySettings.NameOverride = NameOverride;

	OutAssemblySettings.CommonDirectory = CommonDirectory;
	OutAssemblySettings.RootDirectory = RootDirectory;
}

void UMetaHumanCharacterEditorPipelineToolProperties::UpdateSelectedPipeline()
{
	TSoftClassPtr<UMetaHumanCharacterPipeline> PipelineClassPtr = GetSelectedPipelineClass();
	if (PipelineClassPtr != nullptr)
	{
		TNotNull<UMetaHumanCharacterEditorPipelineTool*> PipelineTool = GetTypedOuter<UMetaHumanCharacterEditorPipelineTool>();
		TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(PipelineTool->GetTarget());

		TObjectPtr<UMetaHumanCollectionPipeline>& CollectionPipeline = Character->PipelinesPerClass.FindOrAdd(GetSelectedPipelineClass().LoadSynchronous());
		if (CollectionPipeline == nullptr)
		{
			CollectionPipeline = NewObject<UMetaHumanCollectionPipeline>(Character, GetSelectedPipelineClass().LoadSynchronous());
		}
	}

	OnPipelineSelectionChanged.ExecuteIfBound();
}

TObjectPtr<UMetaHumanCollectionPipeline> UMetaHumanCharacterEditorPipelineToolProperties::GetSelectedPipeline() const
{
	TSoftClassPtr<UMetaHumanCollectionPipeline> PipelineClassPtr = GetSelectedPipelineClass();
	if (PipelineClassPtr != nullptr)
	{
		TNotNull<UMetaHumanCharacterEditorPipelineTool*> PipelineTool = GetTypedOuter<UMetaHumanCharacterEditorPipelineTool>();
		TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(PipelineTool->GetTarget());

		return Character->PipelinesPerClass.FindOrAdd(GetSelectedPipelineClass().LoadSynchronous());
	}

	return nullptr;
}

TObjectPtr<UMetaHumanCollectionEditorPipeline> UMetaHumanCharacterEditorPipelineToolProperties::GetSelectedEditorPipeline() const
{
	if (TObjectPtr<UMetaHumanCollectionPipeline> ActivePipeline = GetSelectedPipeline())
	{
		return ActivePipeline->GetMutableEditorPipeline();
	}

	return nullptr;
}

FMetaHumanCharacterEditorBuildParameters UMetaHumanCharacterEditorPipelineToolProperties::InitBuildParameters() const
{
	FMetaHumanCharacterEditorBuildParameters BuildParams;
	BuildParams.PipelineOverride = GetSelectedPipeline();
	BuildParams.PipelineType = PipelineType;
	BuildParams.PipelineQuality = PipelineQuality;
	BuildParams.AnimationSystemName = AnimationSystemName;

	if (BuildParams.PipelineType == EMetaHumanDefaultPipelineType::Cinematic)
	{
		BuildParams.PipelineQuality = EMetaHumanQualityLevel::Cinematic;
	}

	if (PipelineType == EMetaHumanDefaultPipelineType::Cinematic || PipelineType == EMetaHumanDefaultPipelineType::Optimized)
	{
		if (RootDirectory.Path.IsEmpty())
		{
			// Make the full output path to be used based on the MH SDK settings
			if (const UMetaHumanSDKSettings* Settings = GetDefault<UMetaHumanSDKSettings>())
			{
				if (PipelineQuality == EMetaHumanQualityLevel::Cinematic)
				{
					BuildParams.AbsoluteBuildPath = Settings->CinematicImportPath.Path;
				}
				else
				{
					BuildParams.AbsoluteBuildPath = Settings->OptimizedImportPath.Path;
				}
			}
		}
		else
		{
			BuildParams.AbsoluteBuildPath = RootDirectory.Path;
		}

		BuildParams.CommonFolderPath = CommonDirectory.Path;
		BuildParams.NameOverride = NameOverride;
	}
	else if (PipelineType == EMetaHumanDefaultPipelineType::UEFN)
	{
		BuildParams.NameOverride = NameOverride;
	}

	return BuildParams;
}

TSoftClassPtr<UMetaHumanCollectionPipeline> UMetaHumanCharacterEditorPipelineToolProperties::GetSelectedPipelineClass() const
{
	return FMetaHumanCharacterEditorBuild::GetDefaultPipelineClass(PipelineType, PipelineQuality);
}

bool UMetaHumanCharacterEditorPipelineToolProperties::IsUEFN() const
{
	if (IModularFeatures::Get().IsModularFeatureAvailable(IMetaHumanCharacterBuildExtender::FeatureName))
	{
		TArray<IMetaHumanCharacterBuildExtender*> Extenders = IModularFeatures::Get().GetModularFeatureImplementations<IMetaHumanCharacterBuildExtender>(IMetaHumanCharacterBuildExtender::FeatureName);
		for (IMetaHumanCharacterBuildExtender* Extender : Extenders)
		{
			if (Extender->IsUEFN())
			{
				return true;
			}
		}
	}

	return false;
}

bool UMetaHumanCharacterEditorPipelineToolProperties::InitializePipelineTypeVisibility() const
{
	return (IsUEFN() == false);
}

TArray<FName> UMetaHumanCharacterEditorPipelineToolProperties::GetAnimationSystemOptions() const
{
	TArray<FName> Options = { DefaultAnimationSystemNameAnimBP };

	if (IModularFeatures::Get().IsModularFeatureAvailable(IMetaHumanCharacterBuildExtender::FeatureName))
	{
		TArray<IMetaHumanCharacterBuildExtender*> Extenders = IModularFeatures::Get().GetModularFeatureImplementations<IMetaHumanCharacterBuildExtender>(IMetaHumanCharacterBuildExtender::FeatureName);
		for (IMetaHumanCharacterBuildExtender* Extender : Extenders)
		{
			Options += Extender->GetAnimationSystemOptions();
		}
	}

	return Options;
}

bool UMetaHumanCharacterEditorPipelineToolProperties::InitializeAnimationSystemNameVisibility() const
{
	return ((PipelineType == EMetaHumanDefaultPipelineType::Cinematic || PipelineType == EMetaHumanDefaultPipelineType::Optimized) &&
		(GetAnimationSystemOptions().Num() > 1));
}

void UMetaHumanCharacterEditorPipelineTool::Setup()
{
	Super::Setup();

	SetToolDisplayName(LOCTEXT("AssemblyToolName", "Assembly"));

	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);

	PropertyObject = NewObject<UMetaHumanCharacterEditorPipelineToolProperties>(this);
	PropertyObject->DefaultName = Character->GetName();
	AddToolPropertySource(PropertyObject);

	PreviousAssemblySettings = Character->AssemblySettings;
	PropertyObject->CopyFrom(PreviousAssemblySettings);
	PropertyObject->UpdateSelectedPipeline();

	PropertyObject->RestoreProperties(this, Character->GetName());

	if (UMetaHumanCharacterEditorSettings* Settings = GetMutableDefault<UMetaHumanCharacterEditorSettings>())
	{
		UseVirtualTexturesChangedHandle = Settings->OnUseVirtualTexturesChanged.AddUObject(this, &UMetaHumanCharacterEditorPipelineTool::UpdateToolMessage);
	}

	UpdateToolMessage();
}

void UMetaHumanCharacterEditorPipelineTool::UpdateToolMessage()
{
	FText Message = FText::GetEmpty();

	if (PropertyObject)
	{
		if (UMetaHumanCollectionEditorPipeline* SelectedPipeline = PropertyObject->GetSelectedEditorPipeline())
		{
			const FBoolProperty* BakeMaterialsProp = FindFProperty<FBoolProperty>(SelectedPipeline->GetClass(), TEXT("bBakeMaterials"));
			const bool bBakeMaterials = BakeMaterialsProp ? BakeMaterialsProp->GetPropertyValue_InContainer(SelectedPipeline) : false;

			if (bBakeMaterials)
			{
				const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
				if (Settings && Settings->ShouldUseVirtualTextures())
				{
					Message = LOCTEXT("PipelineToolVirtualTexturesInfo", "The assembled MetaHuman will use materials with Virtual Textures. This can be disabled in the project settings.");
				}

				// When override materials are enabled, warn if any of them are not parented (directly or
				// transitively) to one of the official MetaHuman base materials. Baking will still proceed.
				if (const UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target))
				{
					const TArray<FString> NonMHMaterialNames = UE::MetaHuman::Private::GetNonMetaHumanOverrideMaterialNames(*Character);
					if (!NonMHMaterialNames.IsEmpty())
					{
						const FText OverrideWarning = FText::Format(
							LOCTEXT("PipelineToolNonMetaHumanOverrideMaterials",
									"The following override materials are not based on the default MetaHuman materials:\n{0}\n"
									"Baking will still happen, but it may incur in unexpected results."),
							FText::FromString(FString::Join(NonMHMaterialNames, LINE_TERMINATOR)));

						Message = Message.IsEmpty()
							? OverrideWarning
							: FText::Format(LOCTEXT("PipelineToolMessageWithOverrideWarning", "{0}\n\n{1}"), Message, OverrideWarning);
					}
				}
			}
			else
			{
				Message = LOCTEXT("PipelineToolBakeMaterialsDisabledWarning", "Material baking is disabled. The assembled MetaHuman will use unoptimized materials and export a large number of textures.");
			}
		}
	}

	GetToolManager()->DisplayMessage(Message, EToolMessageLevel::UserWarning);
}

void UMetaHumanCharacterEditorPipelineTool::Shutdown(EToolShutdownType ShutdownType)
{
	TNotNull<UMetaHumanCharacter*> Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);

	PropertyObject->SaveProperties(this, Character->GetName());

	FMetaHumanCharacterAssemblySettings CurrentAssemblySettings;
	PropertyObject->CopyTo(CurrentAssemblySettings);

	const bool bAssemblySettingsSimilar = FMetaHumanCharacterAssemblySettings::StaticStruct()->CompareScriptStruct(&Character->AssemblySettings, &CurrentAssemblySettings, PPF_None);

	if (!bAssemblySettingsSimilar)
	{
		Character->Modify();
		Character->AssemblySettings = CurrentAssemblySettings;

		// Add the undo command
		TUniquePtr<FPipelineToolCommandChange> CommandChange = MakeUnique<FPipelineToolCommandChange>(PreviousAssemblySettings, CurrentAssemblySettings, GetToolManager());
		GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("PipelineToolCommandChangeTransaction", "Edit Assembly"));
	}

	PropertyObject->OnPipelineSelectionChanged.Unbind();

	if (UseVirtualTexturesChangedHandle.IsValid())
	{
		if (UMetaHumanCharacterEditorSettings* Settings = GetMutableDefault<UMetaHumanCharacterEditorSettings>())
		{
			Settings->OnUseVirtualTexturesChanged.Remove(UseVirtualTexturesChangedHandle);
		}
		UseVirtualTexturesChangedHandle.Reset();
	}
}

void UMetaHumanCharacterEditorPipelineTool::OnPropertyModified(UObject* InPropertySet, FProperty* InProperty)
{
	if (InPropertySet == PropertyObject)
	{
		if (UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target))
		{
			FMetaHumanCharacterAssemblySettings NewAssemblySettings;
			PropertyObject->CopyTo(NewAssemblySettings);

			TUniquePtr<FPipelineToolCommandChange> CommandChange = MakeUnique<FPipelineToolCommandChange>(PreviousAssemblySettings, NewAssemblySettings, GetToolManager());
			GetToolManager()->GetContextTransactionsAPI()->AppendChange(Character, MoveTemp(CommandChange), LOCTEXT("PipelineToolCommandChange", "Edit Assembly"));

			PreviousAssemblySettings = NewAssemblySettings;
		}
	}

	UpdateToolMessage();
}

bool UMetaHumanCharacterEditorPipelineTool::CanBuild(FText& OutErrorMsg) const
{
	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);

	// This shouldn't happen, so we don't need an error message
	check(Character);

	if (!UMetaHumanCharacterEditorSubsystem::Get()->CanBuildMetaHuman(Character, OutErrorMsg))
	{
		return false;
	}

	UMetaHumanCollectionEditorPipeline* SelectedPipeline = PropertyObject->GetSelectedEditorPipeline();

	if (!SelectedPipeline)
	{
		OutErrorMsg = LOCTEXT("NoSelectedPipeline", "Select a pipeline to assemble MetaHuman");

		return false;
	}

	if (!SelectedPipeline->CanBuild())
	{
		OutErrorMsg = LOCTEXT("CantBuildPipeline", "Selected pipeline can't be built. Please check that all values are correct and valid");

		return false;
	}

	// Check if character override name is valid.
	{
		FName CharacterName = FName(*PropertyObject->NameOverride);
		FText CharacterNameErrors;

		if (!CharacterName.IsValidXName(CharacterNameErrors, INVALID_OBJECTNAME_CHARACTERS INVALID_LONGPACKAGE_CHARACTERS))
		{
			OutErrorMsg = FText::Format(LOCTEXT("CantUseCharacterNameOverride", "Invalid character name. {0}"), { CharacterNameErrors });
			return false;
		}
	}

	return true;
}

void UMetaHumanCharacterEditorPipelineTool::Build() const
{
	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	if (Character)
	{
		FPlatformMemoryStats Stats;

		FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
		constexpr uint64 GB = 1024 * 1024 * 1024;
		if (MemoryStats.AvailableVirtual < (10 * GB))
		{
			const FText NotEnoughMemoryTitle = LOCTEXT("NotEnoughMemoryDialogTitle", "Not enough memory to assemble MetaHuman");
			const FText NotEnoughMemoryMessage = FText::Format(LOCTEXT("PipelineNotEnoughMemoryDialogMessage", "Assembling a MetaHuman Character requires at least 10 GiB of free memory but only {0} is available.\n"
															   "If you proceed the editor might crash. Would you like to continue?"),
															   FText::AsMemory(MemoryStats.AvailableVirtual));

			FSuppressableWarningDialog::FSetupInfo SetupInfo(NotEnoughMemoryMessage, NotEnoughMemoryTitle, TEXT("MetaHumanCharacterSuppressNotEnoughMemory"));
			SetupInfo.ConfirmText = LOCTEXT("PipelineNotEnoughMemoryDialogConfirmText", "Yes");
			SetupInfo.CancelText = LOCTEXT("PipelineNotEnoughMemoryDialogCancelText", "Cancel");

			const FSuppressableWarningDialog NotEnoughMemoryDialog{ SetupInfo };

			const FSuppressableWarningDialog::EResult Result = NotEnoughMemoryDialog.ShowModal();
			if (Result == FSuppressableWarningDialog::EResult::Cancel)
			{
				return;
			}
		}

		const FMetaHumanCharacterEditorBuildParameters BuildParams = PropertyObject->InitBuildParameters();
		UMetaHumanCharacterEditorSubsystem::Get()->BuildMetaHuman(Character, BuildParams);
	}
}

#undef LOCTEXT_NAMESPACE 