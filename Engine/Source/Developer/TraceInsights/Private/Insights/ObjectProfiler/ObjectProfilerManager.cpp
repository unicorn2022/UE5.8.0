// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectProfilerManager.h"

#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

// TraceServices
#include "Common/ProviderLock.h"
#include "TraceServices/Model/ObjectProvider.h"

// TraceInsights
#include "Insights/Common/InsightsMenuBuilder.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/ObjectProfiler/AssetInfoProvider.h"
#include "Insights/ObjectProfiler/EditorAssetInfoProvider.h"
#include "Insights/ObjectProfiler/IAssetInfoProvider.h"
#include "Insights/ObjectProfiler/ViewModels/ObjectTable.h"
#include "Insights/ObjectProfiler/Widgets/SObjectProfilerWindow.h"

#define LOCTEXT_NAMESPACE "UE::Insights::ObjectProfiler"

// For Object Insights in editor preview.
static bool GEnableObjectInsightsInEditor = true;
static FAutoConsoleVariableRef CVAREnableObjectInsights(
	TEXT("Trace.EnableObjectInsights"),
	GEnableObjectInsightsInEditor,
	TEXT("Enables the Object Insights feature in the Editor."),
	ECVF_Default
);

namespace UE::Insights::ObjectProfiler
{

DEFINE_LOG_CATEGORY(LogObjectProfiler);

TSharedPtr<FObjectProfilerManager> FObjectProfilerManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FObjectProfilerManager> FObjectProfilerManager::Get()
{
	return FObjectProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FObjectProfilerManager> FObjectProfilerManager::CreateInstance()
{
	ensure(!FObjectProfilerManager::Instance.IsValid());
	if (FObjectProfilerManager::Instance.IsValid())
	{
		FObjectProfilerManager::Instance.Reset();
	}

	FObjectProfilerManager::Instance = MakeShared<FObjectProfilerManager>(FInsightsManager::Get()->GetCommandList());

	return FObjectProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FObjectProfilerManager::FObjectProfilerManager(TSharedRef<FUICommandList> InCommandList)
	: CommandList(InCommandList)
	, ActionManager(this)
	, MajorTabId(ObjectProfilerTabId)
	, LogListingName("ObjectInsights")
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FObjectProfilerManager::Initialize(IUnrealInsightsModule& InsightsModule)
{
	ensure(!bIsInitialized);
	if (bIsInitialized)
	{
		return;
	}
	bIsInitialized = true;

	UE_LOGF(LogObjectProfiler, Log, "Initialize");

	// Register tick functions.
	OnTick = FTickerDelegate::CreateSP(this, &FObjectProfilerManager::Tick);
	OnTickHandle = FTSTicker::GetCoreTicker().AddTicker(OnTick, 0.0f);

	FObjectProfilerCommands::Register();
	BindCommands();

	InsightsModule.OnRegisterMajorTabExtension(MajorTabId);

	FInsightsManager::Get()->GetSessionChangedEvent().AddSP(this, &FObjectProfilerManager::OnSessionChanged);
	OnSessionChanged();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FObjectProfilerManager::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}
	bIsInitialized = false;

	// If the MessageLog module was already unloaded as part of the global Shutdown process, do not load it again.
	if (FModuleManager::Get().IsModuleLoaded("MessageLog"))
	{
		FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
		if (MessageLogModule.IsRegisteredLogListing(GetLogListingName()))
		{
			MessageLogModule.UnregisterLogListing(GetLogListingName());
		}
	}

	FInsightsManager::Get()->GetSessionChangedEvent().RemoveAll(this);

	FObjectProfilerCommands::Unregister();

	// Unregister tick function.
	FTSTicker::RemoveTicker(OnTickHandle);

	FObjectProfilerManager::Instance.Reset();

	UE_LOGF(LogObjectProfiler, Log, "Shutdown");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FObjectProfilerManager::~FObjectProfilerManager()
{
	ensure(!bIsInitialized);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FObjectProfilerManager::BindCommands()
{
	ActionManager.Map_ToggleObjectTableTreeViewVisibility();
	ActionManager.Map_ToggleObjectDetailsViewVisibility();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FObjectProfilerManager::RegisterMajorTabs(IUnrealInsightsModule& InsightsModule)
{
	bool bShouldRegister = true;

#if WITH_EDITOR
	bShouldRegister = GEnableObjectInsightsInEditor;
#endif

	if (!bIsTabRegistered && bShouldRegister)
	{
		bIsTabRegistered = true;
		ETabSpawnerMenuType::Type MenuType = ETabSpawnerMenuType::Enabled;
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Insights.EnableUEFNMode")))
		{
			if (CVar->GetBool())
			{
				MenuType = ETabSpawnerMenuType::Hidden;
			}
		}
		const FInsightsMajorTabConfig& Config = InsightsModule.FindMajorTabConfig(MajorTabId);
		if (Config.bIsAvailable)
		{
			// Register tab spawner for the Object Insights.
			FTabSpawnerEntry& TabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(MajorTabId,
					FOnSpawnTab::CreateRaw(this, &FObjectProfilerManager::SpawnTab),
					FCanSpawnTab::CreateRaw(this, &FObjectProfilerManager::CanSpawnTab))
				.SetDisplayName(Config.TabLabel.IsSet() ? Config.TabLabel.GetValue() : LOCTEXT("ObjectProfilerTabTitle", "Object Insights"))
				.SetTooltipText(Config.TabTooltip.IsSet() ? Config.TabTooltip.GetValue() : LOCTEXT("ObjectProfilerTooltipText", "Open the Object Insights tab."))
				.SetIcon(Config.TabIcon.IsSet() ? Config.TabIcon.GetValue() : FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.ObjectProfiler"))
				.SetMenuType(MenuType);

			if (MenuType != ETabSpawnerMenuType::Hidden)
			{
				TSharedRef<FWorkspaceItem> Group = Config.WorkspaceGroup.IsValid() ? Config.WorkspaceGroup.ToSharedRef() : FInsightsManager::Get()->GetInsightsMenuBuilder()->GetInsightsToolsGroup();
				TabSpawnerEntry.SetGroup(Group);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FObjectProfilerManager::UnregisterMajorTabs()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(MajorTabId);
	bIsTabRegistered = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FObjectProfilerManager::SpawnTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	// Register OnTabClosed to handle Object profiler manager shutdown.
	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FObjectProfilerManager::OnTabClosed));

	// Create the SObjectProfilerWindow widget.
	TSharedRef<SObjectProfilerWindow> Window = SNew(SObjectProfilerWindow, DockTab, Args.GetOwnerWindow());
	DockTab->SetContent(Window);

	AssignProfilerWindow(Window);

	// Apply any externally registered asset info provider (e.g. from UEFN MemoryViewExtender).
	IUnrealInsightsModule& InsightsModule = FModuleManager::GetModuleChecked<IUnrealInsightsModule>("TraceInsights");
	AssetInfoProvider = InsightsModule.GetAssetInfoProvider();

	if (!AssetInfoProvider)
	{
#if WITH_EDITOR
		AssetInfoProvider = MakeShared<FEditorAssetInfoProvider>();
#else
		AssetInfoProvider = MakeShared<FAssetInfoProvider>();
#endif
	}

	Window->SetAssetInfoProvider(AssetInfoProvider);

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FObjectProfilerManager::CanSpawnTab(const FSpawnTabArgs& Args) const
{
#if WITH_EDITOR
	return true;
#else
	return bIsAvailable;
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FObjectProfilerManager::OnTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	OnWindowClosedEvent();

	RemoveProfilerWindow();

	// Disable TabClosed delegate.
	TabBeingClosed->SetOnTabClosed(SDockTab::FOnTabClosedCallback());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedRef<FUICommandList> FObjectProfilerManager::GetCommandList() const
{
	return CommandList;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FObjectProfilerCommands& FObjectProfilerManager::GetCommands()
{
	return FObjectProfilerCommands::Get();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FObjectProfilerActionManager& FObjectProfilerManager::GetActionManager()
{
	return FObjectProfilerManager::Instance->ActionManager;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FObjectProfilerManager::Tick(float DeltaTime)
{
#if WITH_EDITOR
	if (!bIsTabRegistered && GEnableObjectInsightsInEditor)
	{
		IUnrealInsightsModule& TraceInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
		RegisterMajorTabs(TraceInsightsModule);
	}
#endif

	// Check if the session has UObject snapshots, but not too often.
	if (!bIsAvailable && AvailabilityCheck.Tick())
	{
		bool bShouldBeAvailable = false;

		TSharedPtr<FInsightsManager> InsightsManager = FInsightsManager::Get();
		check(InsightsManager.IsValid());

		TSharedPtr<const TraceServices::IAnalysisSession> Session = InsightsManager->GetSession();
		if (Session.IsValid())
		{
			{
				TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
				if (Session->IsAnalysisComplete())
				{
					// Never check again during this session.
					AvailabilityCheck.Disable();
				}
			}

			const TraceServices::IObjectProvider* ObjectProvider = TraceServices::ReadObjectProvider(*Session.Get());
			if (ObjectProvider)
			{
				TraceServices::FProviderReadScopeLock ProviderReadScope(*ObjectProvider);
				if (ObjectProvider->GetNumSnapshots() > 0)
				{
					bShouldBeAvailable = true;
				}
			}
		}
		else
		{
			// Do not check again until the next session changed event (see OnSessionChanged).
			AvailabilityCheck.Disable();
		}

		if (bShouldBeAvailable)
		{
			bIsAvailable = true;

			FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
			MessageLogModule.RegisterLogListing(GetLogListingName(), LOCTEXT("ObjectInsights", "Object Insights"));
			MessageLogModule.EnableMessageLogDisplay(true);

#if !WITH_EDITOR
			if (FGlobalTabmanager::Get()->HasTabSpawner(MajorTabId))
			{
				UE_LOGF(LogObjectProfiler, Log, "Opening the \"Object Insights\" tab...");
				FGlobalTabmanager::Get()->TryInvokeTab(MajorTabId);
			}
#endif
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FObjectProfilerManager::OnSessionChanged()
{
	UE_LOGF(LogObjectProfiler, Log, "OnSessionChanged");

	bIsAvailable = false;
	if (FInsightsManager::Get()->GetSession().IsValid())
	{
		AvailabilityCheck.Enable(0.5);
	}
	else
	{
		AvailabilityCheck.Disable();
	}

	TSharedPtr<SObjectProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd)
	{
		Wnd->Reset();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FObjectProfilerManager::ShowHideObjectTableTreeView(bool bIsVisible)
{
	bIsObjectTableTreeViewVisible = bIsVisible;

	TSharedPtr<SObjectProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd)
	{
		Wnd->ShowHideTab(FObjectProfilerTabs::ObjectTableTreeViewId, bIsVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FObjectProfilerManager::ShowHideObjectDetailsView(bool bIsVisible)
{
	bIsObjectDetailsViewVisible = bIsVisible;

	TSharedPtr<SObjectProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd)
	{
		Wnd->ShowHideTab(FObjectProfilerTabs::ObjectDetailsViewId, bIsVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FObjectProfilerManager::ShowHideSegmentedBarGraph(bool bIsVisible)
{
	bIsSegmentedBarGraphVisible = bIsVisible;

	TSharedPtr<SObjectProfilerWindow> Wnd = GetProfilerWindow();
	if (Wnd)
	{
		Wnd->ShowHideSegmentedBarGraph(bIsVisible);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FObjectProfilerManager::OnWindowClosedEvent()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FObjectProfilerManager::Exec(const TCHAR* Cmd, FOutputDevice& Ar)
{
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::ObjectProfiler

#undef LOCTEXT_NAMESPACE
