// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanManager.h"

#include "MetaHumanAssetReport.h"
#include "ProjectUtilities/MetaHumanAssetManager.h"
#include "UI/MetaHumanStyleSet.h"
#include "UI/SAssetGroupItemView.h"
#include "UI/SAssetGroupNavigation.h"
#include "UI/SPackageDestinationDialog.h"
#include "UI/SPackagingInstructions.h"
#include "Verification/MetaHumanVerificationRuleCollection.h"
#include "Verification/VerifyMetaHumanCharacter.h"
#include "Verification/VerifyMetaHumanGroom.h"
#include "Verification/VerifyMetaHumanOutfitClothing.h"
#include "Verification/VerifyMetaHumanSkeletalClothing.h"
#include "Verification/VerifyMetaHumanPackageSource.h"
#include "Verification/VerifyObjectValid.h"

#include "ContentBrowserMenuContexts.h"
#include "DesktopPlatformModule.h"
#include "Engine/SkeletalMesh.h"
#include "EngineAnalytics.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "GroomBindingAsset.h"
#include "Interfaces/IMainFrameModule.h"
#include "Interfaces/IPluginManager.h"
#include "MetaHumanSDKEditor.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/MessageDialog.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "ToolMenus.h"
#include "UObject/UnrealType.h"
#include "Algo/AllOf.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

#define LOCTEXT_NAMESPACE "MetaHumanManager"

namespace UE::MetaHuman
{

/**
 * The main MetaHuman Manager window.
 */
class SMetaHumanManagerWindow final : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SMetaHumanManagerWindow)
		{
		}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		AnalyticsEvent(TEXT("ManagerShown"));

		SWindow::Construct(
			SWindow::FArguments()
			.Title(LOCTEXT("MetaHumanManagerTitle", "MetaHuman Manager"))
			.SupportsMinimize(true)
			.SupportsMaximize(true)
			.ClientSize(FMetaHumanStyleSet::Get().GetVector("MetaHumanManager.WindowSize"))
			.MinWidth(FMetaHumanStyleSet::Get().GetFloat("MetaHumanManager.WindowMinWidth"))
			.MinHeight(FMetaHumanStyleSet::Get().GetFloat("MetaHumanManager.WindowMinHeight"))
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex(this, &SMetaHumanManagerWindow::GetMainSwitcherIndex)
				+ SWidgetSwitcher::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SNew(SPackagingInstructions)
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.MinWidth(FMetaHumanStyleSet::Get().GetFloat("MetaHumanManager.NavigationWidth"))
					[
						SAssignNew(Navigation, SAssetGroupNavigation)
						.OnNavigate(FOnNavigate::CreateSP(this, &SMetaHumanManagerWindow::OnNavigationChanged))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.FillContentWidth(1)
					.Padding(FMetaHumanStyleSet::Get().GetMargin("MetaHumanManager.ItemViewPadding"))
					[
						SAssignNew(ItemView, SAssetGroupItemView)
						.EnablePackageButton(this, &SMetaHumanManagerWindow::EnablePackageButton)
						.VerifyButtonText(this, &SMetaHumanManagerWindow::GetVerifyButtonText)
						.PackageButtonText(this, &SMetaHumanManagerWindow::GetPackageButtonText)
						.OnVerify(FOnVerify::CreateSP(this, &SMetaHumanManagerWindow::VerifyItems))
						.OnPackage(FOnPackage::CreateSP(this, &SMetaHumanManagerWindow::PackageItems))
					]
				]
			]
		);

		// Populate the navigation sections and trigger the initial OnNavigate so
		// that the widget switcher flips to the content view and the detail pane
		// shows the first item.
		Refresh();
	}

	virtual bool OnIsActiveChanged(const FWindowActivateEvent& ActivateEvent) override
	{
		// IsActive is updated after this function is called, so this catches only events which change us from
		// a non-active to an active state. This avoids too much churn.
		if (!IsActive() && ActivateEvent.GetActivationType() != FWindowActivateEvent::EA_Deactivate)
		{
			Refresh();
		}
		return SWindow::OnIsActiveChanged(ActivateEvent);
	}

	/**
	 * Refresh the navigation from disk and select the supplied asset, if it is currently
	 * surfaced in the navigation. Called from the "Open in MetaHuman Manager" content browser
	 * action; if the asset is not packageable or cannot be matched, the window still opens with
	 * the default first-item selection.
	 */
	void SelectAsset(const FAssetData& Asset)
	{
		Refresh();
		if (Navigation.IsValid() && Asset.IsValid())
		{
			Navigation->SelectAsset(Asset);
		}
	}

private:
	int GetMainSwitcherIndex() const
	{
		return bHasItems ? 1 : 0;
	}

	bool EnablePackageButton() const
	{
		return !MultiSelectItems.IsEmpty() && Algo::AllOf(MultiSelectItems, [](const TSharedRef<FMetaHumanAssetDescription>& Item)
		{
			return IsValid(Item->VerificationReport) && Item->VerificationReport->GetReportResult() == EMetaHumanOperationResult::Success;
		});
	}

	FText GetVerifyButtonText() const
	{
		const int32 Count = MultiSelectItems.Num();
		if (Count > 1)
		{
			return FText::Format(LOCTEXT("VerifyItemsButtonFmt", "Verify {0} Items"), Count);
		}
		return LOCTEXT("VerifyButtonText", "Verify");
	}

	FText GetPackageButtonText() const
	{
		const int32 Count = MultiSelectItems.Num();
		if (Count > 1)
		{
			return FText::Format(LOCTEXT("PackageItemsButtonFmt", "Package {0} Items..."), Count);
		}
		return LOCTEXT("PackageButtonText", "Package...");
	}

	void Refresh()
	{
		Navigation->Refresh();
		RefreshItemView();
	}

	void RefreshItemView()
	{
		if (ItemView.IsValid())
		{
			// Keep the detail pane showing the last-clicked item; update the "others selected" count.
			const TSharedPtr<FMetaHumanAssetDescription> DetailItem = DetailSelection.IsValid()
				? DetailSelection
				: nullptr;

			const int32 OtherCount = MultiSelectItems.Num() > 1 ? MultiSelectItems.Num() - 1 : 0;
			ItemView->SetItem(DetailItem, OtherCount);
		}
	}

	/** Called by SAssetGroupNavigation whenever the user's selection changes. */
	void OnNavigationChanged(const FNavigatePayload& Payload)
	{
		bHasItems = true;

		DetailSelection = Payload.DetailItems.IsEmpty() ? nullptr : Payload.DetailItems[0].ToSharedPtr();
		MultiSelectItems = Payload.MultiSelectItems;

		// The subtitle count is derived here from the authoritative MultiSelectItems array.
		// Every mutation of CheckedItems in SAssetGroupNavigation fires this delegate, so this
		// value is always consistent with the navigation layer's source of truth.
		RefreshItemView();
	}

	void VerifyItems()
	{
		if (MultiSelectItems.IsEmpty())
		{
			return;
		}

		// Verification requires packages to be saved on disk for correct reference checking, so prompt the user to save
		// if there are any unsaved packages. This is important for files that have just been copied into position or
		// just generated from MHC.
		constexpr bool bPromptUserToSave = true;
		constexpr bool bSaveMapPackages = false;
		constexpr bool bSaveContentPackages = true;
		constexpr bool bFastSave = false;
		constexpr bool bNotifyNoPackagesSaved = false;
		constexpr bool bCanBeDeclined = false;
		bool bOutPackagesNeededSaving = false;
		if (!FEditorFileUtils::SaveDirtyPackages(bPromptUserToSave, bSaveMapPackages, bSaveContentPackages, bFastSave, bNotifyNoPackagesSaved, bCanBeDeclined, &bOutPackagesNeededSaving))
		{
			return;
		}

		FScopedSlowTask VerifyingTask((MultiSelectItems.Num() * 2) + 1, LOCTEXT("VerificationProgressMessage", "Verifying assets..."));
		VerifyingTask.MakeDialog();

		for (const TSharedRef<FMetaHumanAssetDescription>& SelectedItem : MultiSelectItems)
		{
			if (bOutPackagesNeededSaving)
			{
				constexpr bool bForceRescan = true;
				IAssetRegistry::GetChecked().ScanPathsSynchronous({SelectedItem->AssetData.PackagePath.ToString()}, bForceRescan);
				UMetaHumanAssetManager::UpdateAssetDependencies(SelectedItem.Get());
			}

			VerifyingTask.EnterProgressFrame(1, FText::Format(LOCTEXT("LoadingMessage", "Loading {0}."), FText::FromName(SelectedItem->Name)));

			UMetaHumanVerificationRuleCollection* VerificationCollection = NewObject<UMetaHumanVerificationRuleCollection>();

			// Common verification tests
			VerificationCollection->AddVerificationRule(NewObject<UVerifyObjectValid>());
			VerificationCollection->AddVerificationRule(NewObject<UVerifyMetaHumanPackageSource>());

			// AssetType-specific verification tests
			// Note that character assets do not currently have any asset-type-specific verification as they are
			// strongly typed and so the default asset verification is sufficient.
			if (SelectedItem->AssetType == EMetaHumanAssetType::CharacterAssembly)
			{
				VerificationCollection->AddVerificationRule(NewObject<UVerifyMetaHumanCharacter>());
			}
			if (SelectedItem->AssetType == EMetaHumanAssetType::SkeletalClothing)
			{
				VerificationCollection->AddVerificationRule(NewObject<UVerifyMetaHumanSkeletalClothing>());
			}
			if (SelectedItem->AssetType == EMetaHumanAssetType::OutfitClothing)
			{
				VerificationCollection->AddVerificationRule(NewObject<UVerifyMetaHumanOutfitClothing>());
			}
			if (SelectedItem->AssetType == EMetaHumanAssetType::Groom)
			{
				VerificationCollection->AddVerificationRule(NewObject<UVerifyMetaHumanGroom>());
			}

			// TODO: get actual export options from UI
			FMetaHumanVerificationOptions VerificationOptions{
				false, // bVerbose
				false  // bTreatWarningsAsErrors
			};

			TStrongObjectPtr<UMetaHumanAssetReport> LifetimeManager;
			LifetimeManager.Reset(NewObject<UMetaHumanAssetReport>());
			TStrongObjectPtr<UObject> Asset(SelectedItem->AssetData.GetAsset());
			VerifyingTask.EnterProgressFrame(1, FText::Format(LOCTEXT("VerifyingMessage", "Verifying {0}."), FText::FromName(SelectedItem->Name)));
			VerificationCollection->ApplyAllRules(Asset.Get(), LifetimeManager.Get(), VerificationOptions);
			SelectedItem->VerificationReport = LifetimeManager.Get();

			// Keep hold of all reports until we close the window.
			Reports.Add(LifetimeManager);
		}

		// This is just to stop it sitting at 100% completed when there is still work to do.
		VerifyingTask.EnterProgressFrame();

		// Refresh the UI
		RefreshItemView();
	}

	void PackageItems()
	{
		if (MultiSelectItems.IsEmpty())
		{
			return;
		}

		// ── Single-item fast path: no modal, behaves exactly as before ────────
		if (MultiSelectItems.Num() == 1)
		{
			PackageCombined();
			return;
		}

		// ── Multi-item: show the destination mode dialog ──────────────────────
		// ChosenMode is captured by reference into the OnModeSelected delegate and also into
		// the OnWindowClosed hook below, so it must outlive the modal. This is safe because
		// FSlateApplication::AddModalWindow is synchronous on the game thread for editor modals:
		// it pumps the Slate tick loop internally and only returns once the window is destroyed.
		// ChosenMode is therefore valid and fully written before the line after AddModalWindow.
		EPackageDestinationMode ChosenMode = EPackageDestinationMode::Cancelled;

		TSharedRef<SWindow> DialogWindow = SNew(SWindow)
			.Title(FText::Format(
				LOCTEXT("PackageDialogTitle", "Package {0} Assets"),
				MultiSelectItems.Num()))
			.SupportsMinimize(false)
			.SupportsMaximize(false)
			.SizingRule(ESizingRule::Autosized)
			.IsTopmostWindow(true)
			[
				SNew(SPackageDestinationDialog)
				.AssetCount(MultiSelectItems.Num())
				.OnModeSelected_Lambda([&ChosenMode](EPackageDestinationMode Mode)
				{
					ChosenMode = Mode;
				})
			];

		// Register an OnWindowClosed hook so that dismissing the dialog via the OS close button
		// (which bypasses the Cancel button) still results in the Cancelled state being set,
		// keeping the contract symmetric with the explicit Cancel path.
		DialogWindow->SetOnWindowClosed(FOnWindowClosed::CreateLambda([&ChosenMode](const TSharedRef<SWindow>&)
		{
			if (ChosenMode == EPackageDestinationMode::Cancelled)
			{
				// Already Cancelled — either the Cancel button was clicked or no choice was made.
				// Either way, no further action needed; the value is already correct.
			}
			// If a choice was made before the window closed (e.g. button click fires delegate
			// then RequestDestroyWindow), ChosenMode is already set — do not overwrite it.
		}));

		// Synchronous modal pump — returns only after the window is fully closed.
		FSlateApplication::Get().AddModalWindow(DialogWindow, AsShared());

		if (ChosenMode == EPackageDestinationMode::CombinedArchive)
		{
			PackageCombined();
		}
		else if (ChosenMode == EPackageDestinationMode::SeparateArchives)
		{
			PackageSeparate();
		}
		// Cancelled — do nothing.
	}

	/** Package all selected items into one combined archive chosen by SaveFileDialog. */
	void PackageCombined()
	{
		IDesktopPlatform* DesktopPlatformModule = FDesktopPlatformModule::Get();
		TArray<FString> SelectedFilenames;
		// Use the first item's name as the default filename suggestion.
		const FString DefaultName = MultiSelectItems.IsEmpty() ? TEXT("") : MultiSelectItems[0]->Name.ToString();
		if (!DesktopPlatformModule || !DesktopPlatformModule->SaveFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()),
			TEXT("Save as MetaHuman Package file..."),
			TEXT(""),
			DefaultName,
			TEXT("MetaHuman Package file (*.mhpkg)|*.mhpkg"),
			EFileDialogFlags::None,
			SelectedFilenames))
		{
			return;
		}

		if (SelectedFilenames.IsEmpty())
		{
			return;
		}

		FScopedSlowTask PackagingTask(1, LOCTEXT("PackagingProgressMessage", "Packaging Assets..."));
		PackagingTask.MakeDialog();
		PackagingTask.EnterProgressFrame();

		TArray<FMetaHumanAssetDescription> ToPackage;
		for (const TSharedRef<FMetaHumanAssetDescription>& SelectedItem : MultiSelectItems)
		{
			FMetaHumanAssetDescription Item = SelectedItem.Get();
			UMetaHumanAssetManager::UpdateAssetDependencies(Item);
			UMetaHumanAssetManager::UpdateAssetDetails(Item);
			ToPackage.Add(Item);
		}

		UMetaHumanAssetManager::CreateArchive(ToPackage, SelectedFilenames[0]);
		FPlatformProcess::ExploreFolder(*SelectedFilenames[0]);
	}

	/** Package each selected item into its own archive inside a folder chosen by OpenDirectoryDialog. */
	void PackageSeparate()
	{
		IDesktopPlatform* DesktopPlatformModule = FDesktopPlatformModule::Get();
		FString SelectedFolder;
		if (!DesktopPlatformModule || !DesktopPlatformModule->OpenDirectoryDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()),
			TEXT("Choose a folder to save MetaHuman Package files into..."),
			TEXT(""),
			SelectedFolder))
		{
			return;
		}

		FScopedSlowTask PackagingTask(static_cast<float>(MultiSelectItems.Num()),
			LOCTEXT("PackagingSeparateProgressMessage", "Packaging Assets..."));
		PackagingTask.MakeDialog();

		FString LastOutputPath;
		for (const TSharedRef<FMetaHumanAssetDescription>& SelectedItem : MultiSelectItems)
		{
			PackagingTask.EnterProgressFrame(1, FText::Format(
				LOCTEXT("PackagingSeparateItemMessage", "Packaging {0}..."),
				FText::FromName(SelectedItem->Name)));

			FMetaHumanAssetDescription Item = SelectedItem.Get();
			UMetaHumanAssetManager::UpdateAssetDependencies(Item);
			UMetaHumanAssetManager::UpdateAssetDetails(Item);

			// Derive the output filename from the leaf segment of the package path.
			const FString ArchiveFilename = FPackageName::GetShortName(Item.AssetData.PackageName) + TEXT(".mhpkg");
			LastOutputPath = FPaths::Combine(SelectedFolder, ArchiveFilename);

			// Guard against filename collisions — two assets in different sections can share
			// the same short name (e.g. a Character and a Groom both called "Ada").
			if (FPaths::FileExists(LastOutputPath))
			{
				const FText WarningMessage = FText::Format(
					LOCTEXT("OverwriteWarning",
						"The file \"{0}\" already exists in the output folder.\n\n"
						"Continuing will overwrite it. Do you want to proceed?"),
					FText::FromString(ArchiveFilename));

				if (FMessageDialog::Open(EAppMsgType::YesNo, WarningMessage) != EAppReturnType::Yes)
				{
					continue;
				}
			}

			UMetaHumanAssetManager::CreateArchive({Item}, LastOutputPath);
		}

		// Open the output folder so the user can see all the produced files.
		FPlatformProcess::ExploreFolder(*SelectedFolder);
	}

	TSharedPtr<SAssetGroupItemView> ItemView;
	TSharedPtr<SAssetGroupNavigation> Navigation;

	// Required to stop Reports being GC'd as lifetime management within Slate does not use UE GC-aware pointers
	TArray<TStrongObjectPtr<UMetaHumanAssetReport>> Reports;

	/** The single item currently shown in the detail pane (last clicked). */
	TSharedPtr<FMetaHumanAssetDescription> DetailSelection;

	/**
	 * The full set of items to operate on (verify / package).
	 * In single-select mode this always has 0 or 1 entry.
	 * In multi-select mode it contains all checked items across sections.
	 */
	TArray<TSharedRef<FMetaHumanAssetDescription>> MultiSelectItems;

	bool bHasItems = false;
};

namespace Private
{
	/** True when AssetData refers to one of the packageable MetaHuman asset types listed by the MM navigation. */
	bool IsAssetSupportedByMetaHumanManager(const FAssetData& AssetData)
	{
		// Mirror UMetaHumanAssetManager::GetMainAssetClassPathForAssetType — Character /
		// SkeletalClothing / OutfitClothing / Groom. CharacterAssembly is intentionally
		// excluded: it is a UBlueprint with a sibling VersionInfo.txt, which we cannot test
		// from a class-path comparison alone, and the context menu visibility filter does not
		// surface this entry on UBlueprint assets in the first place.
		static const FTopLevelAssetPath OutfitClassPath(FName("/Script/ChaosOutfitAssetEngine"), FName("ChaosOutfitAsset"));
		static const FTopLevelAssetPath CharacterClassPath(FName("/Script/MetaHumanCharacter"), FName("MetaHumanCharacter"));

		return AssetData.IsValid()
			&& (AssetData.AssetClassPath == USkeletalMesh::StaticClass()->GetClassPathName()
			||  AssetData.AssetClassPath == UGroomBindingAsset::StaticClass()->GetClassPathName()
			||  AssetData.AssetClassPath == OutfitClassPath
			||  AssetData.AssetClassPath == CharacterClassPath);
	}

	/**
	 * Resolve a content-browser asset to the asset that the MetaHuman Manager should select.
	 *
	 * - If the asset is itself a packageable MM type, it is returned unchanged.
	 * - If the asset is a UMetaHumanWardrobeItem, its PrincipalAsset is read via FProperty
	 *   reflection (the property is an FEditorOnlyAssetReference whose AssetIdentifier is the
	 *   always-available FSoftObjectPath form). The resolved asset is returned only if it is
	 *   itself a packageable MM type; otherwise an invalid FAssetData is returned.
	 * - Anything else returns an invalid FAssetData.
	 *
	 * Reflection — rather than a build-time dependency on MetaHumanCharacterPalette — keeps
	 * MetaHumanSDKEditor's link surface unchanged. If the WardrobeItem schema ever changes
	 * (property renamed or restructured) the resolution gracefully fails and the caller opens
	 * the manager with no auto-selection.
	 */
	FAssetData ResolveSelectionTarget(const FAssetData& AssetData)
	{
		static const FTopLevelAssetPath WardrobeItemClassPath(FName("/Script/MetaHumanCharacterPalette"), FName("MetaHumanWardrobeItem"));

		if (IsAssetSupportedByMetaHumanManager(AssetData))
		{
			return AssetData;
		}

		if (AssetData.AssetClassPath != WardrobeItemClassPath)
		{
			return FAssetData();
		}

		UObject* Loaded = AssetData.GetAsset();
		if (!Loaded)
		{
			return FAssetData();
		}

		FStructProperty* PrincipalAssetStructProp = CastField<FStructProperty>(Loaded->GetClass()->FindPropertyByName(TEXT("PrincipalAsset")));
		if (!PrincipalAssetStructProp
			|| !PrincipalAssetStructProp->Struct
			|| PrincipalAssetStructProp->Struct->GetFName() != TEXT("EditorOnlyAssetReference"))
		{
			// Property name matches but the struct shape isn't what we expect — bail out
			// rather than reinterpret unrelated memory below.
			return FAssetData();
		}

		FStructProperty* IdentifierStructProp = CastField<FStructProperty>(PrincipalAssetStructProp->Struct->FindPropertyByName(TEXT("AssetIdentifier")));
		if (!IdentifierStructProp || IdentifierStructProp->Struct != TBaseStructure<FSoftObjectPath>::Get())
		{
			// AssetIdentifier exists but is not an FSoftObjectPath — refusing to do a typed
			// reinterpret on memory of unknown layout. ContainerPtrToValuePtr<FSoftObjectPath>
			// is a typed reinterpret, not a runtime-validated cast.
			return FAssetData();
		}

		void* PrincipalAssetPtr = PrincipalAssetStructProp->ContainerPtrToValuePtr<void>(Loaded);
		if (!PrincipalAssetPtr)
		{
			return FAssetData();
		}

		const FSoftObjectPath* PathPtr = IdentifierStructProp->ContainerPtrToValuePtr<FSoftObjectPath>(PrincipalAssetPtr);
		if (!PathPtr || PathPtr->IsNull())
		{
			return FAssetData();
		}

		const FAssetData PrincipalAssetData = IAssetRegistry::GetChecked().GetAssetByObjectPath(*PathPtr);
		return IsAssetSupportedByMetaHumanManager(PrincipalAssetData) ? PrincipalAssetData : FAssetData();
	}
} // namespace Private

class FMetaHumanManagerImpl
{
	static void RegisterMenuItems()
	{
		// Create the MetaHumanManager entry for the main window menu
		if (UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window")))
		{
			WindowMenu->FindOrAddSection("MetaHuman", LOCTEXT("MetaHumanSection", "MetaHuman"), {TEXT("Log"), EToolMenuInsertType::Before})
					.AddMenuEntry(
						MetaHumanManagerMenuItemName,
						MetaHumanManagerName,
						MetaHumanManagerToolTip,
						FSlateIcon(FMetaHumanStyleSet::Get().GetStyleSetName(), "MenuIcon"),
						FUIAction(
							FExecuteAction::CreateStatic(&FMetaHumanManager::CreateWindow)
						)
					);
		}
	}

	static void RegisterContentBrowserMenus()
	{
		// Surface "Open in MetaHuman Manager" on the asset context menu for the two
		// MetaHuman-authored asset types whose lifecycle the manager owns.
		// MetaHumanWardrobeItem is included because, while not a packageable type itself, its
		// PrincipalAsset is — clicking on the wardrobe item opens MM and selects the
		// underlying mesh / groom / outfit / character.
		const FName ContextMenuPaths[] = {
			TEXT("ContentBrowser.AssetContextMenu.MetaHumanCharacter"),
			TEXT("ContentBrowser.AssetContextMenu.MetaHumanWardrobeItem"),
		};

		for (const FName MenuPath : ContextMenuPaths)
		{
			UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(MenuPath);
			if (!Menu)
			{
				continue;
			}

			FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
			Section.AddDynamicEntry(
				ContentBrowserMenuEntryName,
				FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
				{
					// Use the FAssetData from the context — never trigger a load just to render the menu.
					const UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
					if (!Context || Context->SelectedAssets.IsEmpty())
					{
						return;
					}

					const FAssetData AssetForAction = Context->SelectedAssets[0];
					InSection.AddMenuEntry(
						ContentBrowserMenuEntryName,
						LOCTEXT("OpenInMHM_Label", "Open in MetaHuman Manager"),
						LOCTEXT("OpenInMHM_Tooltip", "Open the MetaHuman Manager window with this asset selected."),
						FSlateIcon(FMetaHumanStyleSet::Get().GetStyleSetName(), "MenuIcon"),
						FUIAction(FExecuteAction::CreateLambda([AssetForAction]()
						{
							FMetaHumanManager::OpenAndSelectAsset(AssetForAction);
						}))
					);
				})
			);
		}
	}

	// UI management
	void InitializeStyle()
	{
		FSlateStyleRegistry::RegisterSlateStyle(FMetaHumanStyleSet::Get());
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}

	void DestroyStyle()
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(FMetaHumanStyleSet::Get());
	}

	// UI strings
	static const FText MetaHumanManagerToolTip;
	static const FText MetaHumanManagerName;

	// UI element names
	static const FName MetaHumanManagerMenuItemName;
	static const FName ContentBrowserMenuEntryName;

	// Handle to UI instance
	TWeakPtr<SWindow> MaybeCurrentWindow;

public:
	void Initialize()
	{
		if (FSlateApplicationBase::IsInitialized() && IPluginManager::Get().FindEnabledPlugin(TEXT("MetaHumanCharacter")))
		{
			// Register UI entrypoints
			InitializeStyle();
			RegisterMenuItems();
			RegisterContentBrowserMenus();
		}
	}

	void Shutdown()
	{
		if (FSlateApplicationBase::IsInitialized())
		{
			// Clean up UI
			DestroyStyle();
		}
	}

	void CreateWindow()
	{
		TSharedPtr<SWindow> CurrentWindow = MaybeCurrentWindow.Pin();
		if (CurrentWindow.IsValid())
		{
			CurrentWindow->BringToFront();
		}
		else
		{
			IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
			TSharedRef<SWindow> NewWindow = SNew(SMetaHumanManagerWindow);
			if (MainFrameModule.GetParentWindow().IsValid())
			{
				FSlateApplication::Get().AddWindowAsNativeChild(NewWindow, MainFrameModule.GetParentWindow().ToSharedRef());
			}
			else
			{
				FSlateApplication::Get().AddWindow(NewWindow);
			}
			MaybeCurrentWindow = NewWindow.ToWeakPtr();
		}
	}

	void OpenAndSelectAsset(const FAssetData& Asset)
	{
		// Resolve once before opening — for WardrobeItems this may load the asset to read
		// PrincipalAsset via reflection, which is acceptable on an explicit user click.
		const FAssetData Target = Private::ResolveSelectionTarget(Asset);

		CreateWindow();

		const TSharedPtr<SWindow> Window = MaybeCurrentWindow.Pin();
		if (!Window.IsValid())
		{
			return;
		}

		// SMetaHumanManagerWindow is the only SWindow subclass we put into MaybeCurrentWindow.
		const TSharedPtr<SMetaHumanManagerWindow> ManagerWindow = StaticCastSharedPtr<SMetaHumanManagerWindow>(Window);
		ManagerWindow->SelectAsset(Target);
	}
};

// Statics:
TUniquePtr<FMetaHumanManagerImpl> FMetaHumanManager::Instance;
const FText FMetaHumanManagerImpl::MetaHumanManagerToolTip = LOCTEXT("MenuTooltip", "Launch MetaHuman Manager");
const FText FMetaHumanManagerImpl::MetaHumanManagerName = LOCTEXT("MenuName", "MetaHuman Manager");

// UI element names
const FName FMetaHumanManagerImpl::MetaHumanManagerMenuItemName = TEXT("OpenMetaHumanManagerTab");
const FName FMetaHumanManagerImpl::ContentBrowserMenuEntryName = TEXT("OpenInMetaHumanManager");


void FMetaHumanManager::Initialize()
{
	if (!Instance.IsValid())
	{
		Instance = MakeUnique<FMetaHumanManagerImpl>();
	}
	Instance->Initialize();
}

void FMetaHumanManager::Shutdown()
{
	Instance.Reset();
}

void FMetaHumanManager::CreateWindow()
{
	if (Instance.IsValid())
	{
		Instance->CreateWindow();
	}
}

void FMetaHumanManager::OpenAndSelectAsset(const FAssetData& Asset)
{
	if (Instance.IsValid())
	{
		Instance->OpenAndSelectAsset(Asset);
	}
}

} // namespace UE::MetaHuman

#undef LOCTEXT_NAMESPACE
