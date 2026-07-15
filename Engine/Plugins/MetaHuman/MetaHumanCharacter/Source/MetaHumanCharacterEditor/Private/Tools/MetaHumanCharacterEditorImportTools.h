// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveToolBuilder.h"
#include "MetaHumanCharacterEditorSubTools.h"
#include "MetaHumanCharacterEditorSubsystem.h"

#include "MetaHumanCharacterEditorImportTools.generated.h"

UENUM()
enum class EMetaHumanImportToolMode : uint8
{
	MeshFit,
	Replace,
	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanImportToolMode, EMetaHumanImportToolMode::Count);

UCLASS()
class UMetaHumanCharacterEditorImportFromDNAToolBuilder : public UMetaHumanCharacterEditorToolWithToolTargetsBuilder
{
	GENERATED_BODY()

public:

	//~Begin UInteractiveToolWithToolTargetsBuilder interface
	virtual bool CanBuildTool(const FToolBuilderState& InSceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& InSceneState) const override;
	//~End UInteractiveToolWithToolTargetsBuilder interface
};

UCLASS()
class UMetaHumanCharacterEditorImportFromIdentityToolBuilder : public UMetaHumanCharacterEditorToolWithToolTargetsBuilder
{
	GENERATED_BODY()

public:

	//~Begin UInteractiveToolWithToolTargetsBuilder interface
	virtual bool CanBuildTool(const FToolBuilderState& InSceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& InSceneState) const override;
	//~End UInteractiveToolWithToolTargetsBuilder interface
};

UCLASS()
class UMetaHumanCharacterEditorImportFromTemplateToolBuilder : public UMetaHumanCharacterEditorToolWithToolTargetsBuilder
{
	GENERATED_BODY()

public:

	//~Begin UInteractiveToolWithToolTargetsBuilder interface
	virtual bool CanBuildTool(const FToolBuilderState& InSceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& InSceneState) const override;
	//~End UInteractiveToolWithToolTargetsBuilder interface
};

UCLASS(Abstract)
class UMetaHumanCharacterImportSubToolBase : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	virtual bool CanConform() const PURE_VIRTUAL(UMetaHumanCharacterImportSubToolBase::CanConform(), { return false; });
	virtual void Conform() PURE_VIRTUAL(UMetaHumanCharacterImportSubToolBase::Conform(), {});

	virtual bool CanImportMesh() const PURE_VIRTUAL(UMetaHumanCharacterImportSubToolBase::CanImportMesh(), { return false; });
	virtual void ImportMesh() PURE_VIRTUAL(UMetaHumanCharacterImportSubToolBase::ImportMesh(), {});

	virtual bool CanImportJoints() const PURE_VIRTUAL(UMetaHumanCharacterImportSubToolBase::CanImportJoints(), { return false; });
	virtual void ImportJoints() PURE_VIRTUAL(UMetaHumanCharacterImportSubToolBase::ImportJoints(), {});

	/* Display a conform error message */
	virtual void DisplayConformError(const FText& ErrorMessageText) const;

	UPROPERTY()
	EMetaHumanImportToolMode Mode = EMetaHumanImportToolMode::MeshFit;
};

UCLASS()
class UMetaHumanCharacterImportFromDNAProperties : public UMetaHumanCharacterImportSubToolBase
{
	GENERATED_BODY()

public:

	//~Begin UObject Interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~End UObject Interface

	// File path to the .dna file containing the head rig data to import. 
	UPROPERTY(EditAnywhere, Category = "Load DNA Assets", DisplayName = "DNA Head", meta = (FilePathFilter = "DNA file (*.dna)|*.dna"))
	FFilePath DNAHead;

	// File path to the .dna file containing the body rig data to import. 
	UPROPERTY(EditAnywhere, Category = "Load DNA Assets", DisplayName = "DNA Body", meta = (FilePathFilter = "DNA file (*.dna)|*.dna"))
	FFilePath DNABody;

	UPROPERTY(EditAnywhere, Category = "Head Options", meta = (ShowOnlyInnerProperties))
	FImportFromDNAParams ImportOptions;

	UPROPERTY(EditAnywhere, Category = "Body Options", meta = (ShowOnlyInnerProperties))
	FConformBodyParams BodyParams;

	// When enabled, the MetaHuman's body surface mesh is replaced with the geometry stored in the DNA file. Disable if you only want to update joints without changing the visible mesh.
	UPROPERTY(EditAnywhere, Category = "Body Options")
	bool bReplaceMesh = true;

	// When enabled, the MetaHuman's body joint positions and orientations are replaced with those from the body DNA. Disable if you only want to update the surface mesh without affecting the skeleton.
	UPROPERTY(EditAnywhere, Category = "Body Options")
	bool bReplaceBodyJoints = true;

	UPROPERTY(EditAnywhere, Category = "Options")
	bool bAlignScale = true;

	UPROPERTY(EditAnywhere, Category = "Options")
	bool bAlignRotation = true;

	UPROPERTY(EditAnywhere, Category = "Options")
	bool bAlignTranslation = true;

public:
	//~Begin UMetaHumanCharacterImportSubToolBase interface
	virtual bool CanImportMesh() const override;
	virtual void ImportMesh() override;
	virtual bool CanConform() const override;
	virtual void Conform() override;
	virtual bool CanImportJoints() const override;
	virtual void ImportJoints() override;
	//~End UMetaHumanCharacterImportSubToolBase interface

	bool CanImportWholeRig() const;
	void ImportWholeRig();

private:
	bool GetErrorMessageText(EImportErrorCode InErrorCode, FText& OutErrorMessage) const;
};

UCLASS()
class UMetaHumanCharacterImportFromIdentityProperties : public UMetaHumanCharacterImportSubToolBase
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Asset")
	TSoftObjectPtr<class UMetaHumanIdentity> MetaHumanIdentity;

	UPROPERTY(EditAnywhere, Category = "Import Identity Options", meta = (ShowOnlyInnerProperties))
	FImportFromIdentityParams ImportOptions;

public:

	//~Begin UMetaHumanCharacterImportSubToolBase interface
	virtual bool CanImportMesh() const override;
	virtual void ImportMesh() override;
	virtual bool CanConform() const override { return false; }
	virtual void Conform() override {};
	virtual bool CanImportJoints() const override { return false; }
	virtual void ImportJoints() override {};
	//~End UMetaHumanCharacterImportSubToolBase interface
};

UCLASS()
class UMetaHumanCharacterImportFromTemplateProperties : public UMetaHumanCharacterImportSubToolBase
{
	GENERATED_BODY()

public:

	//~Begin UObject Interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~End UObject Interface

	UPROPERTY(EditAnywhere, Category = "Asset", meta = (AllowedClasses = "/Script/Engine.StaticMesh, /Script/Engine.SkeletalMesh"))
	TSoftObjectPtr<UObject> HeadMesh;

	UPROPERTY(EditAnywhere, Category = "Asset", meta = (AllowedClasses = "/Script/Engine.StaticMesh, /Script/Engine.SkeletalMesh"))
	TSoftObjectPtr<UObject> BodyMesh;

	UPROPERTY(EditAnywhere, Category = "Asset", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TSoftObjectPtr<UObject> LeftEyeMesh;

	UPROPERTY(EditAnywhere, Category = "Asset", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TSoftObjectPtr<UObject> RightEyeMesh;

	UPROPERTY(EditAnywhere, Category = "Asset", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TSoftObjectPtr<UObject> TeethMesh;

	// When enabled, vertices are matched to the MetaHuman template by comparing UV coordinates instead of vertex order. Use this when the template mesh has the same UV layout as the MetaHuman standard but a different vertex ordering.
	UPROPERTY(EditAnywhere, Category = "Asset")
	bool bMatchVerticesByUVs = true;

	UPROPERTY(EditAnywhere, Category = "Options", meta = (ShowOnlyInnerProperties))
	FImportFromTemplateParams ImportOptions;

	UPROPERTY(EditAnywhere, Category = "Options", meta = (ShowOnlyInnerProperties))
	FConformBodyParams BodyParams;

	// When enabled, the MetaHuman's body surface mesh is replaced with the geometry from the template mesh. Disable if you only want to update joints without changing the visible mesh.
	UPROPERTY(EditAnywhere, Category = "Options")
	bool bReplaceMesh = true;

	// When enabled, the MetaHuman's body joint positions and orientations are replaced with those from the template mesh. Disable if you only want to update the surface mesh without affecting the skeleton.
	UPROPERTY(EditAnywhere, Category = "Options")
	bool bReplaceBodyJoints = true;

	UPROPERTY(EditAnywhere, Category = "Options")
	bool bAlignScale = true;

	UPROPERTY(EditAnywhere, Category = "Options")
	bool bAlignRotation = true;

	UPROPERTY(EditAnywhere, Category = "Options")
	bool bAlignTranslation = true;

public:

	//~Begin UMetaHumanCharacterImportSubToolBase interface
	virtual bool CanImportMesh() const override;
	virtual void ImportMesh() override;
	virtual bool CanConform() const override;
	virtual void Conform() override;
	virtual bool CanImportJoints() const override;
	virtual void ImportJoints() override;
	//~End UMetaHumanCharacterImportSubToolBase interface

private:

	bool GetErrorMessageText(EImportErrorCode InErrorCode, FText& OutErrorMessage) const;
};

UCLASS(Abstract)
class UMetaHumanCharacterEditorImportTool : public USingleSelectionTool
{
	GENERATED_BODY()

public:
	/** Get the Import Tool properties. */
	UMetaHumanCharacterImportSubToolBase* GetImportToolProperties() const { return ImportProperties; }

	//~Begin USingleSelectionTool interface
	virtual void Shutdown(EToolShutdownType InShutdownType) override;
	//~End USingleSelectionTool interface

	void UpdateOriginalState();
	void UpdateOriginalDNABuffer();

	TSharedRef<const FMetaHumanCharacterIdentity::FState> GetOriginalFaceState() const;
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> GetOriginalBodyState() const;
	const TArray<uint8>& GetOriginalFaceDNABuffer() const;
	const TArray<uint8>& GetOriginalBodyDNABuffer() const;

protected:

	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterImportSubToolBase> ImportProperties;

	// Hold the original face state of the character, used to undo changes on cancel
	TSharedPtr<FMetaHumanCharacterIdentity::FState> OriginalFaceState;

	// Hold the original body state of the character, used to undo changes on cancel
	TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> OriginalBodyState;

	// Hold the original Face DNA buffer of the character, used to undo changes on cancel
	TArray<uint8> OriginalFaceDNABuffer;

	// Hold the original Body DNA buffer of the character, used to undo changes on cancel
	TArray<uint8> OriginalBodyDNABuffer;
};

UCLASS()
class UMetaHumanCharacterEditorImportFromDNATool : public UMetaHumanCharacterEditorImportTool
{
	GENERATED_BODY()

public:

	//~Begin UMetaHumanCharacterEditorImportTool interface
	virtual void Setup() override;
	//~End UMetaHumanCharacterEditorImportTool interface

};

UCLASS()
class UMetaHumanCharacterEditorImportFromIdentityTool : public UMetaHumanCharacterEditorImportTool
{
	GENERATED_BODY()

public:

	//~Begin UMetaHumanCharacterEditorImportTool interface
	virtual void Setup() override;
	//~End UMetaHumanCharacterEditorImportTool interface

};

UCLASS()
class UMetaHumanCharacterEditorImportFromTemplateTool : public UMetaHumanCharacterEditorImportTool
{
	GENERATED_BODY()

public:
	
	//~Begin UMetaHumanCharacterEditorImportTool interface
	virtual void Setup() override;
	//~End UMetaHumanCharacterEditorImportTool interface

};
