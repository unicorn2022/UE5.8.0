// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanParameterMappingTable.h"
#include "MetaHumanCharacterPipeline.h"
#include "MetaHumanPaletteItemPath.h"

#include "PreviewScene.h"
#include "Tools/BaseAssetToolkit.h"

#include "MetaHumanCharacterPaletteEditorToolkit.generated.h"

class AActor;
enum class EMetaHumanCharacterAssemblyResult : uint8;
struct FMetaHumanCharacterPaletteItem;
class FPreviewScene;
class IDetailsView;
class UMetaHumanCharacterPaletteAssetEditor;
class UMetaHumanCharacterPaletteItemWrapper;
class UMetaHumanCollection;
class SCollectionItemTileView;

USTRUCT()
struct FMetaHumanPaletteEditorItemInstanceParameters
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FMetaHumanPaletteItemPath ItemPath;

	/** Friendly name for the item, used as the category header by the details customization. */
	UPROPERTY()
	FText DisplayName;

	UPROPERTY(EditAnywhere, Category = "Overrides", meta = (FixedLayout, ShowOnlyInnerProperties))
	FInstancedPropertyBag InstanceParameters;
};

UCLASS(Transient)
class UMetaHumanPaletteEditorCollectionInstanceParameters : public UObject
{
	GENERATED_BODY()

public:
	// This name needs to be unique among any properties defined in Instance Parameter structs provided by item pipelines,
	// because of the way property change notifications work.
	UPROPERTY(EditAnywhere, Category = "Costume", meta = (ShowOnlyInnerProperties))
	TArray<FMetaHumanPaletteEditorItemInstanceParameters> UMetaHumanPaletteEditorCollectionInstanceParameters_Items;
};

/**
 * The core class of the Palette editor
 */
class FMetaHumanCharacterPaletteEditorToolkit : public FBaseAssetToolkit
{
public:
	FMetaHumanCharacterPaletteEditorToolkit(UMetaHumanCharacterPaletteAssetEditor* InOwningAssetEditor);

protected:
	// Begin FBaseAssetToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual AssetEditorViewportFactoryFunction GetViewportDelegate() override;
	virtual TSharedPtr<FEditorViewportClient> CreateEditorViewportClient() const override;
	virtual TSharedRef<IDetailsView> CreateDetailsView() override;
	virtual void RegisterToolbar() override;
	virtual void PostInitAssetEditor() override;
	virtual void SetEditingObject(UObject* InObject) override;
	virtual void SaveAsset_Execute() override;
	virtual void SaveAssetAs_Execute() override;
	virtual bool OnRequestClose(EAssetEditorCloseReason InCloseReason) override;
	// End FBaseAssetToolkit interface

private:
	TSharedRef<SDockTab> SpawnTab_PartsView(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_ItemDetails(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_ItemInstanceParameters(const FSpawnTabArgs& Args);
	void OnPartsViewSelectionChanged(TSharedPtr<FMetaHumanCharacterPaletteItem> NewSelectedItem, ESelectInfo::Type SelectInfo);
	void OnPartsViewDoubleClick(TSharedPtr<FMetaHumanCharacterPaletteItem> Item, FName SlotName);
	void OnFinishedChangingItemProperties(const FPropertyChangedEvent& Event);
	void OnFinishedChangingInstanceParameters(const FPropertyChangedEvent& Event);
	void OnCollectionModified();
	void OnFinishedChangingCollectionProperties(const FPropertyChangedEvent& Event);
	
	void ToggleAutoBuildForPreview();
	bool IsAutoBuildForPreviewEnabled() const;

	void ApplyEditsToAsset();
	void BuildForProduction();
	bool IsApplyButtonEnabled() const;

	// Shows a dialog when there are unapplied edits, asking if the user wants to apply before 
	// saving.
	//
	// Returns true if the save should continue.
	bool PromptToApplyEditsBeforeSave();

	// Returns the short name of the asset being edited
	FString GetEditingAssetName() const;

	void ToggleAutoApplyInstanceEdits();
	bool ShouldAutoApplyInstanceEdits() const;

	void ToggleBuildAllItemsOnEdit();
	bool ShouldBuildAllItemsOnEdit() const;

	void ClearBuildCache();

	void UpdatePreview();
	void OnMetaHumanCharacterAssembled(EMetaHumanCharacterAssemblyResult Status);

	UMetaHumanCharacterPaletteAssetEditor* GetMutableCharacterEditor();
	const UMetaHumanCharacterPaletteAssetEditor* GetCharacterEditor() const;

	bool IsCollectionEditable() const;
	bool ShouldViewportShowProductionBuild() const;

	// The preview scene displayed in the viewport of the asset editor.
	TUniquePtr<FPreviewScene> PreviewScene;

	// The actor spawned in the world of the preview scene
	//
	// This is a weak pointer because the PreviewScene should hold a strong reference to the actor,
	// so the actor won't be deleted while the scene is alive, but we don't want this to become a
	// dangling pointer after the scene is cleaned up.
	TWeakObjectPtr<AActor> PreviewActor;

	TArray<TSharedPtr<FMetaHumanCharacterPaletteItem>> ListItems;

	TSharedPtr<IDetailsView> ItemDetailsView;
	TSharedPtr<IDetailsView> ItemInstanceParametersDetailsView;
	TSharedPtr<SCollectionItemTileView> PartsViewWidget;
	TSharedPtr<FMetaHumanCharacterPaletteItem> CurrentlySelectedItem;
	FMetaHumanPaletteItemKey CurrentlySelectedItemKey;

	TStrongObjectPtr<UMetaHumanCharacterPaletteItemWrapper> ItemWrapper;
	TStrongObjectPtr<UMetaHumanPaletteEditorCollectionInstanceParameters> InstanceParameterWrapper;

	TStrongObjectPtr<UMetaHumanCollection> PreviewCollection;
	TStrongObjectPtr<UMetaHumanInstance> PreviewInstance;

	bool bPreviewCollectionNeedsBuild = false;
	bool bCollectionHasUnappliedEdits = false;
	bool bInstanceHasUnappliedEdits = false;
	bool bShouldAutoApplyInstanceEdits = true;
};
