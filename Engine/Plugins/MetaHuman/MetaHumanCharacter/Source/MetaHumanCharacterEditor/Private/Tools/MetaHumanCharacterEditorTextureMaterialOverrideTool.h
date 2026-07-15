// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCharacterEditorSubTools.h"
#include "SingleSelectionTool.h"
#include "MetaHumanCharacter.h"

#include "MetaHumanCharacterEditorTextureMaterialOverrideTool.generated.h"

UCLASS()
class UMetaHumanCharacterEditorTextureMaterialOverrideToolBuilder : public UMetaHumanCharacterEditorToolWithToolTargetsBuilder
{
	GENERATED_BODY()

public:
	//~Begin UInteractiveToolWithToolTargetsBuilder interface
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& InSceneState) const override;
	//~End UInteractiveToolWithToolTargetsBuilder interface
};

/**
 * Properties for the Texture & Material Overrides tool.
 * Allows the user to override skin textures and material parameters.
 */
UCLASS()
class UMetaHumanCharacterEditorTextureMaterialOverrideToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	//~Begin UObject Interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~End UObject Interface

	DECLARE_DELEGATE_OneParam(FOnTextureMaterialOverridePropertyValueSetDelegate, const FPropertyChangedEvent& PropertyChangedEvent);
	FOnTextureMaterialOverridePropertyValueSetDelegate OnPropertyValueSetDelegate;

	/** Copies texture override fields into the given skin settings, leaving other fields untouched */
	void CopyTo(FMetaHumanCharacterSkinSettings& OutSkinSettings) const;

	/** Reads texture override fields from the given skin settings */
	void CopyFrom(const FMetaHumanCharacterSkinSettings& InSkinSettings);

	UPROPERTY(EditAnywhere, Category = "Texture & Material Overrides", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterTextureMaterialOverrides TextureMaterialOverrides;

	UPROPERTY(Transient, EditAnywhere, Category = "Viewport")
	bool bShowTeeth = false;
};

/**
 * The Texture & Material Overrides Tool allows the user to override skin textures
 * and material parameters on the MetaHuman Character.
 */
UCLASS()
class UMetaHumanCharacterEditorTextureMaterialOverrideTool : public USingleSelectionTool
{
	GENERATED_BODY()

public:
	/** Get the tool properties */
	UMetaHumanCharacterEditorTextureMaterialOverrideToolProperties* GetTextureMaterialOverrideToolProperties() const { return ToolProperties; }

	//~Begin USingleSelectionTool interface
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType InShutdownType) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override { return true; }
	//~End USingleSelectionTool interface

protected:
	void UpdateTextureMaterialOverrideState() const;
	void UpdateShowTeethState() const;

private:
	friend class FMetaHumanCharacterEditorTextureMaterialOverrideToolCommandChange;

	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorTextureMaterialOverrideToolProperties> ToolProperties;

	FMetaHumanCharacterSkinSettings PreviousSkinSettings;
};
