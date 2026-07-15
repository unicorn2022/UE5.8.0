// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorExportTools.h"

#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterExportBlueprintLibrary.h"
#include "Tools/MetaHumanCharacterEditorToolTargetUtil.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorExportTools"

// DCC Export

UClass* UMetaHumanCharacterEditorDCCExportTool::GetExportPropertiesClass() const
{
	return UMetaHumanCharacterEditorDCCExportProperties::StaticClass();
}

FText UMetaHumanCharacterEditorDCCExportTool::GetExportToolDisplayName() const
{
	return LOCTEXT("DCCExportToolName", "DCC Export");
}

FText UMetaHumanCharacterEditorDCCExportTool::GetExportButtonText() const
{
	return LOCTEXT("DCCExportButtonText", "Export DCC Package");
}

bool UMetaHumanCharacterEditorDCCExportTool::CanExport(FText& OutErrorMsg) const
{
	const UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);

	if (!IsValid(Character) || !Character->IsCharacterValid())
	{
		OutErrorMsg = LOCTEXT("DCCExportDisabled_InvalidCharacter", "Character is not valid.");
		return false;
	}

	const UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	check(Subsystem);
	if (Subsystem->GetRiggingState(Character) != EMetaHumanCharacterRigState::Rigged)
	{
		OutErrorMsg = LOCTEXT("DCCExportDisabled_NotRigged", "Character is not rigged. Create a rig before exporting.");
		return false;
	}

	if (!Character->HasHighResolutionTextures())
	{
		OutErrorMsg = LOCTEXT("DCCExportDisabled_NoHighResTextures", "The Character is missing textures, use Download Texture Sources to create them before exporting.");
		return false;
	}

	// DCCExportTool should only have UMetaHumanCharacterEditorDCCExportProperties
	const UMetaHumanCharacterEditorDCCExportProperties* DCCExportProperties = CastChecked<UMetaHumanCharacterEditorDCCExportProperties>(GetExportProperties());
	if (DCCExportProperties->ExternalPath.Path.IsEmpty())
	{
		OutErrorMsg = LOCTEXT("DCCExportDisabled_NoExternalPath", "Please specify an external path for the DCC export.");
		return false;
	}

	return true;
}

void UMetaHumanCharacterEditorDCCExportTool::Export() const
{
	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	const UMetaHumanCharacterEditorDCCExportProperties* DCCExportProperties = Cast<UMetaHumanCharacterEditorDCCExportProperties>(GetExportProperties());

	// These are validated by CanExport() which gates the Export button
	check(IsValid(Character) && Character->IsCharacterValid());
	check(DCCExportProperties);

	FMetaHumanDCCExportParams Params;
	Params.ExternalPath = DCCExportProperties->ExternalPath.Path;
	Params.bBakeMakeUp = DCCExportProperties->bBakeMakeUp;
	Params.bCompressInZipFile = DCCExportProperties->bCompressInZipFile;
	Params.ArchiveName = DCCExportProperties->ArchiveName;

	UMetaHumanCharacterExportBlueprintLibrary::ExportDCC(Character, Params);
}

// DNA Export

UClass* UMetaHumanCharacterEditorDNAExportTool::GetExportPropertiesClass() const
{
	return UMetaHumanCharacterEditorDNAExportProperties::StaticClass();
}

FText UMetaHumanCharacterEditorDNAExportTool::GetExportToolDisplayName() const
{
	return LOCTEXT("DNAExportToolName", "DNA Export");
}

FText UMetaHumanCharacterEditorDNAExportTool::GetExportButtonText() const
{
	return LOCTEXT("DNAExportButtonText", "Export DNA");
}

bool UMetaHumanCharacterEditorDNAExportTool::CanExport(FText& OutErrorMsg) const
{
	const UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);

	if (!IsValid(MetaHumanCharacter) || !MetaHumanCharacter->IsCharacterValid())
	{
		OutErrorMsg = LOCTEXT("DNAExportDisabled_InvalidCharacter", "Character is not valid.");
		return false;
	}

	const UMetaHumanCharacterEditorDNAExportProperties* DNAExportProperties = CastChecked<UMetaHumanCharacterEditorDNAExportProperties>(GetExportProperties());

	if (DNAExportProperties->ExternalPath.Path.IsEmpty())
	{
		OutErrorMsg = LOCTEXT("DNAExportDisabled_NoExternalPath", "Please specify an external path for the DNA export.");
		return false;
	}

	if (!DNAExportProperties->bDNAHead && !DNAExportProperties->bDNABody)
	{
		OutErrorMsg = LOCTEXT("DNAExportDisabled_NothingSelected", "Please enable at least one export option.");
		return false;
	}

	if (DNAExportProperties->bDNAHead && !MetaHumanCharacter->HasFaceDNA())
	{
		OutErrorMsg = LOCTEXT("DNAExportDisabled_NoFaceDNA", "Character does not have face DNA. Create a rig before exporting.");
		return false;
	}

	return true;
}

void UMetaHumanCharacterEditorDNAExportTool::Export() const
{
	UMetaHumanCharacter* MetaHumanCharacter = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	const UMetaHumanCharacterEditorDNAExportProperties* DNAExportProperties = Cast<UMetaHumanCharacterEditorDNAExportProperties>(GetExportProperties());

	// These are validated by CanExport() which gates the Export button
	check(IsValid(MetaHumanCharacter) && MetaHumanCharacter->IsCharacterValid());
	check(DNAExportProperties);

	FMetaHumanDNAExportParams Params;
	Params.ExternalPath = DNAExportProperties->ExternalPath.Path;
	Params.bDNAHead = DNAExportProperties->bDNAHead;
	Params.bDNABody = DNAExportProperties->bDNABody;

	UMetaHumanCharacterExportBlueprintLibrary::ExportDNA(MetaHumanCharacter, Params);
}

// Geometry Export

UClass* UMetaHumanCharacterEditorGeometryExportTool::GetExportPropertiesClass() const
{
	return UMetaHumanCharacterEditorGeometryExportProperties::StaticClass();
}

FText UMetaHumanCharacterEditorGeometryExportTool::GetExportToolDisplayName() const
{
	return LOCTEXT("GeometryExportToolName", "Geometry Export");
}

FText UMetaHumanCharacterEditorGeometryExportTool::GetExportButtonText() const
{
	return LOCTEXT("GeometryExportButtonText", "Export Geometry");
}

bool UMetaHumanCharacterEditorGeometryExportTool::CanExport(FText& OutErrorMsg) const
{
	const UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	if (!IsValid(Character) || !Character->IsCharacterValid())
	{
		OutErrorMsg = LOCTEXT("GeometryExportDisabled_InvalidCharacter", "Character is not valid.");
		return false;
	}

	const UMetaHumanCharacterEditorGeometryExportProperties* GeometryExportProperties = CastChecked<UMetaHumanCharacterEditorGeometryExportProperties>(GetExportProperties());

	if (GeometryExportProperties->ProjectPath.Path.IsEmpty())
	{
		OutErrorMsg = LOCTEXT("GeometryExportDisabled_NoPath", "Please specify a project path for the geometry export.");
		return false;
	}

	if (!GeometryExportProperties->bHeadSkeletalMesh && !GeometryExportProperties->bBodySkeletalMesh && !GeometryExportProperties->bFullBodySkeletalMesh)
	{
		OutErrorMsg = LOCTEXT("GeometryExportDisabled_NothingSelected", "Please enable at least one export option.");
		return false;
	}

	return true;
}

void UMetaHumanCharacterEditorGeometryExportTool::Export() const
{
	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	const UMetaHumanCharacterEditorGeometryExportProperties* GeometryExportProperties = Cast<UMetaHumanCharacterEditorGeometryExportProperties>(GetExportProperties());

	// These are validated by CanExport() which gates the Export button
	check(IsValid(Character) && Character->IsCharacterValid());
	check(GeometryExportProperties);

	FMetaHumanGeometryExportParams Params;
	Params.ProjectPath = GeometryExportProperties->ProjectPath.Path;
	Params.bHeadSkeletalMesh = GeometryExportProperties->bHeadSkeletalMesh;
	Params.bBodySkeletalMesh = GeometryExportProperties->bBodySkeletalMesh;
	Params.bFullBodySkeletalMesh = GeometryExportProperties->bFullBodySkeletalMesh;
	Params.bOverwriteExistingAssets = GeometryExportProperties->bOverwriteExistingAssets;

	UMetaHumanCharacterExportBlueprintLibrary::ExportGeometry(Character, Params);
}

// Materials Export

UClass* UMetaHumanCharacterEditorMaterialsExportTool::GetExportPropertiesClass() const
{
	return UMetaHumanCharacterEditorMaterialsExportProperties::StaticClass();
}

FText UMetaHumanCharacterEditorMaterialsExportTool::GetExportToolDisplayName() const
{
	return LOCTEXT("MaterialsExportToolName", "Materials Export");
}

FText UMetaHumanCharacterEditorMaterialsExportTool::GetExportButtonText() const
{
	return LOCTEXT("MaterialsExportButtonText_Materials", "Export Materials");
}

bool UMetaHumanCharacterEditorMaterialsExportTool::CanExport(FText& OutErrorMsg) const
{
	const UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);

	if (!IsValid(Character) || !Character->IsCharacterValid())
	{
		OutErrorMsg = LOCTEXT("MaterialsExportDisabled_InvalidCharacter", "Character is not valid.");
		return false;
	}

	if (!Character->HasHighResolutionTextures())
	{
		OutErrorMsg = LOCTEXT("MaterialsExportDisabled_NoHighResTextures", "The Character is missing textures. Use Download Texture Sources to create them before exporting.");
		return false;
	}

	const UMetaHumanCharacterEditorMaterialsExportProperties* MaterialsExportProperties = CastChecked<UMetaHumanCharacterEditorMaterialsExportProperties>(GetExportProperties());

	if (MaterialsExportProperties->ProjectPath.Path.IsEmpty())
	{
		OutErrorMsg = LOCTEXT("MaterialsExportDisabled_NoPath", "Please specify a project path for the materials export.");
		return false;
	}

	return true;
}

void UMetaHumanCharacterEditorMaterialsExportTool::Export() const
{
	UMetaHumanCharacter* Character = UE::ToolTarget::GetTargetMetaHumanCharacter(Target);
	const UMetaHumanCharacterEditorMaterialsExportProperties* MaterialsExportProperties = Cast<UMetaHumanCharacterEditorMaterialsExportProperties>(GetExportProperties());

	// These are validated by CanExport() which gates the Export button
	check(IsValid(Character) && Character->IsCharacterValid());
	check(MaterialsExportProperties);

	FMetaHumanMaterialsExportParams Params;
	Params.ProjectPath = MaterialsExportProperties->ProjectPath.Path;
	Params.bApplyAsOverrides = MaterialsExportProperties->bMaterialsApplyAsOverrides;

	UMetaHumanCharacterExportBlueprintLibrary::ExportMaterials(Character, Params);
}

#undef LOCTEXT_NAMESPACE
