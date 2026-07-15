// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorToolkit.h"

#include "AssetEditorModeManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Cloud/MetaHumanARServiceRequest.h"
#include "Cloud/MetaHumanServiceRequest.h"
#include "Components/SkeletalMeshComponent.h"
#include "ContentBrowserModule.h"
#include "DesktopPlatformModule.h"
#include "DNA.h"
#include "DNAReader.h"
#include "DNAUtils.h"
#include "Editor/EditorEngine.h"
#include "EditorDialogLibrary.h"
#include "EditorViewportTabContent.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "IAssetTools.h"
#include "IContentBrowserSingleton.h"
#include "ImageUtils.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "Logging/MessageLog.h"
#include "Logging/StructuredLog.h"
#include "Logging/TokenizedMessage.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterActorInterface.h"
#include "MetaHumanCharacterAnalytics.h"
#include "MetaHumanCharacterAnimInstance.h"
#include "MetaHumanCharacterAssetEditor.h"
#include "MetaHumanCharacterAssetEditorContext.h"
#include "MetaHumanCharacterEditorActorInterface.h"
#include "MetaHumanCharacterEditorCommands.h"
#include "MetaHumanCharacterEditorLog.h"
#include "MetaHumanCharacterEditorMode.h"
#include "MetaHumanCharacterEditorModule.h"
#include "MetaHumanCharacterEditorPipelineSpecification.h"
#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanCharacterEditorStyle.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterEditorUILayer.h"
#include "MetaHumanCharacterEditorViewportClient.h"
#include "MetaHumanCharacterEnvironmentLightRig.h"
#include "MetaHumanInstance.h"
#include "MetaHumanSDKEditor.h"
#include "MetaHumanCharacterPaletteProjectSettings.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionEditorPipeline.h"
#include "MetaHumanIdentity.h"
#include "MetaHumanIdentityParts.h"
#include "MetaHumanIdentityPose.h"
#include "MetaHumanInvisibleDrivingActor.h"
#include "MetaHumanRigEvaluatedState.h"
#include "MetaHumanWardrobeItem.h"
#include "Misc/ITransaction.h"
#include "Misc/UObjectToken.h"
#include "Verification/MetaHumanCharacterValidation.h"
#include "Misc/FileHelper.h"
#include "PackageTools.h"
#include "PreviewScene.h"
#include "ScopedTransaction.h"
#include "Selection.h"
#include "SMetaHumanCharacterEditorPreviewSettingsView.h"
#include "ToolMenus.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "Tools/MetaHumanCharacterEditorEyesTool.h"
#include "UI/SMetaHumanAuthenticationMenuButton.h"
#include "UI/Viewport/SMetaHumanCharacterEditorViewport.h"
#include "UI/Viewport/SMetaHumanCharacterEditorViewportAnimationBar.h"
#include "MetaHumanCharacterViewport.h"
#include "Widgets/Colors/SColorPicker.h"
#include "GroomComponent.h"
#include "Dialogs/Dialogs.h"
#include "UI/Views/SMetaHumanCharacterEditorRenderingQualityView.h"
#include "UObject/Linker.h"

extern UNREALED_API UEditorEngine* GEditor;

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditor"

static const TCHAR* MetaHumanCharacterEditorToolkitTransactionContext = TEXT("MetaHumanCharacterEditorToolkitTransaction");


namespace UE::MetaHuman
{
	// Request unload of a streaming level loaded with bLoadAsTempPackage=true.
	// Also clears RF_Standalone on the inner world + package + nested objects: the engine's
	// GC helper skips that in editor mode, so the inner UWorld would otherwise leak.
	static void RequestStreamingLevelUnload(ULevelStreaming* Level)
	{
		if (!IsValid(Level))
		{
			return;
		}

		if (ULevel* LoadedLevel = Level->GetLoadedLevel())
		{
			if (UWorld* InnerWorld = LoadedLevel->GetTypedOuter<UWorld>())
			{
				InnerWorld->ClearFlags(RF_Standalone | RF_Public);
				if (UPackage* InnerPackage = InnerWorld->GetPackage())
				{
					InnerPackage->ClearFlags(RF_Standalone);
					ForEachObjectWithPackage(InnerPackage, [](UObject* Obj)
						{
							Obj->ClearFlags(RF_Standalone);
							return true;
						}, EGetObjectsFlags::IncludeNestedObjects);
				}
			}
		}

		Level->SetShouldBeLoaded(false);
		Level->SetShouldBeVisibleInEditor(false);
		Level->SetIsRequestingUnloadAndRemoval(true);
	}

	static void SaveBufferToFileWithDialog(const FSharedBuffer& StateData)
	{
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		TArray<FString> OutFilenames;

		const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		const FString DialogTitle = LOCTEXT("SaveFaceDNADialogTitle", "Save Face DNA file").ToString();
		const FString DefaultPath = TEXT("");
		const FString DefaultFile = TEXT("");
		const FString FileTypes = TEXT("All Files (*.*)|*.*");
		if (DesktopPlatform->SaveFileDialog(ParentWindowHandle, DialogTitle, DefaultPath, DefaultFile, FileTypes, EFileDialogFlags::None, OutFilenames))
		{
			if (OutFilenames.Num() == 1)
			{
				// The serialized state is a json string
				FFileHelper::SaveArrayToFile(TArrayView64<const uint8>{
					reinterpret_cast<const uint8*>(StateData.GetData()), static_cast<int64>(StateData.GetSize())}, * OutFilenames[0]);
			}
		}
	}
}

const FName FMetaHumanCharacterEditorToolkit::MetaHumanCharacterPreviewTabID(TEXT("MetaHumanCharacterEditor_PreviewSettingsTab"));
const FName FMetaHumanCharacterEditorToolkit::MetaHumanCharcterAnimationPanelID(TEXT("MetaHumanCharacterEditor_AnimationPanelTab"));

FMetaHumanCharacterEditorToolkit::FMetaHumanCharacterEditorToolkit(UMetaHumanCharacterAssetEditor* InOwningAssetEditor)
	: FBaseAssetToolkit{ InOwningAssetEditor }
{
	StandaloneDefaultLayout = FTabManager::NewLayout(TEXT("MetaHumanCharacterEditorLayout_5"))
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.2f)
					// ->AddTab(UAssetEditorUISubsystem::TopLeftTabID, ETabState::OpenedTab)
					->SetExtensionId("EditorSidePanelArea")
					->SetHideTabWell(true)
				)
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.5)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.95f)
						->AddTab(ViewportTabID, ETabState::OpenedTab)
						->SetExtensionId(TEXT("ViewportArea"))
						->SetHideTabWell(true)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.05f)
						->AddTab(MetaHumanCharcterAnimationPanelID, ETabState::OpenedTab)
						->SetExtensionId(TEXT("AnimationArea"))
						->SetHideTabWell(true)
					)

				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.3f)
					->AddTab(DetailsTabID, ETabState::ClosedTab)
					->AddTab(MetaHumanCharacterPreviewTabID, ETabState::OpenedTab)
					->SetExtensionId(TEXT("DetailsArea"))
					->SetHideTabWell(false)
				)
			)
		);

	LayoutExtender = MakeShared<FLayoutExtender>();
	FMetaHumanCharacterEditorModule::GetChecked().OnRegisterLayoutExtensions().Broadcast(*LayoutExtender);
	StandaloneDefaultLayout->ProcessExtensions(*LayoutExtender);

	// Constructs the preview scene without its default directional light since W
	PreviewScene = MakeUnique<FPreviewScene>(FPreviewScene::ConstructionValues()
											 .SetCreateDefaultLighting(false)
											 .AllowLumenPrimitiveTrackingInPreviewWorld(true));

	UWorld* PreviewWorld = PreviewScene->GetWorld();
	
	// Creating Character Actor 
	UMetaHumanCharacter* MetaHumanCharacter = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	check(MetaHumanCharacter);
	check(MetaHumanCharacter->IsCharacterValid());

	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>();
	// Object should have been added before this toolkit was created
	check(MetaHumanCharacterSubsystem->IsObjectAddedForEditing(MetaHumanCharacter));

	// Creates the MetaHuman Preview Actor using information from the asset
	PreviewActor = MetaHumanCharacterSubsystem->CreateMetaHumanCharacterEditorActor(MetaHumanCharacter, PreviewWorld);
	check(PreviewActor);
	check(PreviewActor.GetObject()->IsA<AActor>());

	// Create the invisible driving actor used for animation preview. This will act as the retargeting source for the preview actor.
	MetaHumanCharacterSubsystem->CreateMetaHumanInvisibleDrivingActor(MetaHumanCharacter, PreviewActor, PreviewWorld);

	// Start a validation report in case there are invalid items in the collection
	UMetaHumanCharacterValidationContext::FBeginReportParams ReportParams;
	ReportParams.ObjectToValidate = MetaHumanCharacter;

	UMetaHumanCharacterValidationContext::FScopedReport ScopedValidationReport{ ReportParams };

	UMetaHumanCollection* Collection = MetaHumanCharacterSubsystem->GetPreviewCollection(MetaHumanCharacter);
	check(Collection);

	Collection->GetMutablePipeline()->GetMutableEditorPipeline()->SetValidationContext(ScopedValidationReport.Context.Get());

	// Build the collection and assemble the character wardrobe items
	MetaHumanCharacterSubsystem->RunCharacterEditorPipelineForPreview(MetaHumanCharacter);

	// Initialize Preview Scene Details
	InitPreviewSceneDetails();
}

FMetaHumanCharacterEditorToolkit::~FMetaHumanCharacterEditorToolkit()
{
	// We need to force the editor mode deletion now because otherwise the preview world
	// will end up getting destroyed before the mode's Exit() function gets to run, and we'll get some
	// warnings when we destroy any mode actors.
	EditorModeManager->DestroyMode(UMetaHumanCharacterEditorMode::EM_MetaHumanCharacterEditorModeId);
}

FName FMetaHumanCharacterEditorToolkit::GetToolkitFName() const
{
	return TEXT("MetaHumanCharacterEditor");
}

FText FMetaHumanCharacterEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("BaseToolkitName", "MetaHuman Character Editor");
}

void FMetaHumanCharacterEditorToolkit::CreateEditorModeManager()
{
	FBaseAssetToolkit::CreateEditorModeManager();

	// The mode manager is the authority on what the world is for the mode and the tools context,
	// and setting the preview scene here makes our GetWorld() function return the preview scene
	// world instead of the normal level editor one. Important because that is where we create
	// any preview meshes, gizmo actors, etc.
	StaticCastSharedPtr<FAssetEditorModeManager>(EditorModeManager)->SetPreviewScene(PreviewScene.Get());
}

void FMetaHumanCharacterEditorToolkit::SaveAsset_Execute()
{
	// Restart the active tool will commit any changes so they can actually be saved
	GetMetaHumanCharacterEditorMode()->RestartCurrentlyActiveTool();

	// Close the color picker on save
	DestroyColorPicker();

	FBaseAssetToolkit::SaveAsset_Execute();
}

void FMetaHumanCharacterEditorToolkit::InitToolMenuContext(FToolMenuContext& InMenuContext)
{
	FAssetEditorToolkit::InitToolMenuContext(InMenuContext);

	UMetaHumanCharacterAssetEditorContext* Context = NewObject<UMetaHumanCharacterAssetEditorContext>();
	Context->MetaHumanCharacterAssetEditor = SharedThis(this);
	InMenuContext.AddObject(Context);
}

void FMetaHumanCharacterEditorToolkit::OnClose()
{
	// Close any color picker opened during an edit session
	DestroyColorPicker();

	// Unload every streaming level in the preview world (built-in scenarios and any active custom env) before close.
	if (UWorld* PreviewWorld = PreviewScene ? PreviewScene->GetWorld() : nullptr)
	{
		for (ULevelStreaming* Level : PreviewWorld->GetStreamingLevels())
		{
			UE::MetaHuman::RequestStreamingLevelUnload(Level);
		}

		PreviewWorld->FlushLevelStreaming(EFlushLevelStreamingType::Full);
		PreviewWorld->ClearStreamingLevels();
	}

	// We also held raw ULevelStreaming* in PostProcessLevels for the post-process scenarios.
	// Those entries are now dangling (the levels were just removed); clear the cache.
	PostProcessLevels.Empty();

	if (UMetaHumanCharacter* MetaHumanCharacter = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit())
	{
		UE::MetaHuman::Analytics::RecordCloseCharacterEditorEvent(MetaHumanCharacter);

		// OnClose is called twice for the same character, so only remove if currently editing it
		if (UMetaHumanCharacterEditorSubsystem::Get()->IsObjectAddedForEditing(MetaHumanCharacter))
		{
			// Deactivate the current active tool before removing object to ensure tool can commit its changes
			GetMetaHumanCharacterEditorMode()->GetToolManager()->DeactivateTool(EToolSide::Left, EToolShutdownType::Completed);

			UMetaHumanCharacterEditorSubsystem::Get()->RemoveObjectToEdit(MetaHumanCharacter);
		}
	}
}

void FMetaHumanCharacterEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FBaseAssetToolkit::RegisterTabSpawners(InTabManager);

	MetaHumanCharacterEditorMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_MetaHumanCharacterEditor", "MetaHuman"));

	InTabManager->RegisterTabSpawner(MetaHumanCharcterAnimationPanelID, FOnSpawnTab::CreateSP(this, &FMetaHumanCharacterEditorToolkit::SpawnTab_AnimationBar))
		.SetDisplayName(LOCTEXT("AnimationPanel", "Animation Bar"))
		.SetGroup(MetaHumanCharacterEditorMenuCategory.ToSharedRef())
		.SetCanSidebarTab(false);

	InTabManager->RegisterTabSpawner(MetaHumanCharacterPreviewTabID, FOnSpawnTab::CreateSP(this, &FMetaHumanCharacterEditorToolkit::SpawnTab_PreviewSceneDetails))
		.SetDisplayName(LOCTEXT("PreviewSceneDetails", "Preview Scene Details"))
		.SetGroup(MetaHumanCharacterEditorMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FMetaHumanCharacterEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FBaseAssetToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(MetaHumanCharcterAnimationPanelID);
	InTabManager->UnregisterTabSpawner(MetaHumanCharacterPreviewTabID);
}

AssetEditorViewportFactoryFunction FMetaHumanCharacterEditorToolkit::GetViewportDelegate()
{
	AssetEditorViewportFactoryFunction ViewportDelegateFunction = [this](FAssetEditorViewportConstructionArgs InArgs)
	{
		return SNew(SMetaHumanCharacterEditorViewport, InArgs)
			.EditorViewportClient(ViewportClient);
	};

	return ViewportDelegateFunction;
}

TSharedPtr<FEditorViewportClient> FMetaHumanCharacterEditorToolkit::CreateEditorViewportClient() const
{
	UMetaHumanCharacter* MetaHumanCharacter = CastChecked<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();

	return MakeShared<FMetaHumanCharacterViewportClient>(EditorModeManager.Get(), PreviewScene.Get(), PreviewActor, MetaHumanCharacter);
}

void FMetaHumanCharacterEditorToolkit::PostInitAssetEditor()
{
	// Make sure the viewport is always available as the mode will try to add an overlay to it
	if (!TabManager->FindExistingLiveTab(ViewportTabID))
	{
		TabManager->TryInvokeTab(ViewportTabID);
	}

	// default hide the details tab
	TSharedPtr<SDockTab> DetailsTab = TabManager->FindExistingLiveTab(FBaseAssetToolkit::DetailsTabID);
	if (DetailsTab.IsValid())
	{
		DetailsTab->RequestCloseTab();
	}

	check(ToolkitHost.IsValid());
	TSharedPtr<IToolkitHost> PinnedToolkitHost = ToolkitHost.Pin();
	ModeUILayer = MakeShared<FMetaHumanCharacterEditorModeUILayer>(PinnedToolkitHost.Get());
	ModeUILayer->SetModeMenuCategory(MetaHumanCharacterEditorMenuCategory);

	// Currently, aside from setting up all the UI elements, the toolkit also kicks off the
	// editor mode, which is the mode that the editor always works in (things are packaged into
	// a mode so that they can be moved to another asset editor if necessary).
	check(EditorModeManager.IsValid());
	EditorModeManager->ActivateMode(UMetaHumanCharacterEditorMode::EM_MetaHumanCharacterEditorModeId);

	TNotNull<UMetaHumanCharacter*> Character = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	GetMetaHumanCharacterEditorMode()->SetCharacter(Character);

	ExtendToolbar();
	ExtendMenu();
	BindCommands();

	USelection* SelectedActors = EditorModeManager->GetSelectedActors();
	USelection* SelectedComponents = EditorModeManager->GetSelectedComponents();
	check(SelectedActors && SelectedComponents);
	// The selection set of the editor mode manager is used by the tools
	// to determine which ones can be built, this can be mechanism to enable
	// disable tools depending on which part of the character we are editing
	// SelectedActors->Select(PreviewActor);
	// 
	// TODO: Remove this. Select the Face component to enable tools that rely on USkeletalMeshComponentToolTargetFactory
	// The logic to handle which component is selected should be handled by the Mode Toolkit since it knows category of tools
	// the user enabled
	// 
	// Cast away const-ness to suit the mode manager API. The face component will not be modified.
	SelectedComponents->Select(const_cast<USkeletalMeshComponent*>(static_cast<const USkeletalMeshComponent*>(PreviewActor->GetFaceComponent())));

	// We need the viewport client to start out focused, or else it won't get ticked until
	// we click inside it. This makes sure streaming of assets will actually finish before
	// the user clicks on the viewport
	ViewportClient->ReceivedFocus(ViewportClient->Viewport);

	UMetaHumanCharacter* MetaHumanCharacter = CastChecked<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	check(MetaHumanCharacter);
	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>();

	// Bind to light environment delegate so we can update the preview scene
	MetaHumanCharacterSubsystem->OnNotifyLightingEnvironmentChanged(MetaHumanCharacter).BindSP(this, &FMetaHumanCharacterEditorToolkit::OnEnvironmentChanged);
	MetaHumanCharacterSubsystem->OnLightRotationChanged(MetaHumanCharacter).BindSP(this, &FMetaHumanCharacterEditorToolkit::OnLightRotationChanged);
	MetaHumanCharacterSubsystem->OnBackgroundColorChanged(MetaHumanCharacter).BindSP(this, &FMetaHumanCharacterEditorToolkit::OnBackgroundColorChanged);

	// Set widget of viewport client
	TSharedRef<FMetaHumanCharacterViewportClient> MHCViewportClient = StaticCastSharedPtr<FMetaHumanCharacterViewportClient>(ViewportClient).ToSharedRef();
	TSharedPtr<SEditorViewport> ViewportWidget = StaticCastSharedPtr<SMetaHumanCharacterEditorViewport>(ViewportTabContent->GetFirstViewport());
	MHCViewportClient->SetViewportWidget(ViewportWidget);
	
	MetaHumanCharacterSubsystem->OnCameraFocusRequested(MetaHumanCharacter).BindSP(MHCViewportClient, &FMetaHumanCharacterViewportClient::HandleCameraFocusRequest);

	// Load all of the lighting environments which are represented as streaming levels
	TArray<FSoftObjectPath> LightingScenarioPaths =
	{
		FSoftObjectPath(TEXT("/" UE_PLUGIN_NAME "/LightingEnvironments/Studio.Studio")),
		FSoftObjectPath(TEXT("/" UE_PLUGIN_NAME "/LightingEnvironments/Split.Split")),
		FSoftObjectPath(TEXT("/" UE_PLUGIN_NAME "/LightingEnvironments/Fireside.Fireside")),
		FSoftObjectPath(TEXT("/" UE_PLUGIN_NAME "/LightingEnvironments/Moonlight.Moonlight")),
		FSoftObjectPath(TEXT("/" UE_PLUGIN_NAME "/LightingEnvironments/Tungsten.Tungsten")),
		FSoftObjectPath(TEXT("/" UE_PLUGIN_NAME "/LightingEnvironments/Portrait.Portrait")),
		FSoftObjectPath(TEXT("/" UE_PLUGIN_NAME "/LightingEnvironments/RedLantern.RedLantern")),
		FSoftObjectPath(TEXT("/" UE_PLUGIN_NAME "/LightingEnvironments/TextureBooth.TextureBooth"))
	};

	FSoftObjectPath BaseEnvironment = FSoftObjectPath(TEXT("/" UE_PLUGIN_NAME "/LightingEnvironments/L_BaseEnvironment.L_BaseEnvironment"));
	TArray<FSoftObjectPath> PostProcessLevelsPaths = 
	{
		FSoftObjectPath(TEXT("/" UE_PLUGIN_NAME "/LightingEnvironments/L_BaseEnvironment.L_BaseEnvironment")),
	};

	ResidentLevelPaths.Append(LightingScenarioPaths);
	ResidentLevelPaths.Add(BaseEnvironment);

	EnsureValidLightingEnvIsSelected();
	LoadPostProcessScenariosInWorld(BaseEnvironment);
	LoadLightingScenariosInWorld(LightingScenarioPaths);

	IAssetRegistry::Get()->OnAssetUpdated().AddSPLambda(this, [this](const FAssetData& InAssetData)
	{
		if (InAssetData.AssetClassPath != UWorld::StaticClass()->GetClassPathName())
		{
			return;
		}

		const UMetaHumanCharacter* Character = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
		check(Character);
		bool bUpdateEnvironment = SelectedCustomLightingEnvStreamingLevels.ContainsByPredicate([&InAssetData](const TWeakObjectPtr<ULevelStreaming>& InSelectedCustomLightingEnvStreamingLevel)
		{
			return InSelectedCustomLightingEnvStreamingLevel.IsValid() && InSelectedCustomLightingEnvStreamingLevel->PackageNameToLoad == InAssetData.PackageName;
		});

		bUpdateEnvironment |= Character->ViewportSettings.CharacterEnvironment != EMetaHumanCharacterEnvironment::Custom && ResidentLevelPaths.Contains(InAssetData.ToSoftObjectPath());

		if (bUpdateEnvironment)
		{
			OnEnvironmentChanged();
		}
	});

	UMetaHumanCharacterEditorSettings* Settings = GetMutableDefault<UMetaHumanCharacterEditorSettings>();
	check(Settings);
	if (!Settings->OnCustomLightPresetsChanged.IsBoundToObject(this))
	{
		Settings->OnCustomLightPresetsChanged.AddSPLambda(this, [this]()
		{
			if (EnsureValidLightingEnvIsSelected())
			{
				OnEnvironmentChanged();
			}
		});
	}

	// Rendering Quality Profiles
	if (!Settings->IsValidRenderingQualityProfileIndex(MetaHumanCharacter->ViewportSettings.RenderingQualityProfileIndex))
	{
		// UMetaHumanCharacterEditorSettings::PostInitProperties ensures we have Epic RenderingQualityProfile at-least
		check(Settings->IsValidRenderingQualityProfileIndex(0));
		if (RenderingQualityWidget.IsValid())
		{
			RenderingQualityWidget->SetSelectedItem(0);
		}
		else
		{
			OnRenderingQualityProfileUpdate(0);
		}
	}

	// Callback for when rendering quality profile is changed from viewport toolbar
	MetaHumanCharacterSubsystem->OnViewportToolbarRenderingQualityProfileChange(Character).BindSPLambda(this, [this](const int32 InIndex)
	{
		if (RenderingQualityWidget.IsValid())
		{
			RenderingQualityWidget->SetSelectedItem(InIndex);
		}
	});
}

void FMetaHumanCharacterEditorToolkit::ExtendToolbar()
{
	const FName MainToolbarMenuName = GetToolMenuToolbarName();
	const FName SectionName = UToolMenus::JoinMenuPaths(MainToolbarMenuName, TEXT("DynamicToolbarSection"));

	if (UToolMenu* ToolBarMenu = UToolMenus::Get()->ExtendMenu(MainToolbarMenuName))
	{
		// Define the dynamic section only once and use the UMetaHumanCharacterAssetEditorContext
		// to get the state of the open asset
		if (!ToolBarMenu->FindSection(SectionName))
		{
			ToolBarMenu->AddDynamicSection(SectionName, FNewToolMenuDelegate::CreateLambda([this](UToolMenu* InMenu)
			{
				UMetaHumanCharacterAssetEditorContext* Context = InMenu->FindContext<UMetaHumanCharacterAssetEditorContext>();
				if (Context && Context->MetaHumanCharacterAssetEditor.IsValid())
				{
					FMetaHumanCharacterEditorToolkit* AssetEditor = Context->MetaHumanCharacterAssetEditor.Pin().Get();
					FToolMenuSection& CharacterToolsSection = InMenu->AddSection(TEXT("MetaHumanCharacterTools"));

					/*
					// Disable save thumbnail for now
					CharacterToolsSection.AddEntry
					(
						FToolMenuEntry::InitToolBarButton
						(
							FMetaHumanCharacterEditorCommands::Get().SaveThumbnail,
							FMetaHumanCharacterEditorCommands::Get().SaveThumbnail->GetLabel(),
							FMetaHumanCharacterEditorCommands::Get().SaveThumbnail->GetDescription(),
							FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), TEXT("MetaHumanCharacterEditor.Toolbar.SaveThumbnail"))
						)
					);

					CharacterToolsSection.AddSeparator(NAME_None);
					*/

					CharacterToolsSection.AddEntry
					(
						FToolMenuEntry::InitToolBarButton
						(
							FMetaHumanCharacterEditorCommands::Get().AutoRigFaceBlendShapes,
							FMetaHumanCharacterEditorCommands::Get().AutoRigFaceBlendShapes->GetLabel(),
							FMetaHumanCharacterEditorCommands::Get().AutoRigFaceBlendShapes->GetDescription(),
							FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), TEXT("MetaHumanCharacterEditor.Toolbar.AddRigFull"))
						)
					);

					CharacterToolsSection.AddEntry
					(
						FToolMenuEntry::InitToolBarButton
						(
							FMetaHumanCharacterEditorCommands::Get().AutoRigFaceJointsOnly,
							FMetaHumanCharacterEditorCommands::Get().AutoRigFaceJointsOnly->GetLabel(),
							FMetaHumanCharacterEditorCommands::Get().AutoRigFaceJointsOnly->GetDescription(),
							FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), TEXT("MetaHumanCharacterEditor.Toolbar.AddRigSkeletal"))
						)
					);

					CharacterToolsSection.AddEntry
					(
						FToolMenuEntry::InitToolBarButton
						(
							FMetaHumanCharacterEditorCommands::Get().RemoveFaceRig,
							FMetaHumanCharacterEditorCommands::Get().RemoveFaceRig->GetLabel(),
							FMetaHumanCharacterEditorCommands::Get().RemoveFaceRig->GetDescription(),
							FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), TEXT("MetaHumanCharacterEditor.Toolbar.RemoveRig"))
						)
					);
					
					CharacterToolsSection.AddSeparator(NAME_None);

					CharacterToolsSection.AddEntry
					(
						FToolMenuEntry::InitToolBarButton
						(
							FMetaHumanCharacterEditorCommands::Get().DownloadTextureSources,
							TAttribute<FText>::CreateSP(AssetEditor, &FMetaHumanCharacterEditorToolkit::GetDownloadTextureSourcesLabel),
							TAttribute<FText>::CreateSP(AssetEditor, &FMetaHumanCharacterEditorToolkit::GetDownloadTextureSourcesTooltip)
						)
					);

					CharacterToolsSection.AddSeparator(NAME_None);

					CharacterToolsSection.AddEntry
					(
						FToolMenuEntry::InitToolBarButton
						(
							FMetaHumanCharacterEditorCommands::Get().RefreshPreview,
							FMetaHumanCharacterEditorCommands::Get().RefreshPreview->GetLabel(),
							FMetaHumanCharacterEditorCommands::Get().RefreshPreview->GetDescription(),
							FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("Icons.Refresh"))
						)
					);

					CharacterToolsSection.AddSeparator(NAME_None);

					CharacterToolsSection.AddEntry
					(
						FToolMenuEntry::InitWidget
						(
							TEXT("AuthenticationMenuButton"),
							SNew(SMetaHumanAuthenticationMenuButton),
							LOCTEXT("AuthenticationMenuButton_Label", "Authentication Menu")
						)
					);
				}
			}));
		}
	}
}

void FMetaHumanCharacterEditorToolkit::ExtendMenu()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	const FMetaHumanCharacterEditorCommands& Commands = FMetaHumanCharacterEditorCommands::Get();
	const FName MHCMenuName = UToolMenus::JoinMenuPaths(GetToolMenuName(), TEXT("MetaHumanCharacter"));
	const FName MHCSectionName = UToolMenus::JoinMenuPaths(MHCMenuName, TEXT("MetaHumanCharacterSectionName"));

	if (!ToolMenus->IsMenuRegistered(MHCMenuName))
	{
		UToolMenu* MHCMainMenu = ToolMenus->RegisterMenu(MHCMenuName);
		MHCMainMenu->AddDynamicSection(MHCSectionName, FNewToolMenuDelegate::CreateLambda([Commands](UToolMenu* InMenu)
		{
			UMetaHumanCharacterAssetEditorContext* Context = InMenu->FindContext<UMetaHumanCharacterAssetEditorContext>();
			if (Context && Context->MetaHumanCharacterAssetEditor.IsValid())
			{				
				FToolMenuSection& Section = InMenu->AddSection(TEXT("MetaHumanCharacterAssetServicesActions"), LOCTEXT("MetaHumanCharacterAssetServicesActionsSection", "MetaHuman Character Online Services"));

				Section.AddMenuEntry(FMetaHumanCharacterEditorCommands::Get().DownloadTextureSources);

				Section.AddMenuEntry
				(
					Commands.AutoRigFaceBlendShapes,
					FMetaHumanCharacterEditorCommands::Get().AutoRigFaceBlendShapes->GetLabel(),
					FMetaHumanCharacterEditorCommands::Get().AutoRigFaceBlendShapes->GetDescription(),
					FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), TEXT("MetaHumanCharacterEditor.Toolbar.AddRigFull"))
				);

				Section.AddMenuEntry
				(
					Commands.AutoRigFaceJointsOnly,
					FMetaHumanCharacterEditorCommands::Get().AutoRigFaceJointsOnly->GetLabel(),
					FMetaHumanCharacterEditorCommands::Get().AutoRigFaceJointsOnly->GetDescription(),
					FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), TEXT("MetaHumanCharacterEditor.Toolbar.AddRigSkeletal"))
				);

				// Add the data menu
				{
					FMetaHumanCharacterEditorDebugCommands DataCommands = FMetaHumanCharacterEditorDebugCommands::Get();
					FToolMenuSection& DataSection = InMenu->AddSection(TEXT("MetaHumanCharacterDataActions"), LOCTEXT("MetaHumanCharacterDataActionsSection", "MetaHuman Character Data"));

					// Identity state
					//DataSection.AddMenuEntry(DataCommands.DebugSaveFaceState);
					//DataSection.AddMenuEntry(DataCommands.DebugSaveFaceStateToDNA);
					//DataSection.AddMenuEntry(DataCommands.DebugDumpFaceStateDataForAR);
					//DataSection.AddMenuEntry(DataCommands.DebugSaveBodyState);

					// Textures
					DataSection.AddMenuEntry(DataCommands.SaveFaceTextures);

					// Presets
					//DataSection.AddMenuEntry(DataCommands.SaveEyePreset);

					// Screenshot
					DataSection.AddMenuEntry(DataCommands.TakeHighResScreenshot);

					DataSection.AddMenuEntry(TEXT("CloudAuthentication"),
						LOCTEXT("MetaHuman.Cloud.Authentication", "Authentication"),
						FText::GetEmpty(),
						FSlateIcon(),
						FToolUIActionChoice(
							FExecuteAction::CreateLambda([Context]
								{
									UE::MetaHuman::ServiceAuthentication::CheckHasLoggedInUserAsync(UE::MetaHuman::ServiceAuthentication::FOnCheckHasLoggedInUserCompleteDelegate::CreateLambda([](bool bLoggedIn, FString InAccountIdString, FString InAccountUserName)
										{
											static FText WindowTitle = FText::FromString(TEXT("MetaHuman Cloud Authentication"));
											if (bLoggedIn)
											{
												EAppReturnType::Type Result = UEditorDialogLibrary::ShowMessage(WindowTitle,
													FText::Format(LOCTEXT("MetaHuman.Cloud.Authentication.LoggedIn", "{0} (ID {1}) is logged in\nSign out?"), FText::FromString(InAccountUserName), FText::FromString(InAccountIdString)), EAppMsgType::OkCancel);
												if (Result == EAppReturnType::Ok)
												{
													UE::MetaHuman::ServiceAuthentication::LogoutFromAuthEnvironment(UE::MetaHuman::ServiceAuthentication::FOnLogoutCompleteDelegate::CreateLambda([InAccountIdString] {
														UEditorDialogLibrary::ShowMessage(WindowTitle,
															FText::Format(LOCTEXT("MetaHuman.Cloud.Authentication.LoggedOut", "Account ID {0} signed out"), FText::FromString(InAccountIdString)), EAppMsgType::Ok);
														}));
												}
											}
											else
											{
												UEditorDialogLibrary::ShowMessage(WindowTitle,
													LOCTEXT("MetaHuman.Cloud.Authentication.NoUserLoggedIn", "No user logged in, please autorig to trigger log-in flow"), EAppMsgType::Ok);
											}
										}));
								})));

				}
			}
		}));
	}

	const FName CharacterMainMenuName = UToolMenus::JoinMenuPaths(GetToolMenuName(), TEXT("MetaHumanCharacter"));

	if (!ToolMenus->IsMenuRegistered(CharacterMainMenuName))
	{
		ToolMenus->RegisterMenu(CharacterMainMenuName, MHCMenuName);
	}

	if (UToolMenu* MainMenu = ToolMenus->ExtendMenu(GetToolMenuName()))
	{
		const FToolMenuInsert MenuInsert{ TEXT("Tools"), EToolMenuInsertType::After };

		FToolMenuSection& Section = MainMenu->FindOrAddSection(NAME_None);

		FToolMenuEntry& MetaHumanCharacterEntry = Section.AddSubMenu(TEXT("MetaHumanCharacter"),
			LOCTEXT("MetaHumanCharacterEditorMenuLabel", "MetaHuman Character"),
			LOCTEXT("MetaHumanCharacterEditorMenuTooltip", "Commands used for MetaHuman Character"),
			FNewToolMenuChoice{});

		MetaHumanCharacterEntry.InsertPosition = MenuInsert;
	}
}

void FMetaHumanCharacterEditorToolkit::BindCommands()
{
	ToolkitCommands->MapAction(FMetaHumanCharacterEditorCommands::Get().DownloadTextureSources,
							   FExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::RequestTextureSources),
							   FCanExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::CanRequestTextureSources));

	ToolkitCommands->MapAction(FMetaHumanCharacterEditorCommands::Get().AutoRigFaceJointsOnly,
	                           FExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::AutoRigFace, EMetaHumanRigType::JointsOnly),
	                           FCanExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::CanAutoRigFace));
	
	ToolkitCommands->MapAction(FMetaHumanCharacterEditorCommands::Get().AutoRigFaceBlendShapes,
	                           FExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::AutoRigFace, EMetaHumanRigType::JointsAndBlendShapes),
	                           FCanExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::CanAutoRigFace));
	
	ToolkitCommands->MapAction(FMetaHumanCharacterEditorCommands::Get().RemoveFaceRig,
	                           FExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::RemoveFaceRig),
	                           FCanExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::CanRemoveFaceRig));
		
	ToolkitCommands->MapAction(FMetaHumanCharacterEditorCommands::Get().RefreshPreview,
	                           FExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::RefreshPreview));

	// Add the debug menu if enabled
	{
		ToolkitCommands->MapAction(FMetaHumanCharacterEditorDebugCommands::Get().SaveFaceState,
		                           FExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::SaveFaceState),
		                           FCanExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::CanSaveStates));
		
		ToolkitCommands->MapAction(FMetaHumanCharacterEditorDebugCommands::Get().SaveFaceStateToDNA,
		                           FExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::SaveFaceStateToDNA),
		                           FCanExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::CanSaveStates));
		
		ToolkitCommands->MapAction(FMetaHumanCharacterEditorDebugCommands::Get().DumpFaceStateDataForAR,
		                           FExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::DumpFaceStateDataForAR),
		                           FCanExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::CanSaveStates));
		
		ToolkitCommands->MapAction(FMetaHumanCharacterEditorDebugCommands::Get().SaveBodyState,
		                           FExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::SaveBodyState),
		                           FCanExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::CanSaveStates));
		
		ToolkitCommands->MapAction(FMetaHumanCharacterEditorDebugCommands::Get().SaveFaceTextures,
		                           FExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::SaveFaceTextures),
		                           FCanExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::CanSaveTextures));
		
		ToolkitCommands->MapAction(FMetaHumanCharacterEditorDebugCommands::Get().SaveEyePreset,
		                           FExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::SaveEyePreset),
		                           FCanExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::CanSaveEyePreset));

		ToolkitCommands->MapAction(FMetaHumanCharacterEditorDebugCommands::Get().TakeHighResScreenshot,
								   FExecuteAction::CreateSP(this, &FMetaHumanCharacterEditorToolkit::TakeHighResScreenshot),
								   FCanExecuteAction());
	}
}

void FMetaHumanCharacterEditorToolkit::OnToolkitHostingStarted(const TSharedRef<IToolkit>& InToolkit)
{
	ModeUILayer->OnToolkitHostingStarted(InToolkit);
}

void FMetaHumanCharacterEditorToolkit::OnToolkitHostingFinished(const TSharedRef<IToolkit>& InToolkit)
{
	ModeUILayer->OnToolkitHostingFinished(InToolkit);
}

bool FMetaHumanCharacterEditorToolkit::CanRequestTextureSources() const
{
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

	UMetaHumanCharacter* Character = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	check(Character);

	return Character->HasSynthesizedTextures() &&
		!Subsystem->IsRequestingHighResolutionTextures(Character) &&
		Subsystem->IsTextureSynthesisEnabled();
}

void FMetaHumanCharacterEditorToolkit::RequestTextureSources()
{
	GetMetaHumanCharacterEditorMode()->RestartCurrentlyActiveTool();

	UMetaHumanCharacter* Character = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	check(Character);

	UMetaHumanCharacterEditorSubsystem::Get()->RequestTextureSources(Character,
																	 FMetaHumanCharacterTextureRequestParams
																	 {
																		 .bReportProgress = true,
																		 .bBlocking = false,
																	 });
}

void FMetaHumanCharacterEditorToolkit::OnLightingStudioEnvironmentChanged(const EMetaHumanCharacterEnvironment NewStudioEnvironment)
{
	check(PreviewScene->GetWorld());

	// Capture the parent light rig rotation before changing the environment so we
	// can restore it after the new level has streamed in.
	CurrentLightRigParentRotation.Reset();
	for (FActorIterator It(PreviewScene->GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->GetClass()->ImplementsInterface(UMetaHumanCharacterEnvironmentLightRig::StaticClass()))
		{
			if (const AActor* ParentActor = Actor->GetAttachParentActor())
			{
				CurrentLightRigParentRotation = ParentActor->GetActorRotation();
				break;
			}
		}
	}

	FString NewStudioEnvironmentName = StaticEnum<EMetaHumanCharacterEnvironment>()->GetAuthoredNameStringByValue(static_cast<uint8>(NewStudioEnvironment));

	for (ULevelStreaming* LevelStreaming : PreviewScene->GetWorld()->GetStreamingLevels())
	{
		// Skip post process levels
		if (PostProcessLevels.Contains(LevelStreaming))
		{
			continue;
		}

		const FSoftObjectPath StreamingLevelPath = LevelStreaming->GetWorldAsset().ToSoftObjectPath();
		FString LightingScenarioName = StreamingLevelPath.GetAssetName();

		if (NewStudioEnvironmentName == LightingScenarioName)
		{
			LevelStreaming->SetShouldBeVisibleInEditor(true);
		}
		else
		{
			LevelStreaming->SetShouldBeVisibleInEditor(false);
		}
	}

	PreviewScene->GetWorld()->FlushLevelStreaming(EFlushLevelStreamingType::Full);

	// Restore the parent light rig rotation on the new level's rig.
	if (CurrentLightRigParentRotation.IsSet())
	{
		SetLightRigParentRotation(CurrentLightRigParentRotation.GetValue());
	}

	UMetaHumanCharacter* MetaHumanCharacter = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	OnLightRotationChanged(MetaHumanCharacter->ViewportSettings.LightRotation);
}

void FMetaHumanCharacterEditorToolkit::SetLightRigParentRotation(const FRotator& InRotation)
{
	UWorld* World = PreviewScene ? PreviewScene->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	for (FActorIterator It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->GetClass()->ImplementsInterface(UMetaHumanCharacterEnvironmentLightRig::StaticClass()))
		{
			if (AActor* ParentActor = Actor->GetAttachParentActor())
			{
				ParentActor->SetActorRotation(InRotation);
				return;
			}
		}
	}
}

void FMetaHumanCharacterEditorToolkit::OnEnvironmentChanged()
{
	check(PreviewScene->GetWorld());

	// Capture the parent light rig rotation before streaming the new environment
	// so we can restore it on the new level's rig and avoid a visible snap.
	CurrentLightRigParentRotation.Reset();
	for (FActorIterator It(PreviewScene->GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->GetClass()->ImplementsInterface(UMetaHumanCharacterEnvironmentLightRig::StaticClass()))
		{
			if (const AActor* ParentActor = Actor->GetAttachParentActor())
			{
				CurrentLightRigParentRotation = ParentActor->GetActorRotation();
				break;
			}
		}
	}

	FMetaHumanCharacterViewportSettings CharacterViewportSettings = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit()->ViewportSettings;

	FString NewStudioEnvironmentName;
	if (CharacterViewportSettings.CharacterEnvironment == EMetaHumanCharacterEnvironment::Custom && !CharacterViewportSettings.CustomLightingEnvironment.IsNull())
	{
		NewStudioEnvironmentName = CharacterViewportSettings.CustomLightingEnvironment.GetAssetName();
	}
	else
	{
		NewStudioEnvironmentName = StaticEnum<EMetaHumanCharacterEnvironment>()->GetAuthoredNameStringByValue(static_cast<uint8>(CharacterViewportSettings.CharacterEnvironment));
	}

	PostProcessLevels[0]->SetShouldBeVisibleInEditor(CharacterViewportSettings.CharacterEnvironment != EMetaHumanCharacterEnvironment::Custom);

	UnloadCustomLightingEnvironment();

	bool bLevelAlreadyLoaded = false;

	for (ULevelStreaming* LevelStreaming : PreviewScene->GetWorld()->GetStreamingLevels())
	{
		// Skip post process levels or LevelStreaming requested for unload and removal (in case of custom lighting environment being unloaded)
		if (PostProcessLevels.Contains(LevelStreaming) || LevelStreaming->GetIsRequestingUnloadAndRemoval())
		{
			continue;
		}

		const FSoftObjectPath StreamingLevelPath = LevelStreaming->GetWorldAsset().ToSoftObjectPath();
		FString LightingScenarioName = StreamingLevelPath.GetAssetName();

		if (NewStudioEnvironmentName == LightingScenarioName)
		{
			LevelStreaming->SetShouldBeVisibleInEditor(true);
			bLevelAlreadyLoaded = true;
		}
		else
		{
			LevelStreaming->SetShouldBeVisibleInEditor(false);
		}
	}

	if (!bLevelAlreadyLoaded && CharacterViewportSettings.CharacterEnvironment == EMetaHumanCharacterEnvironment::Custom && !CharacterViewportSettings.CustomLightingEnvironment.IsNull())
	{
		LoadCustomLightingEnvironment(CharacterViewportSettings.CustomLightingEnvironment.ToSoftObjectPath());
	}

	PreviewScene->GetWorld()->FlushLevelStreaming(EFlushLevelStreamingType::Full);

	// Refresh after the streaming flush so any light rigs / background actors in sublevels or LevelInstances have spawned.
	RefreshEnvironmentCapabilityState();

	// Restore the parent light rig rotation on the new level's rig.
	if (CurrentLightRigParentRotation.IsSet())
	{
		SetLightRigParentRotation(CurrentLightRigParentRotation.GetValue());
	}

	OnLightRotationChanged(CharacterViewportSettings.LightRotation);
	OnBackgroundColorChanged(CharacterViewportSettings.BackgroundColor);
}

void FMetaHumanCharacterEditorToolkit::OnLightRotationChanged(float InRotation)
{
	check(PreviewScene->GetWorld());

	for (FActorIterator It(PreviewScene->GetWorld()); It; ++It)
	{
		AActor* Actor = *It;

		if (Actor->GetClass()->ImplementsInterface(UMetaHumanCharacterEnvironmentLightRig::StaticClass()))
		{
			IMetaHumanCharacterEnvironmentLightRig::Execute_SetRotation(Actor, InRotation);
		}
	}
}

void FMetaHumanCharacterEditorToolkit::RefreshEnvironmentCapabilityState()
{
	check(PreviewScene->GetWorld());

	bool bHasLightRig = false;
	bool bHasBackground = false;
	for (FActorIterator It(PreviewScene->GetWorld()); It; ++It)
	{
		const AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		if (!bHasLightRig && Actor->GetClass()->ImplementsInterface(UMetaHumanCharacterEnvironmentLightRig::StaticClass()))
		{
			bHasLightRig = true;
		}

		if (!bHasBackground && Actor->GetClass()->ImplementsInterface(UMetaHumanCharacterEnvironmentBackground::StaticClass()))
		{
			bHasBackground = true;
		}

		if (bHasLightRig && bHasBackground)
		{
			break;
		}
	}

	if (TSharedPtr<FMetaHumanCharacterViewportClient> MHCViewportClient = StaticCastSharedPtr<FMetaHumanCharacterViewportClient>(ViewportClient))
	{
		MHCViewportClient->SetEnvironmentHasLightRig(bHasLightRig);
		MHCViewportClient->SetEnvironmentHasBackground(bHasBackground);
	}

	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	if (!Settings || !Settings->bValidateLightingEnvironments)
	{
		return;
	}

	const UMetaHumanCharacter* Character = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	bool bAnyMessage = false;

	if (!bHasLightRig)
	{
		TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Warning);
		Message->AddToken(FUObjectToken::Create(Character));
		Message->AddText(LOCTEXT(
			"LightingEnvironmentMissingLightRig_Body",
			": Current lighting environment has no light rig. Add"));
		Message->AddToken(FAssetNameToken::Create(TEXT("/" UE_PLUGIN_NAME "/LightingEnvironments/Dependencies/LightMouseRotator.LightMouseRotator")));
		Message->AddText(LOCTEXT(
			"LightingEnvironmentMissingLightRig_Tail",
			"to the level to enable rotation."));

		FMessageLog(UE::MetaHuman::MessageLogName).AddMessage(Message);
		bAnyMessage = true;
	}

	if (!bHasBackground)
	{
		TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Warning);
		Message->AddToken(FUObjectToken::Create(Character));
		Message->AddText(LOCTEXT(
			"LightingEnvironmentMissingBackground_Body",
			": Current lighting environment has no background actor. Add"));
		Message->AddToken(FAssetNameToken::Create(TEXT("/" UE_PLUGIN_NAME "/LightingEnvironments/Dependencies/BP_HalfSphereBackground.BP_HalfSphereBackground")));
		Message->AddText(LOCTEXT(
			"LightingEnvironmentMissingBackground_Tail",
			"to the level to enable the background color picker."));

		FMessageLog(UE::MetaHuman::MessageLogName).AddMessage(Message);
		bAnyMessage = true;
	}

	if (bAnyMessage)
	{
		FMessageLog(UE::MetaHuman::MessageLogName).Open();
	}
}

void FMetaHumanCharacterEditorToolkit::WarnIfWorldPartitionEnvironment(const FSoftObjectPath& InEnvironmentPath, const UWorld* InLoadedWorld) const
{
	if (!InLoadedWorld || !InLoadedWorld->IsPartitionedWorld())
	{
		return;
	}

	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	if (!Settings || !Settings->bValidateLightingEnvironments)
	{
		return;
	}

	const UMetaHumanCharacter* Character = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();

	// FAssetNameToken targets the source asset by package path. FUObjectToken on the loaded world
	// would point at the temp package created via LoadLevelInstanceBySoftObjectPtr(bLoadAsTempPackage=true).
	TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Warning);
	Message->AddToken(FUObjectToken::Create(Character));
	Message->AddText(LOCTEXT(
		"CustomEnvironmentUsesWorldPartition_Body",
		": Custom lighting environment"));
	Message->AddToken(FAssetNameToken::Create(InEnvironmentPath.GetLongPackageName()));
	Message->AddText(LOCTEXT(
		"CustomEnvironmentUsesWorldPartition_Tail",
		"uses World Partition. World Partition levels are not supported in the MetaHuman Character editor and may not load correctly."));

	FMessageLog(UE::MetaHuman::MessageLogName).AddMessage(Message);
	FMessageLog(UE::MetaHuman::MessageLogName).Open();
}

void FMetaHumanCharacterEditorToolkit::OnBackgroundColorChanged(const FLinearColor& InBackgroundColor)
{
	check(PreviewScene->GetWorld());

	for (FActorIterator It(PreviewScene->GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->GetClass()->ImplementsInterface(UMetaHumanCharacterEnvironmentBackground::StaticClass()))
		{
			IMetaHumanCharacterEnvironmentBackground::Execute_SetBackgroundColor(Actor, InBackgroundColor);
		}
	}
}

void FMetaHumanCharacterEditorToolkit::OnRenderingQualityProfileUpdate(const int32 InActiveProfileIndex)
{
	TSharedRef<FMetaHumanCharacterViewportClient> MHCViewportClient = StaticCastSharedPtr<FMetaHumanCharacterViewportClient>(ViewportClient).ToSharedRef();
	MHCViewportClient->UpdateRenderingQuality(InActiveProfileIndex);

	UMetaHumanCharacter* MetaHumanCharacter = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	check(MetaHumanCharacter);
	MetaHumanCharacter->ViewportSettings.RenderingQualityProfileIndex = InActiveProfileIndex;
	MetaHumanCharacter->MarkPackageDirty();
}

void FMetaHumanCharacterEditorToolkit::OnTonemapperEnvironmentChanged(const bool InTonemapperEnabled)
{}

TNotNull<UMetaHumanCharacterEditorMode*> FMetaHumanCharacterEditorToolkit::GetMetaHumanCharacterEditorMode() const
{
	return CastChecked<UMetaHumanCharacterEditorMode>(EditorModeManager->GetActiveScriptableMode(UMetaHumanCharacterEditorMode::EM_MetaHumanCharacterEditorModeId));
}

bool FMetaHumanCharacterEditorToolkit::HasActiveTool() const
{
	return GetMetaHumanCharacterEditorMode()->GetInteractiveToolsContext()->HasActiveTool();
}

FText FMetaHumanCharacterEditorToolkit::GetDownloadTextureSourcesLabel() const
{
	const TSharedPtr<FUICommandInfo> DownloadTexturesCommand = FMetaHumanCharacterEditorCommands::Get().DownloadTextureSources;

	if (UMetaHumanCharacter* Character = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit())
	{
		if (Character->NeedsToDownloadTextureSources())
		{
			return FText::Format(INVTEXT("{0} *"), DownloadTexturesCommand->GetLabel());
		}
	}

	return DownloadTexturesCommand->GetLabel();
}

FText FMetaHumanCharacterEditorToolkit::GetDownloadTextureSourcesTooltip() const
{
	const TSharedPtr<FUICommandInfo> DownloadTexturesCommand = FMetaHumanCharacterEditorCommands::Get().DownloadTextureSources;

	if (UMetaHumanCharacter* Character = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit())
	{
		const FMetaHumanCharacterTextureSourceResolutions& Resolutions = Character->SkinSettings.DesiredTextureSourcesResolutions;

		auto GetCurrentRes = [](const FInt32Point& Res) 
		{
			return Res.X / 1024;
		};

		const FInt32Point FaceAlbedoResolution = Character->GetSynthesizedFaceTexturesResolution(EFaceTextureType::Basecolor);
		const FInt32Point FaceNormalResolution = Character->GetSynthesizedFaceTexturesResolution(EFaceTextureType::Normal);
		const FInt32Point FaceCavityResolution = Character->GetSynthesizedFaceTexturesResolution(EFaceTextureType::Cavity);
		const FInt32Point FaceAnimMapsResolutions = Character->GetSynthesizedFaceTexturesResolution(EFaceTextureType::Basecolor_Animated_CM1);

		const FInt32Point BodyAlbedoResolution = Character->GetSynthesizedBodyTexturesResolution(EBodyTextureType::Body_Basecolor);
		const FInt32Point BodyNormalResolution = Character->GetSynthesizedBodyTexturesResolution(EBodyTextureType::Body_Normal);
		const FInt32Point BodyCavityResolution = Character->GetSynthesizedBodyTexturesResolution(EBodyTextureType::Body_Cavity);
		const FInt32Point BodyMasksResolution = Character->GetSynthesizedBodyTexturesResolution(EBodyTextureType::Body_Underwear_Mask);

		FText Tooltip = FText::FormatNamed(LOCTEXT("DownloadTextureSourcesTooltip",
										   "{Description} \n\n"
										   "The resolutions of each texture can be defined in the Skin tool under the \n"
										   "Materials group and are currently set as the following:\n\n"
										   "Face \n"
										   "\t Albedo: {FaceAlbedoDesired} (Current: {FaceAlbedoCurrent}k) \n"
										   "\t Normal: {FaceNormalDesired} (Current: {FaceNormalCurrent}k) \n"
										   "\t Cavity: {FaceCavityDesired} (Current: {FaceCavityCurrent}k) \n"
										   "\t Anim. Maps : {FaceAnimMapsDesired} (Current: {FaceAnimMapsCurrent}k) \n\n"
										   "Body \n"
										   "\t Albedo: {BodyAlbedoDesired} (Current: {BodyAlbedoCurrent}k) \n"
										   "\t Normal: {BodyNormalDesired} (Current: {BodyNormalCurrent}k) \n"
										   "\t Cavity: {BodyCavityDesired} (Current: {BodyCavityCurrent}k) \n"
										   "\t Masks: {BodyMasksDesired} (Current: {BodyMasksCurrent}k)"),
										   TEXT("Description"), DownloadTexturesCommand->GetDescription(),
										   TEXT("FaceAlbedoDesired"), UEnum::GetDisplayValueAsText(Resolutions.FaceAlbedo),
										   TEXT("FaceAlbedoCurrent"), GetCurrentRes(FaceAlbedoResolution),
										   TEXT("FaceNormalDesired"), UEnum::GetDisplayValueAsText(Resolutions.FaceNormal),
										   TEXT("FaceNormalCurrent"), GetCurrentRes(FaceNormalResolution),
										   TEXT("FaceCavityDesired"), UEnum::GetDisplayValueAsText(Resolutions.FaceCavity),
										   TEXT("FaceCavityCurrent"), GetCurrentRes(FaceCavityResolution),
										   TEXT("FaceAnimMapsDesired"), UEnum::GetDisplayValueAsText(Resolutions.FaceAnimatedMaps),
										   TEXT("FaceAnimMapsCurrent"), GetCurrentRes(FaceAnimMapsResolutions),
										   TEXT("BodyAlbedoDesired"), UEnum::GetDisplayValueAsText(Resolutions.BodyAlbedo),
										   TEXT("BodyAlbedoCurrent"), GetCurrentRes(BodyAlbedoResolution),
										   TEXT("BodyNormalDesired"), UEnum::GetDisplayValueAsText(Resolutions.BodyNormal),
										   TEXT("BodyNormalCurrent"), GetCurrentRes(BodyNormalResolution),
										   TEXT("BodyCavityDesired"), UEnum::GetDisplayValueAsText(Resolutions.BodyCavity),
										   TEXT("BodyCavityCurrent"), GetCurrentRes(BodyCavityResolution),
										   TEXT("BodyMasksDesired"), UEnum::GetDisplayValueAsText(Resolutions.BodyMasks),
										   TEXT("BodyMasksCurrent"), GetCurrentRes(BodyMasksResolution));

		return Tooltip;
	}

	
	return DownloadTexturesCommand->GetDescription();
}

bool FMetaHumanCharacterEditorToolkit::CanRemoveFaceRig() const
{
	UMetaHumanCharacter* MetaHumanCharacter = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	check(MetaHumanCharacter);

	return MetaHumanCharacter->HasFaceDNA();
}

void FMetaHumanCharacterEditorToolkit::RemoveFaceRig()
{
	UMetaHumanCharacter* MetaHumanCharacter = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	if (MetaHumanCharacter && MetaHumanCharacter->HasFaceDNA())
	{
		UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>();
		check(MetaHumanCharacterSubsystem);

		const FScopedTransaction Transaction(MetaHumanCharacterEditorToolkitTransactionContext, LOCTEXT("CharacterRemoveRigTransaction", "Remove Face Rig"), MetaHumanCharacter);
		MetaHumanCharacter->Modify();

		TArray<uint8> DNABuffer = MetaHumanCharacter->GetFaceDNABuffer();
		TSharedRef<FMetaHumanCharacterIdentity::FState> OriginalFaceState = MetaHumanCharacterSubsystem->CopyFaceState(MetaHumanCharacter);
		TArray<uint8> BodyDNABuffer = MetaHumanCharacter->GetBodyDNABuffer();
		TSharedRef<FMetaHumanCharacterBodyIdentity::FState> OriginalBodyState = MetaHumanCharacterSubsystem->CopyBodyState(MetaHumanCharacter);

		// If not a fixed body, remove body rig
		if (!MetaHumanCharacter->bFixedBodyType && MetaHumanCharacter->HasBodyDNA())
		{
			MetaHumanCharacterSubsystem->RemoveBodyRig(MetaHumanCharacter);
		}

		// remove the rig
		MetaHumanCharacterSubsystem->RemoveFaceRig(MetaHumanCharacter);

		TUniquePtr<FRemoveRigCommandChange> Change = MakeUnique<FRemoveRigCommandChange>(
			DNABuffer,
			OriginalFaceState,
			BodyDNABuffer,
			OriginalBodyState,
			MetaHumanCharacter);

		if (GUndo != nullptr)
		{
			GUndo->StoreUndo(MetaHumanCharacter, MoveTemp(Change));
		}
	}
	else
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Expected Character to have a Face DNA present");
	}
}


bool FMetaHumanCharacterEditorToolkit::CanAutoRigFace() const
{
	UMetaHumanCharacter* MetaHumanCharacter = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	check(MetaHumanCharacter);

	// Prevent auto-rigging if PIE is running since interchange will fail to import the DNA
	const bool bIsRunningPIE = GEditor && GEditor->IsPlaySessionInProgress();
	const bool bIsRigPending = UMetaHumanCharacterEditorSubsystem::Get()->GetRiggingState(MetaHumanCharacter) == EMetaHumanCharacterRigState::RigPending;

	return !bIsRunningPIE && !bIsRigPending;
}


void FMetaHumanCharacterEditorToolkit::AutoRigFace(EMetaHumanRigType InRigType)
{
	UMetaHumanCharacter* MetaHumanCharacter = Cast<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	check(MetaHumanCharacter);
	check(MetaHumanCharacter->IsCharacterValid());

	if (InRigType == EMetaHumanRigType::JointsAndBlendShapes)
	{
		FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
		constexpr uint64 GB = 1024 * 1024 * 1024;
		if (MemoryStats.AvailableVirtual < (10 * GB))
		{
			const FText NotEnoughMemoryTitle = LOCTEXT("ToolkitNotEnoughMemoryDialogTitle", "Not enough memory to auto-rig with blend shapes");
			const FText NotEnoughMemoryMessage = FText::Format(LOCTEXT("ToolkitNotEnoughMemoryDialogMessage", "Auto-rigging with blend shapes requires at least 10 GiB of free memory but only {0} is available.\n"
																									   "If you proceed the editor might crash. Would you like to continue?"),
															   FText::AsMemory(MemoryStats.AvailableVirtual));

			FSuppressableWarningDialog::FSetupInfo SetupInfo(NotEnoughMemoryMessage, NotEnoughMemoryTitle, TEXT("MetaHumanCharacterSuppressNotEnoughMemory"));
			SetupInfo.ConfirmText = LOCTEXT("ToolkitNotEnoughMemoryDialogConfirmText", "Yes");
			SetupInfo.CancelText = LOCTEXT("ToolkitNotEnoughMemoryDialogCancelText", "Cancel");

			const FSuppressableWarningDialog NotEnoughMemoryDialog{ SetupInfo };

			const FSuppressableWarningDialog::EResult Result = NotEnoughMemoryDialog.ShowModal();
			if (Result == FSuppressableWarningDialog::EResult::Cancel)
			{
				return;
			}
		}
	}

	UMetaHumanCharacterEditorSubsystem::Get()->RequestAutoRigging(MetaHumanCharacter,
																  FMetaHumanCharacterAutoRiggingRequestParams
																  {
																	  .RigType = InRigType,
																	  .bReportProgress = true,
																	  .bBlocking = false,
																  });
}

bool FMetaHumanCharacterEditorToolkit::CanSaveStates() const
{
	TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = CastChecked<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	return MetaHumanCharacter->IsCharacterValid();
}


void FMetaHumanCharacterEditorToolkit::SaveFaceState()
{
	TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = CastChecked<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	check(MetaHumanCharacter->IsCharacterValid());

	// The serialized state is a json string
	const FSharedBuffer FaceStateData = MetaHumanCharacter->GetFaceStateData();
	UE::MetaHuman::SaveBufferToFileWithDialog(FaceStateData);
}

void FMetaHumanCharacterEditorToolkit::SaveBodyState()
{
	GetMetaHumanCharacterEditorMode()->RestartCurrentlyActiveTool();
	
	TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = CastChecked<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	check(MetaHumanCharacter->IsCharacterValid());

	const FSharedBuffer BodyStateData = MetaHumanCharacter->GetBodyStateData();
	UE::MetaHuman::SaveBufferToFileWithDialog(BodyStateData);
}


void FMetaHumanCharacterEditorToolkit::SaveFaceStateToDNA()
{
	TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = CastChecked<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	check(MetaHumanCharacter->IsCharacterValid());
	TNotNull<UMetaHumanCharacterEditorSubsystem*> MetaHumanCharacterSubsystem = GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>();

	TSharedPtr<IDNAReader> OutFaceStateDNAReader;
	if (MetaHumanCharacter->HasFaceDNA())
	{
		// Use the stored DNA definition to save the state out if available
		TArray<uint8> FaceDNABuffer = MetaHumanCharacter->GetFaceDNABuffer();
		TSharedPtr<IDNAReader> FaceDNAReader = ReadDNAFromBuffer(&FaceDNABuffer);
		OutFaceStateDNAReader = MetaHumanCharacterSubsystem->GetFaceState(MetaHumanCharacter)->StateToDna(FaceDNAReader->Unwrap());
	}
	else
	{
		// Otherwise, use the dna from the preview skeletal mesh
		const USkeletalMesh* ConstFaceSkeletalMesh = MetaHumanCharacterSubsystem->GetFaceEditMesh(MetaHumanCharacter);
		if (TSharedPtr<IDNAReader> FaceDNAReader = USkelMeshDNAUtils::GetDNAReader(const_cast<USkeletalMesh*>(ConstFaceSkeletalMesh)))
		{
			OutFaceStateDNAReader = MetaHumanCharacterSubsystem->GetFaceState(MetaHumanCharacter)->StateToDna(FaceDNAReader->Unwrap());
		}
	}

	if (OutFaceStateDNAReader.IsValid())
	{
		if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
		{
			const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
			const FString DialogTitle = LOCTEXT("SaveFaceStateToDNADialogTitle", "Save Face State to DNA file").ToString();
			const FString DefaultPath = TEXT("");
			const FString DefaultFile = TEXT("");
			const FString FileTypes = TEXT("DNA file (*.dna)|*.dna");
			TArray<FString> DNAFilenames;
			if (DesktopPlatform->SaveFileDialog(ParentWindowHandle, DialogTitle, DefaultPath, DefaultFile, FileTypes, EFileDialogFlags::None, DNAFilenames))
			{
				if (DNAFilenames.Num() == 1)
				{
					WriteDNAToFile(OutFaceStateDNAReader.Get(), EDNADataLayer::All, *DNAFilenames[0]);
				}
			}
		}
		else
		{
			UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to retrieve Desktop Platform module");
		}
	}
	else
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to read the face DNA");
	}
}

void FMetaHumanCharacterEditorToolkit::DumpFaceStateDataForAR()
{
	TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = CastChecked<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	check(MetaHumanCharacter->IsCharacterValid());

	if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
	{
		// Prompt the user to select a folder where all the face textures will be saved
		const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		const FString DialogTitle = LOCTEXT("SaveFaceTexturesDialogTitle", "Save Face Textures folder").ToString();
		const FString DefaultPath = FPaths::ProjectSavedDir();
		FString OutputFolder;
		if (DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, DialogTitle, DefaultPath, OutputFolder))
		{
			TNotNull<UMetaHumanCharacterEditorSubsystem*> MetaHumanCharacterSubsystem = GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>();
			MetaHumanCharacterSubsystem->GetFaceState(MetaHumanCharacter)->WriteDebugAutoriggingData(OutputFolder);
		}
	}
	else
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to retrieve Desktop Platform module");
	}
}


bool FMetaHumanCharacterEditorToolkit::CanSaveTextures() const
{
	TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = CastChecked<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	if (MetaHumanCharacter->IsCharacterValid())
	{
		return MetaHumanCharacter->HasSynthesizedTextures();
	}

	return false;
}

bool FMetaHumanCharacterEditorToolkit::CanSaveEyePreset() const
{
	return !HasActiveTool();
}

void FMetaHumanCharacterEditorToolkit::SaveFaceTextures()
{
	TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = CastChecked<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	check(MetaHumanCharacter->IsCharacterValid());

	if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
	{
		// Prompt the user to select a folder where all the face textures will be saved
		const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		const FString DialogTitle = LOCTEXT("SaveFaceTexturesDialogTitle", "Save Face Textures folder").ToString();
		const FString DefaultPath = FPaths::ProjectSavedDir();
		FString OutputFolder;
		if (DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, DialogTitle, DefaultPath, OutputFolder))
		{
			const FString MetaHumanAssetName = MetaHumanCharacter->GetName();

			FScopedSlowTask SaveFaceTexturesTask(static_cast<int32>(EFaceTextureType::Count), LOCTEXT("SaveFaceTexturesTaskMessage", "Saving synthesized face textures"));
			SaveFaceTexturesTask.MakeDialog();

			for (const TPair<EFaceTextureType, FMetaHumanCharacterTextureInfo>& Pair: MetaHumanCharacter->SynthesizedFaceTexturesInfo)
			{
				const EFaceTextureType TextureType = Pair.Key;
				const FMetaHumanCharacterTextureInfo& TextureInfo = Pair.Value;

				SaveFaceTexturesTask.EnterProgressFrame();
				
				TFuture<FSharedBuffer> SynthesizedImageBuffer = MetaHumanCharacter->GetSynthesizedFaceTextureDataAsync(TextureType);
				if (!SynthesizedImageBuffer.Get().IsNull())
				{
					// Add the type of the texture as a suffix to the filename
					const FString TextureTypeName = StaticEnum<EFaceTextureType>()->GetAuthoredNameStringByValue(static_cast<int64>(TextureType));
					const FString OutFileName = OutputFolder / MetaHumanAssetName + TEXT("_") + TextureTypeName + TEXT(".png");

					FImageUtils::SaveImageByExtension(*OutFileName, FImageView(TextureInfo.ToImageInfo(), const_cast<void*>(SynthesizedImageBuffer.Get().GetData())));

					UE::MetaHuman::Analytics::RecordSaveHighResolutionTexturesEvent(MetaHumanCharacter);
				}
			}
		}
	}
	else
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to retrieve Desktop Platform module");
	}
}

void FMetaHumanCharacterEditorToolkit::SaveEyePreset()
{
	UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	UMetaHumanCharacterEyePresets* EyePresets = UMetaHumanCharacterEyePresets::Get();
	EyePresets->Modify();

	EyePresets->Presets.Emplace(FMetaHumanCharacterEyePreset
								{
									.EyesSettings = Character->EyesSettings
								});
}

void FMetaHumanCharacterEditorToolkit::TakeHighResScreenshot()
{
	if (ViewportClient)
	{
		// Need to reset the resolution for it to use the current viewport size
		GScreenshotResolutionX = 0;
		GScreenshotResolutionY = 0;

		ViewportClient->TakeHighResScreenShot();
	}
}

TSharedRef<SDockTab> FMetaHumanCharacterEditorToolkit::SpawnTab_AnimationBar(const FSpawnTabArgs& Args)
{
	TSharedRef<FMetaHumanCharacterViewportClient> MHCViewportClient = StaticCastSharedPtr<FMetaHumanCharacterViewportClient>(ViewportClient).ToSharedRef();
	return SNew(SDockTab)
		.CanEverClose(true)
		.Label(FText::GetEmpty())
		.ToolTipText(LOCTEXT("AnimationBarTabTooltip", "Animation Controls for MetaHuman Character"))
		[
			SNew(SMetaHumanCharacterEditorViewportAnimationBar)
				.Cursor(EMouseCursor::Default)
				.AnimationBarViewportClient(MHCViewportClient)
		];
}

TSharedRef<SDockTab> FMetaHumanCharacterEditorToolkit::SpawnTab_PreviewSceneDetails(const FSpawnTabArgs& Args)
{
	if(!PreviewSettingsWidget.IsValid())
	{
		GetPreviewSceneDescription();

		SAssignNew(PreviewSettingsWidget, SMetaHumanCharacterEditorPreviewSettingsView)
			.SettingsObject(PreviewSceneDescription.Get());
	}

	if (!RenderingQualityWidget.IsValid())
	{
		TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = CastChecked<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
		SAssignNew(RenderingQualityWidget, SMetaHumanCharacterEditorRenderingQualityView, MetaHumanCharacter->ViewportSettings.RenderingQualityProfileIndex)
			.OnRenderingQualityProfileUpdate(this, &FMetaHumanCharacterEditorToolkit::OnRenderingQualityProfileUpdate);
	}

	return SNew(SDockTab)
		.CanEverClose(true)
		.Label(FText::GetEmpty())
		.ToolTipText(LOCTEXT("MHPreviewSettingsTooltip", "Preview Settings for MetaHuman Character"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				PreviewSettingsWidget ? PreviewSettingsWidget.ToSharedRef() : SNullWidget::NullWidget
			]
			+ SVerticalBox::Slot()
			[
				RenderingQualityWidget ? RenderingQualityWidget.ToSharedRef() : SNullWidget::NullWidget
			]
		];
}

UMetaHumanCharacterEditorPreviewSceneDescription* FMetaHumanCharacterEditorToolkit::GetPreviewSceneDescription()
{
	return PreviewSceneDescription.Get();
}

void FMetaHumanCharacterEditorToolkit::InitPreviewSceneDetails()
{
	TNotNull<UMetaHumanCharacterEditorSubsystem*> MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = CastChecked<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	AMetaHumanInvisibleDrivingActor* DrivingActor = MetaHumanCharacterSubsystem->GetInvisibleDrivingActor(MetaHumanCharacter);

	PreviewSceneDescription = TStrongObjectPtr(NewObject<UMetaHumanCharacterEditorPreviewSceneDescription>(MetaHumanCharacter));
	
	PreviewSceneDescription->OnAnimationControllerChanged.BindLambda(
		[WeakDrivingActor = TWeakObjectPtr<AMetaHumanInvisibleDrivingActor>(DrivingActor)]
		(EMetaHumanCharacterAnimationController AnimationController, UAnimSequence* FaceAnimSequence, UAnimSequence* BodyAnimSequence)
		{
			AMetaHumanInvisibleDrivingActor* DrivingActor = WeakDrivingActor.Get();
			if (!DrivingActor)
			{
				return;
			}

			switch (AnimationController)
			{
			case EMetaHumanCharacterAnimationController::None:
			{
				DrivingActor->ResetAnimInstance();
				break;
			}
			case EMetaHumanCharacterAnimationController::AnimSequence:
			{
				DrivingActor->InitPreviewAnimInstance();
				DrivingActor->SetAnimation(FaceAnimSequence, BodyAnimSequence);
				break;
			}
			case EMetaHumanCharacterAnimationController::LiveLink:
			{
				DrivingActor->InitLiveLinkAnimInstance();
				break;
			}
			}
		});

	PreviewSceneDescription->OnAnimationChanged.BindLambda(
		[WeakDrivingActor = TWeakObjectPtr<AMetaHumanInvisibleDrivingActor>(DrivingActor)]
		(UAnimSequence* FaceAnimSequence, UAnimSequence* BodyAnimSequence)
	{
		if (AMetaHumanInvisibleDrivingActor* DrivingActor = WeakDrivingActor.Get())
		{
			DrivingActor->SetAnimation(FaceAnimSequence, BodyAnimSequence);
		}
	});

	PreviewSceneDescription->OnPlayRateChanged.BindLambda(
		[WeakDrivingActor = TWeakObjectPtr<AMetaHumanInvisibleDrivingActor>(DrivingActor)]
		(float NewPlayRate)
	{
		if (AMetaHumanInvisibleDrivingActor* DrivingActor = WeakDrivingActor.Get())
		{
			DrivingActor->SetAnimationPlayRate(NewPlayRate);
		}
	});

	PreviewSceneDescription->OnLiveLinkSubjectChanged.BindLambda(
		[WeakDrivingActor = TWeakObjectPtr<AMetaHumanInvisibleDrivingActor>(DrivingActor)]
		(FLiveLinkSubjectName LiveLinkSubjectName)
		{
			if (AMetaHumanInvisibleDrivingActor* DrivingActor = WeakDrivingActor.Get())
			{
				DrivingActor->SetLiveLinkSubjectNameChanged(LiveLinkSubjectName);
			}
		});

	PreviewSceneDescription->OnPreviewModeChanged.BindLambda(
		[WeakCharacter = TWeakObjectPtr<UMetaHumanCharacter>(MetaHumanCharacter),
		 WeakSubsystem = TWeakObjectPtr<UMetaHumanCharacterEditorSubsystem>(MetaHumanCharacterSubsystem)]
		(EMetaHumanCharacterSkinPreviewMaterial InPreviewMaterial)
		{
			UMetaHumanCharacter* Character = WeakCharacter.Get();
			UMetaHumanCharacterEditorSubsystem* Subsystem = WeakSubsystem.Get();
			if (Character && Subsystem)
			{
				Subsystem->UpdateCharacterPreviewMaterial(Character, InPreviewMaterial);
			}
		});

	// Initialize Animation on Editor start
	PreviewSceneDescription->BodyAnimationType = EMetaHumanAnimationType::TemplateAnimation;
	PreviewSceneDescription->FaceAnimationType = EMetaHumanAnimationType::TemplateAnimation;
	PreviewSceneDescription->PlayRate = 1.f;

	if (MetaHumanCharacterSubsystem->GetRiggingState(MetaHumanCharacter) != EMetaHumanCharacterRigState::Rigged)
	{
		PreviewSceneDescription->bAnimationControllerEnabled = false;
		PreviewSceneDescription->AnimationController = EMetaHumanCharacterAnimationController::None;
	}

	MetaHumanCharacter->OnAnimationReinitialized.AddWeakLambda(
		PreviewSceneDescription.Get(),
		[WeakPreviewSceneDescription = TWeakObjectPtr<UMetaHumanCharacterEditorPreviewSceneDescription>(PreviewSceneDescription.Get()),
		 WeakCharacter = TWeakObjectPtr<UMetaHumanCharacter>(MetaHumanCharacter)]()
		{
			UMetaHumanCharacterEditorPreviewSceneDescription* PreviewSceneDescription = WeakPreviewSceneDescription.Get();
			UMetaHumanCharacter* Character = WeakCharacter.Get();
			if (PreviewSceneDescription && Character)
			{
				PreviewSceneDescription->OnRiggingStateChanged(Character);
			}
		});

	// Iterate through all data table assets specified in the settings and add their animations to the list of available template animations.
	if (const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>())
	{
		for (const FSoftObjectPath& ObjectPath : Settings->TemplateAnimationDataTableAssets)
		{
			PreviewSceneDescription->AddTemplateAnimationsFromDataTable(ObjectPath);
		}
	}

	// Use the default template animation on editor startup.
	PreviewSceneDescription->BodyTemplateAnimation = PreviewSceneDescription->DefaultBodyTemplateAnimationName;
	PreviewSceneDescription->FaceTemplateAnimation = PreviewSceneDescription->DefaultFaceTemplateAnimationName;

	UAnimSequence* DefaultBodyTemplateAnimation = PreviewSceneDescription->GetTemplateAnimation(false, PreviewSceneDescription->BodyTemplateAnimation);
	UAnimSequence* DefaultFaceTemplateAnimation = PreviewSceneDescription->GetTemplateAnimation(true, PreviewSceneDescription->FaceTemplateAnimation);
	DrivingActor->SetAnimation(DefaultFaceTemplateAnimation, DefaultBodyTemplateAnimation);

	PreviewSceneDescription->OnGroomHiddenChanged.BindLambda(
		[WeakPreviewActor = TWeakInterfacePtr<IMetaHumanCharacterEditorActorInterface>(PreviewActor),
		 WeakCharacter = TWeakObjectPtr<UMetaHumanCharacter>(MetaHumanCharacter),
		 WeakSubsystem = TWeakObjectPtr<UMetaHumanCharacterEditorSubsystem>(MetaHumanCharacterSubsystem)]
		(EMetaHumanPreviewAssemblyVisibility InValue)
	{
		IMetaHumanCharacterEditorActorInterface* PreviewActor = WeakPreviewActor.Get();
		UMetaHumanCharacter* MetaHumanCharacter = WeakCharacter.Get();
		UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = WeakSubsystem.Get();
		if (!PreviewActor || !MetaHumanCharacter || !MetaHumanCharacterSubsystem)
		{
			return;
		}

		const bool bHideHair = InValue == EMetaHumanPreviewAssemblyVisibility::Hidden;
		MetaHumanCharacter->ViewportSettings.bHairHiddenInPreviewScene = bHideHair;
		PreviewActor->SetHairVisibilityState(bHideHair ? EMetaHumanHairVisibilityState::Hidden : EMetaHumanHairVisibilityState::Shown);
		MetaHumanCharacterSubsystem->RunCharacterEditorPipelineForPreview(MetaHumanCharacter);
	});

	PreviewSceneDescription->OnClothingHiddenChanged.BindLambda(
		[WeakCharacter = TWeakObjectPtr<UMetaHumanCharacter>(MetaHumanCharacter),
		 WeakSubsystem = TWeakObjectPtr<UMetaHumanCharacterEditorSubsystem>(MetaHumanCharacterSubsystem)]
		(EMetaHumanPreviewAssemblyVisibility InValue)
	{
		UMetaHumanCharacter* MetaHumanCharacter = WeakCharacter.Get();
		UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = WeakSubsystem.Get();
		if (!MetaHumanCharacter || !MetaHumanCharacterSubsystem)
		{
			return;
		}

		EMetaHumanClothingVisibilityState ClothingVisibilityState = InValue == EMetaHumanPreviewAssemblyVisibility::Hidden ? EMetaHumanClothingVisibilityState::Hidden : EMetaHumanClothingVisibilityState::Shown;
		MetaHumanCharacterSubsystem->SetClothingVisibilityState(MetaHumanCharacter, ClothingVisibilityState, true);
	});
}

ULevelStreaming* FMetaHumanCharacterEditorToolkit::LoadLevelInWorld(const FSoftObjectPath& LevelPath, const FTransform& LevelTransform)
{
	TSoftObjectPtr<UWorld> LevelAsset(LevelPath);

	bool bLoaded = false;
	const FString OptionalOverrideName;
	const bool bLoadAsTempPackage = true;
	TSubclassOf<ULevelStreamingDynamic> OptionalStreamingClass;
	ULevelStreamingDynamic* StreamingLevel = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(PreviewScene->GetWorld(),
		LevelAsset,
		LevelTransform,
		bLoaded,
		OptionalOverrideName,
		OptionalStreamingClass,
		bLoadAsTempPackage);
	check(bLoaded);
	check(StreamingLevel);

	StreamingLevel->SetShouldBeVisibleInEditor(false);

	PreviewScene->GetWorld()->FlushLevelStreaming(EFlushLevelStreamingType::Full);

	ULevel* NewLevel = StreamingLevel->GetLoadedLevel();

	if (!NewLevel)
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to add lighting scenario {LightingScenario}.", *LevelPath.ToString());
		return nullptr;
	}

	ResetLoaders(StreamingLevel->GetWorldAsset().LoadSynchronous());

	// External actors (One File Per Actor / World Partition) live in their own .uasset packages.
	// Reset loaders on those too, otherwise their FLinkerLoad keeps a file handle open and the
	// source asset can't be saved while MHC has the level loaded as a temp package.
	TSet<UObject*> ExternalActorPackages;
	for (const AActor* Actor : NewLevel->Actors)
	{
		if (UPackage* ExternalPackage = Actor ? Actor->GetExternalPackage() : nullptr)
		{
			ExternalActorPackages.Add(ExternalPackage);
		}
	}
	if (!ExternalActorPackages.IsEmpty())
	{
		ResetLoaders(ExternalActorPackages.Array());
	}

	return StreamingLevel;
}

void FMetaHumanCharacterEditorToolkit::LoadLevelInstanceSubLevelsInWorldRecursive(const TNotNull<ULevel*> InLoadedLevel)
{
	TArray<AActor*> ActorsToDestroy;
	LoadLevelInstanceSubLevelsInWorldRecursive(InLoadedLevel, FTransform::Identity, ActorsToDestroy);

	// Destroy all LevelInstance actors so they never register with the LevelInstanceSubsystem.
	// This is safe because the levels were loaded as temp packages.
	for (AActor* Actor : ActorsToDestroy)
	{
		Actor->Destroy();
	}
}

void FMetaHumanCharacterEditorToolkit::LoadLevelInstanceSubLevelsInWorldRecursive(const TNotNull<ULevel*> InLoadedLevel, const FTransform& InParentTransform, TArray<AActor*>& OutActorsToDestroy)
{
	for (AActor* Actor : InLoadedLevel->Actors)
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		ILevelInstanceInterface* LevelInstance = Cast<ILevelInstanceInterface>(Actor);
		if (!LevelInstance || !LevelInstance->IsWorldAssetValid())
		{
			continue;
		}

		const FSoftObjectPath WorldAssetPath = LevelInstance->GetWorldAsset().ToSoftObjectPath();

		OutActorsToDestroy.Add(Actor);
		const USceneComponent* Root = Actor->GetRootComponent();
		const FTransform ActorTransformWorld = FTransform(Root->GetRelativeRotation(), Root->GetRelativeLocation(), Root->GetRelativeScale3D()) * InParentTransform;

		ULevelStreaming* LevelInstanceLevel = LoadLevelInWorld(WorldAssetPath, ActorTransformWorld);
		if (IsValid(LevelInstanceLevel))
		{
			SelectedCustomLightingEnvStreamingLevels.Add(LevelInstanceLevel);

			if (ULevel* LoadedLevel = LevelInstanceLevel->GetLoadedLevel())
			{
				LoadLevelInstanceSubLevelsInWorldRecursive(LoadedLevel, ActorTransformWorld, OutActorsToDestroy);
			}
		}
	}
}


void FMetaHumanCharacterEditorToolkit::LoadLightingScenariosInWorld(const TArray<FSoftObjectPath>& LevelPaths)
{
	for(const FSoftObjectPath& LightingScenarioPath: LevelPaths)
	{
		ULevelStreaming* LoadedLightingScenario = LoadLevelInWorld(LightingScenarioPath);
	}

	OnEnvironmentChanged();
}

void FMetaHumanCharacterEditorToolkit::LoadPostProcessScenariosInWorld(const FSoftObjectPath& BaseLevelPath)
{
	// Order of adding here is important!
	ULevelStreaming* BaseLevel = LoadLevelInWorld(BaseLevelPath);
	BaseLevel->SetShouldBeVisibleInEditor(true);
	PostProcessLevels.Add(BaseLevel);
}

void FMetaHumanCharacterEditorToolkit::LoadCustomLightingEnvironment(const FSoftObjectPath& EnvironmentPath)
{
	ULevelStreaming* MainLevel = LoadLevelInWorld(EnvironmentPath);
	if (!IsValid(MainLevel))
	{
		FFormatNamedArguments FormatArguments;
		FormatArguments.Add(TEXT("EnvPackage"), FText::FromString(FPackageName::ObjectPathToPackageName(EnvironmentPath.ToString())));
		FMessageLog(UE::MetaHuman::MessageLogName).Error(FText::Format(LOCTEXT("FailedToCreateStreamingLevel", "Failed to create streaming level of ({EnvPackage})"), FormatArguments));
		FMessageLog(UE::MetaHuman::MessageLogName).Open();
		return;
	}
	SelectedCustomLightingEnvStreamingLevels.Add(MainLevel);

	UWorld* OuterWorld = MainLevel->GetWorldAsset().LoadSynchronous();

	WarnIfWorldPartitionEnvironment(EnvironmentPath, OuterWorld);

	if (IsValid(OuterWorld))
	{
		for (const ULevelStreaming* SubLevelStreaming : OuterWorld->GetStreamingLevels())
		{
			if (!IsValid(SubLevelStreaming))
			{
				continue;
			}

			const FSoftObjectPath SubLevelPath = SubLevelStreaming->GetWorldAsset().ToSoftObjectPath();
			if (ResidentLevelPaths.Contains(SubLevelPath))
			{
				for (ULevelStreaming* ExistingLevel: PreviewScene->GetWorld()->GetStreamingLevels())
				{
					if (ExistingLevel->PackageNameToLoad == *FPackageName::ObjectPathToPackageName(SubLevelPath.ToString()))
					{
						ExistingLevel->SetShouldBeVisibleInEditor(true);
						break;
					}
				}
			}
			else
			{
				ULevelStreaming* SubLevel = LoadLevelInWorld(SubLevelPath, SubLevelStreaming->LevelTransform);
				if (IsValid(SubLevel))
				{
					SelectedCustomLightingEnvStreamingLevels.Add(SubLevel);
				}
				else
				{
					FFormatNamedArguments FormatArguments;
					FormatArguments.Add(TEXT("SubLevelPackage"), FText::FromString(FPackageName::ObjectPathToPackageName(SubLevelPath.ToString())));
					FMessageLog(UE::MetaHuman::MessageLogName).Error(FText::Format(LOCTEXT("FailedToCreateSubStreamingLevel", "Failed to create sub streaming level of ({SubLevelPackage})"), FormatArguments));
					FMessageLog(UE::MetaHuman::MessageLogName).Open();
				}
			}
		}

		// Recursively stream any LevelInstance sublevels found in the loaded levels.
		const int32 CurrentCount = SelectedCustomLightingEnvStreamingLevels.Num();
		for (int32 Idx = 0; Idx < CurrentCount; ++Idx)
		{
			if (SelectedCustomLightingEnvStreamingLevels[Idx].IsValid())
			{
				if (ULevel* LoadedLevel = SelectedCustomLightingEnvStreamingLevels[Idx]->GetLoadedLevel())
				{
					LoadLevelInstanceSubLevelsInWorldRecursive(LoadedLevel);
				}
			}
		}
	}

	for (TWeakObjectPtr<ULevelStreaming>& Level: SelectedCustomLightingEnvStreamingLevels)
	{
		if (Level.IsValid())
		{
			Level->SetShouldBeVisibleInEditor(true);
		}
	}
}

void FMetaHumanCharacterEditorToolkit::UnloadCustomLightingEnvironment()
{
	UWorld* PreviewWorld = PreviewScene ? PreviewScene->GetWorld() : nullptr;

	// Capture loaded ULevel*s before unload so we can drop them from PreviewWorld->Levels[] after the flush.
	// UWorld::RemoveFromWorld only removes from Levels[] for game worlds; in editor we have to do it ourselves.
	TArray<ULevel*> LoadedLevelsToRemove;
	for (TWeakObjectPtr<ULevelStreaming>& Level : SelectedCustomLightingEnvStreamingLevels)
	{
		if (ULevelStreaming* StreamingLevel = Level.Get())
		{
			if (ULevel* LoadedLevel = StreamingLevel->GetLoadedLevel())
			{
				LoadedLevelsToRemove.Add(LoadedLevel);
			}
			UE::MetaHuman::RequestStreamingLevelUnload(StreamingLevel);
		}
	}
	SelectedCustomLightingEnvStreamingLevels.Empty();

	if (PreviewWorld)
	{
		PreviewWorld->FlushLevelStreaming(EFlushLevelStreamingType::Full);

		for (ULevel* LoadedLevel : LoadedLevelsToRemove)
		{
			if (IsValid(LoadedLevel))
			{
				PreviewWorld->RemoveLevel(LoadedLevel);

				// Tear down the inner world (uninitializes WorldPartition, releases scene/physics) before it GCs;
				// matches UEditorLevelUtils::PrivateDestroyLevel.
				if (UWorld* InnerWorld = LoadedLevel->GetTypedOuter<UWorld>())
				{
					if (InnerWorld != PreviewWorld)
					{
						InnerWorld->CleanupWorld();
						InnerWorld->MarkAsGarbage();
					}
				}
				LoadedLevel->MarkAsGarbage();
			}
		}
	}
}

bool FMetaHumanCharacterEditorToolkit::EnsureValidLightingEnvIsSelected() const
{
	bool bShouldUpdateEnvironment = false;
	UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	check(Character);

	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	check(Settings);

	auto ApplyDefaultLightingEnvironment = [&]()
	{
		EMetaHumanCharacterEnvironment DefaultEnv;
		FString DefaultPresetKey;
		TSoftObjectPtr<UWorld> DefaultPresetWorld;
		Settings->ResolveDefaultLightingEnvironment(DefaultEnv, DefaultPresetKey, DefaultPresetWorld);

		Character->ViewportSettings.CharacterEnvironment = DefaultEnv;
		Character->ViewportSettings.CustomLightingEnvironmentKey = DefaultPresetKey;
		Character->ViewportSettings.CustomLightingEnvironment = DefaultPresetWorld;
		bShouldUpdateEnvironment = true;
	};

	if (Character->ViewportSettings.CharacterEnvironment == EMetaHumanCharacterEnvironment::Custom)
	{
		bool bFound = false;
		// Find by Key Match first, only if a key match not found we assume that it might be a key rename
		for (const TPair<FString, TSoftObjectPtr<UWorld>>& CustomLightPreset: Settings->CustomLightPresets)
		{
			// Key matching
			if (Character->ViewportSettings.CustomLightingEnvironmentKey == CustomLightPreset.Key)
			{
				// If lighting map asset aka value is changed in CustomLightPresets Pair
				if (Character->ViewportSettings.CustomLightingEnvironment != CustomLightPreset.Value)
				{
					Character->ViewportSettings.CustomLightingEnvironment = CustomLightPreset.Value;
					Character->MarkPackageDirty();
					bShouldUpdateEnvironment = true;
				}
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			for (const TPair<FString, TSoftObjectPtr<UWorld>>& CustomLightPreset: Settings->CustomLightPresets)
			{
				// Value matching
				if (Character->ViewportSettings.CustomLightingEnvironment == CustomLightPreset.Value)
				{
					// If lighting map display name aka key is changed in CustomLightPresets Pair
					if (Character->ViewportSettings.CustomLightingEnvironmentKey != CustomLightPreset.Key)
					{
						Character->ViewportSettings.CustomLightingEnvironmentKey = CustomLightPreset.Key;
						Character->MarkPackageDirty();
					}
					bFound = true;
					break;
				}
			}
		}

		if (!bFound || Character->ViewportSettings.CustomLightingEnvironment.IsNull())
		{
			ApplyDefaultLightingEnvironment();
			Character->MarkPackageDirty();
		}
	}
	else if (Character->ViewportSettings.CharacterEnvironment == EMetaHumanCharacterEnvironment::Default)
	{
		ApplyDefaultLightingEnvironment();
	}

	return bShouldUpdateEnvironment;
}

void FMetaHumanCharacterEditorToolkit::RefreshPreview()
{
	UMetaHumanCharacter* MetaHumanCharacter = CastChecked<UMetaHumanCharacterAssetEditor>(OwningAssetEditor)->GetObjectToEdit();
	check(MetaHumanCharacter);

	// Start a validation report in case there are invalid items in the collection
	UMetaHumanCharacterValidationContext::FBeginReportParams ReportParams;
	ReportParams.ObjectToValidate = MetaHumanCharacter;
	
	UMetaHumanCharacterValidationContext::FScopedReport ScopedValidationReport{ ReportParams };

	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCollection* Collection = Subsystem->GetPreviewCollection(MetaHumanCharacter);
	if (Collection)
	{
		Collection->GetMutablePipeline()->GetMutableEditorPipeline()->SetValidationContext(ScopedValidationReport.Context.Get());
		
		Subsystem->RunCharacterEditorPipelineForPreview(MetaHumanCharacter);
	}
}

#undef LOCTEXT_NAMESPACE
