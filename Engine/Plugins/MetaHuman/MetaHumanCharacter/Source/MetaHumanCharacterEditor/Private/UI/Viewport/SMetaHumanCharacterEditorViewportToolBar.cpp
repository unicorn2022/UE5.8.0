// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorViewportToolBar.h"
#include "UI/Widgets/SMetaHumanCharacterEditorTileView.h"

#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "PropertyCustomizationHelpers.h"
#include "SResetToDefaultPropertyEditor.h"
#include "IDetailPropertyRow.h"
#include "SWarningOrErrorBox.h"

#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorLog.h"
#include "MetaHumanCharacterEditorActor.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterEditorStyle.h"
#include "SMetaHumanCharacterEditorViewport.h"
#include "MetaHumanCharacterEditorViewportClient.h"
#include "MetaHumanCharacterEditorCommands.h"

#include "EditorViewportCommands.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "ViewportToolbar/UnrealEdViewportToolbarContext.h"
#include "ToolMenus.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserDelegates.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanSDKEditor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "SPositiveActionButton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#include "UI/Widgets/SMetaHumanCharacterEditorTextComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "NaniteVisualizationMenuCommands.h"
#include "LumenVisualizationMenuCommands.h"
#include "SubstrateVisualizationMenuCommands.h"
#include "VirtualShadowMapVisualizationMenuCommands.h"
#include "VirtualShadowMapVisualizationData.h"
#include "GroomVisualizationMenuCommands.h"
#include "BufferVisualizationMenuCommands.h"
#include "GroomVisualizationData.h"
#include "RenderUtils.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorViewportToolBar"

void GetCustomLightPresetComboBoxOptions(TArray<TSharedPtr<FString>>& OutCustomLightPresetComboBoxOptions)
{
	const UMetaHumanCharacterEditorSettings* MetaHumanEditorSettings = GetDefault<UMetaHumanCharacterEditorSettings>();
	for (const TTuple<FString, TSoftObjectPtr<UWorld>>& CustomLightPreset: MetaHumanEditorSettings->CustomLightPresets)
	{
		// A deleted preset can still be in memory and IsNull would return false, hence the extra asset registry check
		if (!CustomLightPreset.Value.IsNull() && IAssetRegistry::Get()->GetAssetByObjectPath(CustomLightPreset.Value.ToSoftObjectPath()).IsValid())
		{
			OutCustomLightPresetComboBoxOptions.Add(MakeShared<FString>(CustomLightPreset.Key));
		}
	}
}

TSharedRef<SWidget> CreateEnvironmentWidget(TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport)
{
	check(MetaHumanCharacterEditorViewport);

	UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
	check(MetaHumanCharacter);

	FProperty* LightRotationProperty = FMetaHumanCharacterViewportSettings::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterViewportSettings, LightRotation));
	const float MinValue = LightRotationProperty->GetFloatMetaData(TEXT("ClampMin"));
	const float MaxValue = LightRotationProperty->GetFloatMetaData(TEXT("ClampMax"));

	FProperty* BackgroundColorProperty = FMetaHumanCharacterViewportSettings::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterViewportSettings, BackgroundColor));
	check(BackgroundColorProperty);

	TWeakPtr<SMetaHumanCharacterEditorTileView<EMetaHumanCharacterEnvironment>> WeakCharacterEnvironmentTileView;

	TArray<TSharedPtr<FString>> CustomLightPresetComboBoxOptions;
	GetCustomLightPresetComboBoxOptions(CustomLightPresetComboBoxOptions);

	TSharedPtr<FString> InitiallySelectedCustomLightPreset;
	if (MetaHumanCharacter->ViewportSettings.CharacterEnvironment == EMetaHumanCharacterEnvironment::Custom)
	{
		const TSharedPtr<FString>* Found = CustomLightPresetComboBoxOptions.FindByPredicate([&CustomLightingEnvironmentKey = MetaHumanCharacter->ViewportSettings.CustomLightingEnvironmentKey](const TSharedPtr<FString>& CustomLightPresetComboBoxOption)
		{
			return CustomLightPresetComboBoxOption.IsValid() && *CustomLightPresetComboBoxOption == CustomLightingEnvironmentKey;
		});

		if (Found)
		{
			InitiallySelectedCustomLightPreset = *Found;
		}
	}

	TSharedRef<TWeakPtr<SMetaHumanCharacterEditorTextComboBox>> CustomLightPresetComboBoxHolder = MakeShared<TWeakPtr<SMetaHumanCharacterEditorTextComboBox>>();
	TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = MetaHumanCharacterEditorViewport;

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(5.0f, 5.0f, 5.f, 5.0f)
		[
			SNew(SBox)
			.WidthOverride(310.f)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(5.0f, 5.0f, 5.f, 5.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.MinWidth(300)
					[
						SAssignNew(WeakCharacterEnvironmentTileView, SMetaHumanCharacterEditorTileView<EMetaHumanCharacterEnvironment>)
						.IsFocusable_Lambda([WeakMetaHumanCharacterEditorViewport]() -> bool
						{
							if (const TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
							{
								UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
								check(MetaHumanCharacter);
								return MetaHumanCharacter->ViewportSettings.CharacterEnvironment != EMetaHumanCharacterEnvironment::Custom && MetaHumanCharacter->ViewportSettings.CharacterEnvironment != EMetaHumanCharacterEnvironment::Default;
							}
							return true;
						})
						.InitiallySelectedItem_Lambda([WeakMetaHumanCharacterEditorViewport]
						{
							// Default isn't a real tile in the dropdown - resolve it to whatever the project setting points to
							auto ResolveProjectDefault = []()
							{
								const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
								check(Settings);
								EMetaHumanCharacterEnvironment ResolvedEnv;
								FString DefaultPresetKey;
								TSoftObjectPtr<UWorld> DefaultPresetWorld;
								Settings->ResolveDefaultLightingEnvironment(ResolvedEnv, DefaultPresetKey, DefaultPresetWorld);
								return ResolvedEnv;
							};

							if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
							{
								UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
								check(MetaHumanCharacter);

								const EMetaHumanCharacterEnvironment Env = MetaHumanCharacter->ViewportSettings.CharacterEnvironment;
								return Env == EMetaHumanCharacterEnvironment::Default ? ResolveProjectDefault() : Env;
							}
							return ResolveProjectDefault();
						})
						.OnGetSlateBrush_Lambda([](uint8 InItem) -> const FSlateBrush*
						{
							const FString EnvironmentName = StaticEnum<EMetaHumanCharacterEnvironment>()->GetAuthoredNameStringByValue(InItem);
							const FString EnvironmentBrushName = FString::Format(TEXT("Viewport.LightScenarios.{0}"), { EnvironmentName });
							return FMetaHumanCharacterEditorStyle::Get().GetBrush(*EnvironmentBrushName);
						})
						.OnSelectionChanged_Lambda([WeakMetaHumanCharacterEditorViewport, CustomLightPresetComboBoxHolder](uint8 InItem)
						{
							if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
							{
								UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
								check(MetaHumanCharacter);
								MetaHumanCharacter->ViewportSettings.CharacterEnvironment = static_cast<EMetaHumanCharacterEnvironment>(InItem);

								if (const TSharedPtr<SMetaHumanCharacterEditorTextComboBox> CustomLightPresetComboBox = CustomLightPresetComboBoxHolder->Pin())
								{
									CustomLightPresetComboBox->ClearSelection();
								}

								UMetaHumanCharacterEditorSubsystem::Get()->NotifyLightingEnvironmentChanged(MetaHumanCharacter);
								MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->Invalidate();
							}
						})
						]
					]
					+ SVerticalBox::Slot()
					.Padding(5.0f, 5.0f, 5.0f, 0.0f)
					.AutoHeight()
					[
						SAssignNew(*CustomLightPresetComboBoxHolder, SMetaHumanCharacterEditorTextComboBox, CustomLightPresetComboBoxOptions, InitiallySelectedCustomLightPreset)
						.PlaceholderText(LOCTEXT("SelectCustomLightPresetsText", "Select Custom Light Presets"))
						.OnSelectionChangedItem_Lambda([WeakMetaHumanCharacterEditorViewport, WeakCharacterEnvironmentTileView](const FString& InItem)
						{
							if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
							{
								UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
								check(MetaHumanCharacter);
								const UMetaHumanCharacterEditorSettings* MetaHumanEditorSettings = GetDefault<UMetaHumanCharacterEditorSettings>();
								MetaHumanCharacter->ViewportSettings.CustomLightingEnvironment = MetaHumanEditorSettings->CustomLightPresets.FindChecked(InItem);
								MetaHumanCharacter->ViewportSettings.CustomLightingEnvironmentKey = InItem;
								MetaHumanCharacter->ViewportSettings.CharacterEnvironment = EMetaHumanCharacterEnvironment::Custom;

								if (TSharedPtr<SMetaHumanCharacterEditorTileView<EMetaHumanCharacterEnvironment>> CharacterEnvironmentTileView = WeakCharacterEnvironmentTileView.Pin())
								{
									CharacterEnvironmentTileView->ClearSelection();
								}

								UMetaHumanCharacterEditorSubsystem::Get()->NotifyLightingEnvironmentChanged(MetaHumanCharacter);
								MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->Invalidate();
							}
						})
					]
					+ SVerticalBox::Slot()
					.Padding(5.0f, 5.0f, 5.0f, 10.0f)
					.AutoHeight()
					[
						SNew(SPositiveActionButton)
						.Icon(FAppStyle::GetBrush("Icons.Plus"))
						.Text(LOCTEXT("CustomLightPresetText", "Custom Light Preset"))
						.ToolTipText(LOCTEXT("CustomLightPresetToolTipText", "Creates a Custom Light Preset Level from currently selected environment"))
						.OnClicked_Lambda([WeakMetaHumanCharacterEditorViewport, WeakCharacterEnvironmentTileView, CustomLightPresetComboBoxHolder]() -> FReply
						{
							if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
							{
								UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
								check(MetaHumanCharacter);
								FString CurrentLightingEnvObjectPath;
								FString CurrentLightingEnvName;
								FString Suffix = TEXT("");
								// If CharacterEnvironment is Custom, CustomLightingEnvironment would be valid
								if (MetaHumanCharacter->ViewportSettings.CharacterEnvironment == EMetaHumanCharacterEnvironment::Custom)
								{
									CurrentLightingEnvName = MetaHumanCharacter->ViewportSettings.CustomLightingEnvironment.GetAssetName();
									CurrentLightingEnvObjectPath = MetaHumanCharacter->ViewportSettings.CustomLightingEnvironment.ToSoftObjectPath().ToString();
								}
								else
								{
									Suffix = TEXT("_Custom");
									CurrentLightingEnvName = StaticEnum<EMetaHumanCharacterEnvironment>()->GetAuthoredNameStringByValue(static_cast<uint8>(MetaHumanCharacter->ViewportSettings.CharacterEnvironment));
									CurrentLightingEnvObjectPath = FString::Format(TEXT("/{0}/LightingEnvironments/{1}.{1}"), { UE_PLUGIN_NAME, CurrentLightingEnvName });
								}

								IAssetTools& AssetTools = IAssetTools::Get();
								FString OutPackageName;
								FString OutAssetName;
								AssetTools.CreateUniqueAssetName(
									FPackageName::GetLongPackagePath(MetaHumanCharacter->GetPackage()->GetPathName()) / CurrentLightingEnvName,
									Suffix, OutPackageName, OutAssetName);

								FSaveAssetDialogConfig SaveAssetDialogConfig;
								SaveAssetDialogConfig.DefaultPath = FPackageName::GetLongPackagePath(OutPackageName);
								SaveAssetDialogConfig.DefaultAssetName = OutAssetName;
								SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::Disallow;
								SaveAssetDialogConfig.DialogTitleOverride = LOCTEXT("SaveCustomLightPresetTitle", "Save Custom Light Preset");

								const FString AssetPathAndName = IContentBrowserSingleton::Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);

								if (!AssetPathAndName.IsEmpty())
								{
									const FString SaveAssetName = FPackageName::ObjectPathToPathWithinPackage(AssetPathAndName);
									const FString SaveAssetDir = FPackageName::GetLongPackagePath(AssetPathAndName);

									UObject* SourceAsset = LoadObject<UWorld>(nullptr, *CurrentLightingEnvObjectPath);
									if (!IsValid(SourceAsset))
									{
										FFormatNamedArguments FormatArguments;
										FormatArguments.Add(TEXT("LevelPackage"), FText::FromString(FPackageName::ObjectPathToPackageName(CurrentLightingEnvObjectPath)));
										FMessageLog(UE::MetaHuman::MessageLogName).Error(FText::Format(LOCTEXT("FailedToLoadLevel", "Failed to load level ({LevelPackage}) to duplicate Custom Light Preset from"), FormatArguments));
										FMessageLog(UE::MetaHuman::MessageLogName).Open();
										return FReply::Handled();
									}

									UObject* DuplicatedAsset = AssetTools.DuplicateAsset(SaveAssetName, SaveAssetDir, SourceAsset);
									if (IsValid(DuplicatedAsset))
									{
										// Ensure the duplicated package is saved to disk. AssetTools::DuplicateAsset only saves
										// when revision control is enabled, which means on projects without SCC the new level
										// lives only in memory and later LoadLevelInstanceBySoftObjectPtr calls in
										// FMetaHumanCharacterEditorToolkit::LoadCustomLightingEnvironment fail to resolve it.
										const bool bOnlyDirty = false;
										if (!UEditorLoadingAndSavingUtils::SavePackages({ DuplicatedAsset->GetPackage() }, bOnlyDirty))
										{
											FMessageLog(UE::MetaHuman::MessageLogName).Error()
												->AddToken(FUObjectToken::Create(DuplicatedAsset))
												->AddText(LOCTEXT("FailedToSaveCustomLightPreset",
													"could not be saved to disk and will not load. Check that the target folder is writable and the package can be checked out."));
											FMessageLog(UE::MetaHuman::MessageLogName).Open();
											return FReply::Handled();
										}

										GEditor->SyncBrowserToObject(DuplicatedAsset);
									}
									else
									{
										FMessageLog(UE::MetaHuman::MessageLogName).Error()
											->AddToken(FUObjectToken::Create(SourceAsset))
											->AddText(LOCTEXT("FailedToDuplicateLevel",
												"could not be duplicated to create a Custom Light Preset. The destination name or path may be invalid, or an asset already exists there."));
										FMessageLog(UE::MetaHuman::MessageLogName).Open();
										return FReply::Handled();
									}

									UMetaHumanCharacterEditorSettings* MetaHumanEditorSettings = GetMutableDefault<UMetaHumanCharacterEditorSettings>();
									// Same map name in different dirs can exist which can cause a key to not be unique. MakeUniqueKeyForCustomLightPresets ensures that does not happen.
									const FString UniqueKey = UMetaHumanCharacterEditorSettings::MakeUniqueKey(DuplicatedAsset->GetName(), [MetaHumanEditorSettings](const FString& Key)
									{
										return MetaHumanEditorSettings->CustomLightPresets.Contains(Key) || UMetaHumanCharacterEditorSettings::IsReservedLightingEnvironmentName(Key);
									});
									MetaHumanEditorSettings->CustomLightPresets.Add(UniqueKey, CastChecked<UWorld>(DuplicatedAsset));
									MetaHumanEditorSettings->SaveConfig();

									if (const TSharedPtr<SMetaHumanCharacterEditorTextComboBox> CustomLightPresetComboBox = CustomLightPresetComboBoxHolder->Pin())
									{
										TArray<TSharedPtr<FString>> ComboBoxOptions;
										GetCustomLightPresetComboBoxOptions(ComboBoxOptions);
										CustomLightPresetComboBox->SetOptions(ComboBoxOptions);
									}

									MetaHumanCharacter->ViewportSettings.CharacterEnvironment = EMetaHumanCharacterEnvironment::Custom;
									MetaHumanCharacter->ViewportSettings.CustomLightingEnvironment = CastChecked<UWorld>(DuplicatedAsset);
									MetaHumanCharacter->ViewportSettings.CustomLightingEnvironmentKey = UniqueKey;

									if (TSharedPtr<SMetaHumanCharacterEditorTileView<EMetaHumanCharacterEnvironment>> CharacterEnvironmentTileView = WeakCharacterEnvironmentTileView.Pin())
									{
										CharacterEnvironmentTileView->ClearSelection();
									}

									UMetaHumanCharacterEditorSubsystem::Get()->NotifyLightingEnvironmentChanged(MetaHumanCharacter);
									MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->Invalidate();

									if (MetaHumanEditorSettings->bOpenCustomLightPresetInLevelEditor)
									{
										GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(DuplicatedAsset);

										if (MetaHumanEditorSettings->bAutoSpawnPreviewActor)
										{
											const bool bKeepTransient = true;
											UMetaHumanCharacterEditorSubsystem::Get()->SpawnMetaHumanActor(MetaHumanCharacter, bKeepTransient);
										}
									}
								}
							}

							return FReply::Handled();
						})
					]
					+ SVerticalBox::Slot()
					.Padding(5.0f, 10.0f, 5.0f, 5.0f)
					.AutoHeight()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.Padding(FMargin(5.0f, 0.0f, 5.0f, 8.0f))
						.AutoHeight()
						[
							SNew(SWarningOrErrorBox)
							.MessageStyle(EMessageStyle::Warning)
							.Padding(FMargin(8.0f, 6.0f))
							.IconSize(FVector2D(16.0f, 16.0f))
							.Message(LOCTEXT("BackgroundMissingWarning", "No background actor in this environment."))
							.Visibility_Lambda([WeakMetaHumanCharacterEditorViewport]() -> EVisibility
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									return MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->DoesEnvironmentHaveBackground() ? EVisibility::Collapsed : EVisibility::Visible;
								}
								return EVisibility::Collapsed;
							})
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							.IsEnabled_Lambda([WeakMetaHumanCharacterEditorViewport]() -> bool
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									return MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->DoesEnvironmentHaveBackground();
								}
								return true;
							})
							+ SHorizontalBox::Slot()
							.HAlign(HAlign_Left)
							.VAlign(VAlign_Center)
							.Padding(5.0f, 5.0f)
							[
								SNew(STextBlock)
								.Text(BackgroundColorProperty->GetDisplayNameText())
							]
							+ SHorizontalBox::Slot()
							[
								SNew(SColorBlock)
								.Color_Lambda([WeakMetaHumanCharacterEditorViewport]
								{
									if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
									{
										TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
										return MetaHumanCharacter->ViewportSettings.BackgroundColor;
									}
									return FLinearColor::White;
								})
								.OnMouseButtonDown_Lambda([WeakMetaHumanCharacterEditorViewport](const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
								{
									if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
									{
										TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();

										if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
										{
											return FReply::Unhandled();
										}

										FColorPickerArgs Args;
										Args.bIsModal = false;
										Args.bOnlyRefreshOnMouseUp = false;
										Args.bOnlyRefreshOnOk = false;
										Args.bUseAlpha = false;
										Args.bOpenAsMenu = false;
										Args.bClampValue = true;
										Args.ParentWidget = MetaHumanCharacterEditorViewport;
										Args.InitialColor = MetaHumanCharacter->ViewportSettings.BackgroundColor;
										Args.OnColorCommitted = FOnLinearColorValueChanged::CreateWeakLambda(MetaHumanCharacter,
										[MetaHumanCharacter, WeakMetaHumanCharacterEditorViewport](const FLinearColor& NewColor)
										{
											if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
											{
												UMetaHumanCharacterEditorSubsystem::Get()->UpdateBackgroundColor(MetaHumanCharacter, NewColor);
												MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->Invalidate();
											}
										});
											OpenColorPicker(Args);
										}

									return FReply::Handled();
								})
								.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
								.AlphaBackgroundBrush(FAppStyle::Get().GetBrush("ColorPicker.RoundedAlphaBackground"))
								.ShowBackgroundForAlpha(true)
								.CornerRadius(FVector4(2.f, 2.f, 2.f, 2.f))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.Padding(4.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SResetToDefaultPropertyEditor, nullptr)
								.CustomResetToDefault(FResetToDefaultOverride::Create(
									TAttribute<bool>::CreateLambda([WeakMetaHumanCharacterEditorViewport]
									{
										if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
										{
											TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
											const FLinearColor DefaultBackgroundColor = GetDefault<UMetaHumanCharacter>()->ViewportSettings.BackgroundColor;
											return !MetaHumanCharacter->ViewportSettings.BackgroundColor.Equals(DefaultBackgroundColor);
										}
										return false;
									}),
									FSimpleDelegate::CreateLambda([WeakMetaHumanCharacterEditorViewport]
									{
										if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
										{
											TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
											const FLinearColor DefaultBackgroundColor = GetDefault<UMetaHumanCharacter>()->ViewportSettings.BackgroundColor;
											UMetaHumanCharacterEditorSubsystem::Get()->UpdateBackgroundColor(MetaHumanCharacter, DefaultBackgroundColor);
											MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->Invalidate();
										}
									})))
							]
						]
					]
					+ SVerticalBox::Slot()
					.Padding(5.0f, 5.0f)
					.AutoHeight()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.Padding(5.0f, 2.0f)
						.AutoHeight()
						[
							SNew(SWarningOrErrorBox)
							.MessageStyle(EMessageStyle::Warning)
							.Padding(FMargin(8.0f, 6.0f))
							.IconSize(FVector2D(16.0f, 16.0f))
							.Message(LOCTEXT("LightRigMissingWarning", "No light rig actor in this environment."))
							.Visibility_Lambda([WeakMetaHumanCharacterEditorViewport]() -> EVisibility
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									return MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->DoesEnvironmentHaveLightRig() ? EVisibility::Collapsed : EVisibility::Visible;
								}
								return EVisibility::Collapsed;
							})
						]
						+ SVerticalBox::Slot()
						.Padding(5.0f, 5.0f)
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("LightRigRotationLabel", "Light Rig Rotation"))
							.IsEnabled_Lambda([WeakMetaHumanCharacterEditorViewport]() -> bool
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									return MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->DoesEnvironmentHaveLightRig();
								}
								return true;
							})
						]
						+ SVerticalBox::Slot()
						.Padding(0.0f, 5.0f)
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(SNumericEntryBox<float>)
								.IsEnabled_Lambda([WeakMetaHumanCharacterEditorViewport]() -> bool
								{
									if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
									{
										return MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->DoesEnvironmentHaveLightRig();
									}
									return true;
								})
								.AllowSpin(true)
								.EditableTextBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
								.SpinBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox"))
								.MinValue(MinValue)
								.MaxValue(MaxValue)
								.MinSliderValue(MinValue)
								.MaxSliderValue(MaxValue)
								.MaxFractionalDigits(2)
								.LinearDeltaSensitivity(1.0)
								.PreventThrottling(true)
								.Value_Lambda([WeakMetaHumanCharacterEditorViewport]() -> TOptional<float>
								{
									if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
									{
										if (UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get())
										{
											return MetaHumanCharacter->ViewportSettings.LightRotation;
										}
									}
									return TOptional<float>();
								})
								.OnValueChanged_Lambda([WeakMetaHumanCharacterEditorViewport](float NewValue)
								{
									if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
									{
										if (UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get())
										{
											UMetaHumanCharacterEditorSubsystem::Get()->UpdateLightRotation(MetaHumanCharacter, NewValue);
											MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->Invalidate();
										}
									}
								})
								.OnValueCommitted_Lambda([WeakMetaHumanCharacterEditorViewport](float NewValue, ETextCommit::Type InType)
								{
									if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
									{
										if (UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get())
										{
											UMetaHumanCharacterEditorSubsystem::Get()->UpdateLightRotation(MetaHumanCharacter, NewValue);
											MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->Invalidate();
										}
									}
								})
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.Padding(4.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SResetToDefaultPropertyEditor, nullptr)
								.CustomResetToDefault(FResetToDefaultOverride::Create(
									TAttribute<bool>::CreateLambda([WeakMetaHumanCharacterEditorViewport]
									{
										if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
										{
											TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
											const float DefaultLightRotation = GetDefault<UMetaHumanCharacter>()->ViewportSettings.LightRotation;
											return !FMath::IsNearlyEqual(MetaHumanCharacter->ViewportSettings.LightRotation, DefaultLightRotation);
										}
										return false;
									}),
									FSimpleDelegate::CreateLambda([WeakMetaHumanCharacterEditorViewport]
									{
										if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
										{
											TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
											const float DefaultLightRotation = GetDefault<UMetaHumanCharacter>()->ViewportSettings.LightRotation;
											UMetaHumanCharacterEditorSubsystem::Get()->UpdateLightRotation(MetaHumanCharacter, DefaultLightRotation);
											MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->Invalidate();
										}
									})))
							]
						]
					]
				]
			];
}

TSharedPtr<SMetaHumanCharacterEditorViewport> GetMetaHumanCharacterEditorViewportFromContext(UUnrealEdViewportToolbarContext* InEditorViewportContext)
{
	if (!InEditorViewportContext)
	{
		return {};
	}

	const TSharedPtr<SEditorViewport> EditorViewport = InEditorViewportContext->Viewport.Pin();
	if (!EditorViewport)
	{
		return {};
	}

	TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = StaticCastSharedPtr<SMetaHumanCharacterEditorViewport>(EditorViewport);
	return MetaHumanCharacterEditorViewport;
}

namespace FocalLengthUtils
{
	// Default sensor width from UCineCameraSettings (Super 35mm - Academy)
	constexpr float DefaultSensorWidth = 24.89f;

	float FOVToFocalLength(float FOVDegrees, float SensorWidth = DefaultSensorWidth)
	{
		const float ClampedFOV = FMath::Clamp(FOVDegrees, UE_SMALL_NUMBER, 179.0f);
		const float TanHalfFOV = FMath::Tan(FMath::DegreesToRadians(ClampedFOV / 2.0f));
		return (SensorWidth / 2.0f) / FMath::Max(TanHalfFOV, UE_SMALL_NUMBER);
	}

	float FocalLengthToFOV(float FocalLength, float SensorWidth = DefaultSensorWidth)
	{
		if (FocalLength > UE_SMALL_NUMBER)
		{
			return FMath::RadiansToDegrees(2.0f * FMath::Atan(SensorWidth / (2.0f * FocalLength)));
		}
		return 0.0f;
	}
}

FToolMenuEntry Create3DViewToggle()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicViewport3DViewToggle",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>());
				InDynamicSection.AddEntry(FToolMenuEntry::InitToolBarButton(
					NAME_None,
					FToolUIActionChoice(FUIAction(
						FExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									constexpr bool bIsTrackingVisible = false;
									MetaHumanCharacterEditorViewport->OnTrackerImageVisibilityChangedDelegate.ExecuteIfBound(bIsTrackingVisible);
									MetaHumanCharacterEditorViewport->Update2DViewportOverlay(bIsTrackingVisible);
								}
							}),
						FCanExecuteAction(),
						FGetActionCheckState::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									const TSharedRef<STrackerImageViewer> TrackerImageViewer = MetaHumanCharacterEditorViewport->GetTrackerImageViewer();
									return 
										TrackerImageViewer->GetVisibility() == EVisibility::Hidden ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								}
								return ECheckBoxState::Checked;
							}),
						FIsActionButtonVisible::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									return MetaHumanCharacterEditorViewport->IsViewportModeVisible();
								}
								return false;
							})
					)),
					LOCTEXT("ViewportToolbar3DViewToggle_Label", "3D View"),
					LOCTEXT("ViewportToolbar3DViewToggle_ToolTip", "Switch to 3D View"),
					FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), "Viewport.Icons.Camera"),
					EUserInterfaceActionType::ToggleButton
				));
			}));
}

FToolMenuEntry Create2DViewFacialTracingToggle()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicViewport2DViewFacialTracingToggle",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>());
				InDynamicSection.AddEntry(FToolMenuEntry::InitToolBarButton(
					NAME_None,
					FToolUIActionChoice(FUIAction(
						FExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									constexpr bool bIsTrackingVisible = true;
									MetaHumanCharacterEditorViewport->OnTrackerImageVisibilityChangedDelegate.ExecuteIfBound(bIsTrackingVisible);
									MetaHumanCharacterEditorViewport->Update2DViewportOverlay(bIsTrackingVisible);
								}
							}),
						FCanExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									const TSharedRef<STrackerImageViewer> TrackerImageViewer = MetaHumanCharacterEditorViewport->GetTrackerImageViewer();
									return TrackerImageViewer->ContainsData();
								}
								return false;
							}),
						FGetActionCheckState::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									const TSharedRef<STrackerImageViewer> TrackerImageViewer = MetaHumanCharacterEditorViewport->GetTrackerImageViewer();
									return TrackerImageViewer->GetVisibility() == EVisibility::Visible ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								}
								return ECheckBoxState::Checked;
							}),
						FIsActionButtonVisible::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									return MetaHumanCharacterEditorViewport->IsViewportModeVisible();
								}
								return false;
							})
					)),
					LOCTEXT("ViewportToolbar2DViewFacialTracingToggle_Label", "2D View"),
					LOCTEXT("ViewportToolbar2DViewFacialTracingToggle_ToolTip", "Switch to 2D View for facial tracing editing"),
					FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), "Viewport.Icons.Camera"),
					EUserInterfaceActionType::ToggleButton
				));
			}));
}

FToolMenuEntry Create2DViewOverlayVisibilityToggle()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicViewport2DViewOverlayVisibilityToggle",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>());
				InDynamicSection.AddEntry(FToolMenuEntry::InitToolBarButton(
					NAME_None,
					FToolUIActionChoice(FUIAction(
						FExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									MetaHumanCharacterEditorViewport->Toggle2DViewOverlay();
								}
							}),
						FCanExecuteAction(),
						FGetActionCheckState::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									return MetaHumanCharacterEditorViewport->Is2DViewOverlayEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								}
								return ECheckBoxState::Unchecked;
							}),
						FIsActionButtonVisible::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									const TSharedRef<STrackerImageViewer> TrackerImageViewer = MetaHumanCharacterEditorViewport->GetTrackerImageViewer();
									return 
										MetaHumanCharacterEditorViewport->IsViewportModeVisible() && 
										TrackerImageViewer->ContainsData();
								}
								return false;
							})
					)),
					FText::GetEmpty(),
					LOCTEXT("ViewportToolbar2DViewOverlayVisibilityToggle_ToolTip", "Set the visibility of the 2D View overlay"),
					FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Visibility"),
					EUserInterfaceActionType::ToggleButton
				));
			}));
}

TSharedRef<SWidget> CreateFocalLengthMenuWidget(const TSharedRef<SEditorViewport>& InViewport)
{
	TWeakPtr<FEditorViewportClient> ViewportClientWeak = InViewport->GetViewportClient();

	return
		SNew(SBox)
		.HAlign(HAlign_Right)
		[
			SNew(SBox)
			.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
			.WidthOverride(100.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("Menu.WidgetBorder"))
				.Padding(FMargin(1.0f))
				[
					SNew(SSpinBox<float>)
					.Style(&FAppStyle::Get(), "Menu.SpinBox")
					.ToolTipText(LOCTEXT("FocalLengthTooltip", "Focal length in mm. Changes the field of view."))
					.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
					.MinValue(1.0f)
					.MaxValue(1000.0f)
					.Value_Lambda([ViewportClientWeak]()
					{
						if (TSharedPtr<FEditorViewportClient> ViewportClient = ViewportClientWeak.Pin())
						{
							return FocalLengthUtils::FOVToFocalLength(ViewportClient->ViewFOV);
						}
						return FocalLengthUtils::FOVToFocalLength(UE::MetaHuman::ViewportDefaults::DefaultFOV);
					})
					.OnValueChanged_Lambda([ViewportClientWeak](float InNewValue)
					{
						if (TSharedPtr<FEditorViewportClient> ViewportClient = ViewportClientWeak.Pin())
						{
							const float NewFOV = FocalLengthUtils::FocalLengthToFOV(InNewValue);
							ViewportClient->FOVAngle = NewFOV;
							ViewportClient->ViewFOV = NewFOV;
							ViewportClient->Invalidate();
						}
					})
				]
			]
		];
}

TSharedRef<SWidget> CreateFOVMenuWidget(const TSharedRef<SEditorViewport>& InViewport)
{
	constexpr float FOVMin = 5.0f;
	constexpr float FOVMax = 170.0f;

	TWeakPtr<FEditorViewportClient> ViewportClientWeak = InViewport->GetViewportClient();

	return
		SNew(SBox)
		.HAlign(HAlign_Right)
		[
			SNew(SBox)
			.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
			.WidthOverride(100.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("Menu.WidgetBorder"))
				.Padding(FMargin(1.0f))
				[
					SNew(SSpinBox<float>)
					.Style(&FAppStyle::Get(), "Menu.SpinBox")
					.Font( FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
					.MinValue(FOVMin)
					.MaxValue(FOVMax)
					.Value_Lambda([ViewportClientWeak]()
					{
						if (TSharedPtr<FEditorViewportClient> ViewportClient = ViewportClientWeak.Pin())
						{
							return ViewportClient->ViewFOV;
						}
						return UE::MetaHuman::ViewportDefaults::DefaultFOV;
					})
					.OnValueChanged_Lambda([ViewportClientWeak](float InNewValue)
					{
						if (TSharedPtr<FEditorViewportClient> ViewportClient = ViewportClientWeak.Pin())
						{
							ViewportClient->FOVAngle = InNewValue;
							ViewportClient->ViewFOV = InNewValue;
							ViewportClient->Invalidate();
						}
					})
				]
			]
		];
}

TSharedRef<SWidget> CreateNearViewPlaneMenuWidget(const TSharedRef<SEditorViewport>& InViewport)
{
	TWeakPtr<FEditorViewportClient> ViewportClientWeak = InViewport->GetViewportClient();

	return
		SNew(SBox)
		.HAlign(HAlign_Right)
		[
			SNew(SBox)
			.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
			.WidthOverride(100.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("Menu.WidgetBorder"))
				.Padding(FMargin(1.0f))
				[
					SNew(SSpinBox<float>)
					.Style(&FAppStyle::Get(), "Menu.SpinBox")
					.ToolTipText(LOCTEXT("NearViewPlaneTooltip", "Distance to use as the near view plane"))
					.MinValue(0.001f)
					.MaxValue(100.0f)
					.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
					.Value_Lambda([ViewportClientWeak]()
					{
						if (TSharedPtr<FEditorViewportClient> ViewportClient = ViewportClientWeak.Pin())
						{
							return ViewportClient->GetNearClipPlane();
						}

						return UE::MetaHuman::ViewportDefaults::DefaultNearClippingPlane;
					})
					.OnValueChanged_Lambda([ViewportClientWeak](float InNewValue)
					{
						if (TSharedPtr<FEditorViewportClient> ViewportClient = ViewportClientWeak.Pin())
						{
							ViewportClient->OverrideNearClipPlane(InNewValue);
							ViewportClient->Invalidate();
						}
					})
				]
			]
		];
}

TSharedRef<SWidget> CreateFarViewPlaneMenuWidget(const TSharedRef<SEditorViewport>& InViewport)
{
	TWeakPtr<FEditorViewportClient> ViewportClientWeak = InViewport->GetViewportClient();

	/*
	 * FEditorViewportClient treats a far clip plane value of 0.0 as "infinity".
	 * This Spin Box transforms the maximum value to that 0.0 and back again,
	 * allowing the maximum value to be treated as infinity and creating a more
	 * natural interface.
	 */
	constexpr float MaxValue = 100000.0f;

	return
		SNew(SBox)
		.HAlign(HAlign_Right)
		[
			SNew(SBox)
			.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
			.WidthOverride(100.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("Menu.WidgetBorder"))
				.Padding(FMargin(1.0f))
				[
					SNew(SSpinBox<float>)
					.Style(&FAppStyle::Get(), "Menu.SpinBox")
					.ToolTipText(LOCTEXT("FarViewPlaneTooltip", "Distance to use as the far view plane"))
					.MinValue(0.01f)
					.MaxValue(MaxValue)
					.SliderExponent(3.0f) // Gives better precision for smaller ranges
					.OnGetDisplayValue_Lambda([](float InValue)
					{
						if (InValue >= MaxValue)
						{
							return TOptional<FText>(LOCTEXT("Infinity", "Infinity"));
						}
						return TOptional<FText>();
					})
					.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
					.Value_Lambda([ViewportClientWeak]()
					{
						if (TSharedPtr<FEditorViewportClient> ViewportClient = ViewportClientWeak.Pin())
						{
							const float Override = ViewportClient->GetFarClipPlaneOverride(); 
							if (Override > 0.0f)
							{
								return Override;
							}
						}

						return MaxValue;
					})
					.OnValueChanged_Lambda([ViewportClientWeak](float InNewValue)
					{
						if (TSharedPtr<FEditorViewportClient> ViewportClient = ViewportClientWeak.Pin())
						{
							ViewportClient->OverrideFarClipPlane(InNewValue >= MaxValue ? 0.0f : InNewValue);
							ViewportClient->Invalidate();
						}
					})
				]
			]
		];
}

TSharedRef<SWidget> CreateLensPresetButton(TWeakPtr<FEditorViewportClient> ViewportClientWeak, float FocalLengthMM)
{
	return
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "Button")
		.HAlign(HAlign_Center)
		.ContentPadding(FMargin(8.0f, 2.0f))
		.ToolTipText(FText::Format(LOCTEXT("LensPresetTooltip", "Set focal length to {0}mm"), FText::AsNumber(static_cast<int32>(FocalLengthMM))))
		.OnClicked_Lambda([ViewportClientWeak, FocalLengthMM]()
		{
			if (TSharedPtr<FEditorViewportClient> ViewportClient = ViewportClientWeak.Pin())
			{
				const float NewFOV = FocalLengthUtils::FocalLengthToFOV(FocalLengthMM);
				ViewportClient->FOVAngle = NewFOV;
				ViewportClient->ViewFOV = NewFOV;
				ViewportClient->Invalidate();
			}
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.TextStyle(FAppStyle::Get(), "SmallButtonText")
			.Text(FText::Format(LOCTEXT("LensPresetLabel", "{0}mm"), FText::AsNumber(static_cast<int32>(FocalLengthMM))))
		];
}

TSharedRef<SWidget> CreateLensPresetsWidget(const TSharedRef<SEditorViewport>& InViewport)
{
	TWeakPtr<FEditorViewportClient> ViewportClientWeak = InViewport->GetViewportClient();

	return
		SNew(SBox)
		.Padding(FMargin(8.0f, 2.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				CreateLensPresetButton(ViewportClientWeak, 35.0f)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(2.0f, 0.0f)
			[
				CreateLensPresetButton(ViewportClientWeak, 50.0f)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				CreateLensPresetButton(ViewportClientWeak, 100.0f)
			]
		];
}

TSharedRef<SWidget> CreateResetCameraDefaultsWidget(const TSharedRef<SEditorViewport>& InViewport)
{
	TWeakPtr<FEditorViewportClient> ViewportClientWeak = InViewport->GetViewportClient();

	return
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "Button")
		.HAlign(HAlign_Center)
		.ContentPadding(FMargin(4.0f, 2.0f))
		.Text(LOCTEXT("ResetCameraDefaultsLabel", "Reset Camera Defaults"))
		.ToolTipText(LOCTEXT("ResetCameraDefaultsTooltip", "Reset field of view, near and far view planes to their default values"))
		.OnClicked_Lambda([ViewportClientWeak]()
		{
			if (TSharedPtr<FEditorViewportClient> ViewportClient = ViewportClientWeak.Pin())
			{
				ViewportClient->FOVAngle = UE::MetaHuman::ViewportDefaults::DefaultFOV;
				ViewportClient->ViewFOV = UE::MetaHuman::ViewportDefaults::DefaultFOV;
				ViewportClient->OverrideNearClipPlane(UE::MetaHuman::ViewportDefaults::DefaultNearClippingPlane);
				ViewportClient->OverrideFarClipPlane(UE::MetaHuman::ViewportDefaults::DefaultFarClippingPlane);
				ViewportClient->Invalidate();
			}
			return FReply::Handled();
		});
}

FToolMenuEntry CreateEnvironmentSubmenu()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicEnvironmentOptions",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>());

				const TAttribute<FText> Label = TAttribute<FText>::CreateLambda(
					[WeakMetaHumanCharacterEditorViewport]()
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
							if (UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get())
							{
								{
									if (MetaHumanCharacter->ViewportSettings.CharacterEnvironment != EMetaHumanCharacterEnvironment::Custom)
									{
										return UEnum::GetDisplayValueAsText(MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter->ViewportSettings.CharacterEnvironment);
									}
									else
									{
										return FText::FromString(MetaHumanCharacter->ViewportSettings.CustomLightingEnvironmentKey);
									}
								}
							}

						return LOCTEXT("EnvironmentLabel", "Environment");
					}
				);

				FToolMenuEntry& Entry = InDynamicSection.AddSubMenu(
					"Environment",
					Label,
					LOCTEXT("EnvironmentSubmenuTooltip", "Select environment"),
					FNewToolMenuDelegate::CreateLambda(
						[](UToolMenu* Submenu) -> void
						{
							TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(Submenu->FindContext<UUnrealEdViewportToolbarContext>());
							if (!MetaHumanCharacterEditorViewport.IsValid())
							{
								return;
							}

							Submenu->AddMenuEntry(NAME_None, FToolMenuEntry::InitWidget(NAME_None, CreateEnvironmentWidget(MetaHumanCharacterEditorViewport), FText()));
						}
					),
					false,
					FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), "Viewport.Icons.Environment")
				);
				Entry.ToolBarData.ResizeParams.ClippingPriority = 800;
			}
		)
	);
}

FToolMenuEntry CreateCameraSelectionSubmenu()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicCameraOptions",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>());

				const TAttribute<FText> Label = TAttribute<FText>::CreateLambda(
					[WeakMetaHumanCharacterEditorViewport]()
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							if(UMetaHumanCharacter* Character = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get())
							{
								return UEnum::GetDisplayValueAsText(Character->ViewportSettings.CameraFrame);
							}
						}
						return LOCTEXT("CameraSelectionSubmenuLabel", "Camera Selection");
					}
				);

				FToolMenuEntry& Entry = InDynamicSection.AddSubMenu(
					"Camera",
					Label,
					LOCTEXT("CameraSelectionSubmenuTooltip", "Select camera framing"),
					FNewToolMenuDelegate::CreateLambda(
						[WeakMetaHumanCharacterEditorViewport](UToolMenu* Submenu) -> void
						{
							const FMetaHumanCharacterEditorViewportToolbarCommands& Commands = FMetaHumanCharacterEditorViewportToolbarCommands::Get();
							{
								FToolMenuSection& PerspectiveSection = Submenu->AddSection("Perspective", LOCTEXT("PerspectiveFraming","Framing"));

								PerspectiveSection.AddMenuEntry(Commands.FocusFace);
								PerspectiveSection.AddMenuEntry(Commands.FocusBody);
								PerspectiveSection.AddMenuEntry(Commands.FocusFar);
								PerspectiveSection.AddMenuEntry(Commands.FocusHands);
								PerspectiveSection.AddMenuEntry(Commands.FocusFeet);
							}
							Submenu->AddDynamicSection(
								"LensPresetsSection",
								FNewToolMenuDelegate::CreateLambda(
									[](UToolMenu* InMenu) -> void
									{
										TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InMenu->FindContext<UUnrealEdViewportToolbarContext>());
										if (!MetaHumanCharacterEditorViewport.IsValid())
										{
											return;
										}

										FToolMenuSection& LensSection = InMenu->AddSection("LensPresets", LOCTEXT("LensPresetsSection", "Lens Presets"));
										FToolMenuEntry LensPresetsEntry = FToolMenuEntry::InitWidget(
											"LensPresets",
											CreateLensPresetsWidget(MetaHumanCharacterEditorViewport.ToSharedRef()),
											FText()
										);
										LensSection.AddEntry(LensPresetsEntry);
										LensSection.Visibility = UE::UnrealEd::GetIsPerspectiveAttribute(MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient());
									}
								)
							);

							if (const TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
							{
								FToolMenuSection& ViewSection = Submenu->AddSection("View", LOCTEXT("ViewSection", "View"));

								FToolMenuEntry CameraFocalLength = FToolMenuEntry::InitWidget(
									"CameraFocalLength",
									CreateFocalLengthMenuWidget(MetaHumanCharacterEditorViewport.ToSharedRef()),
									LOCTEXT("CameraSubmenu_FocalLengthLabel", "Focal Length (mm)"),
									/* bNoIndent */ false
								);
								CameraFocalLength.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "ViewportToolbar.FieldOfView");
								CameraFocalLength.ToolTip = LOCTEXT("FocalLengthMenuTooltip", "Sets the focal length of the viewport's camera in mm.");
								ViewSection.AddEntry(CameraFocalLength);

								FToolMenuEntry CameraFOV = FToolMenuEntry::InitWidget(
									"CameraFOV",
									CreateFOVMenuWidget(MetaHumanCharacterEditorViewport.ToSharedRef()),
									LOCTEXT("CameraSubmenu_FieldOfViewLabel", "Field of View"), 
									/* bNoIndent */ false
								);
								CameraFOV.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "ViewportToolbar.FieldOfView");
								CameraFOV.ToolTip = LOCTEXT("CameraMovementTooltip", "Sets the field of view of the viewport's camera.");

								ViewSection.AddEntry(CameraFOV);

								FToolMenuEntry CameraNearViewPlane = FToolMenuEntry::InitWidget(
									"CameraNearViewPlane",
									CreateNearViewPlaneMenuWidget(MetaHumanCharacterEditorViewport.ToSharedRef()),
									LOCTEXT("CameraSubmenu_NearViewPlaneLabel", "Near View Plane"),
									/* bNoIndent */ false
								);
								CameraNearViewPlane.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "ViewportToolbar.NearViewPlane");
								ViewSection.AddEntry(CameraNearViewPlane);

								FToolMenuEntry CameraFarViewPlane = FToolMenuEntry::InitWidget(
									"CameraFarViewPlane",
									CreateFarViewPlaneMenuWidget(MetaHumanCharacterEditorViewport.ToSharedRef()),
									LOCTEXT("CameraSubmenu_FarViewPlaneLabel", "Far View Plane"),
									/* bNoIndent */ false
								);
								CameraFarViewPlane.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "ViewportToolbar.FarViewPlane");
								ViewSection.AddEntry(CameraFarViewPlane);

								FToolMenuEntry ResetEntry = FToolMenuEntry::InitWidget(
									"ResetCameraDefaults",
									SNew(SBox)
									.Padding(FMargin(32.0f, 2.0f, 12.0f, 4.0f))
									[
										CreateResetCameraDefaultsWidget(MetaHumanCharacterEditorViewport.ToSharedRef())
									],
									FText::GetEmpty(),
									/* bNoIndent */ true
								);
								ViewSection.AddEntry(ResetEntry);

								ViewSection.Visibility = UE::UnrealEd::GetIsPerspectiveAttribute(MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient());

								FToolMenuSection& SpeedSection = Submenu->AddSection("Speed", LOCTEXT("CameraSpeedSection", "Speed"));
								SpeedSection.AddMenuEntry(FEditorViewportCommands::Get().ToggleDistanceBasedCameraSpeed);
							}
						}
					),
					false,
					FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), "Viewport.Icons.Camera")
				);
				Entry.ToolBarData.ResizeParams.ClippingPriority = 800;

			}
		)
	);
}

void PopulateLODMenu(UToolMenu* InMenu)
{
	TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InMenu->FindContext<UUnrealEdViewportToolbarContext>());
	if (!WeakMetaHumanCharacterEditorViewport.IsValid())
	{
		return;
	}

	FToolMenuSection& LODSection = InMenu->FindOrAddSection("MetaHumanCharacterEditorViewport_LOD", LOCTEXT("LODSubmenuLabel", "Level of Detail"));

	for (EMetaHumanCharacterLOD LODOption : TEnumRange<EMetaHumanCharacterLOD>())
	{
		LODSection.AddMenuEntry(
			NAME_None,
			UEnum::GetDisplayValueAsText(LODOption),
			UEnum::GetDisplayValueAsText(LODOption),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport, LODOption]()
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();

							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							check(MetaHumanCharacter);

							MetaHumanCharacterSubsystem->UpdateCharacterLOD(MetaHumanCharacter, LODOption);
						}
					}),
				FCanExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport, LODOption]() -> bool
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();

							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							check(MetaHumanCharacter);

							return LODOption == EMetaHumanCharacterLOD::LOD0 || MetaHumanCharacterSubsystem->GetRiggingState(MetaHumanCharacter) != EMetaHumanCharacterRigState::Unrigged;
						}
						return false;
					})

			),
			EUserInterfaceActionType::Button
		);
	}

	LODSection.AddSeparator(NAME_None);

	LODSection.AddMenuEntry(
		NAME_None,
		LOCTEXT("AlwaysUseCardsLabel", "Always Use Hair Cards"),
		LOCTEXT("AlwaysUseCardsSubmenuTooltip", "Toggle always use hair cards on groom components"),
		FSlateIcon(),
		FUIAction(
		FExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
		{
			if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
			{
				UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
				UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
				check(MetaHumanCharacter);
				const bool bUseCards = !MetaHumanCharacter->ViewportSettings.bAlwaysUseHairCards;
				MetaHumanCharacter->ViewportSettings.bAlwaysUseHairCards = bUseCards;
				MetaHumanCharacterSubsystem->UpdateAlwaysUseHairCardsOption(MetaHumanCharacter, bUseCards);
				MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->Invalidate();
			}
		}),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
		{
			if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
			{
				UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();

				UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
				check(MetaHumanCharacter);

				return MetaHumanCharacter->ViewportSettings.bAlwaysUseHairCards;
			}
			return false;
		})
		),
		EUserInterfaceActionType::Check
	);
}

FToolMenuEntry CreateLODSubmenu()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicLODOptions",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>());

				const TAttribute<FText> Label = TAttribute<FText>::CreateLambda(
					[WeakMetaHumanCharacterEditorViewport]()
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							return UEnum::GetDisplayValueAsText(MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter->ViewportSettings.LevelOfDetail);
						}
						return LOCTEXT("LODSelectionSubmenuLabel", "LOD Selection");
					}
				);

				FToolMenuEntry& Entry = InDynamicSection.AddSubMenu(
					"LOD",
					Label,
					LOCTEXT("LODSubmenuTooltip", "Select LOD"),
					FNewToolMenuDelegate::CreateLambda(
						[](UToolMenu* Submenu) -> void
						{
							PopulateLODMenu(Submenu);
							Submenu->bSearchable = false;
						}
					),
					false,
					FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), "Viewport.Icons.LOD")
				);
				Entry.ToolBarData.ResizeParams.ClippingPriority = 800;
			}
		)
	);
}

void PopulateRenderingQualityMenu(UToolMenu* InMenu)
{
	TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InMenu->FindContext<UUnrealEdViewportToolbarContext>());
	if (!WeakMetaHumanCharacterEditorViewport.IsValid())
	{
		return;
	}

	FToolMenuSection& RenderingQualitySection = InMenu->FindOrAddSection("MetaHumanCharacterEditorViewport_RenderingQuality", LOCTEXT("RenderingQualitySubmenuLabel", "Rendering Quality"));
	const UMetaHumanCharacterEditorSettings* MetaHumanEditorSettings = GetDefault<UMetaHumanCharacterEditorSettings>();
	check(MetaHumanEditorSettings)

	const TArray<FMetaHumanCharacterRenderingQualityProfile> AllProfiles = MetaHumanEditorSettings->GetAllRenderingQualityProfiles();
	for (int32 Idx = 0; Idx < AllProfiles.Num(); Idx++)
	{
		const FText RenderingQualityOption = FText::AsCultureInvariant(AllProfiles[Idx].ProfileName);
		RenderingQualitySection.AddMenuEntry(
			NAME_None,
			RenderingQualityOption,
			RenderingQualityOption,
			FSlateIcon(),
			FUIAction
			(
				FExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport, ProfileIndex = Idx]()
				{
					if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
					{
						UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
						check(MetaHumanCharacter);

						const UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
						MetaHumanCharacterSubsystem->NotifyViewportToolbarRenderingQualityProfileChange(MetaHumanCharacter, ProfileIndex);
					}
				}),
				FCanExecuteAction()
			),
			EUserInterfaceActionType::Button
		);

	}
}

FToolMenuEntry CreateRenderingQualitySubmenu()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicRenderingQualityOptions",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>());

				const TAttribute<FText> Label = TAttribute<FText>::CreateLambda(
					[WeakMetaHumanCharacterEditorViewport]()
					{
						const FText DefaultText = LOCTEXT("RenderingQualitySelectionSubmenuLabel", "Rendering Quality Selection");
						if (const TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							const UMetaHumanCharacterEditorSettings* MetaHumanEditorSettings = GetDefault<UMetaHumanCharacterEditorSettings>();
							check(MetaHumanEditorSettings)
							const int32 ProfileIndex = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter->ViewportSettings.RenderingQualityProfileIndex;
							return MetaHumanEditorSettings->IsValidRenderingQualityProfileIndex(ProfileIndex)
								? FText::AsCultureInvariant(MetaHumanEditorSettings->GetRenderingQualityProfile(ProfileIndex).ProfileName)
								: DefaultText;
						}
						return DefaultText;
					}
				);

				FToolMenuEntry& Entry = InDynamicSection.AddSubMenu(
					"RenderingQuality",
					Label,
					LOCTEXT("RenderingQualitySubmenuTooltip", "Select rendering quality"),
					FNewToolMenuDelegate::CreateLambda(
						[](UToolMenu* Submenu) -> void
						{
							PopulateRenderingQualityMenu(Submenu);
						}
					),
					false,
					FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), "Viewport.Icons.Quality")
				);
				Entry.ToolBarData.ResizeParams.ClippingPriority = 800;
			}
		)
	);
}

void PopulateDebugSubmenu(UToolMenu* InMenu)
{
	TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InMenu->FindContext<UUnrealEdViewportToolbarContext>());
	if (!WeakMetaHumanCharacterEditorViewport.IsValid())
	{
		return;
	}

	FToolMenuSection& FaceDebugOptionsSection = InMenu->FindOrAddSection("MetaHumanCharacterEditorViewport_FaceDebug", LOCTEXT("FaceDebugSubmenuLabel", "Face"));
	FToolMenuSection& BodyDebugOptionsSection = InMenu->FindOrAddSection("MetaHumanCharacterEditorViewport_BodyDebug", LOCTEXT("BodyDebugSubmenuLabel", "Body"));

	FaceDebugOptionsSection.AddEntry(FToolMenuEntry::InitWidget(
		NAME_None,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 1.0f)
		.AutoHeight()
		[
			SNew(SCheckBox)
			.Padding(5.0f)
			.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
				{
					bool Value = false;
					if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
					{
						UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
						return MetaHumanCharacter->ViewportSettings.bShowFaceBones ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}
				
					return ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
			{
				if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
				{
					UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
					MetaHumanCharacter->ViewportSettings.bShowFaceBones = NewState == ECheckBoxState::Checked ? true : false;
					MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowFaceBonesOnCharacter(NewState == ECheckBoxState::Checked ? true : false);
				}
			})
			.Content()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1.f)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Persona.AssetClass.Skeleton"))
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1.f)
				[
					SNew(STextBlock).Text(LOCTEXT("DebugSubmenuShowBonesOnFace", "Bones"))
				]
			]
			
		]
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(5.0f, 1.0f)
		[
			SNew(SSlider)
			.Visibility_Lambda([WeakMetaHumanCharacterEditorViewport]() -> EVisibility
			{
				if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
				{
					UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
					return MetaHumanCharacter->ViewportSettings.bShowFaceBones? EVisibility::Visible : EVisibility::Collapsed;
				}
				return EVisibility::Collapsed;
			})
			.Value_Lambda([WeakMetaHumanCharacterEditorViewport]() -> float
			{
				float BoneSize = 1.f;	
				if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
				{
					UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
					BoneSize = MetaHumanCharacter->ViewportSettings.FaceBoneSize;
				}

				return BoneSize;
			})
			.OnValueChanged_Lambda([WeakMetaHumanCharacterEditorViewport](float InValue)
			{
				if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
				{
					UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
					MetaHumanCharacter->ViewportSettings.FaceBoneSize = InValue;

					MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowFaceBonesOnCharacter(true);
				}
			}
			)
		]
		,
		FText(),
		true
		));


	BodyDebugOptionsSection.AddEntry(FToolMenuEntry::InitWidget(
		NAME_None,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(5.0f,1.0)
		[
			SNew(SCheckBox)
			.Padding(5.0f)
			.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
			{
				bool Value = false;
				if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
				{
					UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
					return MetaHumanCharacter->ViewportSettings.bShowBodyBones ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}

				return ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
			{
				if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
				{
					UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
					MetaHumanCharacter->ViewportSettings.bShowBodyBones = NewState == ECheckBoxState::Checked ? true : false;
					MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowBodyBonesOnCharacter(NewState == ECheckBoxState::Checked ? true : false);
				}
			})
			
			.Content()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1.f)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Persona.AssetClass.Skeleton"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1.f)
				[
					SNew(STextBlock).Text(LOCTEXT("DebugSubmenuShowBonesOnBody", "Bones"))
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(5.0f, 1.f)
		[
			SNew(SSlider)
				.Visibility_Lambda([WeakMetaHumanCharacterEditorViewport]() -> EVisibility
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							return MetaHumanCharacter->ViewportSettings.bShowBodyBones ? EVisibility::Visible : EVisibility::Collapsed;
						}
						return EVisibility::Collapsed;
					})
				.Value_Lambda([WeakMetaHumanCharacterEditorViewport]() -> float
					{
						float BoneSize = 1.f;
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							BoneSize = MetaHumanCharacter->ViewportSettings.BodyBoneSize;
						}

						return BoneSize;
					})
				.OnValueChanged_Lambda([WeakMetaHumanCharacterEditorViewport](float InValue)
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							MetaHumanCharacter->ViewportSettings.BodyBoneSize = InValue;

							MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowBodyBonesOnCharacter(true);
						}
					}
				)
		]
		,
		FText(),
		true
	));

	FaceDebugOptionsSection.AddEntry(FToolMenuEntry::InitWidget(
		NAME_None,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 1.0f)
		[
			SNew(SCheckBox)
				.Padding(5.0f)
				.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
					{
						bool Value = false;
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							return MetaHumanCharacter->ViewportSettings.bShowFaceNormals ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}

						return ECheckBoxState::Unchecked;
					})
				.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							MetaHumanCharacter->ViewportSettings.bShowFaceNormals = NewState == ECheckBoxState::Checked ? true : false;
							MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowFaceNormalsOnCharacter(NewState == ECheckBoxState::Checked ? true : false);
						}
					})
				.Content()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("AnimViewportMenu.SetShowNormals.Small"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.f)
					[
						SNew(STextBlock).Text(LOCTEXT("DebugSubmenuShowNormalsOnFace", "Normals"))
					]
				]
		]
	,
		FText(),
		true
	));

	BodyDebugOptionsSection.AddEntry(FToolMenuEntry::InitWidget(
		NAME_None,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 1.0f)
		[
			SNew(SCheckBox)
				.Padding(5.0f)
				.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
					{
						bool Value = false;
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							return MetaHumanCharacter->ViewportSettings.bShowBodyNormals ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}

						return ECheckBoxState::Unchecked;
					})
				.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							MetaHumanCharacter->ViewportSettings.bShowBodyNormals = NewState == ECheckBoxState::Checked ? true : false;
							MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowBodyNormalsOnCharacter(NewState == ECheckBoxState::Checked ? true : false);
						}
					})
				.Content()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("AnimViewportMenu.SetShowNormals.Small"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.f)
					[
						SNew(STextBlock).Text(LOCTEXT("DebugSubmenuShowNormalsOnBody", "Normals"))
					]
				]
		]
	,
		FText(),
		true
	));

	FaceDebugOptionsSection.AddEntry(FToolMenuEntry::InitWidget(
		NAME_None,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 1.0f)
		[
			SNew(SCheckBox)
				.Padding(5.0f)
				.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
					{
						bool Value = false;
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							return MetaHumanCharacter->ViewportSettings.bShowFaceTangents ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}

						return ECheckBoxState::Unchecked;
					})
				.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							MetaHumanCharacter->ViewportSettings.bShowFaceTangents = NewState == ECheckBoxState::Checked ? true : false;
							MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowFaceTangentsOnCharacter(NewState == ECheckBoxState::Checked ? true : false);
						}
					})
				.Content()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("AnimViewportMenu.SetShowTangents.Small"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.f)
					[
						SNew(STextBlock).Text(LOCTEXT("DebugSubmenuShowTangentsOnFace", "Tangents"))
					]
				]
		]
	,
		FText(),
		true
	));

	BodyDebugOptionsSection.AddEntry(FToolMenuEntry::InitWidget(
		NAME_None,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 1.0f)
		[
			SNew(SCheckBox)
				.Padding(5.0f)
				.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
					{
						bool Value = false;
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							return MetaHumanCharacter->ViewportSettings.bShowBodyTangents ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}

						return ECheckBoxState::Unchecked;
					})
				.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							MetaHumanCharacter->ViewportSettings.bShowBodyTangents = NewState == ECheckBoxState::Checked ? true : false;
							MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowBodyTangentsOnCharacter(NewState == ECheckBoxState::Checked ? true : false);
						}
					})
				.Content()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("AnimViewportMenu.SetShowTangents.Small"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.f)
					[
						SNew(STextBlock).Text(LOCTEXT("DebugSubmenuShowTangentsOnBody", "Tangents"))
					]
				]
		]
	,
		FText(),
		true
	));

	FaceDebugOptionsSection.AddEntry(FToolMenuEntry::InitWidget(
		NAME_None,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 1.0f)
		[
			SNew(SCheckBox)
				.Padding(5.0f)
				.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
					{
						bool Value = false;
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							return MetaHumanCharacter->ViewportSettings.bShowFaceBinormals ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}

						return ECheckBoxState::Unchecked;
					})
				.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							MetaHumanCharacter->ViewportSettings.bShowFaceBinormals = NewState == ECheckBoxState::Checked ? true : false;
							MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowFaceBinormalsOnCharacter(NewState == ECheckBoxState::Checked ? true : false);
						}
					})
				.Content()
				[
					SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(1.f)
						[
							SNew(SImage)
								.Image(FAppStyle::GetBrush("AnimViewportMenu.SetShowBinormals.Small"))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(1.f)
						[
							SNew(STextBlock).Text(LOCTEXT("DebugSubmenuShowBinormalsOnFace", "Binormals"))
						]
				]
		]
	,
		FText(),
		true
	));

	BodyDebugOptionsSection.AddEntry(FToolMenuEntry::InitWidget(
		NAME_None,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 1.0f)
		[
			SNew(SCheckBox)
				.Padding(5.0f)
				.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
					{
						bool Value = false;
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							return MetaHumanCharacter->ViewportSettings.bShowBodyBinormals ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}

						return ECheckBoxState::Unchecked;
					})
				.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							MetaHumanCharacter->ViewportSettings.bShowBodyBinormals = NewState == ECheckBoxState::Checked ? true : false;
							MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowBodyBinormalsOnCharacter(NewState == ECheckBoxState::Checked ? true : false);
						}
					})
				.Content()
				[
					SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(1.f)
						[
							SNew(SImage)
								.Image(FAppStyle::GetBrush("AnimViewportMenu.SetShowBinormals.Small"))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(1.f)
						[
							SNew(STextBlock).Text(LOCTEXT("DebugSubmenuShowBinormalsOnBody", "Binormals"))
						]
				]
		]
	,
		FText(),
		true
	));

}

FToolMenuEntry CreateDebugSubmenu()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicDebugOptions",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				const TAttribute<FText> Label = TAttribute<FText>(FText::GetEmpty());
				
				FToolMenuEntry& Entry = InDynamicSection.AddSubMenu(
					"DebugSkeletalMesh",
					Label,
					LOCTEXT("DebugSkeletalMeshSubmenuTooltip", "Rendering debug options for SkeletalMeshes"),
					FNewToolMenuDelegate::CreateLambda(
						[](UToolMenu* Submenu)->void
						{
							PopulateDebugSubmenu(Submenu);
						}
					),
					false,
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visibility", "Icons.Visibility.Small")
				);
				Entry.ToolBarData.ResizeParams.ClippingPriority = 800;
			}
		)
	);
}

void PopulatePreviewMaterialMenu(UToolMenu* InMenu)
{
	TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InMenu->FindContext<UUnrealEdViewportToolbarContext>());
	if (!WeakMetaHumanCharacterEditorViewport.IsValid())
	{
		return;
	}

	FToolMenuSection& PreviewMaterialSection = InMenu->FindOrAddSection("MetaHumanCharacterEditorViewport_PreviewMaterial", LOCTEXT("PreviewMaterialSubmenuLabel", "Preview Material"));

	for (EMetaHumanCharacterSkinPreviewMaterial PreviewMaterialOption : TEnumRange<EMetaHumanCharacterSkinPreviewMaterial>())
	{
		PreviewMaterialSection.AddMenuEntry(
			NAME_None,
			UEnum::GetDisplayValueAsText(PreviewMaterialOption),
			UEnum::GetDisplayValueAsText(PreviewMaterialOption),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport, PreviewMaterialOption]()
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();

							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							check(MetaHumanCharacter);

							MetaHumanCharacterSubsystem->UpdateCharacterPreviewMaterial(MetaHumanCharacter, PreviewMaterialOption);
						}
					}),
				FCanExecuteAction()
			),
			EUserInterfaceActionType::Button
		);
	}
}

FToolMenuEntry CreatePreviewMaterialSubmenu()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicMaterialOptions",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>());

				const TAttribute<FText> Label = TAttribute<FText>::CreateLambda(
					[WeakMetaHumanCharacterEditorViewport]()
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							return UEnum::GetDisplayValueAsText(MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter->PreviewMaterialType);
						}
						return LOCTEXT("MaterialSubmenuLabel", "Preview Material");
					}
				);

				FToolMenuEntry& Entry = InDynamicSection.AddSubMenu(
					"PreviewMaterial",
					Label,
					LOCTEXT("MaterialSubmenuLabelTooltip", "Select preview material"),
					FNewToolMenuDelegate::CreateLambda(
						[](UToolMenu* Submenu) -> void
						{
							PopulatePreviewMaterialMenu(Submenu);
						}
					),
					false,
					FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), "Viewport.Icons.Clay")
				);
				Entry.ToolBarData.ResizeParams.ClippingPriority = 800;
			}
		)
	);
}

FToolMenuEntry CreateViewportOverlayToggle()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicViewportOverlayToggle",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>());
				InDynamicSection.AddEntry(FToolMenuEntry::InitToolBarButton(
					NAME_None,
					FToolUIActionChoice(FUIAction(
						FExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									if (UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get())
									{
										MetaHumanCharacter->ViewportSettings.bShowViewportOverlays = !MetaHumanCharacter->ViewportSettings.bShowViewportOverlays;
									}
								}
							}),
						FCanExecuteAction(),
						FGetActionCheckState::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
									if (MetaHumanCharacter)
									{
										return MetaHumanCharacter->ViewportSettings.bShowViewportOverlays ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
									}
								}
								return ECheckBoxState::Unchecked;
							})
					)),
					TAttribute<FText>{},
					LOCTEXT("ViewportToolbarToggleViewport", "Toggle viewport overlay"),
					FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), "Viewport.Icons.Keyboard"),
					EUserInterfaceActionType::ToggleButton
				));
			}
		)
	);
}

void PopulateMetaHumanViewModesMenu(UToolMenu* InMenu)
{
	UE::UnrealEd::PopulateViewModesMenu(InMenu);

	UUnrealEdViewportToolbarContext* Context = InMenu->FindContext<UUnrealEdViewportToolbarContext>();
	if (!Context)
	{
		return;
	}

	const TSharedPtr<SEditorViewport> EditorViewport = Context->Viewport.Pin();
	if (!EditorViewport)
	{
		return;
	}

	TWeakPtr<SEditorViewport> WeakViewport = EditorViewport;
	TWeakPtr<FEditorViewportClient> WeakClient = EditorViewport->GetViewportClient();

	FToolMenuSection& Section = InMenu->FindOrAddSection("ViewMode");

	Section.AddSubMenu(
		"VisualizeBufferViewMode",
		LOCTEXT("VisualizeBufferViewModeDisplayName", "Buffer Visualization"),
		LOCTEXT("BufferVisualizationMenu_ToolTip", "Select a mode for buffer visualization"),
		FNewMenuDelegate::CreateStatic(&FBufferVisualizationMenuCommands::BuildVisualisationSubMenu),
		FUIAction(
			FExecuteAction(),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([WeakClient]()
			{
				if (TSharedPtr<FEditorViewportClient> Client = WeakClient.Pin())
				{
					return Client->IsViewModeEnabled(VMI_VisualizeBuffer);
				}
				return false;
			})
		),
		EUserInterfaceActionType::RadioButton,
		/* bInOpenSubMenuOnClick = */ false,
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.VisualizeBufferMode")
	);

	Section.AddSubMenu(
		"VisualizeNaniteViewMode",
		LOCTEXT("VisualizeNaniteViewModeDisplayName", "Nanite Visualization"),
		LOCTEXT("NaniteVisualizationMenu_ToolTip", "Select a mode for Nanite visualization"),
		FNewMenuDelegate::CreateStatic(&FNaniteVisualizationMenuCommands::BuildVisualisationSubMenu),
		FUIAction(
			FExecuteAction(),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([WeakClient]()
			{
				if (TSharedPtr<FEditorViewportClient> Client = WeakClient.Pin())
				{
					return Client->IsViewModeEnabled(VMI_VisualizeNanite);
				}
				return false;
			})
		),
		EUserInterfaceActionType::RadioButton,
		/* bInOpenSubMenuOnClick = */ false,
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.VisualizeNaniteMode")
	);

	Section.AddSubMenu(
		"VisualizeLumenViewMode",
		LOCTEXT("VisualizeLumenViewModeDisplayName", "Lumen"),
		LOCTEXT("LumenVisualizationMenu_ToolTip", "Select a mode for Lumen visualization"),
		FNewMenuDelegate::CreateStatic(&FLumenVisualizationMenuCommands::BuildVisualisationSubMenu),
		FUIAction(
			FExecuteAction(),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([WeakClient]()
			{
				if (TSharedPtr<FEditorViewportClient> Client = WeakClient.Pin())
				{
					return Client->IsViewModeEnabled(VMI_VisualizeLumen);
				}
				return false;
			})
		),
		EUserInterfaceActionType::RadioButton,
		/* bInOpenSubMenuOnClick = */ false,
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.VisualizeLumenMode")
	);

	if (Substrate::IsSubstrateEnabled())
	{
		Section.AddSubMenu(
			"VisualizeSubstrateViewMode",
			LOCTEXT("VisualizeSubstrateViewModeDisplayName", "Substrate"),
			LOCTEXT("SubstrateVisualizationMenu_ToolTip", "Select a mode for Substrate visualization"),
			FNewMenuDelegate::CreateStatic(&FSubstrateVisualizationMenuCommands::BuildVisualisationSubMenu),
			FUIAction(
				FExecuteAction(),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([WeakClient]()
				{
					if (TSharedPtr<FEditorViewportClient> Client = WeakClient.Pin())
					{
						return Client->IsViewModeEnabled(VMI_VisualizeSubstrate);
					}
					return false;
				})
			),
			EUserInterfaceActionType::RadioButton,
			/* bInOpenSubMenuOnClick = */ false,
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.VisualizeSubstrateMode")
		);
	}

	if (IsGroomEnabled())
	{
		Section.AddEntry(FGroomVisualizationMenuCommands::BuildVisualizationSubMenuItem(WeakViewport));
	}

	Section.AddSubMenu(
		"VisualizeVirtualShadowMapViewMode",
		LOCTEXT("VisualizeVirtualShadowMapViewModeDisplayName", "Virtual Shadow Map"),
		LOCTEXT("VirtualShadowMapVisualizationMenu_ToolTip", "Select a mode for virtual shadow map visualization"),
		FNewMenuDelegate::CreateStatic(&FVirtualShadowMapVisualizationMenuCommands::BuildVisualisationSubMenu),
		FUIAction(
			FExecuteAction(),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([WeakClient]()
			{
				if (TSharedPtr<FEditorViewportClient> Client = WeakClient.Pin())
				{
					return GetVirtualShadowMapVisualizationData().IsVSMViewMode(Client->GetViewMode());
				}
				return false;
			})
		),
		EUserInterfaceActionType::RadioButton,
		/* bInOpenSubMenuOnClick = */ false,
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.VisualizeVirtualShadowMapMode")
	);
}

FToolMenuEntry CreateViewModesSubmenu()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicViewModes",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TAttribute<FText> LabelAttribute = UE::UnrealEd::GetViewModesSubmenuLabel(nullptr);
				TAttribute<FSlateIcon> IconAttribute = TAttribute<FSlateIcon>();
				if (UUnrealEdViewportToolbarContext* Context =
						InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>())
				{
					LabelAttribute = TAttribute<FText>::CreateLambda(
						[WeakViewport = Context->Viewport]()
						{
							return UE::UnrealEd::GetViewModesSubmenuLabel(WeakViewport);
						}
					);

					IconAttribute = TAttribute<FSlateIcon>::CreateLambda(
						[WeakViewport = Context->Viewport]()
						{
							return UE::UnrealEd::GetViewModesSubmenuIcon(WeakViewport);
						}
					);
				}

				FToolMenuEntry& Entry = InDynamicSection.AddSubMenu(
					"ViewModes",
					LabelAttribute,
					LOCTEXT("ViewModesSubmenuTooltip", "View mode settings for the current viewport."),
					FNewToolMenuDelegate::CreateLambda(
						[](UToolMenu* Submenu) -> void
						{
							// Populates the base view modes (Lit, Unlit, Wireframe, etc.) via
							// UE::UnrealEd::PopulateViewModesMenu, then appends additional
							// visualization submenus (Buffer, Nanite, Lumen, Substrate, Groom,
							// Virtual Shadow Map) that are not included in the
							// default PopulateViewModesMenu but are available in the level editor.
							PopulateMetaHumanViewModesMenu(Submenu);
						}
					),
					/* bInOpenSubMenuOnClick = */ false,
					IconAttribute
				);
				Entry.ToolBarData.ResizeParams.ClippingPriority = 800;
			}
		)
	);
}

#undef LOCTEXT_NAMESPACE
