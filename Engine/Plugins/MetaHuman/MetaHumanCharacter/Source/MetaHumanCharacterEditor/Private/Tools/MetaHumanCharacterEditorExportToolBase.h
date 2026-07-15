// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveToolBuilder.h"
#include "MetaHumanCharacterEditorSubTools.h"
#include "SingleSelectionTool.h"

#include "UObject/StrongObjectPtr.h"

#include "MetaHumanCharacterEditorExportToolBase.generated.h"

class UInteractiveToolPropertySet;

/** Shared builder for all Export tools. Set ToolClass to the specific export tool subclass to create. */
UCLASS()
class UMetaHumanCharacterEditorExportToolBuilder : public UMetaHumanCharacterEditorToolWithToolTargetsBuilder
{
	GENERATED_BODY()

public:

	//~Begin UInteractiveToolWithToolTargetsBuilder interface
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& InSceneState) const override;
	//~End UInteractiveToolWithToolTargetsBuilder interface

	UPROPERTY()
	TSubclassOf<class UMetaHumanCharacterEditorExportToolBase> ToolClass;

private:

	/** Long-lived property set, lazily created on the first BuildTool call. */
	mutable TStrongObjectPtr<UInteractiveToolPropertySet> PersistentProperties;
};

/**
 * Abstract base class for all Export tools. Subclasses provide the concrete property set class
 * via GetExportPropertiesClass and override CanExport / Export / GetExportButtonText.
 */
UCLASS(Abstract)
class UMetaHumanCharacterEditorExportToolBase : public USingleSelectionTool
{
	GENERATED_BODY()

public:

	//~Begin UInteractiveTool interface
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual bool HasCancel() const override { return false; }
	virtual bool HasAccept() const override { return false; }
	virtual bool CanAccept() const override { return false; }
	//~End UInteractiveTool interface

	/** Returns the active export property set. */
	UInteractiveToolPropertySet* GetExportProperties() const { return ExportProperties; }

	/** Called by the builder to inject the property set this tool will display. */
	void InitializeExportProperties(UInteractiveToolPropertySet* InExportProperties)
	{
		ExportProperties = InExportProperties;
	}

	/** Returns the concrete UInteractiveToolPropertySet subclass this tool wants. */
	virtual UClass* GetExportPropertiesClass() const PURE_VIRTUAL(UMetaHumanCharacterEditorExportToolBase::GetExportPropertiesClass, return nullptr;);

	/** Returns true if the export can proceed. */
	virtual bool CanExport(FText& OutErrorMsg) const;

	/** Performs the export. */
	virtual void Export() const;

	/** Returns the text to display on the Export button. */
	virtual FText GetExportButtonText() const;

protected:

	/** Returns the display name for this export tool. */
	virtual FText GetExportToolDisplayName() const PURE_VIRTUAL(UMetaHumanCharacterEditorExportToolBase::GetExportToolDisplayName, return FText::GetEmpty(););

	/** The property set displayed in this tool's details panel. Owned by the builder. */
	UPROPERTY()
	TObjectPtr<UInteractiveToolPropertySet> ExportProperties;
};
