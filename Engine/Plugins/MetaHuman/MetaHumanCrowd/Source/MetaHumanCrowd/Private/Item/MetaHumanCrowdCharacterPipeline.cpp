// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/MetaHumanCrowdCharacterPipeline.h"

#include "MetaHumanItemEditorPipeline.h"
#include "MetaHumanCrowdLog.h"

#include "Logging/StructuredLog.h"

UMetaHumanCrowdCharacterPipeline::UMetaHumanCrowdCharacterPipeline()
{
	// Initialize the specification
	{
		Specification = CreateDefaultSubobject<UMetaHumanCharacterPipelineSpecification>("Specification", /*bTransient*/ true);

		Specification->BuildOutputStruct = FMetaHumanCrowdCharacterBuildOutput::StaticStruct();
		Specification->AssemblyOutputStruct = FMetaHumanCrowdCharacterAssemblyOutput::StaticStruct();
	}
}

#if WITH_EDITOR
void UMetaHumanCrowdCharacterPipeline::SetDefaultEditorPipeline()
{
	EditorPipeline = nullptr;

	const TSubclassOf<UMetaHumanItemEditorPipeline> EditorPipelineClass = GetEditorPipelineClass();
	if (EditorPipelineClass)
	{
		EditorPipeline = NewObject<UMetaHumanItemEditorPipeline>(this, EditorPipelineClass);
	}
}

const UMetaHumanItemEditorPipeline* UMetaHumanCrowdCharacterPipeline::GetEditorPipeline() const
{
	// If there's no editor pipeline instance, we can use the Class Default Object, because
	// pipelines are stateless and won't be modified when used.
	//
	// This is unfortunately a slow path, as it involves looking the class up by name. We could
	// cache this if it becomes a performance issue.
	if (!EditorPipeline)
	{
		const TSubclassOf<UMetaHumanItemEditorPipeline> EditorPipelineClass = GetEditorPipelineClass();
		if (EditorPipelineClass)
		{
			return EditorPipelineClass.GetDefaultObject();
		}
	}

	return EditorPipeline;
}

TSubclassOf<UMetaHumanItemEditorPipeline> UMetaHumanCrowdCharacterPipeline::GetEditorPipelineClass() const
{
	const TSoftClassPtr<UMetaHumanItemEditorPipeline> SoftEditorPipelineClass(FSoftObjectPath(TEXT("/Script/MetaHumanCrowdEditor.MetaHumanCrowdCharacterEditorPipeline")));

	return SoftEditorPipelineClass.Get();
}
#endif

void UMetaHumanCrowdCharacterPipeline::AssembleItem(const FAssembleItemParams& Params, const FOnAssemblyComplete& OnComplete) const
{
	const FInstancedStruct& BuildOutput = Params.ItemBuiltData[Params.BaseItemPath].BuildOutput;

	if (!BuildOutput.GetPtr<FMetaHumanCrowdCharacterBuildOutput>())
	{
		UE_LOGFMT(LogMetaHumanCrowd, Error, "Build output not provided to Character pipeline during assembly");

		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	const FMetaHumanCrowdCharacterBuildOutput& CharacterBuildOutput = BuildOutput.Get<FMetaHumanCrowdCharacterBuildOutput>();

	FMetaHumanAssemblyOutput AssemblyOutput;
	FMetaHumanCrowdCharacterAssemblyOutput& CharacterAssemblyOutput = AssemblyOutput.PipelineAssemblyOutput.InitializeAs<FMetaHumanCrowdCharacterAssemblyOutput>();

	CharacterAssemblyOutput.FaceMesh = CharacterBuildOutput.FaceMesh;
	CharacterAssemblyOutput.BodyMesh = CharacterBuildOutput.BodyMesh;
	CharacterAssemblyOutput.MergedHeadAndBodyMesh = CharacterBuildOutput.MergedHeadAndBodyMesh;
	CharacterAssemblyOutput.BodyMeasurements = CharacterBuildOutput.BodyMeasurements;

	OnComplete.ExecuteIfBound(MoveTemp(AssemblyOutput));
}

TNotNull<const UMetaHumanCharacterPipelineSpecification*> UMetaHumanCrowdCharacterPipeline::GetSpecification() const
{
	return Specification;
}
