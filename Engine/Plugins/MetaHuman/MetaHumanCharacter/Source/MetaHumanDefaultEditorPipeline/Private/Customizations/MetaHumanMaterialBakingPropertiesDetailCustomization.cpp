// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMaterialBakingPropertiesDetailCustomization.h"
#include "MetaHumanDefaultEditorPipelineBase.h"

#include "Algo/AnyOf.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "ScopedTransaction.h"
#include "SEnumCombo.h"
#include "TG_Graph.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MetaHumanMaterialBakingPropertiesDetailsCustomizations"

TSharedRef<IPropertyTypeCustomization> FMetaHumanMaterialBakingPropertiesDetailCustomization::MakeInstance()
{
	return MakeShared<FMetaHumanMaterialBakingPropertiesDetailCustomization>();
}

void FMetaHumanMaterialBakingPropertiesDetailCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	InHeaderRow.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		InPropertyHandle->CreatePropertyValueWidget()
	];
}

void FMetaHumanMaterialBakingPropertiesDetailCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	ActiveTab = MakeShared<EBakingTab>(EBakingTab::Head);

	TSharedPtr<IPropertyHandle> FaceProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetaHumanMaterialBakingProperties, FaceBakingOptions));
	TSharedPtr<IPropertyHandle> BodyProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetaHumanMaterialBakingProperties, BodyBakingOptions));

	TWeakPtr<EBakingTab> WeakTab = ActiveTab;

	// Tab switcher
	InChildBuilder.AddCustomRow(LOCTEXT("TabSearch", "Face Body"))
		.WholeRowContent()
		[
			SNew(SBox)
				.Padding(FMargin(20.0f, 0.0f))
				[
					SNew(SSegmentedControl<EBakingTab>)
						.Value_Lambda([WeakTab]()
						{
							TSharedPtr<EBakingTab> Tab = WeakTab.Pin();
							return Tab.IsValid() ? *Tab : EBakingTab::Head;
						})
						.OnValueChanged_Lambda([WeakTab](EBakingTab NewTab)
						{
							if (TSharedPtr<EBakingTab> Tab = WeakTab.Pin())
							{
								*Tab = NewTab;
							}
						})
						+ SSegmentedControl<EBakingTab>::Slot(EBakingTab::Head)
							.Text(LOCTEXT("HeadTab", "Head"))
						+ SSegmentedControl<EBakingTab>::Slot(EBakingTab::Body)
							.Text(LOCTEXT("BodyTab", "Body"))
				]
		];

	// Add widgets for both tabs (visibility-toggled)
	if (FaceProperty.IsValid())
	{
		AddBakingOptionsWidgets(InChildBuilder, FaceProperty.ToSharedRef(), ActiveTab, EBakingTab::Head);
	}
	if (BodyProperty.IsValid())
	{
		AddBakingOptionsWidgets(InChildBuilder, BodyProperty.ToSharedRef(), ActiveTab, EBakingTab::Body);
	}
}

void FMetaHumanMaterialBakingPropertiesDetailCustomization::AddBakingOptionsWidgets(
	IDetailChildrenBuilder& InChildBuilder,
	TSharedRef<IPropertyHandle> BakingOptionsHandle,
	TSharedPtr<EBakingTab> InActiveTab,
	EBakingTab Tab)
{
	TWeakPtr<EBakingTab> WeakTab = InActiveTab;

	auto MakeVisibility = [WeakTab, Tab]()
	{
		TSharedPtr<EBakingTab> PinnedTab = WeakTab.Pin();
		return (PinnedTab.IsValid() && *PinnedTab == Tab) ? EVisibility::Visible : EVisibility::Collapsed;
	};

	// Show the BakingSettings property only when editing the asset directly (CDO/archetype), not inside a tool
	TSharedPtr<IPropertyHandle> BakingSettingsHandle = BakingOptionsHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetaHumanMaterialBakingOptions, BakingSettings));
	if (BakingSettingsHandle.IsValid())
	{
		TArray<UObject*> OwnerObjects;
		BakingOptionsHandle->GetOuterObjects(OwnerObjects);

		const bool bIsCDOOrArchetype = !OwnerObjects.IsEmpty() && OwnerObjects[0]->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject);
		if (bIsCDOOrArchetype)
		{
			InChildBuilder.AddProperty(BakingSettingsHandle.ToSharedRef())
				.Visibility(MakeAttributeLambda(MakeVisibility));
		}
	}

	// Resolve the pipeline and baking options from the outer UObject
	TArray<UObject*> OuterObjects;
	BakingOptionsHandle->GetOuterObjects(OuterObjects);
	if (OuterObjects.IsEmpty())
	{
		return;
	}

	UMetaHumanDefaultEditorPipelineBase* Pipeline = Cast<UMetaHumanDefaultEditorPipelineBase>(OuterObjects[0]);
	if (Pipeline == nullptr)
	{
		return;
	}

	FMetaHumanMaterialBakingOptions* BakingOptions = (Tab == EBakingTab::Head)
		? &Pipeline->MaterialBakingOptions.FaceBakingOptions
		: &Pipeline->MaterialBakingOptions.BodyBakingOptions;

	const UMetaHumanMaterialBakingSettings* BakingSettings = BakingOptions->BakingSettings.LoadSynchronous();
	if (BakingSettings == nullptr)
	{
		return;
	}

	// Determine which LODs the assembly will output for this tab
	const TArray<int32>& ActiveLODs = (Tab == EBakingTab::Head)
		? Pipeline->LODProperties.FaceLODs
		: Pipeline->LODProperties.BodyLODs;
	// Precompute default resolutions from texture graph output settings and group textures by category
	TMap<FName, EMetaHumanBuildTextureResolution> DefaultResolutions;
	TMap<FName, TArray<const FMetaHumanOutputTextureProperties*>> CategorizedTextures;
	for (const FMetaHumanTextureGraphOutputProperties& TextureGraph : BakingSettings->TextureGraphs)
	{
		for (const FMetaHumanOutputTextureProperties& OutputTexture : TextureGraph.OutputTextures)
		{
			// Skip textures not used by any of the active LODs
			if (!OutputTexture.UsedInLODs.IsEmpty() && !ActiveLODs.IsEmpty())
			{
				const bool bUsedInActiveLOD = Algo::AnyOf(OutputTexture.UsedInLODs,
														  [&ActiveLODs](int32 LOD)
														  {
															  return ActiveLODs.Contains(LOD);
														  });
				if (!bUsedInActiveLOD)
				{
					continue;
				}
			}

			FName CategoryName = OutputTexture.Category.IsNone() ? FName(TEXT("Uncategorized")) : OutputTexture.Category;
			CategorizedTextures.FindOrAdd(CategoryName).Add(&OutputTexture);

			// Use the pipeline's existing override as the default if present, otherwise search the texture graph output settings
			EMetaHumanBuildTextureResolution DefaultResolution = EMetaHumanBuildTextureResolution::Res2048;
			if (const EMetaHumanBuildTextureResolution* Existing = BakingOptions->TextureResolutionsOverrides.Find(OutputTexture.OutputTextureName))
			{
				DefaultResolution = *Existing;
			}
			else if (IsValid(TextureGraph.TextureGraphInstance) && ensure(IsValid(TextureGraph.TextureGraphInstance->Graph())))
			{
				for (const TPair<FTG_Id, FTG_OutputSettings>& OutputNode : TextureGraph.TextureGraphInstance->OutputSettingsMap)
				{
					const int32 PinIndex = 3;
					const FTG_Id PinId(OutputNode.Key.NodeIdx(), PinIndex);
					const FName OutputParamName = TextureGraph.TextureGraphInstance->Graph()->GetParamName(PinId);

					if (OutputParamName == OutputTexture.OutputTextureNameInGraph)
					{
						const FTG_OutputSettings& OutputSettings = OutputNode.Value;
						if (OutputSettings.Width == OutputSettings.Height)
						{
							const int32 OutputResolution = static_cast<int32>(OutputSettings.Width);
							if (static_cast<int32>(EMetaHumanBuildTextureResolution::Res256) <= OutputResolution
								&& OutputResolution <= static_cast<int32>(EMetaHumanBuildTextureResolution::Res8192))
							{
								DefaultResolution = static_cast<EMetaHumanBuildTextureResolution>(OutputResolution);
							}
						}
						break;
					}
				}
			}
			DefaultResolutions.Add(OutputTexture.OutputTextureName, DefaultResolution);
		}
	}

	const UEnum* ResolutionEnum = StaticEnum<EMetaHumanBuildTextureResolution>();

	// Capture the pipeline as a weak pointer for the lambdas
	TWeakObjectPtr<UMetaHumanDefaultEditorPipelineBase> WeakPipeline = Pipeline;

	// Build ordered list of categories from the BakingSettings asset, with a fallback for uncategorized textures
	TArray<FName> OrderedCategories = BakingSettings->TextureCategories;
	for (const TPair<FName, TArray<const FMetaHumanOutputTextureProperties*>>& Pair : CategorizedTextures)
	{
		if (!OrderedCategories.Contains(Pair.Key))
		{
			OrderedCategories.Add(Pair.Key);
		}
	}

	for (const FName& CategoryName : OrderedCategories)
	{
		const TArray<const FMetaHumanOutputTextureProperties*>* Textures = CategorizedTextures.Find(CategoryName);
		if (Textures == nullptr || Textures->IsEmpty())
		{
			continue;
		}

		const FString TabPrefix = (Tab == EBakingTab::Head) ? TEXT("Face_") : TEXT("Body_");
		const FName GroupName = FName(TabPrefix + CategoryName.ToString());

		const bool bStartExpanded = false;
		IDetailGroup& Group = InChildBuilder.AddGroup(GroupName, FText::FromName(CategoryName), bStartExpanded);
		Group.HeaderRow()
			.Visibility(MakeAttributeLambda(MakeVisibility))
			.NameContent()
			[
				SNew(STextBlock)
					.Text(FText::FromName(CategoryName))
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
			];

		for (const FMetaHumanOutputTextureProperties* OutputTexture : *Textures)
		{
			const FName TextureName = OutputTexture->OutputTextureName;
			EBakingTab CapturedTab = Tab;

			Group.AddWidgetRow()
				.Visibility(MakeAttributeLambda(MakeVisibility))
				.OverrideResetToDefault(FResetToDefaultOverride::Create(
					TAttribute<bool>::CreateLambda([WeakPipeline, CapturedTab, TextureName, DefaultResolution = DefaultResolutions.FindRef(TextureName)]() -> bool
					{
						if (const UMetaHumanDefaultEditorPipelineBase* PipelinePtr = WeakPipeline.Get())
						{
							const FMetaHumanMaterialBakingOptions& Options = (CapturedTab == EBakingTab::Head)
								? PipelinePtr->MaterialBakingOptions.FaceBakingOptions
								: PipelinePtr->MaterialBakingOptions.BodyBakingOptions;

							if (const EMetaHumanBuildTextureResolution* Current = Options.TextureResolutionsOverrides.Find(TextureName))
							{
								return *Current != DefaultResolution;
							}
						}
						return false;
					}),
					FSimpleDelegate::CreateLambda([WeakPipeline, CapturedTab, TextureName]()
					{
						if (UMetaHumanDefaultEditorPipelineBase* PipelinePtr = WeakPipeline.Get())
						{
							const UMetaHumanDefaultEditorPipelineBase* CDO = GetDefault<UMetaHumanDefaultEditorPipelineBase>(PipelinePtr->GetClass());

							FMetaHumanMaterialBakingOptions& Options = (CapturedTab == EBakingTab::Head)
								? PipelinePtr->MaterialBakingOptions.FaceBakingOptions
								: PipelinePtr->MaterialBakingOptions.BodyBakingOptions;

							const FMetaHumanMaterialBakingOptions& CDOOptions = (CapturedTab == EBakingTab::Head)
								? CDO->MaterialBakingOptions.FaceBakingOptions
								: CDO->MaterialBakingOptions.BodyBakingOptions;

							FScopedTransaction Transaction(LOCTEXT("ResetResolution", "Reset Texture Resolution"));
							PipelinePtr->Modify();

							if (const EMetaHumanBuildTextureResolution* DefaultValue = CDOOptions.TextureResolutionsOverrides.Find(TextureName))
							{
								Options.TextureResolutionsOverrides.Add(TextureName, *DefaultValue);
							}
							else
							{
								Options.TextureResolutionsOverrides.Remove(TextureName);
							}
						}
					})))
				.NameContent()
				[
					SNew(STextBlock)
						.Text(FText::FromName(TextureName))
						.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.ValueContent()
				[
					SNew(SEnumComboBox, ResolutionEnum)
					.CurrentValue_Lambda([WeakPipeline, CapturedTab, TextureName, DefaultResolution = DefaultResolutions.FindRef(TextureName)]() -> int32
					{
						if (UMetaHumanDefaultEditorPipelineBase* PipelinePtr = WeakPipeline.Get())
						{
							const FMetaHumanMaterialBakingOptions& Options = (CapturedTab == EBakingTab::Head)
								? PipelinePtr->MaterialBakingOptions.FaceBakingOptions
								: PipelinePtr->MaterialBakingOptions.BodyBakingOptions;

							if (const EMetaHumanBuildTextureResolution* Found = Options.TextureResolutionsOverrides.Find(TextureName))
							{
								return static_cast<int32>(*Found);
							}
						}
						return static_cast<int32>(DefaultResolution);
					})
					.OnEnumSelectionChanged_Lambda([WeakPipeline, CapturedTab, TextureName](int32 NewValue, ESelectInfo::Type)
					{
						if (UMetaHumanDefaultEditorPipelineBase* PipelinePtr = WeakPipeline.Get())
						{
							FMetaHumanMaterialBakingOptions& Options = (CapturedTab == EBakingTab::Head)
								? PipelinePtr->MaterialBakingOptions.FaceBakingOptions
								: PipelinePtr->MaterialBakingOptions.BodyBakingOptions;

							FScopedTransaction Transaction(LOCTEXT("ChangeResolution", "Change Texture Resolution"));
							PipelinePtr->Modify();

							Options.TextureResolutionsOverrides.Add(TextureName, static_cast<EMetaHumanBuildTextureResolution>(NewValue));
						}
					})
				];
		}
	}
}

#undef LOCTEXT_NAMESPACE
