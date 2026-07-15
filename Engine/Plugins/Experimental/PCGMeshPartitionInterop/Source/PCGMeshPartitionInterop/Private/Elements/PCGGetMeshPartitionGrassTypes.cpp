// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGGetMeshPartitionGrassTypes.h"

#include "PCGParamData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Utils/PCGLogErrors.h"

#include "MeshPartitionDefinition.h"

#include "LandscapeGrassType.h"
#include "LandscapeUtils.h"

#include "MaterialCachedData.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

#define LOCTEXT_NAMESPACE "PCGGetMeshPartitionGrassTypesElement"

namespace UE::MeshPartition
{

#if WITH_EDITOR
FText UPCGGetMeshPartitionGrassTypesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Get Mesh Partition Grass Types");
}

FText UPCGGetMeshPartitionGrassTypesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Emits an attribute set with the landscape grass types declared by a mesh partition definition's material.");
}
#endif // WITH_EDITOR

TArray<FPCGPinProperties> UPCGGetMeshPartitionGrassTypesSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Param);
	Pin.bAllowMultipleData = false;
	return PinProperties;
}

FPCGElementPtr UPCGGetMeshPartitionGrassTypesSettings::CreateElement() const
{
	return MakeShared<FPCGGetMeshPartitionGrassTypesElement>();
}

bool FPCGGetMeshPartitionGrassTypesElement::PrepareDataInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGetMeshPartitionGrassTypesElement::PrepareDataInternal);
	check(InContext);

	const UPCGGetMeshPartitionGrassTypesSettings* Settings = InContext->GetInputSettings<UPCGGetMeshPartitionGrassTypesSettings>();
	check(Settings);

	if (Settings->Definition.IsNull())
	{
		return true;
	}

	FPCGGetMeshPartitionGrassTypesContext* Context = static_cast<FPCGGetMeshPartitionGrassTypesContext*>(InContext);

	if (!Context->WasLoadRequested())
	{
		return Context->RequestResourceLoad(Context, { Settings->Definition.ToSoftObjectPath() }, !Settings->bSynchronousLoad);
	}

	return true;
}

bool FPCGGetMeshPartitionGrassTypesElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGetMeshPartitionGrassTypesElement::ExecuteInternal);
	check(InContext);

	const UPCGGetMeshPartitionGrassTypesSettings* Settings = InContext->GetInputSettings<UPCGGetMeshPartitionGrassTypesSettings>();
	check(Settings);

	const UMeshPartitionDefinition* Definition = Settings->Definition.Get();
	if (!Definition)
	{
		return true;
	}

	UMaterialInterface* MaterialInterface = Definition->GetMaterial();
	if (!MaterialInterface)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("NoMaterial", "Mesh partition definition has no material assigned."), InContext);
		return true;
	}

	const UMaterial* Material = MaterialInterface->GetMaterial();
	if (!Material)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("NoBaseMaterial", "Mesh partition definition material has no resolvable base material."), InContext);
		return true;
	}

	const TMap<FName, TObjectPtr<ULandscapeGrassType>> ValidNamedGrassTypes = UE::Landscape::Grass::FilterValidNamedGrassTypes(Material->GetCachedExpressionData().NamedGrassTypes);

	UPCGParamData* OutParamData = FPCGContext::NewObject_AnyThread<UPCGParamData>(InContext);
	check(OutParamData && OutParamData->Metadata);

	FPCGMetadataAttribute<FName>* GrassNameAttribute = OutParamData->Metadata->CreateAttribute<FName>(Settings->GrassNameAttributeName, NAME_None, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);
	if (!GrassNameAttribute)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("GrassNameAttributeCreateFailed", "Failed to create attribute '{0}' on output parameter data."), FText::FromName(Settings->GrassNameAttributeName)), InContext);
		return true;
	}

	FPCGMetadataAttribute<FSoftObjectPath>* GrassTypeAttribute = OutParamData->Metadata->CreateAttribute<FSoftObjectPath>(Settings->GrassTypeAttributeName, FSoftObjectPath(), /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);
	if (!GrassTypeAttribute)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("GrassTypeAttributeCreateFailed", "Failed to create attribute '{0}' on output parameter data."), FText::FromName(Settings->GrassTypeAttributeName)), InContext);
		return true;
	}

	for (const TPair<FName, TObjectPtr<ULandscapeGrassType>>& Pair : ValidNamedGrassTypes)
	{
		const PCGMetadataEntryKey EntryKey = OutParamData->Metadata->AddEntry();
		GrassNameAttribute->SetValue(EntryKey, Pair.Key);
		GrassTypeAttribute->SetValue(EntryKey, FSoftObjectPath(Pair.Value));
	}

	FPCGTaggedData& Output = InContext->OutputData.TaggedData.Emplace_GetRef();
	Output.Data = OutParamData;
	Output.Pin = PCGPinConstants::DefaultOutputLabel;

	return true;
}

} // namespace UE::MeshPartition

#undef LOCTEXT_NAMESPACE
