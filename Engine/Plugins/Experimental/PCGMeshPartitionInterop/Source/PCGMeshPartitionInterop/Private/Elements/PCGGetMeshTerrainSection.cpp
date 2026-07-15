// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGGetMeshTerrainSection.h"

#include "Data/PCGMeshTerrainSectionData.h"

#include "PCGGraphExecutionStateInterface.h"
#include "Elements/PCGActorSelector.h"
#include "Helpers/PCGHelpers.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include "MeshPartition.h"
#include "MeshPartitionCompiledSection.h"

#if WITH_EDITOR
#include "Data/PCGMeshPartitionSelectionKey.h"
#include "Helpers/PCGDynamicTrackingHelpers.h"
#include "MeshPartitionEditorComponent.h"
#include "MeshPartitionPreviewSection.h"
#endif

#define LOCTEXT_NAMESPACE "PCGGetMeshTerrainSectionElement"

namespace UE::MeshPartition
{

#if WITH_EDITOR
FText UPCGGetMeshTerrainSectionSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Get Mesh Terrain Section");
}

FText UPCGGetMeshTerrainSectionSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Emits one Mesh Terrain Section data per mesh terrain section that overlaps the generation volume.");
}
#endif // WITH_EDITOR

TArray<FPCGPinProperties> UPCGGetMeshTerrainSectionSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, FPCGDataTypeInfoMeshTerrainSection::AsId());
	return PinProperties;
}

FPCGElementPtr UPCGGetMeshTerrainSectionSettings::CreateElement() const
{
	return MakeShared<FPCGGetMeshTerrainSectionElement>();
}

bool FPCGGetMeshTerrainSectionElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGetMeshTerrainSectionElement::ExecuteInternal);

	const UPCGGetMeshTerrainSectionSettings* Settings = InContext->GetInputSettings<UPCGGetMeshTerrainSectionSettings>();
	check(Settings);

	IPCGGraphExecutionSource* ExecutionSource = InContext->ExecutionSource.Get();
	if (!ExecutionSource)
	{
		return true;
	}

	const FBox InputBounds = PCGHelpers::GetGridBounds(ExecutionSource->GetExecutionState().GetTypedTarget<AActor>(), ExecutionSource);
	if (!InputBounds.IsValid)
	{
		return true;
	}

	auto EmitSectionActor = [InContext](AActor* SectionActor)
	{
		UPCGMeshTerrainSectionData* OutData = FPCGContext::NewObject_AnyThread<UPCGMeshTerrainSectionData>(InContext);
		OutData->SetSectionActor(SectionActor);

		FPCGTaggedData& Output = InContext->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = OutData;
	};

	FPCGActorSelectorSettings ActorSelector;
	ActorSelector.ActorFilter = EPCGActorFilter::AllWorldActors;
	ActorSelector.ActorSelection = EPCGActorSelection::ByClass;
	ActorSelector.bSelectMultiple = true;

	auto SelfIgnoreCheck = [](const AActor*) -> bool { return true; };

	UWorld* World = ExecutionSource->GetExecutionState().GetWorld();

	if (World && World->IsGameWorld())
	{
		// Runtime path: enumerate ACompiledSection actors directly.
		ActorSelector.ActorSelectionClass = ACompiledSection::StaticClass();

		auto BoundsCheck = [&InputBounds](const AActor* Actor) -> bool
		{
			const FBox Bounds = Actor ? Actor->GetComponentsBoundingBox(/*bNonColliding=*/ true) : FBox(EForceInit::ForceInit);
			return Bounds.IsValid && InputBounds.Overlap(Bounds).GetVolume() > UE_KINDA_SMALL_NUMBER;
		};

		TArray<AActor*> CompiledSectionActors = PCGActorSelector::FindActors(ActorSelector, ExecutionSource, BoundsCheck, SelfIgnoreCheck);
		for (AActor* Actor : CompiledSectionActors)
		{
			if (ACompiledSection* CompiledSection = Cast<ACompiledSection>(Actor))
			{
				EmitSectionActor(CompiledSection);
			}
		}
	}
#if WITH_EDITOR
	else
	{
		// Editor path: preview sections are owned by the partition's editor component, not top-level world actors.
		// The mesh partition actor's own bounds are not representative of its preview sections' bounds, so bounds filtering is deferred until we iterate preview sections below.
		ActorSelector.ActorSelectionClass = AMeshPartition::StaticClass();

		auto BoundsCheck = [](const AActor*) -> bool { return true; };

		TArray<AActor*> MeshPartitionActors = PCGActorSelector::FindActors(ActorSelector, ExecutionSource, BoundsCheck, SelfIgnoreCheck);

		FPCGDynamicTrackingHelper DynamicTracking;
		DynamicTracking.EnableAndInitialize(InContext, /*NumKeysPerActor=*/2 * MeshPartitionActors.Num());

		for (AActor* Actor : MeshPartitionActors)
		{
			AMeshPartition* MeshPartition = Cast<AMeshPartition>(Actor);
			if (!MeshPartition)
			{
				continue;
			}

			UMeshPartitionEditorComponent* EditorComponent = Cast<UMeshPartitionEditorComponent>(MeshPartition->GetMeshPartitionComponent());
			if (!EditorComponent)
			{
				continue;
			}

			// Modifier changes emit FPCGLayerSelectionKey(Final); bounds-only changes emit FPCGGlobalSelectionKey.
			// Register both so per-modifier and global notifications both reach this node.
			FPCGSelectionKey GlobalKey = FPCGSelectionKey::CreateFromObjectPtr(MeshPartition);
			FPCGSelectionKey FinalKey(GlobalKey);

			FinalKey.CustomKey.InitializeAs<FPCGLayerSelectionKey>(EPCGQueryType::Final, /*LayerName=*/ NAME_None, /*SubPriority=*/0, /*bIsFromPrevious=*/false, EPCGLayerSelectionKeyType::Listener);
			DynamicTracking.AddToTracking(MoveTemp(FinalKey), /*bIsCulled=*/true);

			GlobalKey.CustomKey.InitializeAs<FPCGGlobalSelectionKey>();
			DynamicTracking.AddToTracking(MoveTemp(GlobalKey), /*bIsCulled=*/true);

			EditorComponent->ForAllPreviewSections([&EmitSectionActor, &InputBounds](APreviewSection* PreviewSection)
			{
				if (!PreviewSection)
				{
					return true;
				}

				const FBox PreviewBounds = PreviewSection->GetPreviewMeshBounds();
				if (!PreviewBounds.IsValid || InputBounds.Overlap(PreviewBounds).GetVolume() <= UE_KINDA_SMALL_NUMBER)
				{
					return true;
				}

				EmitSectionActor(PreviewSection);
				return true;
			});
		}

		DynamicTracking.Finalize(InContext);
	}
#endif // WITH_EDITOR

	return true;
}

} // namespace UE::MeshPartition

#undef LOCTEXT_NAMESPACE
