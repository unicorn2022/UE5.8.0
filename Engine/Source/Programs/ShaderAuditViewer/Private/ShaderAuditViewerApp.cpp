// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderAuditViewer.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/UICommandList.h"
#include "Styling/StarshipCoreStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SBoxPanel.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "ProjectUtilities.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SSeparator.h"

// ShaderAudit data model & top-level widget (provided by ShaderAuditCore module).
#include "ShaderAuditCore.h" // for LogShaderAudit
#include "ShaderAuditSession.h"
#include "ShaderBytecodeDatabase.h"
#include "Widgets/SShaderAuditWidget.h"


#include "TreeMapStyle.h"

#include "RequiredProgramMainCPPInclude.h"

#define LOCTEXT_NAMESPACE "ShaderAuditViewer"

IMPLEMENT_APPLICATION(ShaderAuditViewer, "ShaderAuditViewer");

DEFINE_LOG_CATEGORY_STATIC(LogShaderAuditViewer, Log, All);

// ============================================================================

// ============================================================================
// Main application
// ============================================================================

static int RunShaderAuditViewer(int32 ArgC, TCHAR* ArgV[])
{
	FTaskTagScope Scope(ETaskTag::EGameThread);
	ON_SCOPE_EXIT
	{
		LLM(FLowLevelMemTracker::Get().UpdateStatsPerFrame());
		RequestEngineExit(TEXT("Exiting"));
		FEngineLoop::AppPreExit();
		FModuleManager::Get().UnloadModulesAtShutdown();
		FEngineLoop::AppExit();
	};

	UE::ProjectUtilities::ParseProjectDirFromCommandline(ArgC, ArgV);

	if (int32 Ret = GEngineLoop.PreInit(ArgC, ArgV))
	{
		return Ret;
	}

	ProcessNewlyLoadedUObjects();
	FModuleManager::Get().StartProcessingNewlyLoadedObjects();

	FSlateApplication::InitializeAsStandaloneApplication(GetStandardStandaloneRenderer());
	FSlateApplication::InitHighDPI(true);

	FAppStyle::SetAppStyleSetName(FStarshipCoreStyle::GetCoreStyle().GetStyleSetName());

	// TreeMap module styles must be initialized before constructing any STreeMap widgets.
	FTreeMapStyle::Initialize();

	// Load ToolWidgets module so FDialogCommands is registered (needed by SCustomDialog)
	FModuleManager::Get().LoadModule("ToolWidgets");

	// --- Shared app state ---
	TArray<TSharedPtr<FShaderAuditSession>>& Sessions = FShaderAuditSession::GetSessions();
	TSharedPtr<FOnSessionsChanged> OnSessionsChanged = MakeShared<FOnSessionsChanged>();
	TSharedPtr<SShaderAuditWidget> AuditWidget;

	// --- Create the main window ---
	TSharedRef<SWindow> MainWindow = SNew(SWindow)
		.Title(LOCTEXT("AppTitle", "Shader Audit Viewer"))
		.ClientSize(FVector2D(1400, 900))
		.SupportsMinimize(true)
		.SupportsMaximize(true);

	// ========================================================================
	// Menu bar
	// ========================================================================
	TSharedRef<FUICommandList> CommandList = MakeShared<FUICommandList>();
	FMenuBarBuilder MenuBarBuilder(CommandList);

	// --- File menu ---
	MenuBarBuilder.AddPullDownMenu(
		LOCTEXT("FileMenu", "File"),
		LOCTEXT("FileMenuTooltip", "Open and manage SHK files"),
		FNewMenuDelegate::CreateLambda([&](FMenuBuilder& Builder)
		{
			Builder.AddMenuEntry(
				LOCTEXT("OpenSHK_Label", "Open SHK..."),
				LOCTEXT("OpenSHK_Tooltip", "Browse NAS and cached SHK files for analysis."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Open"),
				FExecuteAction::CreateLambda([&AuditWidget]()
				{
					if (AuditWidget.IsValid())
					{
						AuditWidget->ShowBrowserTab();
					}
				})
			);

			Builder.AddSeparator();

			Builder.AddMenuEntry(
				LOCTEXT("Exit_Label", "Exit"),
				LOCTEXT("Exit_Tooltip", "Exit Shader Audit Viewer"),
				FSlateIcon(),
				FExecuteAction::CreateLambda([]()
				{
					RequestEngineExit(TEXT("User requested exit"));
				})
			);
		})
	);

	// --- Session menu ---
	MenuBarBuilder.AddPullDownMenu(
		LOCTEXT("SessionMenu", "Session"),
		LOCTEXT("SessionMenuTooltip", "Load optional data for the active session"),
		FNewMenuDelegate::CreateLambda([&Sessions, &AuditWidget](FMenuBuilder& Builder)
		{
			Builder.AddMenuEntry(
				LOCTEXT("AttachShaderArchive_Label", "Attach Shader Archives..."),
				LOCTEXT("AttachShaderArchive_Tooltip", "Load .ushaderbytecode files into the active session."),
				FSlateIcon(),
				FExecuteAction::CreateLambda([&Sessions]()
				{
					// Use most recently loaded session
					TSharedPtr<FShaderAuditSession> Session = Sessions.Num() > 0 ? Sessions.Last() : nullptr;
					if (!Session.IsValid())
					{
						return;
					}

					IDesktopPlatform* const DesktopPlatform = FDesktopPlatformModule::Get();
					if (!DesktopPlatform)
					{
						return;
					}

					FString SelectedDir;
					if (DesktopPlatform->OpenDirectoryDialog(
						FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
						TEXT("Select shader archive directory"),
						FPaths::GetPath(Session->Filename),
						SelectedDir))
					{
						if (!Session->BytecodeDatabase.IsValid())
						{
							Session->BytecodeDatabase = MakeShared<FShaderBytecodeDatabase>();
						}
						Session->BytecodeDatabase->ImportDirectory(SelectedDir);
						UE_LOGF(LogShaderAuditViewer, Log, "Attached shader archives from '%ls'.", *SelectedDir);
					}
				})
			);

			Builder.AddSeparator();

			Builder.AddMenuEntry(
				LOCTEXT("DiffSessions_Label", "Diff Sessions..."),
				LOCTEXT("DiffSessions_Tooltip", "Compare two loaded sessions side by side."),
				FSlateIcon(),
				FExecuteAction::CreateLambda([&AuditWidget]()
				{
					if (AuditWidget.IsValid())
					{
						AuditWidget->ShowDiffPicker();
					}
				})
			);
		})
	);

	// ========================================================================
	// Assemble main window: menu bar + SShaderAuditWidget
	// ========================================================================
	MainWindow->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MenuBarBuilder.MakeWidget()
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SAssignNew(AuditWidget, SShaderAuditWidget)
				.OnSessionsChangedEvent(OnSessionsChanged)
		]
	);

	FSlateApplication::Get().AddWindow(MainWindow);

	// --- Load files from command line if provided ---
	FString SHKPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("-shk="), SHKPath))
	{
		TArray<FString> FilesToLoad;
		SHKPath.ParseIntoArray(FilesToLoad, TEXT("+"), true);

		if (FilesToLoad.Num() > 0)
		{
			// Expand siblings if a single file was provided
			if (FilesToLoad.Num() == 1)
			{
				FilesToLoad = FSessionFileInventory::FindSHKSiblings(FilesToLoad[0]);
			}
			FSessionFileInventory Inventory = FSessionFileInventory::Gather(MakeArrayView(FilesToLoad));
			TArray<TSharedPtr<FShaderAuditSession>> NewSessions = FShaderAuditSession::LoadFromInventory(Inventory);
			Sessions.Append(NewSessions);
			OnSessionsChanged->Broadcast();

			UE_LOGF(LogShaderAuditViewer, Log, "Loaded %d SHK file(s) from command line.", NewSessions.Num());
		}
	}

	// --- Main loop ---
	while (!IsEngineExitRequested())
	{
		BeginExitIfRequested();

		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		UE::Stats::FStats::AdvanceFrame(false);
		FTSTicker::GetCoreTicker().Tick(FApp::GetDeltaTime());
		FSlateApplication::Get().PumpMessages();
		FSlateApplication::Get().Tick();
		FPlatformProcess::Sleep(0.01f);

		GFrameCounter++;
	}

	FCoreDelegates::OnExit.Broadcast();
	FSlateApplication::Shutdown();

	return 0;
}

#undef LOCTEXT_NAMESPACE

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	return RunShaderAuditViewer(ArgC, ArgV);
}
