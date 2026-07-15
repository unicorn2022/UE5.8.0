// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanDefaultEditorPipelineLegacy.h"

#include "MetaHumanDefaultEditorPipelineUEFN.generated.h"

UCLASS()
class METAHUMANDEFAULTEDITORPIPELINE_API UMetaHumanDefaultEditorPipelineUEFN : public UMetaHumanDefaultEditorPipelineLegacy
{
	GENERATED_BODY()

public:

	UMetaHumanDefaultEditorPipelineUEFN();

	//~Begin UMetaHumanCollectionEditorPipeline interface
	virtual bool BeginCharacterEditorAssemble(TNotNull<class UMetaHumanCollection*> InCollection, const FString& InCharacterName) override;
	virtual void EndCharacterEditorAssemble(TNotNull<class UMetaHumanCollection*> InCollection) override;

	virtual UBlueprint* WriteActorBlueprint(const FWriteBlueprintSettings& InWriteBlueprintSettings) const override;
	virtual bool UpdateActorBlueprint(const UMetaHumanInstance* InCharacterInstance, UBlueprint* InBlueprint) const override;
	//~End UMetaHumanCollectionEditorPipeline interface

public:

	/** File path to the UEFN project where the assembled MetaHuman assets will be exported */
	UPROPERTY(EditAnywhere, DisplayName = "UEFN Project", Category = "Targets", meta = (PipelineDisplay = "Targets"))
	FFilePath UefnProjectFilePath;

	/** Max LOD level to evaluate body correctives to be set in the MetaHuman Component for UEFN */
	UPROPERTY(EditAnywhere, Category = "Scalability")
	int32 BodyLODThreshold = INDEX_NONE;

	// Helper functions
	static void ResizeTexture(TNotNull<UTexture2D*> InTexture);
	static TArray<UMaterialExpression*> ReconnectPathTracingQualitySwitches(TNotNull<UObject*> InMaterialOrMaterialFunction);
	static void RemovePathTracingQualitySwitches(TNotNull<UObject*> InMaterialOrMaterialFunction);
	static void RemoveAssetGuidelines(TNotNull<UObject*> InObject);
	static void SaveObjectForUEFNProject(TNotNull<UObject*> TargetObject);
	static void RemoveAllGraphsFromBlueprint(UBlueprint* Blueprint);

protected:

	//~Begin UMetaHumanDefaultPipelineLegacy interface
	virtual TNotNull<USkeleton*> GenerateSkeleton(FMetaHumanCharacterGeneratedAssets& InGeneratedAssets,
												  TNotNull<USkeleton*> InBaseSkeleton,
												  const FString& InTargetFolderName,
												  TNotNull<UObject*> InOuterForGeneratedAssets) const override;
	//~End UMetaHumanDefaultPipelineLegacy interface

private:

	/**
	 * Gather dependencies of the generated assets so they can be unpacked in the target folder of the pipeline.
	 * Dependencies are added as metadata in InGeneratedAssets to be unpacked later
	 */
	void UnpackCommonDependencies(TArray<UObject*> InRootObjects, TNotNull<const class UMetaHumanCollection*> InCollection, TMap<UObject*, UObject*>& OutDuplicatedRootObjects) const;

	void OnCommonDependenciesUnpacked(const TMap<UObject*, UObject*>& InDuplicatedDependencies) const;

	TSharedPtr<class IPlugin> UEFNPlugin;

	FString MountingPoint;
};