// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SShaderCostTreeMap.h" // for FOnExtendShaderAssetContextMenu / FOnOpenShaderAssetInContentBrowser

struct FShaderAuditSession;

/** Caller-supplied hook to populate a session's material parent map (typically: walk the
 *  editor's Asset Registry). Unbound -> the "Fetch Material Hierarchy" button is hidden. */
DECLARE_DELEGATE_OneParam(FOnFetchMaterialHierarchy, TSharedPtr<FShaderAuditSession> /*Session*/);

/**
 * Session document view -- toolbar (view toggle) over a widget switcher
 * containing spreadsheet, raw SHK, cost treemap, and extension-contributed views.
 */
class SShaderSessionView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShaderSessionView) {}
		SLATE_ARGUMENT(TSharedPtr<FShaderAuditSession>, Session)

		/** Forwarded to the embedded cost-treemap widget. */
		SLATE_EVENT(FOnExtendShaderAssetContextMenu, OnExtendAssetContextMenu)
		SLATE_EVENT(FOnOpenShaderAssetInContentBrowser, OnOpenAssetInContentBrowser)

		/** If bound, a "Fetch Material Hierarchy" button is shown; clicking it invokes the hook. */
		SLATE_EVENT(FOnFetchMaterialHierarchy, OnFetchMaterialHierarchy)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual ~SShaderSessionView() override;

private:
	TSharedPtr<FShaderAuditSession> Session;
	TSharedPtr<class SInspectShaderSessionWidget> InspectWidget;
	TSharedPtr<class SShaderCostTreeMap> CostTreeMapWidget;
	TSharedPtr<class SSHKEntryListWidget> RawSHKWidget;
	TSharedPtr<class SWidgetSwitcher> ViewSwitcher;

	/** Next widget index to assign when adding extension views. */
	int32 NextExtensionViewIndex = 3; // 0=Spreadsheet, 1=RawSHK, 2=Treemap

	void AddExtensionContributions(class IShaderAuditExtension& Extension);
	void OnModularFeatureRegistered(const FName& Type, class IModularFeature* Feature);
	void OnModularFeatureUnregistered(const FName& Type, class IModularFeature* Feature);

	TSharedPtr<class SHorizontalBox> ToolbarBox;

	FOnFetchMaterialHierarchy OnFetchMaterialHierarchyHook;
};
