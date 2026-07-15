// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/MetaHumanCrowdHeadPipeline.h"

#include "MetaHumanItemEditorPipeline.h"
#include "MetaHumanCrowdLog.h"

#include "Logging/StructuredLog.h"

UMetaHumanCrowdHeadPipeline::UMetaHumanCrowdHeadPipeline()
{
	// Initialize the specification
	{
		Specification = CreateDefaultSubobject<UMetaHumanCharacterPipelineSpecification>("Specification", /*bTransient*/ true);

		Specification->BuildOutputStruct = FMetaHumanCrowdHeadBuildOutput::StaticStruct();
		Specification->AssemblyOutputStruct = FMetaHumanCrowdHeadAssemblyOutput::StaticStruct();
	}
}

#if WITH_EDITOR
void UMetaHumanCrowdHeadPipeline::SetDefaultEditorPipeline()
{
	EditorPipeline = nullptr;

	const TSubclassOf<UMetaHumanItemEditorPipeline> EditorPipelineClass = GetEditorPipelineClass();
	if (EditorPipelineClass)
	{
		EditorPipeline = NewObject<UMetaHumanItemEditorPipeline>(this, EditorPipelineClass);
	}
}

const UMetaHumanItemEditorPipeline* UMetaHumanCrowdHeadPipeline::GetEditorPipeline() const
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

TSubclassOf<UMetaHumanItemEditorPipeline> UMetaHumanCrowdHeadPipeline::GetEditorPipelineClass() const
{
	const TSoftClassPtr<UMetaHumanItemEditorPipeline> SoftEditorPipelineClass(FSoftObjectPath(TEXT("/Script/MetaHumanCrowdEditor.MetaHumanCrowdHeadEditorPipeline")));
	
	return SoftEditorPipelineClass.Get();
}
#endif

void UMetaHumanCrowdHeadPipeline::AssembleItem(const FAssembleItemParams& Params, const FOnAssemblyComplete& OnComplete) const
{
	const FInstancedStruct& BuildOutput = Params.ItemBuiltData[Params.BaseItemPath].BuildOutput;

	if (!BuildOutput.GetPtr<FMetaHumanCrowdHeadBuildOutput>())
	{
		UE_LOGFMT(LogMetaHumanCrowd, Error, "Build output not provided to Groom pipeline during assembly");
		
		OnComplete.ExecuteIfBound(FMetaHumanAssemblyOutput());
		return;
	}

	const FMetaHumanCrowdHeadBuildOutput& HeadBuildOutput = BuildOutput.Get<FMetaHumanCrowdHeadBuildOutput>();

	FMetaHumanAssemblyOutput AssemblyOutput;
	FMetaHumanCrowdHeadAssemblyOutput& HeadAssemblyOutput = AssemblyOutput.PipelineAssemblyOutput.InitializeAs<FMetaHumanCrowdHeadAssemblyOutput>();

	HeadAssemblyOutput.HeadMesh = HeadBuildOutput.HeadMesh;
	HeadAssemblyOutput.BakedAnimRootProvider = HeadBuildOutput.BakedAnimRootProvider;

	OnComplete.ExecuteIfBound(MoveTemp(AssemblyOutput));
}

TNotNull<const UMetaHumanCharacterPipelineSpecification*> UMetaHumanCrowdHeadPipeline::GetSpecification() const
{
	return Specification;
}
