// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCharacterEditorExportToolBase.h"

#include "MetaHumanCharacterEditorExportTools.generated.h"

// DCC Export

UCLASS()
class UMetaHumanCharacterEditorDCCExportProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "DCC Export Options")
	FDirectoryPath ExternalPath;

	UPROPERTY(EditAnywhere, Category = "DCC Export Options")
	bool bBakeMakeUp = false;

	UPROPERTY(EditAnywhere, Category = "DCC Export Options")
	bool bCompressInZipFile = false;

	UPROPERTY(EditAnywhere, Category = "DCC Export Options", meta = (EditCondition = "bCompressInZipFile", EditConditionHides))
	FString ArchiveName;
};

UCLASS()
class UMetaHumanCharacterEditorDCCExportTool : public UMetaHumanCharacterEditorExportToolBase
{
	GENERATED_BODY()

protected:
	//~Begin UMetaHumanCharacterEditorExportToolBase interface
	virtual UClass* GetExportPropertiesClass() const override;
	virtual FText GetExportToolDisplayName() const override;
	//~End UMetaHumanCharacterEditorExportToolBase interface

public:
	//~Begin UMetaHumanCharacterEditorExportToolBase interface
	virtual FText GetExportButtonText() const override;
	virtual bool CanExport(FText& OutErrorMsg) const override;
	virtual void Export() const override;
	//~End UMetaHumanCharacterEditorExportToolBase interface
};

// DNA Export

UCLASS()
class UMetaHumanCharacterEditorDNAExportProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "DNA File Export Options")
	FDirectoryPath ExternalPath;

	UPROPERTY(EditAnywhere, Category = "DNA File Export Options", DisplayName = "DNA Head")
	bool bDNAHead = true;

	UPROPERTY(EditAnywhere, Category = "DNA File Export Options", DisplayName = "DNA Body")
	bool bDNABody = true;
};

UCLASS()
class UMetaHumanCharacterEditorDNAExportTool : public UMetaHumanCharacterEditorExportToolBase
{
	GENERATED_BODY()

protected:

	//~Begin UMetaHumanCharacterEditorExportToolBase interface
	virtual UClass* GetExportPropertiesClass() const override;
	virtual FText GetExportToolDisplayName() const override;
	virtual FText GetExportButtonText() const override;
	//~End UMetaHumanCharacterEditorExportToolBase interface

public:
	//~Begin UMetaHumanCharacterEditorExportToolBase interface
	virtual bool CanExport(FText& OutErrorMsg) const override;
	virtual void Export() const override;
	//~End UMetaHumanCharacterEditorExportToolBase interface
};

// Geometry Export

UCLASS()
class UMetaHumanCharacterEditorGeometryExportProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Geometry Export Options", meta = (ContentDir))
	FDirectoryPath ProjectPath{ TEXT("/Game/MetaHumans") };

	UPROPERTY(EditAnywhere, Category = "Skeletal Meshes")
	bool bHeadSkeletalMesh = true;

	UPROPERTY(EditAnywhere, Category = "Skeletal Meshes")
	bool bBodySkeletalMesh = true;

	UPROPERTY(EditAnywhere, Category = "Skeletal Meshes")
	bool bFullBodySkeletalMesh = false;

	/** Overwrites onto the existing assets, if false will create new unique asset names for exported assets. */
	UPROPERTY(EditAnywhere, Category = "Skeletal Meshes", AdvancedDisplay)
	bool bOverwriteExistingAssets = true;
};

UCLASS()
class UMetaHumanCharacterEditorGeometryExportTool : public UMetaHumanCharacterEditorExportToolBase
{
	GENERATED_BODY()

protected:
	//~Begin UMetaHumanCharacterEditorExportToolBase interface
	virtual UClass* GetExportPropertiesClass() const override;
	virtual FText GetExportToolDisplayName() const override;
	//~End UMetaHumanCharacterEditorExportToolBase interface

public:
	//~Begin UMetaHumanCharacterEditorExportToolBase interface
	virtual FText GetExportButtonText() const override;
	virtual bool CanExport(FText& OutErrorMsg) const override;
	virtual void Export() const override;
	//~End UMetaHumanCharacterEditorExportToolBase interface
};

// Materials Export

UCLASS()
class UMetaHumanCharacterEditorMaterialsExportProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Materials Export Options", meta = (ContentDir))
	FDirectoryPath ProjectPath{ TEXT("/Game/MetaHumans") };

	/** If true the exported materials will be applied as override materials on MetaHumanCharacter asset */
	UPROPERTY(EditAnywhere, Category = "Materials", DisplayName = "Apply as Overrides")
	bool bMaterialsApplyAsOverrides = true;
};

UCLASS()
class UMetaHumanCharacterEditorMaterialsExportTool : public UMetaHumanCharacterEditorExportToolBase
{
	GENERATED_BODY()

protected:
	//~Begin UMetaHumanCharacterEditorExportToolBase interface
	virtual UClass* GetExportPropertiesClass() const override;
	virtual FText GetExportToolDisplayName() const override;
	virtual FText GetExportButtonText() const override;
	//~End UMetaHumanCharacterEditorExportToolBase interface

public:
	//~Begin UMetaHumanCharacterEditorExportToolBase interface
	virtual bool CanExport(FText& OutErrorMsg) const override;
	virtual void Export() const override;
	//~End UMetaHumanCharacterEditorExportToolBase interface
};
