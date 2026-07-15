// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SMaterialDiffTable.h"   // for FOnNavigateToShaderAsset
#include "Widgets/SShaderCostTreeMap.h"   // for FOnExtendShaderAssetContextMenu / FOnOpenShaderAssetInContentBrowser
#include "Widgets/SShaderSessionView.h"   // for FOnFetchMaterialHierarchy

struct FShaderAuditSession;
class FTabManager;
class SDockTab;

/** Simple multicast event for "sessions changed" notification. */
DECLARE_MULTICAST_DELEGATE(FOnSessionsChanged);


/**
 * Top-level widget for the Shader Audit tab/window.
 * Hosts a local FTabManager for document tabs (session views, diffs).
 * Works in both the editor plugin and the standalone ShaderAuditViewer.
 */
class SShaderAuditWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShaderAuditWidget) {}
		/** Optional: subscribe to this delegate to auto-refresh when sessions change. */
		SLATE_ARGUMENT(TSharedPtr<FOnSessionsChanged>, OnSessionsChangedEvent)

		// --- Editor hooks (forwarded to child widgets that need them) ---
		/** Adds entries to the right-click context menu of a material asset in the cost-treemap. */
		SLATE_EVENT(FOnExtendShaderAssetContextMenu, OnExtendAssetContextMenu)
		/** Handles "open in content browser" requests from the cost-treemap. */
		SLATE_EVENT(FOnOpenShaderAssetInContentBrowser, OnOpenAssetInContentBrowser)
		/** Handles asset-hyperlink clicks in the material-diff table. */
		SLATE_EVENT(FOnNavigateToShaderAsset, OnNavigateToAsset)
		/** Provides the material parent map (typically by walking the editor's Asset Registry). */
		SLATE_EVENT(FOnFetchMaterialHierarchy, OnFetchMaterialHierarchy)
	SLATE_END_ARGS()

	SHADERAUDITCORE_API void Construct(const FArguments& InArgs);
	SHADERAUDITCORE_API virtual ~SShaderAuditWidget() override;

	/** Spawn a session inspect tab (deduplicates by full SHK path). */
	SHADERAUDITCORE_API void SpawnSessionTab(TSharedPtr<FShaderAuditSession> Session);

	/** Spawn a diff tab comparing two sessions. */
	SHADERAUDITCORE_API void SpawnDiffTab(TSharedPtr<FShaderAuditSession> SessionA, TSharedPtr<FShaderAuditSession> SessionB);

	/** Show a popup to pick two sessions and diff them. */
	SHADERAUDITCORE_API void ShowDiffPicker();

	/** Show (or focus) the embedded SHK browser tab. */
	SHADERAUDITCORE_API void ShowBrowserTab();

private:
	TSharedPtr<FOnSessionsChanged> SessionsChangedEvent;
	FDelegateHandle SessionsChangedHandle;
	TSharedPtr<FTabManager> TabManager;
	TSharedPtr<SDockTab> HostTab;

	// --- Editor hooks captured from Slate args, forwarded to child widgets ---
	FOnExtendShaderAssetContextMenu    OnExtendAssetContextMenuHook;
	FOnOpenShaderAssetInContentBrowser OnOpenAssetInContentBrowserHook;
	FOnNavigateToShaderAsset           OnNavigateToAssetHook;
	FOnFetchMaterialHierarchy          OnFetchMaterialHierarchyHook;

	// Track open session tabs to avoid duplicates (key = SessionId)
	TMap<int32, TWeakPtr<SDockTab>> OpenSessionTabs;

	// Browser tab
	TWeakPtr<SDockTab> BrowserTab;
	void SpawnBrowserTab();


	void HandleSessionsChanged();
};
