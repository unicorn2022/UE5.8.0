// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MetaHumanCharacterTargetKeyPoints.h"

#include "MetaHumanCharacterExportBlueprintLibrary.generated.h"

class UMetaHumanCharacter;

/** Parameters for DCC export. */
USTRUCT(BlueprintType)
struct FMetaHumanDCCExportParams
{
	GENERATED_BODY()

	/** Output folder on disk. Must not be empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	FString ExternalPath;

	/** Whether to bake makeup into the face textures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	bool bBakeMakeUp = false;

	/** Whether to compress the output in a ZIP archive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	bool bCompressInZipFile = false;

	/** Archive name. If empty, the character name is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	FString ArchiveName;
};

/** Parameters for DNA export. */
USTRUCT(BlueprintType)
struct FMetaHumanDNAExportParams
{
	GENERATED_BODY()

	/**
	 * Project content path for the exported DNA assets (e.g. "/Game/MetaHumans").
	 *
	 * Optional: if left empty, no project (in-editor) assets are created and only the
	 * external file export is performed. At least one of ProjectPath or ExternalPath
	 * must be set to a valid value; both may be set to export in parallel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	FString ProjectPath;

	/**
	 * Output folder on disk for the exported .dna files.
	 *
	 * Optional: if left empty, no files are written to disk and only the project-path
	 * asset export is performed. At least one of ProjectPath or ExternalPath must be
	 * set to a valid value; both may be set to export in parallel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	FString ExternalPath;

	/** Export head DNA. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	bool bDNAHead = true;

	/** Export body DNA. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	bool bDNABody = true;

	/** Overwrites onto the existing DNA assets. Only applies to project path export. If false, creates new unique asset names. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	bool bOverwriteExistingAssets = true;
};

/** Parameters for geometry export. */
USTRUCT(BlueprintType)
struct FMetaHumanGeometryExportParams
{
	GENERATED_BODY()

	/** Project content path for the exported skeletal mesh assets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	FString ProjectPath = TEXT("/Game/MetaHumans");

	/** Export head skeletal mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	bool bHeadSkeletalMesh = true;

	/** Export body skeletal mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	bool bBodySkeletalMesh = true;

	/** Export combined full-body skeletal mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	bool bFullBodySkeletalMesh = false;

	/** Overwrites onto the existing assets, if false will create new unique asset names for exported assets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	bool bOverwriteExistingAssets = true;
};

/** Parameters for posed-DNA export.
 *
 *  Posed DNA is the combined head+body geometry produced by a previous ConformToTargetMeshes
 *  call, baked into a single DNA file with the body rig applied in posed-joints mode. It's
 *  the artifact downstream tools (e.g. material baking) consume after a conform.
 */
USTRUCT(BlueprintType)
struct FMetaHumanPosedDNAExportParams
{
	GENERATED_BODY()

	/**
	 * Project content folder for the exported posed DNA asset (e.g. "/Game/MetaHumans").
	 * The asset name within this folder is taken from AssetName, falling back to
	 * "<CharacterName>_Posed".
	 *
	 * Optional: if left empty, no project (in-editor) asset is created and only the external
	 * file export is performed. At least one of ProjectPath or ExternalPath must be set to a
	 * valid value; both may be set to export in parallel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	FString ProjectPath;

	/**
	 * Output folder on disk for the exported .dna file. The filename within this folder is
	 * taken from AssetName (with .dna appended), falling back to "<CharacterName>_Posed.dna".
	 *
	 * Optional: if left empty, no file is written to disk and only the project-path asset
	 * export is performed. At least one of ProjectPath or ExternalPath must be set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	FString ExternalPath;

	/**
	 * Identifies the target mesh slot under which the conformed body posed state was stored
	 * during ConformToTargetMeshes / ConformTargetMeshesAsync. Must match the key used at
	 * conform time — otherwise no posed state is found and the export fails.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	FMetaHumanCharacterTargetMeshKey TargetMeshKey;

	/**
	 * Optional asset/file name (without extension) for the exported posed DNA. Used as both
	 * the UDNA asset name under ProjectPath and the .dna filename stem under ExternalPath.
	 * If left empty, defaults to "<CharacterName>_Posed".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	FString AssetName;

	/** Overwrites onto the existing DNA asset at ProjectPath. If false, a unique name is generated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	bool bOverwriteExistingAssets = true;
};

/** Parameters for materials export. */
USTRUCT(BlueprintType)
struct FMetaHumanMaterialsExportParams
{
	GENERATED_BODY()

	/** Project content path for the exported material assets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	FString ProjectPath = TEXT("/Game/MetaHumans");

	/** If true, apply exported materials as overrides on the MetaHumanCharacter asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Export")
	bool bApplyAsOverrides = true;
};

/**
 * Blueprint/Python function library for MetaHuman Character export operations.
 *
 * Provides programmatic access to the same export functionality available through
 * the interactive export tools in the MetaHuman Character Editor UI.
 *
 * Python usage:
 *   import unreal
 *   character = unreal.load_asset("/Game/MetaHumans/MyCharacter")
 *   params = unreal.MetaHumanDCCExportParams()
 *   params.external_path = "D:/Export/MyCharacter"
 *   unreal.MetaHumanCharacterExportBlueprintLibrary.export_dcc(character, params)
 */
UCLASS()
class METAHUMANCHARACTEREDITOR_API UMetaHumanCharacterExportBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Export a MetaHuman character as a DCC package to an external folder on disk.
	 *
	 * Generate an archive/directory containing the MetaHuman assets for consumption in DCC tools.
	 * Character must be valid and rigged.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Export")
	static void ExportDCC(UMetaHumanCharacter* InCharacter, const FMetaHumanDCCExportParams& InParams);

	/**
	 * Export MetaHuman DNA assets (head and/or body) to project content.
	 *
	 * Creates standalone UDNA assets.
	 * Character must be valid. Head DNA export requires face DNA.
	 * At least one of bDNAHead/bDNABody must be true.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Export")
	static void ExportDNA(UMetaHumanCharacter* InCharacter, const FMetaHumanDNAExportParams& InParams);

	/**
	 * Export MetaHuman geometry (skeletal meshes) to project content.
	 *
	 * Creates standalone skeletal mesh assets with persistent topology materials.
	 * Character must be valid and open for editing. At least one mesh option in params must be true.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Export")
	static void ExportGeometry(UMetaHumanCharacter* InCharacter, const FMetaHumanGeometryExportParams& InParams);

	/**
	 * Export MetaHuman materials as persistent MIC assets to project content.
	 *
	 * Generates character assets, persists textures, creates MICs for all face/body material slots,
	 * and optionally applies the exported materials as overrides on the character asset.
	 * Character must be valid and have high-resolution textures.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Export")
	static void ExportMaterials(UMetaHumanCharacter* InCharacter, const FMetaHumanMaterialsExportParams& InParams);

	/**
	 * Export the combined posed DNA produced by a previous ConformToTargetMeshes call.
	 *
	 * Reads the body posed state stored on the character under InParams.TargetMeshKey, converts
	 * it to a combined head+body DNA via FState::StateToDna(BaseDNA, bIsCombine=true,
	 * bUsePosedJoints=true), and writes it as a UDNA asset to ProjectPath and/or as a .dna file
	 * to ExternalPath. This is the headless equivalent of the editor's "Save Pose" button under
	 * the Mesh Import tool's manual solve actions.
	 *
	 * Character must be valid and ConformToTargetMeshes must have run with a matching TargetMeshKey.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Export")
	static void ExportPosedDNA(UMetaHumanCharacter* InCharacter, const FMetaHumanPosedDNAExportParams& InParams);
};
