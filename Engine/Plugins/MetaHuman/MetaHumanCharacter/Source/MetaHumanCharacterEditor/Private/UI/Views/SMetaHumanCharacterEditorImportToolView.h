// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/NotifyHook.h"
#include "SMetaHumanCharacterEditorToolView.h"

enum class EMetaHumanImportToolMode : uint8;
class FReply;
class IDetailsView;
class ISinglePropertyView;
class UMetaHumanCharacterEditorImportTool;
class UMetaHumanCharacterEditorImportFromDNATool;
class UMetaHumanCharacterEditorImportFromIdentityTool;
class UMetaHumanCharacterEditorImportFromTemplateTool;
class UStreamableRenderAsset;

/**
 * Abstract base for import tool views in the MetaHuman Character editor.
 * Provides shared layout helpers and the virtual button/warning interface.
 * Does not assume anything about property sets or undo mechanics.
 */
class SMetaHumanCharacterEditorImportToolView
	: public SMetaHumanCharacterEditorToolView
{
protected:
	/** Creates the section widget for showing the Warning panel. */
	TSharedRef<SWidget> CreateImportToolViewWarningSection();

	/** Creates the section widget for showing the Import button. */
	TSharedRef<SWidget> CreateImportToolViewButtonSection();

	/** True if the Import button is enabled. */
	virtual bool IsImportButtonEnabled() const = 0;

	/** Called when the Import button is clicked.*/
	virtual FReply OnImportButtonClicked() = 0;

	/** Gets the displayed text of the Import button. Defaults to "Apply". */
	virtual FText GetImportButtonText() const;

	/** Gets the text for the warning panel. Defaults to empty (hidden). */
	virtual FText GetWarningText() const;

	/** Gets the visibility of the warning panel */
	EVisibility GetWarningVisibility() const;
};

/**
 * Intermediate base for the built-in import tool views.
 * Adds FNotifyHook for property-change undo tracking and provides
 * implementations driven by UMetaHumanCharacterEditorImportTool.
 */
class SMetaHumanCharacterEditorInternalImportToolView
	: public SMetaHumanCharacterEditorImportToolView
	, public FNotifyHook
{
public:
	SLATE_BEGIN_ARGS(SMetaHumanCharacterEditorInternalImportToolView)
		{}

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs, UMetaHumanCharacterEditorImportTool* InTool);

protected:
	//~ Begin SMetaHumanCharacterEditorToolView interface
	virtual UInteractiveToolPropertySet* GetToolProperties() const override;
	virtual void MakeToolView() override;
	//~ End of SMetaHumanCharacterEditorToolView interface

	//~ Begin FNotifyHook interface
	virtual void NotifyPreChange(FProperty* PropertyAboutToChange) override;
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;
	//~ End of FNotifyHook interface

	/** Creates the section widget for showing the toolbar */
	TSharedRef<SWidget> CreateImportToolToolbarSection();

	/** Gets the current import tool mode from the tool properties. */
	EMetaHumanImportToolMode GetImportToolMode() const;

	/** Called when the import tool mode is changed by the user. */
	void OnImportToolModeChanged(EMetaHumanImportToolMode Mode);

	/** Gets the visibility of the Import Tool properties sections based on the current tool mode. */
	EVisibility GetImportToolModeVisibility(EMetaHumanImportToolMode Mode) const;

	//~ Begin SMetaHumanCharacterEditorImportToolView interface
	virtual bool IsImportButtonEnabled() const override;
	virtual FReply OnImportButtonClicked() override;
	virtual FText GetWarningText() const override;
	//~ End of SMetaHumanCharacterEditorImportToolView interface
};

class SMetaHumanCharacterEditorImportFromDNAToolView
	: public SMetaHumanCharacterEditorInternalImportToolView
{
public:
	SLATE_BEGIN_ARGS(SMetaHumanCharacterEditorImportFromDNAToolView)
		{
		}

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs, UMetaHumanCharacterEditorImportTool* InTool);

	//~ Begin SMetaHumanCharacterEditorToolView interface
	virtual void MakeToolView() override;

private:
	virtual bool IsImportButtonEnabled() const override;
	virtual FReply OnImportButtonClicked() override;
	virtual FText GetWarningText() const override;
	//~ End of SMetaHumanCharacterEditorToolView interface

	/** Creates the section widget for showing the Load DNA Assets properties */
	TSharedRef<SWidget> CreateImportLoadDNAAssetsSection();

	/** Creates the section widget for showing the Body Mesh properties */
	TSharedRef<SWidget> CreateImportBodyMeshSection();

	/** Creates the section widget for showing the Body Joints properties */
	TSharedRef<SWidget> CreateImportBodyJointsSection();

	/** True if the DNA body property should be enabled (disabled when bIsolateHeadFromBody is set). */
	bool IsDNABodyEnabled() const;

	/** True if the replace mesh property should be enabled (disabled when bImportWholeRig is set). */
	bool IsReplaceMeshEnabled() const;

	/** True if the auto rig helper joints property should be enabled. */
	bool IsAutoRigHelperJointsEnabled() const;

	/** True if the import helper joints property should be enabled. */
	bool IsImportHelperJointsEnabled() const;
};

class SMetaHumanCharacterEditorImportFromIdentityToolView
	: public SMetaHumanCharacterEditorInternalImportToolView
{
public:
	SLATE_BEGIN_ARGS(SMetaHumanCharacterEditorImportFromIdentityToolView)
		{
		}

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs, UMetaHumanCharacterEditorImportTool* InTool);

	//~ Begin SMetaHumanCharacterEditorToolView interface
	virtual void MakeToolView() override;

private:
	virtual FText GetWarningText() const override;
	//~ End of SMetaHumanCharacterEditorToolView interface

	/** Creates the section widget for showing the Load Identity Assets properties */
	TSharedRef<SWidget> CreateImportLoadIdentityAssetsSection();

	/** Creates the section widget for showing the View Options properties */
	TSharedRef<SWidget> CreateImportViewOptionsSection();
};

class SMetaHumanCharacterEditorImportFromTemplateToolView
	: public SMetaHumanCharacterEditorInternalImportToolView
{
public:
	SLATE_BEGIN_ARGS(SMetaHumanCharacterEditorImportFromTemplateToolView)
		{
		}

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs, UMetaHumanCharacterEditorImportTool* InTool);

	//~ Begin SMetaHumanCharacterEditorToolView interface
	virtual void MakeToolView() override;
	//~ End of SMetaHumanCharacterEditorToolView interface

private:
	virtual bool IsImportButtonEnabled() const override;
	virtual FReply OnImportButtonClicked() override;
	virtual FText GetWarningText() const override;
	//~ End of SMetaHumanCharacterEditorToolView interface

	/** Creates the section widget for showing the Load Template Assets properties */
	TSharedRef<SWidget> CreateImportLoadTemplateAssetsSection();

	/** Creates the section widget for showing the Head Options properties */
	TSharedRef<SWidget> CreateImportHeadOptionsSection();

	/** Creates the section widget for showing the Body Options properties */
	TSharedRef<SWidget> CreateImportBodyOptionsSection();

	/** Creates the section widget for showing the Body Mesh properties */
	TSharedRef<SWidget> CreateImportBodyMeshSection();

	/** Creates the section widget for showing the Body Joints properties */
	TSharedRef<SWidget> CreateImportBodyJointsSection();

	/** True if the body mesh property should be enabled (disabled when bIsolateHeadFromBody is set). */
	bool IsBodyMeshEnabled() const;

	/** True if the head mesh properties should be enabled. */
	bool IsHeadMeshPropertyEnabledByClass(TSubclassOf<UStreamableRenderAsset> Class) const;

	/** True if the auto rig helper joints property should be enabled. */
	bool IsAutoRigHelperJointsEnabled() const;

	/** True if the import helper joints property should be enabled. */
	bool IsImportHelperJointsEnabled() const;
};
