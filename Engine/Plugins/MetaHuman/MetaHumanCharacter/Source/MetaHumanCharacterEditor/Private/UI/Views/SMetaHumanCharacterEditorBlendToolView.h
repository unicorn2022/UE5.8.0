// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SMetaHumanCharacterEditorToolView.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

struct FMetaHumanCharacterAssetViewItem;
struct FMetaHumanCharacterAssetViewStatus;
struct FMetaHumanCharacterAssetViewsPanelStatus;
class SMetaHumanCharacterEditorBlendToolPanel;
class UMetaHumanCharacter;
class UMetaHumanCharacterEditorMeshBlendTool;
class UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties;

/** View for displaying the Blend Tool in the MetahHumanCharacter editor */
class SMetaHumanCharacterEditorBlendToolView
	: public SMetaHumanCharacterEditorToolView
{

	SLATE_DECLARE_WIDGET(SMetaHumanCharacterEditorBlendToolView, SMetaHumanCharacterEditorToolView)

public:
	SLATE_BEGIN_ARGS(SMetaHumanCharacterEditorBlendToolView)
	{}

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs, UMetaHumanCharacterEditorMeshBlendTool* InTool);

	//~ Begin SMetaHumanCharacterEditorToolView interface
	virtual const FName& GetToolViewNameID() const override;
	virtual UInteractiveToolPropertySet* GetToolProperties() const override;
	virtual void MakeToolView() override;
	virtual void OnPostEditChangeProperty(FProperty* Property, bool bIsInteractive) override;
	//~ End of SMetaHumanCharacterEditorToolView interface
	
	/** Gets the status parameters of the asset views panel. */
	FMetaHumanCharacterAssetViewsPanelStatus GetAssetViewsPanelStatus() const;
	
	/** Sets the status of the asset views panel according to the given parameters. */
	void SetAssetViewsPanelStatus(const FMetaHumanCharacterAssetViewsPanelStatus& Status);

	/** Gets an array with the status parameters of all the asset views in the panel. */
	TArray<FMetaHumanCharacterAssetViewStatus> GetAssetViewsStatusArray() const;

	/** Sets the status of the asset views in the panel according to the given array. */
	void SetAssetViewsStatus(const TArray<FMetaHumanCharacterAssetViewStatus>& StatusArray);

	/** Called when an item's content is set in the blend tool panel. */
	void OnBlendToolItemContentSet(const FAssetData& InAssetData, int32 InItemIndex);

	/** Called when an item's content is deleted in the blend tool panel. */
	void OnBlendToolItemContentDeleted(int32 InItemIndex);

	/** Called when an item is double clicked in the presets of the blend tool panel. */
	void OnBlendToolItemActivated(TSharedPtr<FMetaHumanCharacterAssetViewItem> Item);

	/** Called when "Apply Head Only" is selected from an item's context menu in the blend tool panel. */
	void OnBlendToolItemApplyHeadOnly(TSharedPtr<FMetaHumanCharacterAssetViewItem> Item);

	/** Called when "Apply Body Only" is selected from an item's context menu in the blend tool panel. */
	void OnBlendToolItemApplyBodyOnly(TSharedPtr<FMetaHumanCharacterAssetViewItem> Item);

private:
	/** Creates the section widget for showing the Presets properties. */
	TSharedRef<SWidget> CreateBlendToolViewBlendPanelSection();

	/** Creates the parameters section (manipulator size). */
	TSharedRef<SWidget> CreateParametersViewSection();

	/** Creates the section widget for showing the head blend settings. */
	TSharedRef<SWidget> CreateHeadParametersViewSection();

	/** Creates the section widget for showing the body blend settings. */
	TSharedRef<SWidget> CreateBodyParametersViewSection();

	/** Called to filter assets added to the asset view, e.g. to exclude fixed body types. */
	bool OnFilterAddAssetDataToAssetView(const FAssetData& AssetData) const;

	/** Called to override the item thumbnail brush. */
	void OnOverrideItemThumbnailBrush(TSharedPtr<FMetaHumanCharacterAssetViewItem> Item) const;

	/** Gets the visibility for this tool. */
	EVisibility GetBlendSubToolVisibility() const;

	/** Gets the visibility for the fixed body warning. */
	EVisibility GetFixedBodyWarningVisibility() const;

	/* Perform parameteric fit for fixed body types */
	FReply OnPerformParametricFitButtonClicked() const;

	/** Revert the neck region of the head based on the body */
	FReply OnResetNeckButtonClicked() const;

	/** Reference to the Blend Tool panel, used for handling preset blending. */
	TSharedPtr<SMetaHumanCharacterEditorBlendToolPanel> BlendToolPanel;
	
	/** Name identifier for the slot where virtual assets from the body blend tool are stored. */
	static FName AssetsSlotName;
};
