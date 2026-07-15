// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterExportBlueprintLibrary.h"

#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "DNA.h"
#include "DNAUtils.h"
#include "IContentBrowserSingleton.h"
#include "SkelMeshDNAUtils.h"
#include "SkinnedAssetCompiler.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformFileManager.h"
#include "Logging/MessageLog.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopedSlowTask.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterAnalytics.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterGeneratedAssets.h"
#include "MetaHumanCharacterPaletteUnpackHelpers.h"
#include "MetaHumanCharacterSkelMeshUtils.h"
#include "MetaHumanCommonDataUtils.h"
#include "MetaHumanCharacterSkelMeshUtils.h"
#include "MetaHumanSDKEditor.h"
#include "DCC/MetaHumanCharacterDCCExport.h"
#include "Subsystem/MetaHumanCharacterSkinMaterials.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterExportBlueprintLibrary"

// DCC Export

void UMetaHumanCharacterExportBlueprintLibrary::ExportDCC(UMetaHumanCharacter* InCharacter, const FMetaHumanDCCExportParams& InParams)
{
	if (!IsValid(InCharacter))
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExport_InvalidCharacter", "Character is not valid."));
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}

	const UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	check(Subsystem);

	bool bHasErrors = false;

	if (InParams.ExternalPath.IsEmpty() || !FPaths::ValidatePath(InParams.ExternalPath))
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExport_InvalidPath", "Please specify a valid external path for the DCC export."));
		bHasErrors = true;
	}

	if (Subsystem->GetRiggingState(InCharacter) != EMetaHumanCharacterRigState::Rigged)
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExport_NotRigged", "Character is not rigged. Create a rig before exporting."));
		bHasErrors = true;
	}

	if (!InCharacter->HasHighResolutionTextures())
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExport_NoHighResTextures", "The Character is missing textures. Use Download Texture Sources to create them before exporting."));
		bHasErrors = true;
	}

	if (bHasErrors)
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}

	FMetaHumanCharacterEditorDCCExportParameters ExportParams;
	ExportParams.OutputFolderPath = InParams.ExternalPath;
	ExportParams.bBakeFaceMakeup = InParams.bBakeMakeUp;
	ExportParams.bExportZipFile = InParams.bCompressInZipFile;
	ExportParams.ArchiveName = InParams.ArchiveName;

	FMetaHumanCharacterEditorDCCExport::ExportCharacterForDCC(InCharacter, ExportParams);
}

namespace UE::MetaHuman::ExportUtilInternal
{

/**
 * Resolves the package and asset name for export, ensuring no class mismatch with existing assets.
 * When overwriting is disabled, always generates a unique name.
 * When overwriting is enabled, checks if an existing asset at the target path is a different class
 * and falls back to a unique name if so, since overwriting with a mismatched class causes a fatal crash.
 */
static void ResolveExportAssetName(const FString& InPackagePath, FString& OutPackageName, FString& OutAssetName, const UClass* InExpectedClass, bool bInOverwriteExisting)
{
	OutPackageName = InPackagePath;

	if (!bInOverwriteExisting)
	{
		const FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(InPackagePath, TEXT(""), OutPackageName, OutAssetName);
		return;
	}

	const FSoftObjectPath AssetPath(OutPackageName + TEXT(".") + OutAssetName);
	const FAssetData ExistingAsset = IAssetRegistry::Get()->GetAssetByObjectPath(AssetPath);
	if (ExistingAsset.IsValid() && !ExistingAsset.GetClass()->IsChildOf(InExpectedClass))
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Warning(FText::Format(
			LOCTEXT("Export_ClassMismatch", "An asset of a different type already exists at '{0}'. Creating with a unique name."),
			FText::FromString(OutPackageName)));
		const FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(InPackagePath, TEXT(""), OutPackageName, OutAssetName);
	}
}

} // namespace UE::MetaHuman::ExportUtilInternal
// DNA Export

namespace UE::MetaHuman::DNAExportInternal
{

static UDNA* CreateDNAAsset(const FString& InPackagePath, const FString& InAssetName, const TSharedPtr<IDNAReader>& InDNAReader, const bool bInOverwriteExisting)
{
	FString AssetName = InAssetName;
	FString PackageName = InPackagePath;
	ExportUtilInternal::ResolveExportAssetName(InPackagePath, PackageName, AssetName, UDNA::StaticClass(), bInOverwriteExisting);

	UPackage* NewPackage = CreatePackage(*PackageName);
	UDNA* NewDNA = NewObject<UDNA>(NewPackage, FName(*AssetName));
	NewDNA->SetDNAReader(InDNAReader, EDNACopyPolicy::Alias, ERigLogicInitPolicy::Defer);
	NewDNA->RestoreLegacyUEMHCCompatibility();
	NewDNA->SetFlags(RF_Public | RF_Transactional | RF_Standalone);

	FAssetRegistryModule::AssetCreated(NewDNA);
	NewDNA->MarkPackageDirty();

	return NewDNA;
}

static bool WriteDNAFile(const FString& InDirectoryPath, const FString& InFileName, const IDNAReader* InDNAReader)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.CreateDirectoryTree(*InDirectoryPath))
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(FText::Format(LOCTEXT("DNAExport_CreateDirectoryFailed", "Failed to create directory '{0}' for DNA export."), FText::FromString(InDirectoryPath)));
		return false;
	}
	WriteDNAToFile(InDNAReader, EDNADataLayer::All, FPaths::Combine(InDirectoryPath, InFileName));
	return true;
}

} // namespace UE::MetaHuman::DNAExportInternal

void UMetaHumanCharacterExportBlueprintLibrary::ExportDNA(UMetaHumanCharacter* InCharacter, const FMetaHumanDNAExportParams& InParams)
{
	if (!IsValid(InCharacter))
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DNAExport_InvalidCharacter", "Character is not valid."));
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}

	const bool bValidProjectPath = FPackageName::IsValidLongPackageName(InParams.ProjectPath);
	const bool bValidExternalPath = !InParams.ExternalPath.IsEmpty() && FPaths::ValidatePath(InParams.ExternalPath);

	if (!bValidProjectPath && !bValidExternalPath)
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DNAExport_InvalidPaths", "Please specify a valid external path and/or project path for DNA export."));
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}

	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	check(Subsystem);

	const FString CharacterName = InCharacter->GetName();

	bool bHasErrors = false;
	TArray<UObject*> AssetToSyncContentBrowser;

	// Export Head DNA
	if (InParams.bDNAHead)
	{
		if (!InCharacter->HasFaceDNA())
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DNAExport_NoFaceDNA", "Character does not have face DNA. Create a rig before exporting head DNA."));
			bHasErrors = true;
		}
		else
		{
			TArray<uint8> FaceDNABuffer = InCharacter->GetFaceDNABuffer();
			if (TSharedPtr<IDNAReader> FaceDNAReader = ReadDNAFromBuffer(&FaceDNABuffer))
			{
				const FString HeadAssetName = FString::Printf(TEXT("%s_Head"), *CharacterName);
				if (bValidProjectPath)
				{
					const FString HeadPackagePath = InParams.ProjectPath / HeadAssetName;
					UDNA* NewHeadDNAAsset = UE::MetaHuman::DNAExportInternal::CreateDNAAsset(HeadPackagePath, HeadAssetName, FaceDNAReader, InParams.bOverwriteExistingAssets);
					if (IsValid(NewHeadDNAAsset))
					{
						AssetToSyncContentBrowser.Add(NewHeadDNAAsset);
					}
					else
					{
						FMessageLog(UE::MetaHuman::MessageLogName).Error(FText::Format(LOCTEXT("DNAExportFailure_HeadAssetCreation", "Failed to create head DNA asset at '{0}'."), FText::FromString(HeadPackagePath)));
						bHasErrors = true;
					}
				}
				if (bValidExternalPath)
				{
					bHasErrors |= !UE::MetaHuman::DNAExportInternal::WriteDNAFile(InParams.ExternalPath, FString::Printf(TEXT("%s_Head.dna"), *CharacterName), FaceDNAReader.Get());
				}
				UE::MetaHuman::Analytics::RecordSaveFaceDNAEvent(InCharacter);
			}
			else
			{
				FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DNAExportFailure_Head", "Failed to read face DNA buffer."));
				bHasErrors = true;
			}
		}
	}

	// Export Body DNA
	if (InParams.bDNABody)
	{
		const USkeletalMesh* ConstBodySkeletalMesh = Subsystem->GetBodyEditMesh(InCharacter);
		USkeletalMesh* BodySkeletalMesh = const_cast<USkeletalMesh*>(ConstBodySkeletalMesh);
		if (TSharedPtr<IDNAReader> DNAReader = USkelMeshDNAUtils::GetDNAReader(BodySkeletalMesh))
		{
			TSharedRef<IDNAReader> BodyDnaReader = Subsystem->GetBodyState(InCharacter)->StateToDna(DNAReader->Unwrap());
			const FString BodyAssetName = FString::Printf(TEXT("%s_Body"), *CharacterName);
			if (bValidProjectPath)
			{
				const FString BodyPackagePath = InParams.ProjectPath / BodyAssetName;
				UDNA* NewBodyDNAAsset = UE::MetaHuman::DNAExportInternal::CreateDNAAsset(BodyPackagePath, BodyAssetName, BodyDnaReader, InParams.bOverwriteExistingAssets);
				if (IsValid(NewBodyDNAAsset))
				{
					AssetToSyncContentBrowser.Add(NewBodyDNAAsset);
				}
				else
				{
					FMessageLog(UE::MetaHuman::MessageLogName).Error(FText::Format(LOCTEXT("DNAExportFailure_BodyAssetCreation", "Failed to create body DNA asset at '{0}'."), FText::FromString(BodyPackagePath)));
					bHasErrors = true;
				}
			}
			if (bValidExternalPath)
			{
				bHasErrors |= !UE::MetaHuman::DNAExportInternal::WriteDNAFile(InParams.ExternalPath, FString::Printf(TEXT("%s_Body.dna"), *CharacterName), &BodyDnaReader.Get());
			}

			UE::MetaHuman::Analytics::RecordSaveBodyDNAEvent(InCharacter);
		}
		else
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DNAExportFailure_Body", "Failed to read body DNA from skeletal mesh."));
			bHasErrors = true;
		}
	}

	if (bHasErrors)
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
	}
	else
	{
		FNotificationInfo Info(LOCTEXT("DNAExportSuccess", "DNA export completed successfully."));
		Info.ExpireDuration = 5.0f;
		Info.bFireAndForget = true;
		Info.bUseSuccessFailIcons = true;

		const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification)
		{
			Notification->SetCompletionState(SNotificationItem::CS_Success);
		}

		if (bValidProjectPath)
		{
			IAssetTools::Get().SyncBrowserToAssets(AssetToSyncContentBrowser);
		}
	}
}

// Geometry Export

namespace UE::MetaHuman::GeometryExportInternal
{

static USkeletalMesh* DuplicateSkeletalMeshToPackage(const USkeletalMesh* InSourceMesh, const FString& InPackagePath, const FString& InAssetName, const bool bInOverwriteExisting)
{
	if (!IsValid(InSourceMesh))
	{
		return nullptr;
	}

	FString AssetName = InAssetName;
	FString PackageName = InPackagePath;
	ExportUtilInternal::ResolveExportAssetName(InPackagePath, PackageName, AssetName, USkeletalMesh::StaticClass(), bInOverwriteExisting);

	UPackage* NewPackage = CreatePackage(*PackageName);
	USkeletalMesh* NewMesh = DuplicateObject(InSourceMesh, NewPackage, FName(*AssetName));

	if (!IsValid(NewMesh))
	{
		return nullptr;
	}

	if (UDNA* DNAAsset = USkelMeshDNAUtils::GetMeshDNAAsset(NewMesh))
	{
		DNAAsset->RestoreLegacyUEMHCCompatibility();
	}

	NewMesh->SetFlags(RF_Public | RF_Transactional | RF_Standalone);

	FAssetRegistryModule::AssetCreated(NewMesh);
	NewMesh->MarkPackageDirty();

	return NewMesh;
}

static void ApplyTopologyMaterials(USkeletalMesh* InMesh)
{
	UMaterialInterface* HeadMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Script/Engine.Material'/" UE_PLUGIN_NAME "/Materials/M_GrayTexture_Head.M_GrayTexture_Head'"));
	UMaterialInterface* EyeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Script/Engine.Material'/" UE_PLUGIN_NAME "/Materials/M_GrayTexture_Eyes.M_GrayTexture_Eyes'"));
	UMaterialInterface* TeethMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Script/Engine.Material'/" UE_PLUGIN_NAME "/Materials/M_GrayTexture_Teeth.M_GrayTexture_Teeth'"));
	UMaterialInterface* BodyMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Script/Engine.Material'/" UE_PLUGIN_NAME "/Materials/M_GrayTexture_Body.M_GrayTexture_Body'"));
	UMaterialInterface* HideMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Script/Engine.Material'/" UE_PLUGIN_NAME "/Materials/M_Hide.M_Hide'"));

	// Build a map of known slot names to their topology materials
	TMap<FName, UMaterialInterface*> SlotMaterialMap;
	SlotMaterialMap.Reserve(static_cast<int32>(EMetaHumanCharacterSkinMaterialSlot::Count) + 4);

	for (const EMetaHumanCharacterSkinMaterialSlot SkinSlot : TEnumRange<EMetaHumanCharacterSkinMaterialSlot>())
	{
		SlotMaterialMap.Add(FMetaHumanCharacterSkinMaterials::GetSkinMaterialSlotName(SkinSlot), HeadMaterial);
	}

	SlotMaterialMap.Add(FMetaHumanCharacterSkinMaterials::EyeLeftSlotName, EyeMaterial);
	SlotMaterialMap.Add(FMetaHumanCharacterSkinMaterials::EyeRightSlotName, EyeMaterial);
	SlotMaterialMap.Add(FMetaHumanCharacterSkinMaterials::TeethSlotName, TeethMaterial);
	SlotMaterialMap.Add(FMetaHumanCharacterSkinMaterials::BodySlotName, BodyMaterial);

	// Assign materials by exact slot name match, falling back to HideMaterial for unrecognized slots
	TArray<FSkeletalMaterial> MaterialSlots = InMesh->GetMaterials();

	for (FSkeletalMaterial& Material : MaterialSlots)
	{
		if (UMaterialInterface* const* FoundMaterial = SlotMaterialMap.Find(Material.MaterialSlotName))
		{
			Material.MaterialInterface = *FoundMaterial;
		}
		else
		{
			Material.MaterialInterface = HideMaterial;
		}
	}

	InMesh->SetMaterials(MaterialSlots);
}

static void ApplyClayMaterial(USkeletalMesh* InMesh)
{
	UMaterialInterface* ClayMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Script/Engine.Material'/" UE_PLUGIN_NAME "/Materials/M_FullBody_Clay.M_FullBody_Clay'"));
	check(ClayMaterial);

	TArray<FSkeletalMaterial> MaterialSlots = InMesh->GetMaterials();

	for (FSkeletalMaterial& Material : MaterialSlots)
	{
		Material.MaterialInterface = ClayMaterial;
	}

	InMesh->SetMaterials(MaterialSlots);
}
} // namespace UE::MetaHuman::GeometryExportInternal

void UMetaHumanCharacterExportBlueprintLibrary::ExportGeometry(UMetaHumanCharacter* InCharacter, const FMetaHumanGeometryExportParams& InParams)
{
	if (!IsValid(InCharacter))
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("GeometryExport_InvalidCharacter", "Character is not valid."));
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}

	if (!FPackageName::IsValidLongPackageName(InParams.ProjectPath))
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("GeometryExport_InvalidPath", "Please specify a valid project path for the geometry export."));
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}

	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	check(Subsystem);

	const TSharedRef<FMetaHumanCharacterEditorData>* CharacterDataPtr = Subsystem->GetMetaHumanCharacterEditorData(InCharacter);
	if (!CharacterDataPtr)
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("GeometryExport_NoEditorData", "Character is not open for editing. Use TryAddObjectToEdit before exporting geometry."));
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}

	const TSharedRef<FMetaHumanCharacterEditorData>& CharacterData = *CharacterDataPtr;
	const FString CharacterName = InCharacter->GetName();

	bool bHasErrors = false;
	TArray<UObject*> AssetToSyncContentBrowser;
	TArray<USkinnedAsset*> ExportedMeshesToFinalize;

	// Export Head Skeletal Mesh
	if (InParams.bHeadSkeletalMesh)
	{
		const FString HeadAssetName = FString::Printf(TEXT("%s_Head"), *CharacterName);

		USkeletalMesh* ExportedHeadMesh = UE::MetaHuman::GeometryExportInternal::DuplicateSkeletalMeshToPackage(
			CharacterData->FaceMesh, InParams.ProjectPath / HeadAssetName, HeadAssetName, InParams.bOverwriteExistingAssets);

		if (!IsValid(ExportedHeadMesh))
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("GeometryExportFailure_Head", "Failed to export head skeletal mesh."));
			bHasErrors = true;
		}
		else
		{
			UE::MetaHuman::GeometryExportInternal::ApplyTopologyMaterials(ExportedHeadMesh);
			AssetToSyncContentBrowser.Add(ExportedHeadMesh);
			ExportedMeshesToFinalize.Add(ExportedHeadMesh);
		}
	}

	// Export Body Skeletal Mesh
	if (InParams.bBodySkeletalMesh)
	{
		const FString BodyAssetName = FString::Printf(TEXT("%s_Body"), *CharacterName);

		USkeletalMesh* ExportedBodyMesh = UE::MetaHuman::GeometryExportInternal::DuplicateSkeletalMeshToPackage(
			CharacterData->BodyMesh, InParams.ProjectPath / BodyAssetName, BodyAssetName, InParams.bOverwriteExistingAssets);

		if (!IsValid(ExportedBodyMesh))
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("GeometryExportFailure_Body", "Failed to export body skeletal mesh."));
			bHasErrors = true;
		}
		else
		{
			UE::MetaHuman::GeometryExportInternal::ApplyTopologyMaterials(ExportedBodyMesh);
			AssetToSyncContentBrowser.Add(ExportedBodyMesh);
			ExportedMeshesToFinalize.Add(ExportedBodyMesh);
		}
	}

	// Export Full Body Skeletal Mesh
	if (InParams.bFullBodySkeletalMesh)
	{
		const FString FullBodyAssetName = FString::Printf(TEXT("%s_FullBody"), *CharacterName);

		USkeletalMesh* FullBodyMesh = Subsystem->CreateCombinedFaceAndBodyMesh(InCharacter, InParams.ProjectPath / FullBodyAssetName, InParams.bOverwriteExistingAssets);
		if (!IsValid(FullBodyMesh))
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("GeometryExportFailure_FullBody", "Failed to export full body skeletal mesh."));
			bHasErrors = true;
		}
		else
		{
			UE::MetaHuman::GeometryExportInternal::ApplyClayMaterial(FullBodyMesh);
			AssetToSyncContentBrowser.Add(FullBodyMesh);
			ExportedMeshesToFinalize.Add(FullBodyMesh);
		}
	}

	// PostEditChange after all mutations (material overrides etc.) so the async skel mesh build
	// uses the final state. Then bulk-wait so per-mesh builds can overlap.
	if (!ExportedMeshesToFinalize.IsEmpty())
	{
		for (USkinnedAsset* Mesh : ExportedMeshesToFinalize)
		{
			Mesh->PostEditChange();
		}

		FScopedSlowTask SlowTask(0.0f, LOCTEXT("GeometryExport_Finalizing", "Finalizing exported meshes..."));
		SlowTask.MakeDialog();
		FSkinnedAssetCompilingManager::Get().FinishCompilation(ExportedMeshesToFinalize);
	}

	if (bHasErrors)
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
	}
	else
	{
		FNotificationInfo Info(LOCTEXT("GeometryExportSuccess", "Geometry export completed successfully."));
		Info.ExpireDuration = 5.0f;
		Info.bFireAndForget = true;
		Info.bUseSuccessFailIcons = true;

		const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification)
		{
			Notification->SetCompletionState(SNotificationItem::CS_Success);
		}

		IAssetTools::Get().SyncBrowserToAssets(AssetToSyncContentBrowser);
	}
}

// Materials Export

namespace UE::MetaHuman::MaterialsExportInternal
{

struct FExportedMaterials
{
	TSortedMap<EMetaHumanCharacterSkinMaterialSlot, UMaterialInstanceConstant*> Skin;
	TMap<EMetaHumanCharacterTeethAndEyesSlot, UMaterialInstanceConstant*> TeethAndEyes;
	UMaterialInstanceConstant* Body = nullptr;
};

static UMaterialInstanceConstant* CreateMaterialInPackage(const UMaterialInstance* InSourceMaterial, const FString& InPackagePath, const FString& InAssetName)
{
	if (!IsValid(InSourceMaterial) || !InSourceMaterial->Parent)
	{
		return nullptr;
	}

	UPackage* NewPackage = CreatePackage(*InPackagePath);
	UMaterialInstanceConstant* NewMaterial = NewObject<UMaterialInstanceConstant>(NewPackage, FName(*InAssetName));
	NewMaterial->SetParentEditorOnly(InSourceMaterial->Parent);

	UE::MetaHuman::PaletteUnpackHelpers::CopyMaterialParametersIfNeeded(EMaterialParameterType::Scalar, InSourceMaterial, NewMaterial);
	UE::MetaHuman::PaletteUnpackHelpers::CopyMaterialParametersIfNeeded(EMaterialParameterType::Vector, InSourceMaterial, NewMaterial);
	UE::MetaHuman::PaletteUnpackHelpers::CopyMaterialParametersIfNeeded(EMaterialParameterType::Texture, InSourceMaterial, NewMaterial);
	UE::MetaHuman::PaletteUnpackHelpers::CopyMaterialParametersIfNeeded(EMaterialParameterType::StaticSwitch, InSourceMaterial, NewMaterial);

	NewMaterial->SetFlags(RF_Public | RF_Transactional | RF_Standalone);

	FAssetRegistryModule::AssetCreated(NewMaterial);
	NewMaterial->MarkPackageDirty();

	return NewMaterial;
}

template<typename TextureEnumType>
static bool PersistTextures(
	const TMap<TextureEnumType, TObjectPtr<UTexture2D>>& InSourceTextures,
	const FString& InExportPath,
	TMap<UTexture2D*, UTexture2D*>& InOutTextureRemap)
{
	bool bHasErrors = false;

	for (const TPair<TextureEnumType, TObjectPtr<UTexture2D>>& TexturePair : InSourceTextures)
	{
		if (!IsValid(TexturePair.Value))
		{
			continue;
		}

		const FString TextureName = TexturePair.Value->GetFName().GetPlainNameString();
		const FString PackagePath = InExportPath / TEXT("Textures") / TextureName;
		UPackage* NewPackage = CreatePackage(*PackagePath);
		UTexture2D* PersistentTexture = DuplicateObject(TexturePair.Value.Get(), NewPackage, FName(*TextureName));

		if (IsValid(PersistentTexture))
		{
			PersistentTexture->SetFlags(RF_Public | RF_Transactional | RF_Standalone);
			FAssetRegistryModule::AssetCreated(PersistentTexture);
			PersistentTexture->MarkPackageDirty();

			InOutTextureRemap.Add(TexturePair.Value.Get(), PersistentTexture);
		}
		else
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(
				FText::Format(LOCTEXT("MaterialsExportFailure_Texture", "Failed to export texture: {0}"),
					FText::FromString(TextureName)));
			bHasErrors = true;
		}
	}

	return !bHasErrors;
}

static void RemapMaterialTextures(UMaterialInstance* InMaterial, const TMap<UTexture2D*, UTexture2D*>& InTextureRemap)
{
	if (!IsValid(InMaterial))
	{
		return;
	}

	for (FTextureParameterValue& TextureParam : InMaterial->TextureParameterValues)
	{
		if (UTexture2D* const* PersistentTexture = InTextureRemap.Find(Cast<UTexture2D>(TextureParam.ParameterValue)))
		{
			TextureParam.ParameterValue = *PersistentTexture;
		}
	}
}

static bool ExportAllMaterials(
	const FMetaHumanCharacterGeneratedAssets& InGeneratedAssets,
	const FString& InExportPath,
	const TMap<UTexture2D*, UTexture2D*>& InTextureRemap,
	FExportedMaterials& OutExportedMaterials)
{
	bool bHasErrors = false;

	FMetaHumanCharacterFaceMaterialSet FaceMaterials = FMetaHumanCharacterSkinMaterials::GetHeadMaterialsFromMesh(InGeneratedAssets.FaceMesh);

	auto ExportMaterial = [&](const UMaterialInstance* Material, const FString& AssetName, const FString& SubFolder) -> UMaterialInstanceConstant*
	{
		if (!IsValid(Material))
		{
			return nullptr;
		}

		const FString PackagePath = InExportPath / SubFolder / TEXT("Materials") / AssetName;
		UMaterialInstanceConstant* ExportedMaterial = CreateMaterialInPackage(Material, PackagePath, AssetName);
		if (!IsValid(ExportedMaterial))
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(
				FText::Format(LOCTEXT("MaterialsExportFailure_Material", "Failed to export material: {0}"),
					FText::FromString(AssetName)));
			return nullptr;
		}

		RemapMaterialTextures(ExportedMaterial, InTextureRemap);
		ExportedMaterial->PostEditChange();
		return ExportedMaterial;
	};

	// Export face skin materials (per LOD slot)
	FaceMaterials.ForEachSkinMaterial<UMaterialInstance>(
		[&](EMetaHumanCharacterSkinMaterialSlot Slot, UMaterialInstance* Material)
		{
			const FString SlotName = StaticEnum<EMetaHumanCharacterSkinMaterialSlot>()->GetAuthoredNameStringByValue(static_cast<int64>(Slot));
			const FString AssetName = FString::Printf(TEXT("MI_Face_Skin_%s"), *SlotName);
			if (UMaterialInstanceConstant* Exported = ExportMaterial(Material, AssetName, TEXT("Face")))
			{
				OutExportedMaterials.Skin.Add(Slot, Exported);
			}
			else if (IsValid(Material))
			{
				bHasErrors = true;
			}
		});

	// Export teeth, eyes, and eyelashes materials
	struct FTeethAndEyesEntry
	{
		EMetaHumanCharacterTeethAndEyesSlot Slot;
		UMaterialInstance* Material;
		const TCHAR* Name;
	};

	const TArray<FTeethAndEyesEntry> TeethAndEyesMaterials =
	{
		{ EMetaHumanCharacterTeethAndEyesSlot::EyeLeft,			FaceMaterials.EyeLeft,			TEXT("Eye_Left") },
		{ EMetaHumanCharacterTeethAndEyesSlot::EyeRight,		FaceMaterials.EyeRight,			TEXT("Eye_Right") },
		{ EMetaHumanCharacterTeethAndEyesSlot::EyeShell,		FaceMaterials.EyeShell,			TEXT("EyeShell") },
		{ EMetaHumanCharacterTeethAndEyesSlot::LacrimalFluid,	FaceMaterials.LacrimalFluid,	TEXT("LacrimalFluid") },
		{ EMetaHumanCharacterTeethAndEyesSlot::Teeth,			FaceMaterials.Teeth,			TEXT("Teeth") },
		{ EMetaHumanCharacterTeethAndEyesSlot::Eyelashes,		FaceMaterials.Eyelashes,		TEXT("Eyelashes") },
		{ EMetaHumanCharacterTeethAndEyesSlot::EyelashesHiLods,	FaceMaterials.EyelashesHiLods,	TEXT("EyelashesHiLODs") },
	};

	for (const FTeethAndEyesEntry& Entry : TeethAndEyesMaterials)
	{
		const FString AssetName = FString::Printf(TEXT("MI_Face_%s"), Entry.Name);
		if (UMaterialInstanceConstant* Exported = ExportMaterial(Entry.Material, AssetName, TEXT("Face")))
		{
			OutExportedMaterials.TeethAndEyes.Add(Entry.Slot, Exported);
		}
		else if (IsValid(Entry.Material))
		{
			bHasErrors = true;
		}
	}

	// Export body material
	UMaterialInstance* BodyMaterialInstance = Cast<UMaterialInstance>(InGeneratedAssets.BodyMesh->GetMaterials()[0].MaterialInterface);
	const FString AssetName = FString::Printf(TEXT("MI_Body_Skin"));
	if (UMaterialInstanceConstant* Exported = ExportMaterial(BodyMaterialInstance, AssetName, TEXT("Body")))
	{
		OutExportedMaterials.Body = Exported;
	}
	else if (IsValid(BodyMaterialInstance))
	{
		bHasErrors = true;
	}

	return !bHasErrors;
}

static void ApplyMaterialOverrides(UMetaHumanCharacter* InCharacter, const FExportedMaterials& InExportedMaterials)
{
	FMetaHumanCharacterMaterialOverrideSet& Overrides = InCharacter->SkinSettings.TextureMaterialOverrides.MaterialOverrides;
	InCharacter->SkinSettings.TextureMaterialOverrides.bEnableMaterialOverrides = true;

	for (const TPair<EMetaHumanCharacterSkinMaterialSlot, UMaterialInstanceConstant*>& SkinMaterial : InExportedMaterials.Skin)
	{
		Overrides.Skin.FindOrAdd(SkinMaterial.Key) = SkinMaterial.Value;
	}
	for (const TPair<EMetaHumanCharacterTeethAndEyesSlot, UMaterialInstanceConstant*>& TeethAndEyesMaterial : InExportedMaterials.TeethAndEyes)
	{
		Overrides.TeethAndEyes.FindOrAdd(TeethAndEyesMaterial.Key) = TeethAndEyesMaterial.Value;
	}
	if (IsValid(InExportedMaterials.Body))
	{
		Overrides.Body = InExportedMaterials.Body;
	}

	InCharacter->MarkPackageDirty();
}

static void ReparentSkinLODMaterials(const TSortedMap<EMetaHumanCharacterSkinMaterialSlot, UMaterialInstanceConstant*>& InExportedSkinMaterials)
{
	TArray<UMaterialInstanceConstant*> SkinMaterials;
	InExportedSkinMaterials.GenerateValueArray(SkinMaterials);

	for (int32 Index = 0; Index < SkinMaterials.Num() - 1; ++Index)
	{
		UMaterialInstanceConstant* NewParent = SkinMaterials[Index];
		UMaterialInstanceConstant* Material = Cast<UMaterialInstanceConstant>(SkinMaterials[Index + 1]);

		if (Material && NewParent)
		{
			FMetaHumanCharacterSkinMaterials::SetMaterialInstanceParent(Material, NewParent);
		}
	}
}

} // namespace UE::MetaHuman::MaterialsExportInternal

void UMetaHumanCharacterExportBlueprintLibrary::ExportMaterials(UMetaHumanCharacter* InCharacter, const FMetaHumanMaterialsExportParams& InParams)
{
	if (!IsValid(InCharacter))
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("MaterialsExport_InvalidCharacter", "Character is not valid."));
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}

	if (!FPackageName::IsValidLongPackageName(InParams.ProjectPath))
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("MaterialsExport_InvalidPath", "Please specify a valid project path for the materials export."));
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}

	if (!InCharacter->HasHighResolutionTextures())
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("MaterialsExport_NoHighResTextures", "The Character is missing textures. Use Download Texture Sources to create them before exporting."));
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}

	const FString CharacterName = InCharacter->GetName();
	const FString BaseExportDir = InParams.ProjectPath / FString::Printf(TEXT("%s_MaterialsExport"), *CharacterName);

	// Determine export directory — create a unique name
	FString ExportPath = BaseExportDir;
	if (IAssetRegistry::Get()->PathExists(BaseExportDir))
	{
		int32 Suffix = 1;
		do
		{
			ExportPath = FString::Printf(TEXT("%s_%d"), *BaseExportDir, Suffix++);
		}
		while (IAssetRegistry::Get()->PathExists(ExportPath));
	}

	FScopedSlowTask ExportTask(3, FText::Format(LOCTEXT("MaterialsExport_TaskMessage", "Exporting {0} Materials"), FText::FromString(CharacterName)));
	ExportTask.MakeDialog();

	// Step 0: Generate character assets with VT support based on MHC editor settings
	ExportTask.EnterProgressFrame(0, LOCTEXT("MaterialsExport_Step0", "Generating character assets..."));

	FMetaHumanCharacterGeneratedAssets GeneratedAssets;
	if (!UMetaHumanCharacterEditorSubsystem::Get()->TryGenerateCharacterAssets(InCharacter, GetTransientPackage(), GeneratedAssets))
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("MaterialsExportFailure_GenerateAssets", "Failed to generate character assets for materials export."));
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}

	// Step 1: Persist all textures and build transient -> persistent remap
	ExportTask.EnterProgressFrame(1, LOCTEXT("MaterialsExport_Step1", "Exporting textures..."));

	TMap<UTexture2D*, UTexture2D*> TextureRemap;
	bool bHasErrors = false;

	if (!UE::MetaHuman::MaterialsExportInternal::PersistTextures(GeneratedAssets.SynthesizedFaceTextures, ExportPath / TEXT("Face"), TextureRemap))
	{
		bHasErrors = true;
	}

	if (!UE::MetaHuman::MaterialsExportInternal::PersistTextures(GeneratedAssets.BodyTextures, ExportPath / TEXT("Body"), TextureRemap))
	{
		bHasErrors = true;
	}

	// Step 2: Export materials
	ExportTask.EnterProgressFrame(1, LOCTEXT("MaterialsExport_Step2", "Exporting materials..."));
	UE::MetaHuman::MaterialsExportInternal::FExportedMaterials ExportedMaterials;
	if (UE::MetaHuman::MaterialsExportInternal::ExportAllMaterials(GeneratedAssets, ExportPath, TextureRemap, ExportedMaterials))
	{
		UE::MetaHuman::MaterialsExportInternal::ReparentSkinLODMaterials(ExportedMaterials.Skin);
	}
	else
	{
		bHasErrors = true;
	}

	// Step 3: Apply as overrides if requested
	if (InParams.bApplyAsOverrides)
	{
		UE::MetaHuman::MaterialsExportInternal::ApplyMaterialOverrides(InCharacter, ExportedMaterials);
		UMetaHumanCharacterEditorSubsystem::Get()->ApplySkinSettings(InCharacter, InCharacter->SkinSettings);
		ExportTask.EnterProgressFrame(1, LOCTEXT("MaterialsExport_Step3", "Applying overrides..."));
	}
	else
	{
		ExportTask.EnterProgressFrame();
	}

	if (bHasErrors)
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
	}
	else
	{
		FNotificationInfo Info(LOCTEXT("MaterialsExportSuccess", "Materials export completed successfully."));
		Info.ExpireDuration = 5.0f;
		Info.bFireAndForget = true;
		Info.bUseSuccessFailIcons = true;

		const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification)
		{
			Notification->SetCompletionState(SNotificationItem::CS_Success);
		}
	}
}

// Posed DNA Export

void UMetaHumanCharacterExportBlueprintLibrary::ExportPosedDNA(UMetaHumanCharacter* InCharacter, const FMetaHumanPosedDNAExportParams& InParams)
{
	if (!IsValid(InCharacter))
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("PosedDNAExport_InvalidCharacter", "Character is not valid."));
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}

	const bool bValidProjectPath = FPackageName::IsValidLongPackageName(InParams.ProjectPath);
	const bool bValidExternalPath = !InParams.ExternalPath.IsEmpty() && FPaths::ValidatePath(InParams.ExternalPath);

	if (!bValidProjectPath && !bValidExternalPath)
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("PosedDNAExport_InvalidPaths", "Please specify a valid external path and/or project path for posed DNA export."));
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}

	// Read the body posed state stored on the character at conform time.
	FSharedBuffer PosedStateBuffer = InCharacter->GetBodyTargetPoseStateData(InParams.TargetMeshKey);
	if (PosedStateBuffer.GetSize() == 0)
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("PosedDNAExport_NoPosedState", "No posed state found on the character for the supplied TargetMeshKey. Run ConformToTargetMeshes first with a matching key before exporting."));
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}

	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	check(Subsystem);

	// Reconstruct the body state from the serialized posed buffer.
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> PosedBodyState = Subsystem->CopyBodyState(InCharacter);
	if (!PosedBodyState->Deserialize(PosedStateBuffer))
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("PosedDNAExport_DeserializeFailed", "Failed to load posed state data from the character."));
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}
	PosedBodyState->SetEvaluatePose(true);

	// Resolve a base DNA to layer the posed body onto: the character's own body DNA if present,
	// otherwise the combined archetype DNA.
	TSharedPtr<IDNAReader> BaseDNA;
	if (InCharacter->HasBodyDNA())
	{
		TArray<uint8> BodyDNABuffer = InCharacter->GetBodyDNABuffer();
		BaseDNA = LoadDNAFromBuffer(&BodyDNABuffer, FDNAConfig());
	}
	if (!BaseDNA.IsValid())
	{
		if (UDNA* ArchetypeDNA = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAsset(EMetaHumanImportDNAType::Combined, GetTransientPackage()))
		{
			BaseDNA = ArchetypeDNA->GetDNAReader();
		}
	}
	if (!BaseDNA.IsValid())
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("PosedDNAExport_NoBaseDNA", "Failed to load base DNA template for posed DNA export."));
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}

	// Bake the posed body shape over the base DNA. bIsCombine=true produces a combined head+body
	// DNA; bUsePosedJoints=true uses the conformed joint poses rather than the neutral bind pose.
	const TSharedPtr<IDNAReader> PosedDNA = PosedBodyState->StateToDna(BaseDNA->Unwrap(), /*bIsCombine=*/true, /*bUsePosedJoints=*/true);
	if (!PosedDNA.IsValid())
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("PosedDNAExport_StateToDnaFailed", "Failed to convert posed state to DNA."));
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
		return;
	}

	const FString CharacterName = InCharacter->GetName();
	const FString PosedAssetName = InParams.AssetName.IsEmpty()
		? FString::Printf(TEXT("%s_Posed"), *CharacterName)
		: InParams.AssetName;

	bool bHasErrors = false;
	TArray<UObject*> AssetToSyncContentBrowser;

	if (bValidProjectPath)
	{
		const FString PosedPackagePath = InParams.ProjectPath / PosedAssetName;
		UDNA* NewPosedDNAAsset = UE::MetaHuman::DNAExportInternal::CreateDNAAsset(PosedPackagePath, PosedAssetName, PosedDNA, InParams.bOverwriteExistingAssets);
		if (IsValid(NewPosedDNAAsset))
		{
			AssetToSyncContentBrowser.Add(NewPosedDNAAsset);
		}
		else
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(FText::Format(LOCTEXT("PosedDNAExport_AssetCreationFailed", "Failed to create posed DNA asset at '{0}'."), FText::FromString(PosedPackagePath)));
			bHasErrors = true;
		}
	}

	if (bValidExternalPath)
	{
		bHasErrors |= !UE::MetaHuman::DNAExportInternal::WriteDNAFile(InParams.ExternalPath, FString::Printf(TEXT("%s.dna"), *PosedAssetName), PosedDNA.Get());
	}

	if (bHasErrors)
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Open(EMessageSeverity::Error, /*bForce=*/ false);
	}
	else
	{
		FNotificationInfo Info(LOCTEXT("PosedDNAExportSuccess", "Posed DNA export completed successfully."));
		Info.ExpireDuration = 5.0f;
		Info.bFireAndForget = true;
		Info.bUseSuccessFailIcons = true;

		const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification)
		{
			Notification->SetCompletionState(SNotificationItem::CS_Success);
		}

		if (bValidProjectPath)
		{
			IAssetTools::Get().SyncBrowserToAssets(AssetToSyncContentBrowser);
		}
	}
}

#undef LOCTEXT_NAMESPACE
