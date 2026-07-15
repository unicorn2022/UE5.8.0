// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGGetMeshTerrainSectionActor.h"

#include "Data/PCGMeshTerrainSectionData.h"

#include "PCGParamData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Utils/PCGLogErrors.h"

#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPath.h"

#define LOCTEXT_NAMESPACE "PCGGetMeshTerrainSectionActorElement"

namespace UE::MeshPartition
{

#if WITH_EDITOR
FText UPCGGetMeshTerrainSectionActorSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Get Mesh Terrain Section Actor");
}

FText UPCGGetMeshTerrainSectionActorSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Emits an attribute set with the soft object path to the actor of the given mesh terrain section.");
}
#endif // WITH_EDITOR

TArray<FPCGPinProperties> UPCGGetMeshTerrainSectionActorSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	FPCGPinProperties& InputPin = PinProperties.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, FPCGDataTypeInfoMeshTerrainSection::AsId(), /*bInAllowMultipleConnections=*/false, /*bAllowMultipleData=*/false);
	InputPin.SetRequiredPin();
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGGetMeshTerrainSectionActorSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Param);
	Pin.bAllowMultipleData = false;
	return PinProperties;
}

FPCGElementPtr UPCGGetMeshTerrainSectionActorSettings::CreateElement() const
{
	return MakeShared<FPCGGetMeshTerrainSectionActorElement>();
}

bool FPCGGetMeshTerrainSectionActorElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGetMeshTerrainSectionActorElement::ExecuteInternal);
	check(InContext);

	const UPCGGetMeshTerrainSectionActorSettings* Settings = InContext->GetInputSettings<UPCGGetMeshTerrainSectionActorSettings>();
	check(Settings);

	const TArray<FPCGTaggedData> Inputs = InContext->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	if (Inputs.IsEmpty())
	{
		return true;
	}

	if (Inputs.Num() > 1)
	{
		PCGLog::InputOutput::LogFirstInputOnlyWarning(PCGPinConstants::DefaultInputLabel, InContext);
	}

	const UPCGMeshTerrainSectionData* SectionData = Cast<UPCGMeshTerrainSectionData>(Inputs[0].Data.Get());
	if (!SectionData)
	{
		return true;
	}

	AActor* Actor = SectionData->GetSectionActor();
	if (!Actor)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("InvalidActor", "Input section actor is no longer valid."), InContext);
		return true;
	}

	UPCGParamData* OutParamData = FPCGContext::NewObject_AnyThread<UPCGParamData>(InContext);
	FPCGMetadataAttribute<FSoftObjectPath>* Attribute = OutParamData->Metadata->CreateAttribute<FSoftObjectPath>(Settings->OutputAttributeName, FSoftObjectPath(), /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);

	if (!Attribute)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("AttributeCreateFailed", "Failed to create attribute '{0}' on output parameter data."), FText::FromName(Settings->OutputAttributeName)), InContext);
		return true;
	}

	const PCGMetadataEntryKey EntryKey = OutParamData->Metadata->AddEntry();
	Attribute->SetValue(EntryKey, FSoftObjectPath(Actor));

	FPCGTaggedData& Output = InContext->OutputData.TaggedData.Emplace_GetRef();
	Output.Data = OutParamData;
	Output.Pin = PCGPinConstants::DefaultOutputLabel;

	return true;
}

} // namespace UE::MeshPartition

#undef LOCTEXT_NAMESPACE
