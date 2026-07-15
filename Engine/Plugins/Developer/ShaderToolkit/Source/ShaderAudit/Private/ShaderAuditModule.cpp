// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderAuditModule.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "MaterialValidationModule.h"
#include "Modules/ModuleManager.h"
#include "ShaderAuditCore.h" // for LogShaderAudit
#include "ShaderAuditEditorSubsystem.h"
#include "ShaderAuditSession.h"
#include "ShaderAuditUtils.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SShaderAuditWidget.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "ShaderAudit"

// ---------------------------------------------------------------------------
// Editor-side implementations of widget hooks.
//
// These free functions are bound as lambdas on the SShaderAuditWidget when the
// editor module spawns its tab. They use Engine/UnrealEd APIs (GEditor, AssetRegistry,
// MaterialValidation, ContentBrowser, DesktopPlatform). The widgets themselves remain
// editor-free.
// ---------------------------------------------------------------------------
namespace UE::ShaderAudit::Editor
{
	static void ExtendAssetContextMenu(FMenuBuilder& MenuBuilder, const FString& AssetPath)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("FindSimilarInstances", "Find Similar Instances..."),
			LOCTEXT("FindSimilarInstancesTip", "Open the similarity browser for this material's instance hierarchy"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([AssetPath]()
			{
				FMaterialValidationModule* Module = FModuleManager::GetModulePtr<FMaterialValidationModule>("MaterialValidation");
				if (!Module)
				{
					return;
				}
				FString ResolveError;
				UMaterialInterface* LoadedInterface = UE::ShaderAudit::Utils::ResolveMaterialPath(AssetPath, &ResolveError);
				if (!LoadedInterface)
				{
					UE_LOG(LogShaderAudit, Warning, TEXT("FindSimilarInstances: failed to resolve '%s': %s"), *AssetPath, *ResolveError);
					return;
				}
				Module->OpenSimilarityBrowser(*LoadedInterface);
			}))
		);
	}

	static void OpenAssetInContentBrowser(const FString& AssetPath)
	{
		// AssetPath is like "/Game/MyContent/Foo.Bar" -- strip the object suffix to get the package path.
		FString PackagePath = AssetPath;
		int32 DotIdx = INDEX_NONE;
		if (PackagePath.FindChar(TEXT('.'), DotIdx))
		{
			PackagePath = PackagePath.Left(DotIdx);
		}

		TArray<FAssetData> AssetDatas;
		IAssetRegistry::Get()->GetAssetsByPackageName(*PackagePath, AssetDatas);

		if (AssetDatas.Num() > 0)
		{
			GEditor->SyncBrowserToObjects(AssetDatas);
		}
	}

	static void NavigateToAsset(const FString& AssetPath, bool bCtrlDown)
	{
		const FAssetData AssetData = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()->FindAssetData(AssetPath);
		if (bCtrlDown)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(AssetData.GetAsset());
		}
		else
		{
			FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			ContentBrowserModule.Get().SyncBrowserToAssets({ AssetData });
		}
	}

	static void FetchMaterialHierarchy(TSharedPtr<FShaderAuditSession> Session)
	{
		if (!Session.IsValid())
		{
			return;
		}
		const UE::ShaderAudit::Utils::FMaterialParentMapResult MapResult = UE::ShaderAudit::Utils::BuildMaterialParentMap(FString());
		TMap<FString, FString> ParentMap;
		ParentMap.Reserve(MapResult.Pairs.Num());
		for (const UE::ShaderAudit::Utils::FMaterialParentEntry& Entry : MapResult.Pairs)
		{
			ParentMap.Add(Entry.Child, Entry.Parent);
		}
		Session->SetupMaterialParents(ParentMap);
	}

}


void FShaderAuditEditorModule::StartupModule()
{
	const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner("ShaderAudit", FOnSpawnTab::CreateRaw(this, &FShaderAuditEditorModule::SpawnToolTab))
		.SetDisplayName(LOCTEXT("ShaderAuditLabel", "Shader Audit"))
		.SetTooltipText(LOCTEXT("ShaderAuditTooltip", "Inspect the builds shaders to find regressions and optimize."))
		.SetGroup(MenuStructure.GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "MaterialEditor.ToggleMaterialStats.Tab"));
}

void FShaderAuditEditorModule::ShutdownModule()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner("ShaderAudit");
}

TSharedRef<SDockTab> FShaderAuditEditorModule::SpawnToolTab(const FSpawnTabArgs& Args)
{
	TSharedRef<FUICommandList> CommandList = MakeShared<FUICommandList>();
	FToolBarBuilder ToolbarBuilder(CommandList, FMultiBoxCustomization::None);

	ToolbarBuilder.AddToolBarButton(
		FExecuteAction::CreateLambda([this]()
			{
				if (AuditWidget.IsValid())
				{
					AuditWidget->ShowBrowserTab();
				}
			})
		, NAME_None
		, LOCTEXT("LoadSHK_Label", "Open SHK")
		, LOCTEXT("LoadSHK_Tooltip", "Browse NAS and cached SHK files.")
		, FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Open")
	);

	UShaderAuditEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UShaderAuditEditorSubsystem>();
	check(Subsystem);

	// Bridge: forward subsystem's session-loaded event to the widget's generic delegate
	TSharedPtr<FOnSessionsChanged> SessionsChanged = MakeShared<FOnSessionsChanged>();
	FDelegateHandle SessionLoadedHandle = Subsystem->OnSessionLoaded().AddLambda([SessionsChanged]() { SessionsChanged->Broadcast(); });

	ToolbarBuilder.AddToolBarButton(
		FExecuteAction::CreateLambda([this]()
		{
			if (AuditWidget.IsValid())
			{
				AuditWidget->ShowDiffPicker();
			}
		})
		, NAME_None
		, LOCTEXT("Diff_Label", "Diff Sessions")
		, LOCTEXT("Diff_Tooltip", "Compare two loaded sessions side by side.")
		, FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Compare")
	);

	TSharedRef<SDockTab> Tab = SNew(SDockTab).TabRole(NomadTab);
	Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda([SessionsChanged, SessionLoadedHandle](TSharedRef<SDockTab> ClosedTab)
	{
		ClosedTab->SetContent(SNullWidget::NullWidget); // destroy widget first
		if (UShaderAuditEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UShaderAuditEditorSubsystem>())
		{
			Sub->OnSessionLoaded().Remove(SessionLoadedHandle);
		}
	}));
	Tab->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			ToolbarBuilder.MakeWidget()
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(AuditWidget, SShaderAuditWidget)
				.OnSessionsChangedEvent(SessionsChanged)
				.OnExtendAssetContextMenu_Static(&UE::ShaderAudit::Editor::ExtendAssetContextMenu)
				.OnOpenAssetInContentBrowser_Static(&UE::ShaderAudit::Editor::OpenAssetInContentBrowser)
				.OnNavigateToAsset_Static(&UE::ShaderAudit::Editor::NavigateToAsset)
				.OnFetchMaterialHierarchy_Static(&UE::ShaderAudit::Editor::FetchMaterialHierarchy)
		]
	);

	return Tab;
}
IMPLEMENT_MODULE(FShaderAuditEditorModule, ShaderAudit)

#undef LOCTEXT_NAMESPACE
