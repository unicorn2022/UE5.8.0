// Copyright Epic Games, Inc. All Rights Reserved.

#include "SIoStoreDependencyViewer.h"
#include "CoreMinimal.h"
#include "IoStoreDependencyViewer.h"
#include "IoStoreConfigHelpers.h"
#include "SDependencyGraphPanel.h"
#include "SCloudDownloadDialog.h"
#include "IoStoreReaderAdapter.h"
#include "IoStoreReaderZenBuild.h"
#include "Containers/Ticker.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Misc/MessageDialog.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/LargeMemoryReader.h"
#include "IO/IoStore.h"
#include "IO/IoDispatcher.h"
#include "IO/IoContainerMeta.h"
#include "IO/Serialization/OnDemandContainerToc.h"
#include "Tasks/Task.h"

#define LOCTEXT_NAMESPACE "IoStoreDependencyViewer"

void SIoStoreDependencyViewer::Construct(const FArguments& InArgs)
{
	FString InitialPath = InArgs._InitialPath;

	// Initialize graph view mode options
	GraphViewModeOptions.Add(MakeShared<FString>(TEXT("Selected Asset Dependencies")));
	GraphViewModeOptions.Add(MakeShared<FString>(TEXT("All Assets")));
	GraphViewModeOptions.Add(MakeShared<FString>(TEXT("Container Dependencies")));

	ChildSlot
	[
		SNew(SVerticalBox)

		// Menu bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("LoadDirectory", "Load Directory"))
				.ToolTipText(LOCTEXT("LoadDirectoryTooltip", "Load IoStore containers from a cooked build directory"))
				.OnClicked(this, &SIoStoreDependencyViewer::OnLoadDirectoryClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Refresh", "Refresh"))
				.OnClicked(this, &SIoStoreDependencyViewer::OnRefreshClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Clear", "Clear"))
				.OnClicked(this, &SIoStoreDependencyViewer::OnClearClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("CloudDownload", "Cloud Download"))
				.ToolTipText(LOCTEXT("CloudDownloadTooltip", "Download builds from cloud storage"))
				.OnClicked(this, &SIoStoreDependencyViewer::OnCloudDownloadClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("LoadCloudBuild", "Load Cloud Build"))
				.ToolTipText(LOCTEXT("LoadCloudBuildTooltip", "Load a previously downloaded cloud build from download.ini"))
				.OnClicked(this, &SIoStoreDependencyViewer::OnLoadCloudBuildClicked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(16.0f, 2.0f, 2.0f, 2.0f)  // Extra left padding for separation
			[
				SNew(SButton)
				.Text(LOCTEXT("NavigateBack", "<"))
				.ToolTipText(LOCTEXT("NavigateBackTooltip", "Go to previous asset"))
				.OnClicked(this, &SIoStoreDependencyViewer::OnNavigateBackClicked)
				.IsEnabled(this, &SIoStoreDependencyViewer::CanNavigateBack)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("NavigateForward", ">"))
				.ToolTipText(LOCTEXT("NavigateForwardTooltip", "Go to next asset"))
				.OnClicked(this, &SIoStoreDependencyViewer::OnNavigateForwardClicked)
				.IsEnabled(this, &SIoStoreDependencyViewer::CanNavigateForward)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Right)
			.Padding(2.0f)
			[
				SAssignNew(StatusText, STextBlock)
				.Text(LOCTEXT("ReadyStatus", "Ready - Load a directory containing .utoc/.uondemandtoc files"))
			]
		]

		// Main content area
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			// Left panel: Asset list with search
			+ SSplitter::Slot()
			.Value(0.25f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SAssignNew(SearchBox, SSearchBox)
						.HintText(LOCTEXT("SearchHint", "Search by name, chunk ID, or package ID..."))
						.OnTextChanged(this, &SIoStoreDependencyViewer::OnSearchTextChanged)
					]
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4.0f)
					[
						SAssignNew(AssetListView, SListView<TSharedPtr<FIoStoreAssetInfo>>)
						.ListItemsSource(&FilteredAssets)
						.OnGenerateRow(this, &SIoStoreDependencyViewer::OnGenerateRowForAssetList)
						.OnSelectionChanged(this, &SIoStoreDependencyViewer::OnAssetListSelectionChanged)
						.SelectionMode(ESelectionMode::Single)
					]
				]
			]

			// Middle panel: Asset info and dependencies
			+ SSplitter::Slot()
			.Value(0.35f)
			[
				SNew(SVerticalBox)

				// Asset info section
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(8.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("AssetInfoHeader", "Asset Information"))
							.Font(FAppStyle::GetFontStyle("BoldFont"))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SAssignNew(AssetInfoText, STextBlock)
							.Text(LOCTEXT("NoAssetSelected", "Select an asset to view details"))
							.AutoWrapText(true)
						]
					]
				]

				// Referencers section
				+ SVerticalBox::Slot()
				.FillHeight(0.33f)
				.Padding(4.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ReferencersHeader", "Referencers (Assets that depend on this)"))
							.Font(FAppStyle::GetFontStyle("BoldFont"))
						]
						+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						[
							SAssignNew(ReferencersListView, SListView<TSharedPtr<FAssetDependency>>)
							.ListItemsSource(&CurrentReferencers)
							.OnGenerateRow(this, &SIoStoreDependencyViewer::OnGenerateRowForDependencyList)
							.OnSelectionChanged(this, &SIoStoreDependencyViewer::OnDependencyListSelectionChanged)
							.SelectionMode(ESelectionMode::Single)
							.OnContextMenuOpening(this, &SIoStoreDependencyViewer::OnReferencersContextMenuOpening)
						]
					]
				]

				// Hard dependencies section
				+ SVerticalBox::Slot()
				.FillHeight(0.33f)
				.Padding(4.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("HardDepsHeader", "Hard Dependencies"))
							.Font(FAppStyle::GetFontStyle("BoldFont"))
						]
						+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						[
							SAssignNew(HardDependenciesListView, SListView<TSharedPtr<FAssetDependency>>)
							.ListItemsSource(&CurrentHardDependencies)
							.OnGenerateRow(this, &SIoStoreDependencyViewer::OnGenerateRowForDependencyList)
							.OnSelectionChanged(this, &SIoStoreDependencyViewer::OnDependencyListSelectionChanged)
							.SelectionMode(ESelectionMode::Single)
							.OnContextMenuOpening(this, &SIoStoreDependencyViewer::OnHardDepsContextMenuOpening)
						]
					]
				]

				// Soft dependencies section
				+ SVerticalBox::Slot()
				.FillHeight(0.33f)
				.Padding(4.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SoftDepsHeader", "Soft Dependencies"))
							.Font(FAppStyle::GetFontStyle("BoldFont"))
						]
						+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						[
							SAssignNew(SoftDependenciesListView, SListView<TSharedPtr<FAssetDependency>>)
							.ListItemsSource(&CurrentSoftDependencies)
							.OnGenerateRow(this, &SIoStoreDependencyViewer::OnGenerateRowForDependencyList)
							.OnSelectionChanged(this, &SIoStoreDependencyViewer::OnDependencyListSelectionChanged)
							.SelectionMode(ESelectionMode::Single)
							.OnContextMenuOpening(this, &SIoStoreDependencyViewer::OnSoftDepsContextMenuOpening)
						]
					]
				]
			]

			// Right panel: Dependency graph with depth control
			+ SSplitter::Slot()
			.Value(0.4f)
			[
				SNew(SVerticalBox)

				// Graph depth control and toolbar - first row
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4.0f, 4.0f, 4.0f, 2.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ViewModeLabel", "View:"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.0f, 0.0f)
					[
						SAssignNew(GraphViewModeComboBox, SComboBox<TSharedPtr<FString>>)
						.OptionsSource(&GraphViewModeOptions)
						.OnGenerateWidget(this, &SIoStoreDependencyViewer::OnGenerateGraphViewModeWidget)
						.OnSelectionChanged(this, &SIoStoreDependencyViewer::OnGraphViewModeChanged)
						.Content()
						[
							SNew(STextBlock)
							.Text(this, &SIoStoreDependencyViewer::GetCurrentGraphViewModeText)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(10.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DepthLabel", "Graph Depth:"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.0f, 0.0f)
					[
						SNew(SSpinBox<int32>)
						.MinValue(1)
						.MaxValue(10)
						.Value(this, &SIoStoreDependencyViewer::GetCurrentDepth)
						.OnValueChanged(this, &SIoStoreDependencyViewer::OnDepthChanged)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(10.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ReferencerDepthLabel", "Referencer Depth:"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.0f, 0.0f)
					[
						SNew(SSpinBox<int32>)
						.MinValue(0)
						.MaxValue(10)
						.Value(this, &SIoStoreDependencyViewer::GetCurrentReferencerDepth)
						.OnValueChanged(this, &SIoStoreDependencyViewer::OnReferencerDepthChanged)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(10.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("ZoomToFit", "Zoom to Fit"))
						.OnClicked(this, &SIoStoreDependencyViewer::OnZoomToFitClicked)
					]
				]

				// Graph search - second row
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4.0f, 2.0f, 4.0f, 4.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("GraphSearchLabel", "Search:"))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(4.0f, 0.0f)
					[
						SNew(SSearchBox)
						.HintText(LOCTEXT("GraphSearchHint", "Regex pattern..."))
						.OnTextChanged(this, &SIoStoreDependencyViewer::OnGraphSearchTextChanged)
					]
				]

				// Dependency graph (node-based visualization)
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(4.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(0.0f)
					[
						SAssignNew(GraphPanel, SDependencyGraphPanel)
					]
				]
			]
		]
	];

	// Auto-load if path specified
	if (!InitialPath.IsEmpty())
	{
		RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateLambda(
			[this, InitialPath](double, float) -> EActiveTimerReturnType
			{
				// Clear all data before initial load
				ClearAllData();

				if (LoadFromDirectory(InitialPath))
				{
					// Asset list is updated internally before dialog closes
					LoadedBuildSource = FString::Printf(TEXT("Local: %s"), *InitialPath);
					StatusText->SetText(FText::FromString(FString::Printf(TEXT("Loaded: %s (%d assets)"),
						*InitialPath, AllAssets.Num())));
				}
				return EActiveTimerReturnType::Stop;
			}));
	}
}

SIoStoreDependencyViewer::~SIoStoreDependencyViewer()
{
	// Clear text widgets FIRST to prevent ICU shutdown crash
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::GetEmpty());
	}
	if (AssetInfoText.IsValid())
	{
		AssetInfoText->SetText(FText::GetEmpty());
	}

	// Clear list views
	if (AssetListView.IsValid())
	{
		AssetListView->ClearSelection();
	}
	if (HardDependenciesListView.IsValid())
	{
		HardDependenciesListView->ClearSelection();
	}
	if (SoftDependenciesListView.IsValid())
	{
		SoftDependenciesListView->ClearSelection();
	}
	if (GraphPanel.IsValid())
	{
		GraphPanel->RebuildGraph(nullptr, TMap<FPackageId, TSharedPtr<FIoStoreAssetInfo>>(), 0);
	}

	// Clean up resources explicitly before Slate cleanup
	AllAssets.Empty();
	FilteredAssets.Empty();
	CurrentHardDependencies.Empty();
	CurrentSoftDependencies.Empty();
	ChunkIdToAsset.Empty();
	PackageIdToAsset.Empty();
	CurrentSelectedAsset.Reset();

	// Release IoStore readers
	IoStoreReaders.Empty();
	MetaReader.Reset();

	// Clear widget references
	StatusText.Reset();
	AssetInfoText.Reset();
	AssetListView.Reset();
	HardDependenciesListView.Reset();
	SoftDependenciesListView.Reset();
	SearchBox.Reset();
	GraphPanel.Reset();
}

FReply SIoStoreDependencyViewer::OnLoadDirectoryClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	FString SelectedDirectory;
	const bool bOpened = DesktopPlatform->OpenDirectoryDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select Cooked Build Directory (e.g., .../WindowsClient/FortniteGame/Content/Paks)"),
		TEXT(""),
		SelectedDirectory
	);

	if (bOpened)
	{
		// Clear all data including stale cloud parameters before loading local directory
		ClearAllData();

		// LoadFromDirectory is synchronous with modal progress dialog
		// Asset list is updated internally before dialog closes
		if (LoadFromDirectory(SelectedDirectory))
		{
			LoadedBuildSource = FString::Printf(TEXT("Local: %s"), *SelectedDirectory);
			StatusText->SetText(FText::FromString(FString::Printf(TEXT("Loaded: %s (%d assets)"),
				*SelectedDirectory, AllAssets.Num())));
		}
		else
		{
			LoadedBuildSource.Empty();
			StatusText->SetText(LOCTEXT("LoadFailed", "Failed to load IoStore containers"));
		}
	}

	return FReply::Handled();
}

FReply SIoStoreDependencyViewer::OnRefreshClicked()
{
	UpdateAssetList();
	StatusText->SetText(FText::FromString(FString::Printf(TEXT("Refreshed - %d assets displayed"), FilteredAssets.Num())));
	return FReply::Handled();
}

FReply SIoStoreDependencyViewer::OnClearClicked()
{
	ClearAllData();
	StatusText->SetText(LOCTEXT("Cleared", "Cleared all data"));
	return FReply::Handled();
}

FReply SIoStoreDependencyViewer::OnCloudDownloadClicked()
{
	// Clear all data BEFORE opening dialog to release file handles
	// This prevents file locking errors when downloading a second build
	ClearAllData();

	// Create cloud download dialog
	TSharedRef<SWindow> CloudDownloadWindow = SNew(SWindow)
		.Title(LOCTEXT("CloudDownloadTitle", "Cloud Download"))
		.ClientSize(FVector2D(800, 600))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedRef<SCloudDownloadDialog> CloudDownloadDialog = SNew(SCloudDownloadDialog, CloudDownloadWindow);
	CloudDownloadWindow->SetContent(CloudDownloadDialog);

	FSlateApplication::Get().AddModalWindow(CloudDownloadWindow, FSlateApplication::Get().GetActiveTopLevelWindow());

	// If download was initiated and completed, auto-load the downloaded files
	if (CloudDownloadDialog->WasDownloadInitiated())
	{
		FString DownloadDir = CloudDownloadDialog->GetDownloadDirectory();
		if (!DownloadDir.IsEmpty() && FPaths::DirectoryExists(DownloadDir))
		{
			// Configure cloud build parameters based on whether partial downloads were used
			if (CloudDownloadDialog->WasPartialDownloadUsed())
			{
				UE_LOGF(LogIoStoreDependencyViewer, Display, "Configuring zen reader for partial downloads");
				SetCloudBuildParameters(
					CloudDownloadDialog->GetZenExePath(),
					CloudDownloadDialog->GetOidcExePath(),
					CloudDownloadDialog->GetCloudNamespace(),
					CloudDownloadDialog->GetCloudBucket(),
					CloudDownloadDialog->GetCloudBuildId(),
					CloudDownloadDialog->GetProxyUrl(),
					DownloadDir,  // Base download directory for calculating relative paths
					true  // bUsePartialDownloads
				);
			}
			else
			{
				// Full download - disable partial downloads to use standard FIoStoreReader
				UE_LOGF(LogIoStoreDependencyViewer, Display, "Full download - using standard IoStore reader");
				bUsePartialDownloads = false;
				ZenExePath.Empty();
				OidcExePath.Empty();
			}

			// Give zen.exe time to finish background operations before scanning for files
			// Prevents file sharing violations when zen is moving files to cache
			UE_LOGF(LogIoStoreDependencyViewer, Display, "Waiting for file system to stabilize...");
			FPlatformProcess::Sleep(1.0f);

			// Find the directory containing .utoc files (may be in subdirectory)
			FString ActualDir = FindDirectoryWithTocFiles(DownloadDir);
			if (!ActualDir.IsEmpty())
			{
				UE_LOGF(LogIoStoreDependencyViewer, Display, "Auto-loading downloaded files from: %ls", *ActualDir);

				// LoadFromDirectory is synchronous with modal progress dialog
				// Asset list is updated internally before dialog closes
				if (LoadFromDirectory(ActualDir))
				{
					// Show cloud build info in status
					if (bUsePartialDownloads && !CloudBuildId.IsEmpty())
					{
						LoadedBuildSource = FString::Printf(TEXT("Cloud: %s/%s/%s"),
							*CloudNamespace, *CloudBucket, *CloudBuildId);
						StatusText->SetText(FText::FromString(FString::Printf(TEXT("Loaded: %s/%s/%s (%d assets)"),
							*CloudNamespace, *CloudBucket, *CloudBuildId, AllAssets.Num())));
					}
					else
					{
						LoadedBuildSource = FString::Printf(TEXT("Cloud (full download): %s"), *ActualDir);
						StatusText->SetText(FText::FromString(FString::Printf(TEXT("Loaded: %s (%d assets)"),
							*ActualDir, AllAssets.Num())));
					}
				}
				else
				{
					LoadedBuildSource.Empty();
					StatusText->SetText(LOCTEXT("LoadFailed", "Failed to load IoStore containers"));
				}
			}
			else
			{
				UE_LOGF(LogIoStoreDependencyViewer, Warning, "No .utoc files found in download directory: %ls", *DownloadDir);
				StatusText->SetText(LOCTEXT("NoTocFiles", "No .utoc files found in download directory"));
			}
		}
	}

	return FReply::Handled();
}

FReply SIoStoreDependencyViewer::OnLoadCloudBuildClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	TArray<FString> OutFiles;
	const bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select download.ini from a previously downloaded cloud build"),
		TEXT(""),
		TEXT(""),
		TEXT("Configuration Files (*.ini)|*.ini"),
		EFileDialogFlags::None,
		OutFiles
	);

	if (bOpened && OutFiles.Num() > 0)
	{
		FString DownloadIniPath = OutFiles[0];
		UE_LOGF(LogIoStoreDependencyViewer, Display, "Loading cloud build from: %ls", *DownloadIniPath);

		// Read the download.ini file
		FConfigFile DownloadConfig;
		DownloadConfig.Read(DownloadIniPath);

		FString ConfigNamespace, ConfigBucket, ConfigBuildId, ConfigHost;
		FString ConfigBuildName, ConfigChangelist, ConfigPlatforms;
		FString PartialDownloadsEnabled;

		DownloadConfig.GetString(TEXT("CloudBuild"), TEXT("Namespace"), ConfigNamespace);
		DownloadConfig.GetString(TEXT("CloudBuild"), TEXT("BucketId"), ConfigBucket);
		DownloadConfig.GetString(TEXT("CloudBuild"), TEXT("BuildId"), ConfigBuildId);
		DownloadConfig.GetString(TEXT("CloudBuild"), TEXT("Host"), ConfigHost);
		DownloadConfig.GetString(TEXT("CloudBuild"), TEXT("BuildName"), ConfigBuildName);
		DownloadConfig.GetString(TEXT("CloudBuild"), TEXT("Changelist"), ConfigChangelist);
		DownloadConfig.GetString(TEXT("CloudBuild"), TEXT("Platforms"), ConfigPlatforms);
		DownloadConfig.GetString(TEXT("CloudBuild"), TEXT("PartialDownloadsEnabled"), PartialDownloadsEnabled);

		// Verify we have the required info
		if (ConfigNamespace.IsEmpty() || ConfigBucket.IsEmpty() || ConfigBuildId.IsEmpty())
		{
			UE_LOGF(LogIoStoreDependencyViewer, Error, "download.ini is missing required cloud build information");
			StatusText->SetText(LOCTEXT("InvalidDownloadIni", "Invalid download.ini - missing build information"));
			return FReply::Handled();
		}

		bool bIsPartialDownload = PartialDownloadsEnabled.Equals(TEXT("true"), ESearchCase::IgnoreCase);
		FString SelectedDirectory = FPaths::GetPath(DownloadIniPath);

		// Verify .utoc files exist
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		TArray<FString> UtocFiles;
		PlatformFile.FindFilesRecursively(UtocFiles, *SelectedDirectory, TEXT(".utoc"));

		if (UtocFiles.Num() == 0)
		{
			UE_LOGF(LogIoStoreDependencyViewer, Error, "No .utoc files found in directory: %ls", *SelectedDirectory);
			StatusText->SetText(LOCTEXT("NoTocFilesInCloudBuild", "No .utoc files found in cloud build directory"));
			return FReply::Handled();
		}

		// Clear all data before loading
		ClearAllData();

		// Try to load crypto.json from configured search paths (for encrypted builds)
		if (!ConfigBuildName.IsEmpty() && !ConfigPlatforms.IsEmpty())
		{
			// Parse CL number from build name (e.g., "Fortnite-Main-CL-12345" → "12345")
			FString ChangelistNumber = IoStoreConfig::ParseChangelistFromBuildName(ConfigBuildName);
			if (ChangelistNumber.IsEmpty())
			{
				// Fallback to ConfigChangelist if parsing fails (may be commit hash)
				ChangelistNumber = ConfigChangelist;
			}

			TArray<FString> Platforms;
			ConfigPlatforms.ParseIntoArray(Platforms, TEXT(","));
			for (FString& Platform : Platforms)
			{
				Platform.TrimStartAndEndInline();
				TMap<FString, FString> TemplateVars;
				TemplateVars.Add(TEXT("buildname"), ConfigBuildName);
				TemplateVars.Add(TEXT("branchname"), IoStoreConfig::ParseBranchFromBuildName(ConfigBuildName));
				TemplateVars.Add(TEXT("cl"), ChangelistNumber);
				TemplateVars.Add(TEXT("platform"), Platform);
				IoStoreConfig::TryLoadCryptoJsonFromSearchPaths(TemplateVars, SelectedDirectory);
			}
		}

		if (bIsPartialDownload)
		{
			UE_LOGF(LogIoStoreDependencyViewer, Display, "Loading partial download: %ls/%ls/%ls",
				*ConfigNamespace, *ConfigBucket, *ConfigBuildId);

			// Delete PartCache directory for fresh start
			FString PartCacheDir = FPaths::Combine(SelectedDirectory, TEXT("PartCache"));
			if (FPaths::DirectoryExists(PartCacheDir))
			{
				if (PlatformFile.DeleteDirectoryRecursively(*PartCacheDir))
				{
					UE_LOGF(LogIoStoreDependencyViewer, Display, "Cleaned up PartCache directory: %ls", *PartCacheDir);
				}
				else
				{
					UE_LOGF(LogIoStoreDependencyViewer, Warning, "Failed to delete PartCache directory: %ls", *PartCacheDir);
				}
			}

			// Find and validate zen.exe and OidcToken.exe
			FString LocalZenExePath;
			FString LocalOidcExePath;

			IoStoreConfig::FindZenExePath(LocalZenExePath);
			if (!IoStoreConfig::ValidateZenExe(LocalZenExePath, true))
			{
				return FReply::Handled();
			}

			IoStoreConfig::FindOidcTokenExePath(LocalOidcExePath, LocalZenExePath);
			IoStoreConfig::ValidateOidcTokenExe(LocalOidcExePath, true);

			// Configure cloud build parameters
			UE_LOGF(LogIoStoreDependencyViewer, Display, "Configuring zen reader for partial downloads");
			UE_LOGF(LogIoStoreDependencyViewer, Display, "  zen.exe: %ls", *LocalZenExePath);
			UE_LOGF(LogIoStoreDependencyViewer, Display, "  OidcToken.exe: %ls", *LocalOidcExePath);

			SetCloudBuildParameters(
				LocalZenExePath,
				LocalOidcExePath,
				ConfigNamespace,
				ConfigBucket,
				ConfigBuildId,
				ConfigHost,
				SelectedDirectory,  // Base download directory
				true  // bUsePartialDownloads
			);
		}
		else
		{
			UE_LOGF(LogIoStoreDependencyViewer, Display, "Loading full download: %ls/%ls/%ls",
				*ConfigNamespace, *ConfigBucket, *ConfigBuildId);
		}

		// LoadFromDirectory is synchronous with modal progress dialog
		if (LoadFromDirectory(SelectedDirectory))
		{
			if (bIsPartialDownload)
			{
				LoadedBuildSource = FString::Printf(TEXT("Cloud: %s/%s/%s"), *ConfigNamespace, *ConfigBucket, *ConfigBuildId);
				StatusText->SetText(FText::FromString(FString::Printf(TEXT("Loaded: %s/%s/%s (%d assets)"),
					*ConfigNamespace, *ConfigBucket, *ConfigBuildId, AllAssets.Num())));
			}
			else
			{
				LoadedBuildSource = FString::Printf(TEXT("Cloud (full download): %s/%s/%s"), *ConfigNamespace, *ConfigBucket, *ConfigBuildId);
				StatusText->SetText(FText::FromString(FString::Printf(TEXT("Loaded: %s/%s/%s (%d assets)"),
					*ConfigNamespace, *ConfigBucket, *ConfigBuildId, AllAssets.Num())));
			}
		}
		else
		{
			LoadedBuildSource.Empty();
			StatusText->SetText(LOCTEXT("LoadFailed", "Failed to load IoStore containers"));
		}
	}

	return FReply::Handled();
}

void SIoStoreDependencyViewer::ClearAllData()
{
	// Clear all data arrays
	AllAssets.Empty();
	FilteredAssets.Empty();
	CurrentHardDependencies.Empty();
	CurrentSoftDependencies.Empty();
	CurrentReferencers.Empty();
	CurrentSelectedAsset.Reset();
	ChunkIdToAsset.Empty();
	PackageIdToAsset.Empty();
	PackageIdToReferencers.Empty();
	CurrentSearchText.Empty();

	// Clear navigation history
	NavigationHistory.Empty();
	NavigationHistoryIndex = -1;

	// Close all file handles to release locks
	IoStoreReaders.Empty();
	IoStoreAdapters.Empty();
	MetaReader.Reset();

	// Clean up PartCache directory before resetting parameters
	// Zen really struggles with processing stale PartCache files, so delete them on clear
	if (!BaseDownloadDirectory.IsEmpty())
	{
		FString PartCacheDir = FPaths::Combine(BaseDownloadDirectory, TEXT("PartCache"));
		if (FPaths::DirectoryExists(PartCacheDir))
		{
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			if (PlatformFile.DeleteDirectoryRecursively(*PartCacheDir))
			{
				UE_LOGF(LogIoStoreDependencyViewer, Display, "Cleaned up PartCache directory: %ls", *PartCacheDir);
			}
			else
			{
				UE_LOGF(LogIoStoreDependencyViewer, Warning, "Failed to delete PartCache directory: %ls", *PartCacheDir);
			}
		}
	}

	// Reset cloud/partial download parameters to prevent stale state
	bUsePartialDownloads = false;
	ZenExePath.Empty();
	OidcExePath.Empty();
	CloudNamespace.Empty();
	CloudBucket.Empty();
	CloudBuildId.Empty();
	CloudProxyUrl.Empty();
	BaseDownloadDirectory.Empty();
	LoadedBuildSource.Empty();

	// Clear all UI widgets
	if (AssetListView.IsValid())
	{
		AssetListView->RequestListRefresh();
	}
	if (HardDependenciesListView.IsValid())
	{
		HardDependenciesListView->RequestListRefresh();
	}
	if (SoftDependenciesListView.IsValid())
	{
		SoftDependenciesListView->RequestListRefresh();
	}
	if (ReferencersListView.IsValid())
	{
		ReferencersListView->RequestListRefresh();
	}
	if (SearchBox.IsValid())
	{
		SearchBox->SetText(FText::GetEmpty());
	}
	if (GraphPanel.IsValid())
	{
		GraphPanel->RebuildGraph(nullptr, TMap<FPackageId, TSharedPtr<FIoStoreAssetInfo>>(), 0);
	}
}

bool SIoStoreDependencyViewer::LoadFromDirectory(const FString& DirectoryPath)
{
	if (!FPaths::DirectoryExists(DirectoryPath))
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Directory does not exist: %ls", *DirectoryPath);
		return false;
	}

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Loading IoStore containers from: %ls", *DirectoryPath);

	// Note: ClearAllData() is called by each caller before LoadFromDirectory()
	// This allows cloud download path to set parameters before loading

	// Load .umeta files first for filename resolution
	LoadMetaData(DirectoryPath);

	// Load standard .utoc containers (shows progress dialog internally)
	// This now also loads on-demand containers and updates the asset list before closing
	LoadAllContainersWithProgress(DirectoryPath);

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Loaded %d total assets/chunks", AllAssets.Num());

	return AllAssets.Num() > 0;
}

bool SIoStoreDependencyViewer::LoadMetaData(const FString& DirectoryPath)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Find all .umeta files
	TArray<FString> MetaFiles;
	PlatformFile.FindFilesRecursively(MetaFiles, *DirectoryPath, TEXT(".umeta"));

	if (MetaFiles.Num() == 0)
	{
		UE_LOGF(LogIoStoreDependencyViewer, Warning, "No .umeta files found in %ls", *DirectoryPath);
		return false;
	}

	// Load the first .umeta file found (they usually contain all mappings)
	for (const FString& MetaFile : MetaFiles)
	{
		UE_LOGF(LogIoStoreDependencyViewer, Display, "Loading metadata from: %ls", *MetaFile);

		TIoStatusOr<FIoContainerMetaReader> MetaResult = FIoContainerMetaReader::Load(MetaFile);
		if (MetaResult.IsOk())
		{
			MetaReader = MakeShared<FIoContainerMetaReader>(MetaResult.ConsumeValueOrDie());
			UE_LOGF(LogIoStoreDependencyViewer, Display, "Successfully loaded metadata");
			return true;
		}
		else
		{
			UE_LOGF(LogIoStoreDependencyViewer, Warning, "Failed to load metadata from %ls: %ls",
				*MetaFile, *MetaResult.Status().ToString());
		}
	}

	return false;
}

namespace
{
	/** Helper struct for container loading progress tracking */
	enum class EContainerLoadSeverity : uint8
	{
		Warning,
		Error
	};

	struct FContainerLoadError
	{
		FString ContainerName;
		FString ErrorMessage;
		EContainerLoadSeverity Severity;

		FContainerLoadError(const FString& InName, const FString& InMessage, EContainerLoadSeverity InSeverity)
			: ContainerName(InName), ErrorMessage(InMessage), Severity(InSeverity)
		{}
	};

	/** Helper class to manage progress dialog and UI updates during container loading */
	class FContainerLoadingProgress
	{
	public:
		FContainerLoadingProgress(int32 TotalContainers)
			: TotalTocs(TotalContainers)
			, bLoadingFinished(false)
			, bUserRequestedClose(false)
		{
			// Create progress window
			ProgressWindow = SNew(SWindow)
				.Title(FText::FromString(TEXT("Loading Containers")))
				.ClientSize(FVector2D(600, 400))
				.SupportsMaximize(false)
				.SupportsMinimize(false)
				.IsTopmostWindow(true);

			// Set window closed handler to prevent WaitForUserToClose() from hanging
			ProgressWindow->SetOnWindowClosed(FOnWindowClosed::CreateLambda([this](const TSharedRef<SWindow>&)
			{
				bUserRequestedClose = true;
			}));

			ProgressWindow->SetContent(
				SNew(SVerticalBox)
				// Progress text
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 20.0f, 20.0f, 10.0f)
				.VAlign(VAlign_Top)
				.HAlign(HAlign_Center)
				[
					SAssignNew(ProgressText, STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("Loading 0/%d containers (0 active)..."), TotalContainers)))
				]
				// Error/status list (scrollable)
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(20.0f, 0.0f, 20.0f, 10.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(5.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(ErrorText, STextBlock)
							.AutoWrapText(true)
							.Text(FText::FromString(TEXT("")))
						]
					]
				]
				// OK button (initially disabled)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(20.0f, 0.0f, 20.0f, 20.0f)
				.HAlign(HAlign_Center)
				[
					SAssignNew(OkButton, SButton)
					.Text(LOCTEXT("OkButton", "OK"))
					.IsEnabled(false)
					.OnClicked_Lambda([this]()
					{
						bUserRequestedClose = true;
						if (ProgressWindow.IsValid())
						{
							ProgressWindow->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
			);

			FSlateApplication::Get().AddWindow(ProgressWindow.ToSharedRef(), true);
			ProgressWindow->BringToFront();
			TickUI();
		}

		void SetProgressText(const FString& Text)
		{
			if (ProgressText.IsValid())
			{
				ProgressText->SetText(FText::FromString(Text));
			}
		}

		void AddError(const FString& ContainerName, const FString& ErrorMessage)
		{
			FScopeLock Lock(&ErrorsLock);
			LoadErrors.Add(FContainerLoadError(ContainerName, ErrorMessage, EContainerLoadSeverity::Error));
		}

		void AddWarning(const FString& ContainerName, const FString& WarningMessage)
		{
			FScopeLock Lock(&ErrorsLock);
			LoadErrors.Add(FContainerLoadError(ContainerName, WarningMessage, EContainerLoadSeverity::Warning));
		}

		void AddStatusMessage(const FString& Message)
		{
			FScopeLock Lock(&ErrorsLock);
			StatusLog.Add(Message);
		}

		void UpdateDisplay(int32 CompletedCount, int32 ActiveCount)
		{
			// Update progress text
			if (!bLoadingFinished)
			{
				// Count errors and warnings
				int32 ErrorCount = 0;
				int32 WarningCount = 0;
				{
					FScopeLock Lock(&ErrorsLock);
					for (const FContainerLoadError& LoadError : LoadErrors)
					{
						if (LoadError.Severity == EContainerLoadSeverity::Error)
						{
							ErrorCount++;
						}
						else
						{
							WarningCount++;
						}
					}
				}

				FString StatusSuffix;
				if (ErrorCount > 0 && WarningCount > 0)
				{
					StatusSuffix = FString::Printf(TEXT(", %d errors, %d warnings"), ErrorCount, WarningCount);
				}
				else if (ErrorCount > 0)
				{
					StatusSuffix = FString::Printf(TEXT(", %d errors"), ErrorCount);
				}
				else if (WarningCount > 0)
				{
					StatusSuffix = FString::Printf(TEXT(", %d warnings"), WarningCount);
				}

				SetProgressText(FString::Printf(
					TEXT("Loading %d/%d containers (%d active%s)..."),
					CompletedCount, TotalTocs, ActiveCount, *StatusSuffix));
			}

			// Update error/status log
			UpdateErrorStatusDisplay();

			// Enable OK button when finished
			if (bLoadingFinished && OkButton.IsValid())
			{
				OkButton->SetEnabled(true);
			}

			TickUI();
		}

		void SetFinished()
		{
			bLoadingFinished = true;
		}

		void TickUI()
		{
			const float DeltaTime = 0.016f;  // ~60 FPS
			FTSTicker::GetCoreTicker().Tick(DeltaTime);
			FSlateApplication::Get().PumpMessages();
			FSlateApplication::Get().Tick();
		}

		void WaitForUserToClose()
		{
			while (!bUserRequestedClose && ProgressWindow.IsValid())
			{
				TickUI();
				FPlatformProcess::Sleep(0.016f);
			}
		}

		bool AreAllTasksComplete(const TArray<UE::Tasks::FTask>& Tasks) const
		{
			for (const UE::Tasks::FTask& Task : Tasks)
			{
				if (!Task.IsCompleted())
				{
					return false;
				}
			}
			return true;
		}

		TSharedPtr<SWindow> GetWindow() const { return ProgressWindow; }
		int32 GetErrorCount() const { return LoadErrors.Num(); }
		int32 GetTotalContainers() const { return TotalTocs; }

	private:
		void UpdateErrorStatusDisplay()
		{
			if (!ErrorText.IsValid())
			{
				return;
			}

			FString LogDisplay;
			{
				FScopeLock Lock(&ErrorsLock);

				// Separate errors and warnings
				TArray<const FContainerLoadError*> Errors;
				TArray<const FContainerLoadError*> Warnings;
				for (const FContainerLoadError& LoadError : LoadErrors)
				{
					if (LoadError.Severity == EContainerLoadSeverity::Error)
					{
						Errors.Add(&LoadError);
					}
					else
					{
						Warnings.Add(&LoadError);
					}
				}

				// Show errors first
				if (Errors.Num() > 0)
				{
					LogDisplay += TEXT("=== ERRORS ===\n\n");
					for (const FContainerLoadError* Error : Errors)
					{
						LogDisplay += FString::Printf(TEXT("[%s]: %s\n\n"), *Error->ContainerName, *Error->ErrorMessage);
					}
				}

				// Show warnings
				if (Warnings.Num() > 0)
				{
					if (Errors.Num() > 0)
					{
						LogDisplay += TEXT("\n");
					}
					LogDisplay += TEXT("=== WARNINGS ===\n\n");
					for (const FContainerLoadError* Warning : Warnings)
					{
						LogDisplay += FString::Printf(TEXT("[%s]: %s\n\n"), *Warning->ContainerName, *Warning->ErrorMessage);
					}
				}

				// Show status messages
				if (StatusLog.Num() > 0)
				{
					if (Errors.Num() > 0 || Warnings.Num() > 0)
					{
						LogDisplay += TEXT("\n");
					}
					LogDisplay += TEXT("=== STATUS ===\n\n");
					for (const FString& StatusMsg : StatusLog)
					{
						LogDisplay += StatusMsg + TEXT("\n");
					}
				}

				// If nothing to show yet, show a placeholder
				if (LoadErrors.Num() == 0 && StatusLog.Num() == 0)
				{
					LogDisplay = TEXT("Loading...");
				}
			}
			ErrorText->SetText(FText::FromString(LogDisplay));
		}

		TSharedPtr<SWindow> ProgressWindow;
		TSharedPtr<STextBlock> ProgressText;
		TSharedPtr<STextBlock> ErrorText;
		TSharedPtr<SButton> OkButton;

		TArray<FContainerLoadError> LoadErrors;  // Contains both errors and warnings (differentiated by Severity field)
		TArray<FString> StatusLog;
		FCriticalSection ErrorsLock;

		int32 TotalTocs;
		bool bLoadingFinished;
		bool bUserRequestedClose;
	};
}

bool SIoStoreDependencyViewer::LoadAllContainersWithProgress(const FString& DirectoryPath)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Find all .utoc files
	TArray<FString> TocFiles;
	PlatformFile.FindFilesRecursively(TocFiles, *DirectoryPath, TEXT(".utoc"));

	if (TocFiles.Num() == 0)
	{
		UE_LOGF(LogIoStoreDependencyViewer, Display, "No .utoc files found in %ls, will try .uondemandtoc files", *DirectoryPath);
	}
	else
	{
		UE_LOGF(LogIoStoreDependencyViewer, Display, "Found %d .utoc files", TocFiles.Num());
	}

	// Only perform .utoc validation if we found .utoc files
	if (TocFiles.Num() > 0)
	{
		// Detect if this is a partial download directory (has .utoc but no .ucas files)
		// If user is trying to load a partial download via "Load Directory" instead of "Cloud Download",
		// show a helpful error instead of crashing
		if (!bUsePartialDownloads || ZenExePath.IsEmpty())
		{
			// Check if any .ucas files exist
			TArray<FString> UcasFiles;
			PlatformFile.FindFilesRecursively(UcasFiles, *DirectoryPath, TEXT(".ucas"));

			// Exclude PartCache directory from the count
			int32 RealUcasFiles = 0;
			for (const FString& UcasFile : UcasFiles)
			{
				if (!UcasFile.Contains(TEXT("PartCache")))
				{
					RealUcasFiles++;
				}
			}

			// If we have .utoc files but no .ucas files (excluding PartCache), this is a partial download
			if (RealUcasFiles == 0)
			{
				UE_LOGF(LogIoStoreDependencyViewer, Error, "This directory appears to contain a partial cloud download (no .ucas files found).");
				UE_LOGF(LogIoStoreDependencyViewer, Error, "Partial downloads cannot be loaded via 'Load Directory'.");
				UE_LOGF(LogIoStoreDependencyViewer, Error, "Please use 'Cloud Download' to reload this build, or download the full build (uncheck 'Use partial downloads').");

				// Show error dialog to user
				FText ErrorTitle = FText::FromString(TEXT("Cannot Load Partial Download"));
				FText ErrorMessage = FText::FromString(
					TEXT("This directory contains a partial cloud download (metadata only, no .ucas files).\n\n")
					TEXT("Partial downloads cannot be loaded via 'Load Directory'.\n\n")
					TEXT("Please use 'Cloud Download' to reload this build, or download the full build (uncheck 'Use partial downloads').")
				);
				FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage, ErrorTitle);

				return false;
			}
		}
	}

	// Read MaxConcurrentContainerLoads from config (default 20)
	int32 MaxConcurrentContainerLoads = 20;
	if (GConfig)
	{
		GConfig->GetInt(TEXT("IoStoreDependencyViewer"), TEXT("MaxConcurrentContainerLoads"), MaxConcurrentContainerLoads, GEngineIni);
		// Clamp to reasonable range [1, 100]
		MaxConcurrentContainerLoads = FMath::Clamp(MaxConcurrentContainerLoads, 1, 100);
	}

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Using MaxConcurrentContainerLoads = %d", MaxConcurrentContainerLoads);

	// Filter TOC files - skip ones with corresponding .uondemandtoc (game client behavior)
	TArray<FString> FilteredTocFiles;
	if (TocFiles.Num() > 0)
	{
		for (const FString& TocFile : TocFiles)
		{
			FString OnDemandTocPath = FPaths::ChangeExtension(TocFile, TEXT(".uondemandtoc"));
			if (PlatformFile.FileExists(*OnDemandTocPath))
			{
				UE_LOGF(LogIoStoreDependencyViewer, Display, "Skipping %ls (on-demand TOC exists: %ls)",
					*FPaths::GetCleanFilename(TocFile), *FPaths::GetCleanFilename(OnDemandTocPath));
				continue;
			}
			FilteredTocFiles.Add(TocFile);
		}
	}

	const int32 TotalTocs = FilteredTocFiles.Num();
	if (TotalTocs == 0)
	{
		UE_LOGF(LogIoStoreDependencyViewer, Display, "No standard containers to load after filtering");
	}

	// Load encryption keys from crypto.json files (if present)
	TMap<FGuid, FAES::FAESKey> EncryptionKeys;
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Searching for crypto.json files in: %ls", *DirectoryPath);
	bool bKeysLoaded = IoStoreConfig::LoadEncryptionKeysFromDirectory(DirectoryPath, EncryptionKeys);
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Loaded %d encryption key(s) from crypto.json", EncryptionKeys.Num());

	// Create progress dialog helper (even if TotalTocs is 0, we still need it for on-demand containers)
	FContainerLoadingProgress Progress(TotalTocs > 0 ? TotalTocs : 1);

	// Only load .utoc files if we have any after filtering
	if (TotalTocs > 0)
	{

	// Launch containers in parallel with concurrency limit to prevent system overload
	TArray<UE::Tasks::FTask> AllContainerTasks;  // All tasks for progress tracking
	TArray<UE::Tasks::FTask> ActiveTasks;  // Currently running tasks for concurrency control
	FCriticalSection AdaptersLock;  // Protect shared IoStoreAdapters array
	int32 NextTocIndex = 0;

	// Lambda to create a task for loading a container
	auto CreateLoadTask = [this, &DirectoryPath, &AdaptersLock, &Progress, &EncryptionKeys](const FString& TocFile) -> UE::Tasks::FTask
	{
		return UE::Tasks::Launch(TEXT("LoadContainer"), [this, TocFile, DirectoryPath, &AdaptersLock, &Progress, EncryptionKeys]()
		{
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			FString ContainerName = FPaths::GetBaseFilename(TocFile);

			// Helper lambda to report errors
			auto ReportError = [&Progress, &ContainerName](const FString& ErrorMsg)
			{
				Progress.AddError(ContainerName, ErrorMsg);
			};

			// Helper lambda to report warnings
			auto ReportWarning = [&Progress, &ContainerName](const FString& WarningMsg)
			{
				Progress.AddWarning(ContainerName, WarningMsg);
			};

			UE_LOGF(LogIoStoreDependencyViewer, Display, "Loading container: %ls", *TocFile);

			// First, validate the TOC version before attempting to load
			FIoStoreTocResource TocResource;
			FIoStatus TocStatus = FIoStoreTocResource::Read(*TocFile, EIoStoreTocReadOptions::Default, TocResource);

			if (!TocStatus.IsOk())
			{
				FString ErrorMsg = FString::Printf(TEXT("Failed to read TOC header: %s"), *TocStatus.ToString());
				UE_LOGF(LogIoStoreDependencyViewer, Error, "%ls - %ls", *ContainerName, *ErrorMsg);
				ReportError(ErrorMsg);
				return;
			}

			// Check if the version is compatible
			const uint8 TocVersion = TocResource.Header.Version;
			const uint8 LatestVersion = static_cast<uint8>(EIoStoreTocVersion::Latest);

			if (TocVersion > LatestVersion)
			{
				FString ErrorMsg = FString::Printf(
					TEXT("Incompatible IoStore version: Container version %d, Latest supported %d. ")
					TEXT("This file was created with a newer version of the engine."),
					TocVersion, LatestVersion);
				UE_LOGF(LogIoStoreDependencyViewer, Error, "%ls - %ls", *ContainerName, *ErrorMsg);
				ReportError(ErrorMsg);
				return;
			}
			else if (TocVersion < static_cast<uint8>(EIoStoreTocVersion::Initial))
			{
				FString ErrorMsg = FString::Printf(TEXT("Invalid IoStore version: %d"), TocVersion);
				UE_LOGF(LogIoStoreDependencyViewer, Error, "%ls - %ls", *ContainerName, *ErrorMsg);
				ReportError(ErrorMsg);
				return;
			}

			UE_LOGF(LogIoStoreDependencyViewer, Display, "  TOC Version: %d (Latest: %d)", TocVersion, LatestVersion);

			// Version is compatible, proceed with loading
			FString ContainerPath = FPaths::ChangeExtension(TocFile, TEXT(""));

			TSharedPtr<FIoStoreReaderAdapter> Adapter;

			if (bUsePartialDownloads && !ZenExePath.IsEmpty())
			{
				// Use zen build reader for on-demand downloads
				// Create a unique PartCache directory for this container
				FString PartCacheDir = FPaths::Combine(DirectoryPath, TEXT("PartCache"), ContainerName);

				UE_LOGF(LogIoStoreDependencyViewer, Display, "  Using partial downloads (zen build reader)");
				UE_LOGF(LogIoStoreDependencyViewer, Display, "  PartCache: %ls", *PartCacheDir);

				TUniquePtr<FIoStoreReaderZenBuild> ZenReader = MakeUnique<FIoStoreReaderZenBuild>();
				FIoStatus Status = ZenReader->Initialize(
					ContainerPath,
					EncryptionKeys,
					ZenExePath,
					OidcExePath,
					CloudNamespace,
					CloudBucket,
					CloudBuildId,
					CloudProxyUrl,
					BaseDownloadDirectory,
					PartCacheDir
				);

				if (!Status.IsOk())
				{
					// Check if this is an encryption error
					if (Status.ToString().Contains(TEXT("Missing decryption key")))
					{
						FString WarningMsg = TEXT("Container is encrypted but no decryption key is available.\n")
							TEXT("If this is a cloud build, ensure crypto.json search paths are configured in Engine.ini:\n")
							TEXT("[IoStoreDependencyViewer]\n")
							TEXT("+CryptoJsonSearchPath=<path_template>\n")
							TEXT("See IoStoreDependencyViewer.ini.example for configuration details.");
						UE_LOGF(LogIoStoreDependencyViewer, Warning, "%ls - %ls", *ContainerName, *WarningMsg);
						ReportWarning(WarningMsg);
					}
					else
					{
						FString ErrorMsg = FString::Printf(TEXT("Failed to initialize zen build reader: %s"), *Status.ToString());
						UE_LOGF(LogIoStoreDependencyViewer, Warning, "%ls - %ls", *ContainerName, *ErrorMsg);
						ReportError(ErrorMsg);
					}
					return;
				}

				Adapter = MakeShared<FIoStoreReaderAdapter>(MoveTemp(ZenReader));
			}
			else
			{
				// Standard file-based reader
				TUniquePtr<FIoStoreReader> StandardReader = MakeUnique<FIoStoreReader>();
				FIoStatus Status = StandardReader->Initialize(*ContainerPath, EncryptionKeys);

				if (!Status.IsOk())
				{
					// Check if this is an encryption error
					if (Status.ToString().Contains(TEXT("Missing decryption key")))
					{
						FString WarningMsg = TEXT("Container is encrypted but no decryption key is available.\n")
							TEXT("If this is a cloud build, ensure crypto.json search paths are configured in Engine.ini:\n")
							TEXT("[IoStoreDependencyViewer]\n")
							TEXT("+CryptoJsonSearchPath=<path_template>\n")
							TEXT("See IoStoreDependencyViewer.ini.example for configuration details.");
						UE_LOGF(LogIoStoreDependencyViewer, Warning, "%ls - %ls", *ContainerName, *WarningMsg);
						ReportWarning(WarningMsg);
					}
					else
					{
						FString ErrorMsg = FString::Printf(TEXT("Failed to load container: %s"), *Status.ToString());
						UE_LOGF(LogIoStoreDependencyViewer, Warning, "%ls - %ls", *ContainerName, *ErrorMsg);
						ReportError(ErrorMsg);
					}
					return;
				}

				Adapter = MakeShared<FIoStoreReaderAdapter>(MoveTemp(StandardReader));
			}

			UE_LOGF(LogIoStoreDependencyViewer, Display, "Container: %ls, Chunks: %d",
				*ContainerName, Adapter->GetChunkCount());

			// Enumerate all chunks in this container
			Adapter->EnumerateChunks([this, &ContainerName](FIoStoreTocChunkInfo&& ChunkInfo)
			{
				TSharedPtr<FIoStoreAssetInfo> Asset = MakeShared<FIoStoreAssetInfo>();
				Asset->ChunkId = ChunkInfo.Id;
				Asset->ContainerName = ContainerName;
				Asset->Size = ChunkInfo.Size;
				Asset->CompressedSize = ChunkInfo.CompressedSize;
				Asset->bIsCompressed = ChunkInfo.bIsCompressed;
				Asset->PartitionIndex = ChunkInfo.PartitionIndex;

				// Filename resolution:
				// - Package chunks: resolved later in ResolveDependencyNames() via PackageId lookup
				// - Non-package chunks (bulk data, shaders): use ChunkInfo.FileName or fallback to chunk ID
				// If no filename available, use chunk ID as filename
				if (Asset->FileName.IsEmpty())
				{
					Asset->FileName = ChunkInfo.FileName;
					if (Asset->FileName.IsEmpty())
					{
						Asset->FileName = FString::Printf(TEXT("Chunk_%s"), *LexToString(ChunkInfo.Id));
					}
				}

				// Thread-safe add to shared asset arrays
				{
					FScopeLock Lock(&SharedDataLock);
					AllAssets.Add(Asset);
					ChunkIdToAsset.Add(Asset->ChunkId, Asset);
				}

				return true; // Continue enumeration
			});

		// Try to load and parse the container header for package dependencies
		FIoChunkId ContainerHeaderChunkId = CreateIoChunkId(Adapter->GetContainerId().Value(), 0, EIoChunkType::ContainerHeader);
		TIoStatusOr<FIoBuffer> HeaderBuffer = Adapter->Read(ContainerHeaderChunkId, FIoReadOptions());
		if (HeaderBuffer.IsOk())
		{
			FIoContainerHeader Header;
			FMemoryReaderView HeaderReader(HeaderBuffer.ValueOrDie().GetView());
			HeaderReader << Header;

			// Load soft package references if they exist
			// The default serialization skips them, so we need to manually seek and load
			if (!HeaderReader.IsError() && !HeaderReader.IsCriticalError() && Header.SoftPackageReferencesSerialInfo.Size > 0)
			{
				// Check for integer overflow in offset + size calculation
				const int64 Offset64 = Header.SoftPackageReferencesSerialInfo.Offset;
				const int64 Size64 = Header.SoftPackageReferencesSerialInfo.Size;
				const int64 EndOffset = Offset64 + Size64;
				if (Offset64 >= 0 && Size64 > 0 && EndOffset >= Offset64 && EndOffset <= HeaderReader.TotalSize())
				{
					HeaderReader.Seek(Header.SoftPackageReferencesSerialInfo.Offset);
					HeaderReader << Header.SoftPackageReferences;

					if (!HeaderReader.IsError() && !HeaderReader.IsCriticalError())
					{
						UE_LOGF(LogIoStoreDependencyViewer, Display, "  Loaded soft package references: %d packages",
							Header.SoftPackageReferences.PackageIds.Num());
					}
				}
			}

			// Only process header if serialization succeeded
			if (!HeaderReader.IsError() && !HeaderReader.IsCriticalError())
			{
				// Thread-safe processing (modifies PackageIdToAsset and dependency maps)
				FScopeLock Lock(&SharedDataLock);
				ProcessContainerHeader(Header, ContainerName);
				UE_LOGF(LogIoStoreDependencyViewer, Display, "    Container header loaded: %d packages", Header.PackageIds.Num());
			}
			else
			{
				UE_LOGF(LogIoStoreDependencyViewer, Error, "    Failed to deserialize container header for %ls", *ContainerName);
			}
		}
		else
		{
			UE_LOGF(LogIoStoreDependencyViewer, Warning, "    Failed to read container header for %ls: %ls",
				*ContainerName, *HeaderBuffer.Status().ToString());
		}

		// Thread-safe add to shared adapters array
		{
			FScopeLock Lock(&AdaptersLock);
			IoStoreAdapters.Add(MoveTemp(Adapter));
		}
		}); // End of task lambda
	}; // End of CreateLoadTask lambda

	// Launch tasks in batches to limit concurrent zen requests
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Loading %d containers with max %d concurrent loads...",
		TotalTocs, MaxConcurrentContainerLoads);

	while (NextTocIndex < TotalTocs || ActiveTasks.Num() > 0)
	{
		// Launch new tasks up to the concurrency limit
		while (NextTocIndex < TotalTocs && ActiveTasks.Num() < MaxConcurrentContainerLoads)
		{
			const FString& TocFile = FilteredTocFiles[NextTocIndex++];
			UE::Tasks::FTask Task = CreateLoadTask(TocFile);
			AllContainerTasks.Add(Task);
			ActiveTasks.Add(Task);
		}

		// Remove completed tasks from active list
		for (int32 i = ActiveTasks.Num() - 1; i >= 0; --i)
		{
			if (ActiveTasks[i].IsCompleted())
			{
				ActiveTasks.RemoveAtSwap(i);
			}
		}

		// Update UI with current progress and errors
		int32 CompletedCount = AllContainerTasks.Num() - ActiveTasks.Num();
		Progress.UpdateDisplay(CompletedCount, ActiveTasks.Num());

		// Small sleep to prevent tight loop burning CPU
		FPlatformProcess::Sleep(0.016f);  // ~60 FPS
	}

	// Wait for all tasks to complete their work (with UI updates)
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Waiting for all %d container tasks to complete...", AllContainerTasks.Num());

	Progress.SetProgressText(FString::Printf(TEXT("Finalizing %d containers..."), TotalTocs));
	Progress.TickUI();

	while (!Progress.AreAllTasksComplete(AllContainerTasks))
	{
		Progress.TickUI();
		FPlatformProcess::Sleep(0.016f);
	}

	UE_LOGF(LogIoStoreDependencyViewer, Display, "All container tasks completed. Total assets loaded: %d", AllAssets.Num());

	// All tasks launched and completed - add completion status
	if (Progress.GetErrorCount() == 0)
	{
		Progress.AddStatusMessage(TEXT("All containers loaded successfully!"));
	}
	else
	{
		Progress.AddStatusMessage(FString::Printf(TEXT("Loaded %d containers with %d errors."),
			TotalTocs, Progress.GetErrorCount()));
	}

	// Update UI to show container loading completion
	Progress.SetProgressText(FString::Printf(
		TEXT("Loaded %d containers (%d errors). Resolving dependency names..."),
		TotalTocs, Progress.GetErrorCount()));
	Progress.TickUI();

	UE_LOGF(LogIoStoreDependencyViewer, Display, "All containers loaded, resolving dependency names...");

	// Resolve dependency names (this tracks the count internally)
	int32 TotalDeps = 0;
	int32 ResolvedCount = 0;

	// Count total dependencies
	for (const TSharedPtr<FIoStoreAssetInfo>& Asset : AllAssets)
	{
		TotalDeps += Asset->HardDependencies.Num() + Asset->SoftDependencies.Num();
	}

	// Iterate through all assets and resolve their dependency names
	int32 ProcessedAssets = 0;
	for (const TSharedPtr<FIoStoreAssetInfo>& Asset : AllAssets)
	{
		ProcessedAssets++;

		// Periodically update UI during name resolution (every 100 assets)
		if (ProcessedAssets % 100 == 0)
		{
			float ProgressPercent = TotalDeps > 0 ? (100.0f * ResolvedCount / TotalDeps) : 0.0f;
			Progress.SetProgressText(FString::Printf(
				TEXT("Loaded %d containers (%d errors)\nResolving names: %d/%d assets (%.1f%% resolved)"),
				Progress.GetTotalContainers(), Progress.GetErrorCount(), ProcessedAssets, AllAssets.Num(), ProgressPercent));
			Progress.TickUI();
		}

		// Resolve hard dependencies
		for (FAssetDependency& HardDep : Asset->HardDependencies)
		{
			TSharedPtr<FIoStoreAssetInfo>* DepAssetPtr = PackageIdToAsset.Find(HardDep.PackageId);
			if (DepAssetPtr && DepAssetPtr->IsValid())
			{
				HardDep.PackageName = (*DepAssetPtr)->FileName;
				ResolvedCount++;
			}
			else if (MetaReader.IsValid())
			{
				TUtf8StringBuilder<256> FilenameBuilder;
				FUtf8StringView ResolvedContainerName;
				FIoChunkId PackageChunkId = CreatePackageDataChunkId(HardDep.PackageId);
				FUtf8StringView Filename = MetaReader->GetFilename(PackageChunkId, FilenameBuilder, ResolvedContainerName);
				if (!Filename.IsEmpty())
				{
					HardDep.PackageName = FString(Filename);
					ResolvedCount++;
				}
				else
				{
					HardDep.PackageName = LexToString(HardDep.PackageId);
				}
			}
			else
			{
				HardDep.PackageName = LexToString(HardDep.PackageId);
			}
		}

		// Resolve soft dependencies
		for (FAssetDependency& SoftDep : Asset->SoftDependencies)
		{
			TSharedPtr<FIoStoreAssetInfo>* DepAssetPtr = PackageIdToAsset.Find(SoftDep.PackageId);
			if (DepAssetPtr && DepAssetPtr->IsValid())
			{
				SoftDep.PackageName = (*DepAssetPtr)->FileName;
				ResolvedCount++;
			}
			else if (MetaReader.IsValid())
			{
				TUtf8StringBuilder<256> FilenameBuilder;
				FUtf8StringView ResolvedContainerName;
				FIoChunkId PackageChunkId = CreatePackageDataChunkId(SoftDep.PackageId);
				FUtf8StringView Filename = MetaReader->GetFilename(PackageChunkId, FilenameBuilder, ResolvedContainerName);
				if (!Filename.IsEmpty())
				{
					SoftDep.PackageName = FString(Filename);
					ResolvedCount++;
				}
				else
				{
					SoftDep.PackageName = LexToString(SoftDep.PackageId);
				}
			}
			else
			{
				SoftDep.PackageName = LexToString(SoftDep.PackageId);
			}
		}
	}

	// Also resolve names in the reverse dependency map (referencers)
	for (auto& Pair : PackageIdToReferencers)
	{
		for (FAssetDependency& Referencer : Pair.Value)
		{
			TSharedPtr<FIoStoreAssetInfo>* RefAssetPtr = PackageIdToAsset.Find(Referencer.PackageId);
			if (RefAssetPtr && RefAssetPtr->IsValid())
			{
				Referencer.PackageName = (*RefAssetPtr)->FileName;
			}
			else if (MetaReader.IsValid())
			{
				TUtf8StringBuilder<256> FilenameBuilder;
				FUtf8StringView ResolvedContainerName;
				FIoChunkId PackageChunkId = CreatePackageDataChunkId(Referencer.PackageId);
				FUtf8StringView Filename = MetaReader->GetFilename(PackageChunkId, FilenameBuilder, ResolvedContainerName);
				if (!Filename.IsEmpty())
				{
					Referencer.PackageName = FString(Filename);
				}
				else
				{
					Referencer.PackageName = LexToString(Referencer.PackageId);
				}
			}
			else
			{
				Referencer.PackageName = LexToString(Referencer.PackageId);
			}
		}
	}

	float ResolvePercent = TotalDeps > 0 ? (100.0f * ResolvedCount / TotalDeps) : 0.0f;
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Resolved %d out of %d dependency names (%.1f%%)",
		ResolvedCount, TotalDeps, ResolvePercent);

		// Add name resolution status to log
		Progress.AddStatusMessage(FString::Printf(TEXT("Resolved %d out of %d dependency names (%.1f%%)"),
			ResolvedCount, TotalDeps, ResolvePercent));
	} // End of if (TotalTocs > 0) - standard container loading complete

	// Load on-demand containers while dialog is still up (always called, even if no .utoc files)
	Progress.SetProgressText(TEXT("Loading on-demand containers..."));
	Progress.TickUI();

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Loading on-demand containers...");
	LoadOnDemandContainers(DirectoryPath, EncryptionKeys);

	// Update the asset list with all loaded assets
	Progress.SetProgressText(FString::Printf(TEXT("Updating asset list (%d assets)..."), AllAssets.Num()));
	Progress.TickUI();

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Updating asset list with %d total assets...", AllAssets.Num());
	UpdateAssetList();

	// All done - enable OK button and update final status
	Progress.SetFinished();
	Progress.SetProgressText(FString::Printf(
		TEXT("Complete! Loaded %d total assets. Click OK to continue."), AllAssets.Num()));
	Progress.UpdateDisplay(0, 0);  // Force final UI update to enable OK button

	UE_LOGF(LogIoStoreDependencyViewer, Display, "All loading complete, waiting for user to close dialog");

	// Keep window open and pump messages until user clicks OK
	Progress.WaitForUserToClose();

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Container loading dialog closed");

	// Clear download caches now that loading is complete to free memory
	for (const TSharedPtr<FIoStoreReaderAdapter>& Adapter : IoStoreAdapters)
	{
		if (Adapter.IsValid() && Adapter->GetZenReader())
		{
			Adapter->GetZenReader()->ClearCache();
		}
	}

	return true;
}

bool SIoStoreDependencyViewer::LoadOnDemandContainers(const FString& DirectoryPath, const TMap<FGuid, FAES::FAESKey>& EncryptionKeys)
{
	using namespace UE::IoStore::Serialization;
	using namespace UE::IoStore::Serialization::V1;
	using FChunkEntry = UE::IoStore::Serialization::V1::FOnDemandChunkEntry;

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Find all .uondemandtoc files
	TArray<FString> OnDemandTocFiles;
	PlatformFile.FindFilesRecursively(OnDemandTocFiles, *DirectoryPath, TEXT(".uondemandtoc"));

	if (OnDemandTocFiles.Num() == 0)
	{
		UE_LOGF(LogIoStoreDependencyViewer, Display, "No .uondemandtoc files found");
		return false;
	}

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Found %d .uondemandtoc files", OnDemandTocFiles.Num());

	// Launch all on-demand TOCs in parallel
	TArray<UE::Tasks::FTask> OnDemandTasks;

	for (const FString& TocFile : OnDemandTocFiles)
	{
		UE::Tasks::FTask Task = UE::Tasks::Launch(TEXT("LoadOnDemandTOC"), [this, TocFile, DirectoryPath]()
		{
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

			UE_LOGF(LogIoStoreDependencyViewer, Display, "Loading on-demand TOC: %ls", *TocFile);

		// Use FOnDemandTocReader which handles the new serialization format
		TIoStatusOr<FOnDemandTocReader> MaybeReader = FOnDemandTocReader::Read(TocFile);
		if (!MaybeReader.IsOk())
		{
			UE_LOGF(LogIoStoreDependencyViewer, Warning, "Failed to load on-demand TOC %ls: %ls",
				*TocFile, *MaybeReader.Status().ToString());
			return;
		}

		FOnDemandTocReader Reader = MaybeReader.ConsumeValueOrDie();
		UE_LOGF(LogIoStoreDependencyViewer, Display, "On-demand TOC loaded: %d containers", Reader.Containers().Num());

		// Process each container in the TOC
		for (const FOnDemandContainerEntry& ContainerEntry : Reader.Containers())
		{
			FOnDemandTocStorage Storage;
			TIoStatusOr<FOnDemandContainerTocView> MaybeContainerView =
				Reader.ReadContainer(ContainerEntry, Storage, EOnDemandTocReaderOptions::None);

			if (!MaybeContainerView.IsOk())
			{
				UE_LOGF(LogIoStoreDependencyViewer, Warning, "Failed to read container: %ls",
					*MaybeContainerView.Status().ToString());
				continue;
			}

			FOnDemandContainerTocView ContainerView = MaybeContainerView.ConsumeValueOrDie();
			FString ContainerName = FString(ContainerView.Header.ContainerName());

			UE_LOGF(LogIoStoreDependencyViewer, Display, "  Container: %ls (chunks: %d)",
				*ContainerName, ContainerView.ChunkIds.Num());

			// Validate parallel arrays before iteration
			if (ContainerView.ChunkIds.Num() != ContainerView.ChunkEntries.Num())
			{
				UE_LOGF(LogIoStoreDependencyViewer, Error, "  Container %ls has mismatched ChunkIds (%d) and ChunkEntries (%d) counts",
					*ContainerName, ContainerView.ChunkIds.Num(), ContainerView.ChunkEntries.Num());
				continue;
			}

			// Add chunks from this container FIRST
			for (int32 Idx = 0; Idx < ContainerView.ChunkIds.Num(); ++Idx)
			{
				const FIoChunkId& ChunkId = ContainerView.ChunkIds[Idx];
				const FChunkEntry& ChunkEntry = ContainerView.ChunkEntries[Idx];

				TSharedPtr<FIoStoreAssetInfo> Asset = MakeShared<FIoStoreAssetInfo>();
				Asset->ChunkId = ChunkId;
				Asset->ContainerName = ContainerName;
				Asset->Size = ChunkEntry.RawSize;
				Asset->CompressedSize = ChunkEntry.EncodedSize;
				Asset->bIsCompressed = (ChunkEntry.RawSize != ChunkEntry.EncodedSize);

				// Try to resolve filename from metadata
				if (MetaReader.IsValid())
				{
					TUtf8StringBuilder<256> FilenameBuilder;
					FUtf8StringView ResolvedContainerName;
					FUtf8StringView Filename = MetaReader->GetFilename(ChunkId, FilenameBuilder, ResolvedContainerName);
					if (!Filename.IsEmpty())
					{
						Asset->FileName = FString(Filename);
					}
				}

				// Fallback to chunk ID
				if (Asset->FileName.IsEmpty())
				{
					Asset->FileName = FString::Printf(TEXT("Chunk_%s"), *LexToString(ChunkId));
				}

				// Thread-safe add to shared asset arrays
				{
					FScopeLock Lock(&SharedDataLock);
					AllAssets.Add(Asset);
					ChunkIdToAsset.Add(Asset->ChunkId, Asset);
				}
			}

			// THEN deserialize the container header to get dependency information
			FIoBuffer ContainerHeaderData;

			if (ContainerView.ContainerHeaderChunk.GetSize() > 0)
			{
				// Container header is embedded in the TOC
				ContainerHeaderData = ContainerView.ContainerHeaderChunk;
			}
			else if (bUsePartialDownloads && !ZenExePath.IsEmpty())
			{
				// Container header not embedded - try to fetch it as a chunk using zen reader
				// Check if there's a corresponding .utoc file for this on-demand container
				FString ContainerTocPath = FPaths::Combine(DirectoryPath, ContainerName + TEXT(".utoc"));
				if (PlatformFile.FileExists(*ContainerTocPath))
				{
					UE_LOGF(LogIoStoreDependencyViewer, Display, "    Fetching container header chunk from zen for %ls", *ContainerName);

					// Create a temporary zen reader to fetch the container header
					FString ContainerPath = FPaths::ChangeExtension(ContainerTocPath, TEXT(""));
					FString TempPartCacheDir = FPaths::Combine(DirectoryPath, TEXT("PartCache"), ContainerName);

					TUniquePtr<FIoStoreReaderZenBuild> TempZenReader = MakeUnique<FIoStoreReaderZenBuild>();
					FIoStatus TempStatus = TempZenReader->Initialize(
						ContainerPath,
						TMap<FGuid, FAES::FAESKey>(),
						ZenExePath,
						OidcExePath,
						CloudNamespace,
						CloudBucket,
						CloudBuildId,
						CloudProxyUrl,
						BaseDownloadDirectory,
						TempPartCacheDir
					);

					if (TempStatus.IsOk())
					{
						FIoChunkId ContainerHeaderChunkId = CreateIoChunkId(ContainerView.Header.ContainerId().Value(), 0, EIoChunkType::ContainerHeader);
						TIoStatusOr<FIoBuffer> HeaderBuffer = TempZenReader->Read(ContainerHeaderChunkId, FIoReadOptions());

						if (HeaderBuffer.IsOk())
						{
							ContainerHeaderData = HeaderBuffer.ConsumeValueOrDie();
							UE_LOGF(LogIoStoreDependencyViewer, Display, "    Successfully fetched container header (%llu bytes)", ContainerHeaderData.GetSize());
						}
						else
						{
							UE_LOGF(LogIoStoreDependencyViewer, Warning, "    Failed to fetch container header: %ls", *HeaderBuffer.Status().ToString());
						}
					}
				}
			}

			if (ContainerHeaderData.GetSize() > 0)
			{
				FLargeMemoryReader Ar(
					ContainerHeaderData.GetData(),
					ContainerHeaderData.GetSize());

				FIoContainerHeader ContainerHeader;
				Ar << ContainerHeader;

				// Load soft package references if they exist
				// The default serialization skips them, so we need to manually seek and load
				if (!Ar.IsError() && !Ar.IsCriticalError() && ContainerHeader.SoftPackageReferencesSerialInfo.Size > 0)
				{
					// Check for integer overflow in offset + size calculation
					const int64 Offset64 = ContainerHeader.SoftPackageReferencesSerialInfo.Offset;
					const int64 Size64 = ContainerHeader.SoftPackageReferencesSerialInfo.Size;
					const int64 EndOffset = Offset64 + Size64;
					if (Offset64 >= 0 && Size64 > 0 && EndOffset >= Offset64 && EndOffset <= Ar.TotalSize())
					{
						Ar.Seek(ContainerHeader.SoftPackageReferencesSerialInfo.Offset);
						Ar << ContainerHeader.SoftPackageReferences;

						if (!Ar.IsError() && !Ar.IsCriticalError())
						{
							UE_LOGF(LogIoStoreDependencyViewer, Display, "    Loaded soft package references: %d packages",
								ContainerHeader.SoftPackageReferences.PackageIds.Num());
						}
					}
					else
					{
						UE_LOGF(LogIoStoreDependencyViewer, Warning, "    Invalid soft package reference offset or size");
					}
				}

				if (!Ar.IsError() && !Ar.IsCriticalError())
				{
					UE_LOGF(LogIoStoreDependencyViewer, Display, "    Container header loaded: %d packages",
						ContainerHeader.PackageIds.Num());

					// Process the container header to build package dependencies (thread-safe)
					{
						FScopeLock Lock(&SharedDataLock);
						ProcessContainerHeader(ContainerHeader, ContainerName);
					}
				}
				else
				{
					UE_LOGF(LogIoStoreDependencyViewer, Warning, "    Failed to deserialize container header");
				}
			}
		}
		}); // End of lambda

		OnDemandTasks.Add(Task);
	} // End of for loop

	// Wait for all on-demand TOCs to finish loading in parallel
	if (OnDemandTasks.Num() > 0)
	{
		UE_LOGF(LogIoStoreDependencyViewer, Display, "Waiting for %d on-demand TOCs to load in parallel...", OnDemandTasks.Num());
		UE::Tasks::Wait(OnDemandTasks);
		UE_LOGF(LogIoStoreDependencyViewer, Display, "All on-demand TOCs loaded");
	}

	return true;
}

void SIoStoreDependencyViewer::ProcessContainerHeader(const FIoContainerHeader& Header, const FString& ContainerName)
{
	// Build package ID to chunk ID mapping
	for (int32 PackageIndex = 0; PackageIndex < Header.PackageIds.Num(); ++PackageIndex)
	{
		FPackageId PackageId = Header.PackageIds[PackageIndex];

		// Create chunk ID for this package
		FIoChunkId PackageChunkId = CreatePackageDataChunkId(PackageId);

		// Find corresponding asset
		TSharedPtr<FIoStoreAssetInfo>* AssetPtr = ChunkIdToAsset.Find(PackageChunkId);
		if (AssetPtr && AssetPtr->IsValid())
		{
			// Store package ID
			(*AssetPtr)->PackageId = PackageId;

			// Resolve package filename from metadata
			if (MetaReader.IsValid())
			{
				TUtf8StringBuilder<256> FilenameBuilder;
				FUtf8StringView ResolvedContainerName;
				FUtf8StringView Filename = MetaReader->GetFilename(PackageChunkId, FilenameBuilder, ResolvedContainerName);
				if (!Filename.IsEmpty())
				{
					(*AssetPtr)->FileName = FString(Filename);
					(*AssetPtr)->PackageName = FString(Filename);
				}
				else
				{
					// Fallback to PackageId hex string if filename not in metadata
					FString PackageIdString = LexToString(PackageId);
					(*AssetPtr)->FileName = PackageIdString;
					(*AssetPtr)->PackageName = PackageIdString;
				}
			}
			else
			{
				// No metadata available, use PackageId hex string
				FString PackageIdString = LexToString(PackageId);
				(*AssetPtr)->FileName = PackageIdString;
				(*AssetPtr)->PackageName = PackageIdString;
			}

			PackageIdToAsset.Add(PackageId, *AssetPtr);

			// Extract hard dependencies (imported packages) from store entries
			// Bounds check: ensure entire FFilePackageStoreEntry fits in buffer, not just the start offset
			// Avoid integer overflow by checking bounds before multiplication
			const int32 EntrySize = sizeof(FFilePackageStoreEntry);
			if (PackageIndex < INT32_MAX / EntrySize &&
				Header.StoreEntries.Num() >= (PackageIndex + 1) * EntrySize)
			{
				// FFilePackageStoreEntry uses self-relative pointers (TFilePackageStoreEntryCArrayView)
				// so it MUST be accessed in-place via pointer cast, not copied with memcpy
				// The IoStore reader ensures this buffer is properly structured and aligned
				const uint8* EntryPtr = Header.StoreEntries.GetData() + PackageIndex * EntrySize;

				// Verify alignment (FFilePackageStoreEntry requires proper alignment for safe reinterpret_cast)
				// Skip this entry if alignment is incorrect to avoid UB (works in all build configurations)
				if (!IsAligned(EntryPtr, alignof(FFilePackageStoreEntry)))
				{
					UE_LOGF(LogIoStoreDependencyViewer, Warning,
						"Skipping misaligned FFilePackageStoreEntry at %p (expected %d-byte alignment) for package %ls",
						EntryPtr, alignof(FFilePackageStoreEntry), *(*AssetPtr)->PackageName);
					continue;
				}

				const FFilePackageStoreEntry* StoreEntry = reinterpret_cast<const FFilePackageStoreEntry*>(EntryPtr);

				// Add imported packages as hard dependencies
				for (const FPackageId& ImportedPackage : StoreEntry->ImportedPackages)
				{
					FAssetDependency HardDep(ImportedPackage, false);
					// PackageName will be resolved later by ResolveDependencyNames()
					(*AssetPtr)->HardDependencies.Add(HardDep);

					// Build reverse dependency map (referencers)
					FAssetDependency ReverseHardDep(PackageId, false);
					// PackageName will be resolved later by ResolveDependencyNames()
					PackageIdToReferencers.FindOrAdd(ImportedPackage).Add(ReverseHardDep);
				}
			}
		}
	}

	// Extract soft dependencies from soft package references
	if (Header.SoftPackageReferences.bContainsSoftPackageReferences && Header.SoftPackageReferences.PackageIds.Num() > 0)
	{
		// The PackageIndices array contains indices into PackageIds for each package
		// This is stored as a serialized array view for each package
		const uint8* IndicesData = Header.SoftPackageReferences.PackageIndices.GetData();
		int32 CurrentOffset = 0;

		for (int32 PackageIndex = 0; PackageIndex < Header.PackageIds.Num() && CurrentOffset < Header.SoftPackageReferences.PackageIndices.Num(); ++PackageIndex)
		{
			FPackageId PackageId = Header.PackageIds[PackageIndex];
			FIoChunkId PackageChunkId = CreatePackageDataChunkId(PackageId);

			// Read the array view header (num + offset) for this package
			// CRITICAL: Must advance CurrentOffset for EVERY package, not just packages we have assets for
			// Otherwise offset becomes desynchronized and we read wrong data for subsequent packages
			// Check for integer overflow before addition
			const int32 HeaderSize = sizeof(uint32) * 2;
			if (CurrentOffset <= Header.SoftPackageReferences.PackageIndices.Num() - HeaderSize)
			{
				// Use memcpy to avoid alignment/aliasing undefined behavior
				uint32 ArrayHeader[2];
				FMemory::Memcpy(ArrayHeader, IndicesData + CurrentOffset, sizeof(uint32) * 2);
				uint32 NumSoftRefs = ArrayHeader[0];
				uint32 OffsetToData = ArrayHeader[1];

				// Only process soft refs if we have an asset for this package
				TSharedPtr<FIoStoreAssetInfo>* AssetPtr = ChunkIdToAsset.Find(PackageChunkId);
				if (AssetPtr && AssetPtr->IsValid())
				{
					// Check for integer overflow before addition
					const int64 RequiredSize = (int64)OffsetToData + (int64)NumSoftRefs * sizeof(uint32);
					if (NumSoftRefs > 0 && RequiredSize >= 0 && CurrentOffset <= Header.SoftPackageReferences.PackageIndices.Num() - RequiredSize)
					{
						// Read soft ref indices using memcpy for alignment safety
						for (uint32 i = 0; i < NumSoftRefs; ++i)
						{
							uint32 RefIndex;
							FMemory::Memcpy(&RefIndex, IndicesData + CurrentOffset + OffsetToData + i * sizeof(uint32), sizeof(uint32));

							if (RefIndex < (uint32)Header.SoftPackageReferences.PackageIds.Num())
							{
								FPackageId SoftRefPackageId = Header.SoftPackageReferences.PackageIds[RefIndex];
								FAssetDependency SoftDep(SoftRefPackageId, true);
								// PackageName will be resolved later by ResolveDependencyNames()
								(*AssetPtr)->SoftDependencies.Add(SoftDep);

								// Build reverse dependency map (referencers)
								FAssetDependency ReverseSoftDep(PackageId, true);
								// PackageName will be resolved later by ResolveDependencyNames()
								PackageIdToReferencers.FindOrAdd(SoftRefPackageId).Add(ReverseSoftDep);
							}
						}
					}
				}

				// CRITICAL: Always advance offset, even if we don't have an asset for this package
				// Each package has a header in the packed data that must be skipped
				CurrentOffset += sizeof(uint32) * 2;
			}
		}
	}
}

void SIoStoreDependencyViewer::ResolveDependencyNames()
{
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Resolving dependency names...");

	int32 ResolvedCount = 0;
	int32 TotalDeps = 0;

	// Iterate through all assets and resolve their dependency names
	for (TSharedPtr<FIoStoreAssetInfo>& Asset : AllAssets)
	{
		// Resolve hard dependency names
		for (FAssetDependency& HardDep : Asset->HardDependencies)
		{
			TotalDeps++;

			// Try to find the dependency in PackageIdToAsset
			TSharedPtr<FIoStoreAssetInfo>* DepAssetPtr = PackageIdToAsset.Find(HardDep.PackageId);
			if (DepAssetPtr && DepAssetPtr->IsValid())
			{
				HardDep.PackageName = (*DepAssetPtr)->FileName;
				ResolvedCount++;
			}
			else if (MetaReader.IsValid())
			{
				// Try to get filename from metadata
				TUtf8StringBuilder<256> FilenameBuilder;
				FUtf8StringView ResolvedContainerName;
				FIoChunkId PackageChunkId = CreatePackageDataChunkId(HardDep.PackageId);
				FUtf8StringView Filename = MetaReader->GetFilename(PackageChunkId, FilenameBuilder, ResolvedContainerName);
				if (!Filename.IsEmpty())
				{
					HardDep.PackageName = FString(Filename);
					ResolvedCount++;
				}
				else
				{
					// Fallback: use hex PackageId string for unresolved dependencies
					HardDep.PackageName = LexToString(HardDep.PackageId);
				}
			}
			else
			{
				// Fallback: use hex PackageId string for unresolved dependencies
				HardDep.PackageName = LexToString(HardDep.PackageId);
			}
		}

		// Resolve soft dependency names
		for (FAssetDependency& SoftDep : Asset->SoftDependencies)
		{
			TotalDeps++;

			// Try to find the dependency in PackageIdToAsset
			TSharedPtr<FIoStoreAssetInfo>* DepAssetPtr = PackageIdToAsset.Find(SoftDep.PackageId);
			if (DepAssetPtr && DepAssetPtr->IsValid())
			{
				SoftDep.PackageName = (*DepAssetPtr)->FileName;
				ResolvedCount++;
			}
			else if (MetaReader.IsValid())
			{
				// Try to get filename from metadata
				TUtf8StringBuilder<256> FilenameBuilder;
				FUtf8StringView ResolvedContainerName;
				FIoChunkId PackageChunkId = CreatePackageDataChunkId(SoftDep.PackageId);
				FUtf8StringView Filename = MetaReader->GetFilename(PackageChunkId, FilenameBuilder, ResolvedContainerName);
				if (!Filename.IsEmpty())
				{
					SoftDep.PackageName = FString(Filename);
					ResolvedCount++;
				}
				else
				{
					// Fallback: use hex PackageId string for unresolved dependencies
					SoftDep.PackageName = LexToString(SoftDep.PackageId);
				}
			}
			else
			{
				// Fallback: use hex PackageId string for unresolved dependencies
				SoftDep.PackageName = LexToString(SoftDep.PackageId);
			}
		}
	}

	// Also resolve names in the reverse dependency map (referencers)
	for (auto& Pair : PackageIdToReferencers)
	{
		for (FAssetDependency& Referencer : Pair.Value)
		{
			TSharedPtr<FIoStoreAssetInfo>* RefAssetPtr = PackageIdToAsset.Find(Referencer.PackageId);
			if (RefAssetPtr && RefAssetPtr->IsValid())
			{
				Referencer.PackageName = (*RefAssetPtr)->FileName;
			}
			else if (MetaReader.IsValid())
			{
				TUtf8StringBuilder<256> FilenameBuilder;
				FUtf8StringView ResolvedContainerName;
				FIoChunkId PackageChunkId = CreatePackageDataChunkId(Referencer.PackageId);
				FUtf8StringView Filename = MetaReader->GetFilename(PackageChunkId, FilenameBuilder, ResolvedContainerName);
				if (!Filename.IsEmpty())
				{
					Referencer.PackageName = FString(Filename);
				}
				else
				{
					// Fallback: use hex PackageId string for unresolved referencers
					Referencer.PackageName = LexToString(Referencer.PackageId);
				}
			}
			else
			{
				// Fallback: use hex PackageId string for unresolved referencers
				Referencer.PackageName = LexToString(Referencer.PackageId);
			}
		}
	}

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Resolved %d out of %d dependency names (%.1f%%)",
		ResolvedCount, TotalDeps, TotalDeps > 0 ? (100.0f * ResolvedCount / TotalDeps) : 0.0f);
}

void SIoStoreDependencyViewer::UpdateAssetList()
{
	FilteredAssets.Empty();

	if (CurrentSearchText.IsEmpty())
	{
		FilteredAssets = AllAssets;
	}
	else
	{
		for (const TSharedPtr<FIoStoreAssetInfo>& Asset : AllAssets)
		{
			bool bMatches = false;

			// Search by filename
			if (Asset->FileName.Contains(CurrentSearchText))
			{
				bMatches = true;
			}
			// Search by container name
			else if (Asset->ContainerName.Contains(CurrentSearchText))
			{
				bMatches = true;
			}
			// Search by chunk ID
			else if (LexToString(Asset->ChunkId).Contains(CurrentSearchText))
			{
				bMatches = true;
			}
			// Search by package ID
			else if (Asset->PackageId.IsValid() && LexToString(Asset->PackageId).Contains(CurrentSearchText))
			{
				bMatches = true;
			}
			// Search by package name
			else if (!Asset->PackageName.IsEmpty() && Asset->PackageName.Contains(CurrentSearchText))
			{
				bMatches = true;
			}
			// Search by tags
			else
			{
				for (const FString& AssetTag : Asset->Tags)
				{
					if (AssetTag.Contains(CurrentSearchText))
					{
						bMatches = true;
						break;
					}
				}
			}

			if (bMatches)
			{
				FilteredAssets.Add(Asset);
			}
		}
	}

	if (AssetListView.IsValid())
	{
		AssetListView->RequestListRefresh();
	}
}

void SIoStoreDependencyViewer::UpdateAssetInfoPanel(TSharedPtr<FIoStoreAssetInfo> AssetInfo)
{
	if (!AssetInfo.IsValid())
	{
		AssetInfoText->SetText(LOCTEXT("NoAssetSelected", "Select an asset to view details"));
		CurrentHardDependencies.Empty();
		CurrentSoftDependencies.Empty();
		CurrentReferencers.Empty();

		if (HardDependenciesListView.IsValid())
		{
			HardDependenciesListView->RequestListRefresh();
		}
		if (SoftDependenciesListView.IsValid())
		{
			SoftDependenciesListView->RequestListRefresh();
		}
		if (ReferencersListView.IsValid())
		{
			ReferencersListView->RequestListRefresh();
		}
		return;
	}

	// Get referencers count
	int32 ReferencersCount = 0;
	if (AssetInfo->PackageId.IsValid())
	{
		if (const TArray<FAssetDependency>* Referencers = PackageIdToReferencers.Find(AssetInfo->PackageId))
		{
			ReferencersCount = Referencers->Num();
		}
	}

	// Build info text
	FString InfoString = FString::Printf(
		TEXT("File: %s\n")
		TEXT("Container: %s\n")
		TEXT("Package ID: %s\n")
		TEXT("Chunk ID: %s\n")
		TEXT("Size: %llu bytes (%.2f MB)\n")
		TEXT("Compressed: %llu bytes (%.2f MB)\n")
		TEXT("Compression: %s\n")
		TEXT("Partition: %d\n")
		TEXT("Hard Dependencies: %d\n")
		TEXT("Soft Dependencies: %d\n")
		TEXT("Referencers: %d"),
		*AssetInfo->FileName,
		*AssetInfo->ContainerName,
		AssetInfo->PackageId.IsValid() ? *LexToString(AssetInfo->PackageId) : TEXT("N/A"),
		*LexToString(AssetInfo->ChunkId),
		AssetInfo->Size,
		AssetInfo->Size / (1024.0 * 1024.0),
		AssetInfo->CompressedSize,
		AssetInfo->CompressedSize / (1024.0 * 1024.0),
		AssetInfo->bIsCompressed ? TEXT("Yes") : TEXT("No"),
		AssetInfo->PartitionIndex,
		AssetInfo->HardDependencies.Num(),
		AssetInfo->SoftDependencies.Num(),
		ReferencersCount
	);

	AssetInfoText->SetText(FText::FromString(InfoString));

	// Update dependency lists (convert to shared pointers)
	CurrentHardDependencies.Empty();
	for (const FAssetDependency& Dep : AssetInfo->HardDependencies)
	{
		CurrentHardDependencies.Add(MakeShared<FAssetDependency>(Dep));
	}

	CurrentSoftDependencies.Empty();
	for (const FAssetDependency& Dep : AssetInfo->SoftDependencies)
	{
		CurrentSoftDependencies.Add(MakeShared<FAssetDependency>(Dep));
	}

	// Update referencers list
	CurrentReferencers.Empty();
	if (AssetInfo->PackageId.IsValid())
	{
		if (const TArray<FAssetDependency>* Referencers = PackageIdToReferencers.Find(AssetInfo->PackageId))
		{
			for (const FAssetDependency& Ref : *Referencers)
			{
				CurrentReferencers.Add(MakeShared<FAssetDependency>(Ref));
			}
		}
	}

	if (HardDependenciesListView.IsValid())
	{
		HardDependenciesListView->RequestListRefresh();
	}
	if (SoftDependenciesListView.IsValid())
	{
		SoftDependenciesListView->RequestListRefresh();
	}
	if (ReferencersListView.IsValid())
	{
		ReferencersListView->RequestListRefresh();
	}
}

void SIoStoreDependencyViewer::BuildDependencyGraph(TSharedPtr<FIoStoreAssetInfo> RootAsset)
{
	if (!GraphPanel.IsValid())
	{
		return;
	}

	switch (CurrentGraphViewMode)
	{
	case EGraphViewMode::SelectedAssetDependencies:
		if (RootAsset.IsValid())
		{
			// Set referencer map BEFORE RebuildGraph so CreateReferencerNodes() can use it
			GraphPanel->SetReferencerMap(PackageIdToReferencers);
			GraphPanel->RebuildGraph(RootAsset, PackageIdToAsset, CurrentTreeDepth, CurrentReferencerDepth);
		}
		break;

	case EGraphViewMode::AllAssets:
		GraphPanel->RebuildGraphAllAssets(FilteredAssets, CurrentSelectedAsset);
		break;

	case EGraphViewMode::ContainerDependencies:
		GraphPanel->RebuildGraphContainers(AllAssets);
		break;
	}
}


TSharedPtr<FIoStoreAssetInfo> SIoStoreDependencyViewer::FindAssetByPackageId(const FPackageId& PackageId)
{
	TSharedPtr<FIoStoreAssetInfo>* Found = PackageIdToAsset.Find(PackageId);
	return Found ? *Found : nullptr;
}

TSharedPtr<FIoStoreAssetInfo> SIoStoreDependencyViewer::FindAssetByChunkId(const FIoChunkId& ChunkId)
{
	TSharedPtr<FIoStoreAssetInfo>* Found = ChunkIdToAsset.Find(ChunkId);
	return Found ? *Found : nullptr;
}

TSharedPtr<SWidget> SIoStoreDependencyViewer::OnReferencersContextMenuOpening()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CopyReferencers", "Copy"),
		LOCTEXT("CopyReferencersTooltip", "Copy referencers to clipboard"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SIoStoreDependencyViewer::CopyReferencersToClipboard))
	);

	return MenuBuilder.MakeWidget();
}

TSharedPtr<SWidget> SIoStoreDependencyViewer::OnHardDepsContextMenuOpening()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CopyHardDeps", "Copy"),
		LOCTEXT("CopyHardDepsTooltip", "Copy hard dependencies to clipboard"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SIoStoreDependencyViewer::CopyHardDepsToClipboard))
	);

	return MenuBuilder.MakeWidget();
}

TSharedPtr<SWidget> SIoStoreDependencyViewer::OnSoftDepsContextMenuOpening()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CopySoftDeps", "Copy"),
		LOCTEXT("CopySoftDepsTooltip", "Copy soft dependencies to clipboard"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SIoStoreDependencyViewer::CopySoftDepsToClipboard))
	);

	return MenuBuilder.MakeWidget();
}

void SIoStoreDependencyViewer::CopyReferencersToClipboard()
{
	FString TextToCopy;
	for (const TSharedPtr<FAssetDependency>& Dep : CurrentReferencers)
	{
		if (Dep.IsValid())
		{
			TextToCopy += FString::Printf(TEXT("%s (%s)%s\n"),
				*Dep->PackageName,
				Dep->bIsSoftReference ? TEXT("Soft") : TEXT("Hard"),
				TEXT(""));
		}
	}

	if (!TextToCopy.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*TextToCopy);
	}
}

void SIoStoreDependencyViewer::CopyHardDepsToClipboard()
{
	FString TextToCopy;
	for (const TSharedPtr<FAssetDependency>& Dep : CurrentHardDependencies)
	{
		if (Dep.IsValid())
		{
			TextToCopy += FString::Printf(TEXT("%s\n"), *Dep->PackageName);
		}
	}

	if (!TextToCopy.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*TextToCopy);
	}
}

void SIoStoreDependencyViewer::CopySoftDepsToClipboard()
{
	FString TextToCopy;
	for (const TSharedPtr<FAssetDependency>& Dep : CurrentSoftDependencies)
	{
		if (Dep.IsValid())
		{
			TextToCopy += FString::Printf(TEXT("%s\n"), *Dep->PackageName);
		}
	}

	if (!TextToCopy.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*TextToCopy);
	}
}

FString SIoStoreDependencyViewer::FindDirectoryWithTocFiles(const FString& RootDirectory)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// First check if the root directory itself contains .utoc files
	TArray<FString> TocFiles;
	PlatformFile.FindFilesRecursively(TocFiles, *RootDirectory, TEXT(".utoc"));

	if (TocFiles.Num() > 0)
	{
		// Return the directory of the first .utoc file found
		return FPaths::GetPath(TocFiles[0]);
	}

	// If no .utoc files found, check for .uondemandtoc files
	TArray<FString> OnDemandTocFiles;
	PlatformFile.FindFilesRecursively(OnDemandTocFiles, *RootDirectory, TEXT(".uondemandtoc"));

	if (OnDemandTocFiles.Num() > 0)
	{
		// Return the directory of the first .uondemandtoc file found
		return FPaths::GetPath(OnDemandTocFiles[0]);
	}

	// No TOC files found
	return FString();
}


TSharedRef<ITableRow> SIoStoreDependencyViewer::OnGenerateRowForAssetList(TSharedPtr<FIoStoreAssetInfo> AssetInfo, const TSharedRef<STableViewBase>& OwnerTable)
{
	FString SizeStr = FString::Printf(TEXT("%.2f MB"), AssetInfo->Size / (1024.0 * 1024.0));

	return SNew(STableRow<TSharedPtr<FIoStoreAssetInfo>>, OwnerTable)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(AssetInfo->FileName))
				.Font(FAppStyle::GetFontStyle("NormalFont"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%s | %s"), *AssetInfo->ContainerName, *SizeStr)))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f))
			]
		];
}

void SIoStoreDependencyViewer::OnAssetListSelectionChanged(TSharedPtr<FIoStoreAssetInfo> AssetInfo, ESelectInfo::Type SelectInfo)
{
	CurrentSelectedAsset = AssetInfo;

	// Add to navigation history if this is a user-initiated selection (not during back/forward navigation)
	if (AssetInfo.IsValid() && SelectInfo != ESelectInfo::Direct && !bIsNavigating)
	{
		// Remove any forward history
		if (NavigationHistoryIndex >= 0 && NavigationHistoryIndex < NavigationHistory.Num() - 1)
		{
			NavigationHistory.RemoveAt(NavigationHistoryIndex + 1, NavigationHistory.Num() - NavigationHistoryIndex - 1);
		}

		// Add to history
		NavigationHistory.Add(AssetInfo);
		NavigationHistoryIndex = NavigationHistory.Num() - 1;

		// Limit history size to 50 items
		if (NavigationHistory.Num() > 50)
		{
			NavigationHistory.RemoveAt(0);
			NavigationHistoryIndex--;
		}
	}

	if (AssetInfo.IsValid())
	{
		UpdateAssetInfoPanel(AssetInfo);
		BuildDependencyGraph(AssetInfo);
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("Selected: %s"), *AssetInfo->FileName)));
	}
	else
	{
		UpdateAssetInfoPanel(nullptr);
		if (GraphPanel.IsValid())
		{
			GraphPanel->RebuildGraph(nullptr, TMap<FPackageId, TSharedPtr<FIoStoreAssetInfo>>(), 0);
		}
	}
}

TSharedRef<ITableRow> SIoStoreDependencyViewer::OnGenerateRowForDependencyList(TSharedPtr<FAssetDependency> Dependency, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (!Dependency.IsValid())
	{
		return SNew(STableRow<TSharedPtr<FAssetDependency>>, OwnerTable)
			[
				SNew(STextBlock).Text(LOCTEXT("InvalidDep", "Invalid Dependency"))
			];
	}

	TSharedPtr<FIoStoreAssetInfo> DepAsset = FindAssetByPackageId(Dependency->PackageId);
	FString DisplayName = DepAsset.IsValid() ? DepAsset->FileName : Dependency->PackageName;

	return SNew(STableRow<TSharedPtr<FAssetDependency>>, OwnerTable)
		[
			SNew(STextBlock)
			.Text(FText::FromString(DisplayName))
			.ToolTipText(FText::FromString(FString::Printf(TEXT("Package ID: %s"), *Dependency->PackageName)))
		];
}

void SIoStoreDependencyViewer::OnDependencyListSelectionChanged(TSharedPtr<FAssetDependency> Dependency, ESelectInfo::Type SelectInfo)
{
	if (!Dependency.IsValid())
	{
		return;
	}

	// Navigate to the dependency in the asset list
	TSharedPtr<FIoStoreAssetInfo> DepAsset = FindAssetByPackageId(Dependency->PackageId);
	if (DepAsset.IsValid() && AssetListView.IsValid())
	{
		// Clear search to ensure asset is visible
		if (!CurrentSearchText.IsEmpty() && SearchBox.IsValid())
		{
			CurrentSearchText.Empty();
			SearchBox->SetText(FText::GetEmpty());
			UpdateAssetList();
		}

		// Navigate to the asset
		AssetListView->SetSelection(DepAsset);
		AssetListView->RequestScrollIntoView(DepAsset);
	}
}

void SIoStoreDependencyViewer::OnDepthChanged(int32 NewDepth)
{
	CurrentTreeDepth = NewDepth;

	// Rebuild graph with new depth if an asset is selected
	if (CurrentSelectedAsset.IsValid())
	{
		BuildDependencyGraph(CurrentSelectedAsset);
	}
}

void SIoStoreDependencyViewer::OnReferencerDepthChanged(int32 NewDepth)
{
	CurrentReferencerDepth = NewDepth;

	// Rebuild graph with new referencer depth if an asset is selected
	if (CurrentSelectedAsset.IsValid())
	{
		BuildDependencyGraph(CurrentSelectedAsset);
	}
}

FReply SIoStoreDependencyViewer::OnZoomToFitClicked()
{
	if (GraphPanel.IsValid())
	{
		GraphPanel->ZoomToFit();
	}
	return FReply::Handled();
}

void SIoStoreDependencyViewer::OnGraphSearchTextChanged(const FText& InText)
{
	if (GraphPanel.IsValid())
	{
		GraphPanel->OnGraphSearchTextChanged(InText);
	}
}

void SIoStoreDependencyViewer::OnGraphViewModeChanged(TSharedPtr<FString> NewMode, ESelectInfo::Type SelectInfo)
{
	if (!NewMode.IsValid())
	{
		return;
	}

	// Map selection to enum
	if (*NewMode == TEXT("Selected Asset Dependencies"))
	{
		CurrentGraphViewMode = EGraphViewMode::SelectedAssetDependencies;
	}
	else if (*NewMode == TEXT("All Assets"))
	{
		CurrentGraphViewMode = EGraphViewMode::AllAssets;
	}
	else if (*NewMode == TEXT("Container Dependencies"))
	{
		CurrentGraphViewMode = EGraphViewMode::ContainerDependencies;
	}

	// Rebuild graph with new mode
	if (CurrentGraphViewMode == EGraphViewMode::SelectedAssetDependencies && CurrentSelectedAsset.IsValid())
	{
		BuildDependencyGraph(CurrentSelectedAsset);
	}
	else if (CurrentGraphViewMode == EGraphViewMode::AllAssets)
	{
		BuildDependencyGraph(nullptr);  // nullptr signals to build all assets view
	}
	else if (CurrentGraphViewMode == EGraphViewMode::ContainerDependencies)
	{
		BuildDependencyGraph(nullptr);  // Build container view
	}
}

TSharedRef<SWidget> SIoStoreDependencyViewer::OnGenerateGraphViewModeWidget(TSharedPtr<FString> InItem)
{
	return SNew(STextBlock).Text(FText::FromString(*InItem));
}

FText SIoStoreDependencyViewer::GetCurrentGraphViewModeText() const
{
	switch (CurrentGraphViewMode)
	{
	case EGraphViewMode::SelectedAssetDependencies:
		return FText::FromString(TEXT("Selected Asset Dependencies"));
	case EGraphViewMode::AllAssets:
		return FText::FromString(TEXT("All Assets"));
	case EGraphViewMode::ContainerDependencies:
		return FText::FromString(TEXT("Container Dependencies"));
	default:
		return FText::FromString(TEXT("Selected Asset Dependencies"));
	}
}

void SIoStoreDependencyViewer::OnSearchTextChanged(const FText& InText)
{
	CurrentSearchText = InText.ToString();
	UpdateAssetList();

	if (FilteredAssets.Num() < AllAssets.Num())
	{
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("Filtered: %d / %d assets"), FilteredAssets.Num(), AllAssets.Num())));
	}
	else
	{
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("%d assets loaded"), AllAssets.Num())));
	}
}

void SIoStoreDependencyViewer::OnSearchModeChanged(int32 NewMode)
{
	SearchMode = NewMode;
	UpdateAssetList();
}

// Navigation history

FReply SIoStoreDependencyViewer::OnNavigateBackClicked()
{
	if (CanNavigateBack())
	{
		NavigationHistoryIndex--;
		TSharedPtr<FIoStoreAssetInfo> Asset = NavigationHistory[NavigationHistoryIndex];
		NavigateToAsset(Asset, false);  // Don't add to history
	}
	return FReply::Handled();
}

FReply SIoStoreDependencyViewer::OnNavigateForwardClicked()
{
	if (CanNavigateForward())
	{
		NavigationHistoryIndex++;
		TSharedPtr<FIoStoreAssetInfo> Asset = NavigationHistory[NavigationHistoryIndex];
		NavigateToAsset(Asset, false);  // Don't add to history
	}
	return FReply::Handled();
}

bool SIoStoreDependencyViewer::CanNavigateBack() const
{
	return NavigationHistoryIndex > 0;
}

bool SIoStoreDependencyViewer::CanNavigateForward() const
{
	return NavigationHistoryIndex >= 0 && NavigationHistoryIndex < NavigationHistory.Num() - 1;
}

void SIoStoreDependencyViewer::NavigateToAsset(TSharedPtr<FIoStoreAssetInfo> Asset, bool bAddToHistory)
{
	if (!Asset.IsValid())
	{
		return;
	}

	// Update history if requested (normal user navigation)
	if (bAddToHistory && !bIsNavigating)
	{
		// Remove any forward history
		if (NavigationHistoryIndex >= 0 && NavigationHistoryIndex < NavigationHistory.Num() - 1)
		{
			NavigationHistory.RemoveAt(NavigationHistoryIndex + 1, NavigationHistory.Num() - NavigationHistoryIndex - 1);
		}

		// Add to history
		NavigationHistory.Add(Asset);
		NavigationHistoryIndex = NavigationHistory.Num() - 1;

		// Limit history size to 50 items
		if (NavigationHistory.Num() > 50)
		{
			NavigationHistory.RemoveAt(0);
			NavigationHistoryIndex--;
		}
	}

	// Navigate (will trigger OnAssetListSelectionChanged)
	bIsNavigating = !bAddToHistory;  // Prevent adding to history during programmatic navigation
	AssetListView->SetSelection(Asset, ESelectInfo::Direct);
	AssetListView->RequestScrollIntoView(Asset);
	bIsNavigating = false;
}

// Loading progress tracking

#undef LOCTEXT_NAMESPACE
