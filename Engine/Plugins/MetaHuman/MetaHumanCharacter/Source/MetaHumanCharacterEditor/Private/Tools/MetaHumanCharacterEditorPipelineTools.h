// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseTools/SingleTargetWithSelectionTool.h"
#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorSubTools.h"
#include "MetaHumanCharacterPipeline.h"
#include "MetaHumanTypes.h"
#include "Misc/Optional.h"
#include "Subsystem/MetaHumanCharacterBuild.h"

#include "MetaHumanCharacterEditorPipelineTools.generated.h"

class UMetaHumanCharacter;
class UMetaHumanCollection;

UENUM()
enum class EMetaHumanCharacterPipelineEditingTool : uint8
{
	Pipeline
};

UCLASS()
class UMetaHumanCharacterEditorPipelineToolBuilder : public UMetaHumanCharacterEditorToolWithToolTargetsBuilder
{
	GENERATED_BODY()

public:

	//~Begin UInteractiveToolWithToolTargetsBuilder interface
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& InSceneState) const override;
	//~End UInteractiveToolWithToolTargetsBuilder interface

	UPROPERTY()
	EMetaHumanCharacterPipelineEditingTool ToolType = EMetaHumanCharacterPipelineEditingTool::Pipeline;
};

/**
 * Properties used to customize the pipeline UI and generate the parameters for a pipeline assembly
 * NOTE: this is transient and will reset when the tool closes, it is a temporary solution until we find a better solution
 */
UCLASS(Transient)
class UMetaHumanCharacterEditorPipelineToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	//~Begin UObject Interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~End UObject Interface

	void CopyFrom(const FMetaHumanCharacterAssemblySettings& InAssemblySettings);
	void CopyTo(FMetaHumanCharacterAssemblySettings& OutAssemblySettings);

public:
	bool IsUEFN() const;

	/** Selected type of pipeline to run the assembly */
	UPROPERTY(EditAnywhere, Category = "Assembly Selection", meta = (DisplayName = "Assembly", EditCondition = InitializePipelineTypeVisibility, EditConditionHides))
	EMetaHumanDefaultPipelineType PipelineType = EMetaHumanDefaultPipelineType::Cinematic;

	UFUNCTION()
	bool InitializePipelineTypeVisibility() const;

	FName DefaultAnimationSystemNameAnimBP = TEXT("AnimBP");

	/** Target animation system to be used by the assembled MetaHuman. */
	UPROPERTY(EditAnywhere, Category = "Animation", meta = (DisplayName = "Animation System", GetOptions = GetAnimationSystemOptions, EditCondition = InitializeAnimationSystemNameVisibility, EditConditionHides))
	FName AnimationSystemName = DefaultAnimationSystemNameAnimBP;

	/** Get the list of available animation systems, including ones registered by external plugins. */
	UFUNCTION()
	TArray<FName> GetAnimationSystemOptions() const;

	/**
	 * Controls visibility for the animation system combo box.
	 * The combo box will be visible if another plugin registers an animation system.
	 * Animation system selection currently is only possible for Cinematic and Optimized MetaHumans.
	 */
	UFUNCTION()
	bool InitializeAnimationSystemNameVisibility() const;
	
	/** Quality setting for the pipeline */
	UPROPERTY(EditAnywhere, Category = "Assembly Selection", meta = (DisplayName = "Quality", InvalidEnumValues = "Cinematic", EditConditionHides, EditCondition = "PipelineType == EMetaHumanDefaultPipelineType::Optimized || PipelineType == EMetaHumanDefaultPipelineType::UEFN"))
	EMetaHumanQualityLevel PipelineQuality = EMetaHumanQualityLevel::High;

	/** Path to the Root directory where the assembled assets will be placed so that the final structure is <RootDirectory>/<Name> */
	UPROPERTY(EditAnywhere, Category = "Targets", meta = (ContentDir, PipelineDisplay = "Targets", EditConditionHides, EditCondition = "(PipelineType == EMetaHumanDefaultPipelineType::Cinematic || PipelineType == EMetaHumanDefaultPipelineType::Optimized)"))
	FDirectoryPath RootDirectory{ TEXT("/Game/MetaHumans") };

	/** Path to a project directory where assets shared by assembled MetaHumans are place. If referenced assets are missing, they will be populated as needed. */
	UPROPERTY(EditAnywhere, Category = "Advanced Options", meta = (ContentDir, PipelineDisplay = "Targets", EditConditionHides, EditCondition = "(PipelineType == EMetaHumanDefaultPipelineType::Cinematic || PipelineType == EMetaHumanDefaultPipelineType::Optimized)"))
	FDirectoryPath CommonDirectory{ TEXT("/Game/MetaHumans/Common") };

	/** Character default name, usually the name of the asset. */
	FString DefaultName;

	/** Character name to use for the generated assets. */
	UPROPERTY(EditAnywhere, Category = "Targets", meta = (DisplayName = "Name", EditConditionHides, EditCondition = "(PipelineType == EMetaHumanDefaultPipelineType::Cinematic || PipelineType == EMetaHumanDefaultPipelineType::Optimized || PipelineType == EMetaHumanDefaultPipelineType::UEFN)"))
	FString NameOverride;

	// Trigger when either PipelineType or Quality are modified
	FSimpleDelegate OnPipelineSelectionChanged;

	// Returns the class of the currently selected pipeline
	[[nodiscard]] TSoftClassPtr<class UMetaHumanCollectionPipeline> GetSelectedPipelineClass() const;

	// Helpers to get an UObject pointer to the instance of the currently selected pipeline from the character data
	[[nodiscard]] TObjectPtr<class UMetaHumanCollectionPipeline> GetSelectedPipeline() const;
	[[nodiscard]] TObjectPtr<class UMetaHumanCollectionEditorPipeline> GetSelectedEditorPipeline() const;
	
	// Updates the character data with the selected pipeline
	void UpdateSelectedPipeline();

	// Generates the Build params to set in the tool for passing to FMetaHumanCharacterEditorBuild::BuildMetaHumanCharacter()
	[[nodiscard]] FMetaHumanCharacterEditorBuildParameters InitBuildParameters() const;
};

/**
 * Tool for manipulating the build pipeline.
 */
UCLASS()
class UMetaHumanCharacterEditorPipelineTool : public USingleTargetWithSelectionTool
{
	GENERATED_BODY()

public:
	//~Begin UInteractiveTool interface
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	
	virtual bool HasCancel() const override { return false; }
	virtual bool HasAccept() const override { return false; }
	virtual bool CanAccept() const override { return false; }

	virtual void OnPropertyModified(UObject* InPropertySet, FProperty* InProperty) override;
	//~End UInteractiveTool interface

	UMetaHumanCharacterEditorPipelineToolProperties* GetPipelineProperty() const { return PropertyObject; }

	bool CanBuild(FText& OutErrorMsg) const;
	void Build() const;

protected:

	/** Refreshes informational messages displayed by the tool (e.g. Virtual Textures usage notice). */
	void UpdateToolMessage();
	friend class FPipelineToolCommandChange;

	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorPipelineToolProperties> PropertyObject;

	/** Keep track of previously set assembly settings */
	FMetaHumanCharacterAssemblySettings PreviousAssemblySettings;

	/** Handle to the OnUseVirtualTexturesChanged delegate so it can be removed on Shutdown */
	FDelegateHandle UseVirtualTexturesChangedHandle;
};
