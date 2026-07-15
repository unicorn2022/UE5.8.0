// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SShaderAuditWidget.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "ShaderAuditSession.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SMaterialDiffTable.h"
#include "Widgets/SShaderSessionView.h"
#include "Widgets/SNasSHKBrowserPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ShaderAudit"

static const FName SessionDocTabType("ShaderAuditSessionDoc");

// ---------------------------------------------------------------------------
// Local helper: create a session document tab with a session view inside.
// Lives here (not on FShaderAuditSession) because it's pure UI.
// ---------------------------------------------------------------------------
static TSharedPtr<SDockTab> OpenSessionAsTab(
	TSharedPtr<FShaderAuditSession> InSession,
	TSharedPtr<FTabManager> InTabManager,
	FName TabType,
	const FOnExtendShaderAssetContextMenu& InOnExtendAssetContextMenu,
	const FOnOpenShaderAssetInContentBrowser& InOnOpenAssetInContentBrowser,
	const FOnFetchMaterialHierarchy& InOnFetchMaterialHierarchy,
	TFunction<void(TSharedPtr<FShaderAuditSession>)> OnActivated = nullptr,
	TFunction<void(int32)> OnClosed = nullptr)
{
	if (!InSession.IsValid() || !InTabManager.IsValid())
	{
		return nullptr;
	}

	const FString TabLabel = InSession->SessionName.IsEmpty()
		? FPaths::GetCleanFilename(InSession->Filename)
		: InSession->SessionName;

	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(ETabRole::DocumentTab)
		.Label(FText::FromString(TabLabel));

	Tab->SetContent(
		SNew(SShaderSessionView)
			.Session(InSession)
			.OnExtendAssetContextMenu(InOnExtendAssetContextMenu)
			.OnOpenAssetInContentBrowser(InOnOpenAssetInContentBrowser)
			.OnFetchMaterialHierarchy(InOnFetchMaterialHierarchy));

	if (OnActivated)
	{
		Tab->SetOnTabActivated(SDockTab::FOnTabActivatedCallback::CreateLambda(
			[OnActivated, InSession](TSharedRef<SDockTab>, ETabActivationCause) { OnActivated(InSession); }));
	}

	if (OnClosed)
	{
		const int32 SessionId = InSession->SessionId;
		Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda(
			[OnClosed, SessionId](TSharedRef<SDockTab>) { OnClosed(SessionId); }));
	}

	InTabManager->InsertNewDocumentTab(TabType, FTabManager::FLiveTabSearch(TabType), Tab);

	return Tab;
}

void SShaderAuditWidget::Construct(const FArguments& InArgs)
{
	SessionsChangedEvent = InArgs._OnSessionsChangedEvent;

	OnExtendAssetContextMenuHook    = InArgs._OnExtendAssetContextMenu;
	OnOpenAssetInContentBrowserHook = InArgs._OnOpenAssetInContentBrowser;
	OnNavigateToAssetHook           = InArgs._OnNavigateToAsset;
	OnFetchMaterialHierarchyHook    = InArgs._OnFetchMaterialHierarchy;

	// Create invisible host tab to anchor the local tab manager
	HostTab = SNew(SDockTab).TabRole(ETabRole::MajorTab);
	TabManager = FGlobalTabmanager::Get()->NewTabManager(HostTab.ToSharedRef());

	// Register placeholder spawner for the document tab stack
	TabManager->RegisterTabSpawner(SessionDocTabType,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&) -> TSharedRef<SDockTab>
		{
			return SNew(SDockTab).TabRole(ETabRole::DocumentTab);
		}));

	// Inner layout: single tab stack
	const TSharedRef<FTabManager::FLayout> Layout =
		FTabManager::NewLayout("ShaderAuditLayout_v3")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(1.0f)
				->SetHideTabWell(false)
				->AddTab(SessionDocTabType, ETabState::ClosedTab)
			)
		);

	const TSharedRef<SWidget> DockArea = TabManager->RestoreFrom(Layout, TSharedPtr<SWindow>()).ToSharedRef();

	ChildSlot
	[
		DockArea
	];

	// Subscribe to sessions-changed event for auto-spawning tabs
	if (SessionsChangedEvent)
	{
		SessionsChangedHandle = SessionsChangedEvent->AddSP(this, &SShaderAuditWidget::HandleSessionsChanged);
	}

	// Spawn the browser tab as the default landing page
	SpawnBrowserTab();

	// Spawn tabs for any sessions that were already loaded before this widget was created
	for (const TSharedPtr<FShaderAuditSession>& Session : FShaderAuditSession::GetSessions())
	{
		SpawnSessionTab(Session);
	}
}

SShaderAuditWidget::~SShaderAuditWidget()
{
	if (SessionsChangedEvent && SessionsChangedHandle.IsValid())
	{
		SessionsChangedEvent->Remove(SessionsChangedHandle);
	}
}

void SShaderAuditWidget::SpawnBrowserTab()
{
	// If already open, just focus it
	if (TSharedPtr<SDockTab> Existing = BrowserTab.Pin())
	{
		Existing->ActivateInParent(ETabActivationCause::SetDirectly);
		return;
	}

	if (!TabManager.IsValid())
	{
		return;
	}

	static constexpr const TCHAR* TabLabel = TEXT("Open SHK");

	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(ETabRole::DocumentTab)
		.Label(FText::FromString(TabLabel))
		[
			SNew(SNasSHKBrowserPanel)
				.OnSessionsLoaded(FOnSessionsLoaded::CreateLambda([this]()
				{
					HandleSessionsChanged();
				}))
		];

	TabManager->InsertNewDocumentTab(SessionDocTabType, FTabManager::ESearchPreference::PreferLiveTab, Tab);
	BrowserTab = Tab;
}

void SShaderAuditWidget::ShowBrowserTab()
{
	SpawnBrowserTab();
}

void SShaderAuditWidget::HandleSessionsChanged()
{
	// Auto-open the most recently loaded session
	TArray<TSharedPtr<FShaderAuditSession>>& Sessions = FShaderAuditSession::GetSessions();
	if (Sessions.Num() > 0)
	{
		SpawnSessionTab(Sessions.Last());
	}
}

void SShaderAuditWidget::SpawnSessionTab(TSharedPtr<FShaderAuditSession> Session)
{
	if (!Session.IsValid())
	{
		return;
	}

	// Check if a tab for this session is already open -- just activate it
	const int32 Key = Session->SessionId;
	if (TWeakPtr<SDockTab>* Existing = OpenSessionTabs.Find(Key))
	{
		if (TSharedPtr<SDockTab> Tab = Existing->Pin())
		{
			Tab->ActivateInParent(ETabActivationCause::SetDirectly);
			return;
		}
		// Tab was closed, remove stale entry
		OpenSessionTabs.Remove(Key);
	}


	TWeakPtr<SShaderAuditWidget> WeakThis(SharedThis(this));
	auto OnClosed = [WeakThis](int32 ClosedSessionId)
	{
		TSharedPtr<SShaderAuditWidget> This = WeakThis.Pin();
		if (!This.IsValid())
		{
			return;
		}
		This->OpenSessionTabs.Remove(ClosedSessionId);
		FShaderAuditSession::CloseSession(ClosedSessionId);
		if (This->SessionsChangedEvent.IsValid())
		{
			This->SessionsChangedEvent->Broadcast();
		}
	};

	TSharedPtr<SDockTab> NewTab = OpenSessionAsTab(Session, TabManager, SessionDocTabType,
		OnExtendAssetContextMenuHook, OnOpenAssetInContentBrowserHook,
		OnFetchMaterialHierarchyHook,
		nullptr, OnClosed);
	if (NewTab.IsValid())
	{
		OpenSessionTabs.Add(Key, NewTab);
	}
}

void SShaderAuditWidget::SpawnDiffTab(TSharedPtr<FShaderAuditSession> SessionA, TSharedPtr<FShaderAuditSession> SessionB)
{
	if (!TabManager.IsValid() || !SessionA.IsValid() || !SessionB.IsValid())
	{
		return;
	}

	const FString FilenameA = FPaths::GetCleanFilename(SessionA->Filename);
	const FString FilenameB = FPaths::GetCleanFilename(SessionB->Filename);
	const FString TabLabel = FString::Printf(TEXT("Diff: %s vs %s"), *FilenameA, *FilenameB);

	TSharedRef<SDockTab> NewTab = SNew(SDockTab)
		.TabRole(ETabRole::DocumentTab)
		.Label(FText::FromString(TabLabel))
		[
			SNew(SMaterialDiffTable)
				.SessionA(SessionA)
				.SessionB(SessionB)
				.OnNavigateToAsset(OnNavigateToAssetHook)
		];

	TabManager->InsertNewDocumentTab(SessionDocTabType, FTabManager::ESearchPreference::PreferLiveTab, NewTab);
}

void SShaderAuditWidget::ShowDiffPicker()
{
	TArray<TSharedPtr<FShaderAuditSession>>& Sessions = FShaderAuditSession::GetSessions();
	if (Sessions.Num() < 2)
	{
		return;
	}

	// Build list items for combo boxes
	TSharedPtr<TArray<TSharedPtr<FShaderAuditSession>>> ItemsA = MakeShared<TArray<TSharedPtr<FShaderAuditSession>>>(Sessions);
	TSharedPtr<TArray<TSharedPtr<FShaderAuditSession>>> ItemsB = MakeShared<TArray<TSharedPtr<FShaderAuditSession>>>(Sessions);

	TSharedPtr<TSharedPtr<FShaderAuditSession>> SelectedA = MakeShared<TSharedPtr<FShaderAuditSession>>(Sessions[0]);
	TSharedPtr<TSharedPtr<FShaderAuditSession>> SelectedB = MakeShared<TSharedPtr<FShaderAuditSession>>(Sessions.Num() > 1 ? Sessions[1] : Sessions[0]);

	auto MakeLabel = [](TSharedPtr<FShaderAuditSession> S) -> FText
	{
		if (!S.IsValid()) { return FText::GetEmpty(); }
		const FString Name = S->SessionName.IsEmpty()
			? FPaths::GetCleanFilename(S->Filename)
			: S->SessionName;
		const FString Dir = FPaths::GetPath(S->Filename);
		return FText::FromString(FString::Printf(TEXT("%s  (%s)"), *Name, *Dir));
	};

	TSharedPtr<SWindow> PickerWindow;

	PickerWindow = SNew(SWindow)
		.Title(LOCTEXT("DiffPickerTitle", "Diff Sessions"))
		.ClientSize(FVector2D(700, 160))
		.IsTopmostWindow(true)
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.SizingRule(ESizingRule::FixedSize)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12.f, 12.f, 12.f, 4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("SessionA", "Session A:"))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SComboBox<TSharedPtr<FShaderAuditSession>>)
					.OptionsSource(ItemsA.Get())
					.InitiallySelectedItem(*SelectedA)
					.OnSelectionChanged_Lambda([SelectedA](TSharedPtr<FShaderAuditSession> S, ESelectInfo::Type) { *SelectedA = S; })
					.OnGenerateWidget_Lambda([MakeLabel](TSharedPtr<FShaderAuditSession> S) { return SNew(STextBlock).Text(MakeLabel(S)); })
					.Content()
					[
						SNew(STextBlock).Text_Lambda([SelectedA, MakeLabel]() { return MakeLabel(*SelectedA); })
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12.f, 4.f, 12.f, 4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("SessionB", "Session B:"))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SComboBox<TSharedPtr<FShaderAuditSession>>)
					.OptionsSource(ItemsB.Get())
					.InitiallySelectedItem(*SelectedB)
					.OnSelectionChanged_Lambda([SelectedB](TSharedPtr<FShaderAuditSession> S, ESelectInfo::Type) { *SelectedB = S; })
					.OnGenerateWidget_Lambda([MakeLabel](TSharedPtr<FShaderAuditSession> S) { return SNew(STextBlock).Text(MakeLabel(S)); })
					.Content()
					[
						SNew(STextBlock).Text_Lambda([SelectedB, MakeLabel]() { return MakeLabel(*SelectedB); })
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(12.f, 12.f, 12.f, 12.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("DiffBtn", "Diff"))
					.IsEnabled_Lambda([SelectedA, SelectedB]() { return SelectedA->IsValid() && SelectedB->IsValid() && *SelectedA != *SelectedB; })
					.OnClicked_Lambda([this, SelectedA, SelectedB, &PickerWindow]() -> FReply
					{
						SpawnDiffTab(*SelectedA, *SelectedB);
						PickerWindow->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("CancelBtn", "Cancel"))
					.OnClicked_Lambda([&PickerWindow]() -> FReply
					{
						PickerWindow->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		];

	FSlateApplication::Get().AddModalWindow(PickerWindow.ToSharedRef(), FSlateApplication::Get().GetActiveTopLevelRegularWindow());
}

#undef LOCTEXT_NAMESPACE
