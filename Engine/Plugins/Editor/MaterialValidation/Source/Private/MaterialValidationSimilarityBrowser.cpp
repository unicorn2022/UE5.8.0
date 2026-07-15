// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialValidationSimilarityBrowser.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "AssetThumbnail.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DiffTool/Widgets/SMaterialDiff.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailCustomization.h"
#include "IDetailsView.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialValidationGroup.h"
#include "MaterialValidationLibrary.h"
#include "MaterialValidationLibraryTypes.h"
#include "MaterialValidationToolkitShared.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "UObject/GCObject.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "MaterialValidationSimilarityBrowser"

namespace MaterialValidation {

/** Recursively walks the asset registry referencer graph to collect all UMaterialInstanceConstant assets that are descendants of InParentPath. */
static void CollectChildren(FSoftObjectPath const& InParentPath, TSet<FSoftObjectPath>& OutPaths)
{
	IAssetRegistry* const Registry = IAssetRegistry::Get();
	if (!Registry)
	{
		return;
	}

	TArray<FAssetIdentifier> Referencers;
	Registry->GetReferencers(
		FAssetIdentifier(FName(*InParentPath.GetLongPackageName())),
		Referencers,
		UE::AssetRegistry::EDependencyCategory::Package);

	for (FAssetIdentifier const& RefId : Referencers)
	{
		TArray<FAssetData> Assets;
		Registry->GetAssetsByPackageName(RefId.PackageName, Assets);

		for (FAssetData const& AssetData : Assets)
		{
			if (AssetData.AssetClassPath != UMaterialInstanceConstant::StaticClass()->GetClassPathName())
			{
				continue;
			}

			bool bAlreadyInSet = false;
			OutPaths.Add(AssetData.GetSoftObjectPath(), &bAlreadyInSet);
			if (!bAlreadyInSet)
			{
				CollectChildren(AssetData.GetSoftObjectPath(), OutPaths);
			}
		}
	}
}

/**
 * Customizes the CurrentInstance property to show only material instances that belong to
 * the selected base material's hierarchy. Hierarchy membership is resolved lazily from
 * the asset registry whenever BaseMaterial changes.
 */
class FSimilarityBrowserSettingsCustomization : public IDetailCustomization
{
	/** The settings object whose CurrentInstance property is being customized. */
	TWeakObjectPtr<USimilarityBrowserSettings> WeakSettings;
	/** The base material path at the time CachedMICPaths was last built. */
	FSoftObjectPath BaseMaterialPath;
	/** All MIC asset paths that are descendants of BaseMaterialPath. */
	TSet<FSoftObjectPath> CachedMICPaths;

public:
	explicit FSimilarityBrowserSettingsCustomization(TWeakObjectPtr<USimilarityBrowserSettings> InSettings)
		: WeakSettings(InSettings)
	{}

	static TSharedRef<IDetailCustomization> MakeInstance(TWeakObjectPtr<USimilarityBrowserSettings> InSettings)
	{
		return MakeShared<FSimilarityBrowserSettingsCustomization>(InSettings);
	}

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
	{
		TSharedRef<IPropertyHandle> BaseMaterialHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USimilarityBrowserSettings, BaseMaterial));
		TSharedRef<IPropertyHandle> InstanceHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USimilarityBrowserSettings, CurrentInstance));

		IDetailCategoryBuilder& SelectionCat = DetailBuilder.EditCategory("Selection");
		SelectionCat.AddProperty(BaseMaterialHandle);
		SelectionCat.AddProperty(InstanceHandle).CustomWidget()
			.NameContent()
			[
				InstanceHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UMaterialInstanceConstant::StaticClass())
				.PropertyHandle(InstanceHandle)
				.ThumbnailPool(DetailBuilder.GetThumbnailPool())
				.OnShouldFilterAsset_Lambda([this](FAssetData const& AssetData) -> bool
				{
					return ShouldFilterAsset(AssetData);
				})
			];
	}

private:
	bool ShouldFilterAsset(FAssetData const& InAssetData)
	{
		USimilarityBrowserSettings const* Settings = WeakSettings.Get();
		const FSoftObjectPath CurrentBase = Settings ? Settings->BaseMaterial.ToSoftObjectPath() : FSoftObjectPath();

		if (CurrentBase != BaseMaterialPath)
		{
			RebuildCache(CurrentBase);
		}

		// No base material selected — show all instances unfiltered.
		if (CachedMICPaths.IsEmpty())
		{
			return false;
		}

		return !CachedMICPaths.Contains(InAssetData.GetSoftObjectPath());
	}

	/** Rebuilds CachedMICPaths for the given base material path. */
	void RebuildCache(FSoftObjectPath const& InBaseMaterialPath)
	{
		BaseMaterialPath = InBaseMaterialPath;
		CachedMICPaths.Reset();

		if (!BaseMaterialPath.IsNull())
		{
			CollectChildren(BaseMaterialPath, CachedMICPaths);
		}
	}
};

/** Returns the localized display name for a property category. */
static FText GetCategoryDisplayName(EMaterialPropertyCategory InCategory)
{
	switch (InCategory)
	{
	case EMaterialPropertyCategory::Default: return LOCTEXT("Cat_Default", "Default");
	case EMaterialPropertyCategory::StaticProperty: return LOCTEXT("Cat_Static", "Static Property");
	case EMaterialPropertyCategory::UsageFlag: return LOCTEXT("Cat_Usage", "Usage Flag");
	case EMaterialPropertyCategory::StaticSwitch: return LOCTEXT("Cat_Switch", "Static Switch");
	case EMaterialPropertyCategory::ComponentMask: return LOCTEXT("Cat_Mask", "Component Mask");
	default: return FText::GetEmpty();
	}
}

/** One row in the similarity list: a comparand instance and its distance from the current instance. */
struct FSimilarityRow
{
	/** Asset path of the comparand (base material at index 0, instances at 1+). */
	FSoftObjectPath InstancePath;
	/** Weighted distance (sum of category weights for each differing property). */
	float Distance = 0.f;
	/** Per-property diffs. OldValue = comparand value, NewValue = current instance value. */
	TArray<FMaterialInstancePropertyDiff> Diffs;
	/** Raw count of differing properties per category (unweighted). */
	int32 CategoryCounts[(int32)EMaterialPropertyCategory::Count] = {};
	/** Hash of the full property-value array; used by bShowUniqueOnly to detect duplicate permutations. */
	uint32 ValuesHash = 0;

	/** Returns the header column ID for the given property category. */
	static FName GetColumnIdForCategory(EMaterialPropertyCategory InCategory)
	{
		switch (InCategory)
		{
		case EMaterialPropertyCategory::Default: return ColumnId_Cat_Default;
		case EMaterialPropertyCategory::StaticProperty: return ColumnId_Cat_StaticProp;
		case EMaterialPropertyCategory::UsageFlag: return ColumnId_Cat_UsageFlag;
		case EMaterialPropertyCategory::StaticSwitch: return ColumnId_Cat_StaticSwitch;
		case EMaterialPropertyCategory::ComponentMask: return ColumnId_Cat_ComponentMask;
		default: return NAME_None;
		}
	}

	// Column Ids.
	static const FName ColumnId_Thumbnail;
	static const FName ColumnId_AssetPath;
	static const FName ColumnId_Distance;
	static const FName ColumnId_Cat_Default;
	static const FName ColumnId_Cat_StaticProp;
	static const FName ColumnId_Cat_UsageFlag;
	static const FName ColumnId_Cat_StaticSwitch;
	static const FName ColumnId_Cat_ComponentMask;
};

const FName FSimilarityRow::ColumnId_Thumbnail("CID_Sim_Thumbnail");
const FName FSimilarityRow::ColumnId_AssetPath("CID_Sim_AssetPath");
const FName FSimilarityRow::ColumnId_Distance("CID_Sim_Distance");
const FName FSimilarityRow::ColumnId_Cat_Default("CID_Sim_Cat_Default");
const FName FSimilarityRow::ColumnId_Cat_StaticProp("CID_Sim_Cat_StaticProp");
const FName FSimilarityRow::ColumnId_Cat_UsageFlag("CID_Sim_Cat_UsageFlag");
const FName FSimilarityRow::ColumnId_Cat_StaticSwitch("CID_Sim_Cat_StaticSwitch");
const FName FSimilarityRow::ColumnId_Cat_ComponentMask("CID_Sim_Cat_ComponentMask");

/** One row in the diff detail panel. */
struct FDiffDetailRow
{
	/** Name of the property that differs. */
	FName PropertyName;
	/** Category of the property. */
	EMaterialPropertyCategory Category = EMaterialPropertyCategory::Default;
	/** String value on the current instance. */
	FString CurrentValue;
	/** String value on the comparand instance. */
	FString ComparandValue;

	// Column Ids.
	static const FName ColumnId_Property;
	static const FName ColumnId_Category;
	static const FName ColumnId_CurrentValue;
	static const FName ColumnId_ComparandValue;
	static const FName ColumnId_Exclude;
};

const FName FDiffDetailRow::ColumnId_Property("CID_Diff_Property");
const FName FDiffDetailRow::ColumnId_Category("CID_Diff_Category");
const FName FDiffDetailRow::ColumnId_CurrentValue("CID_Diff_CurrentValue");
const FName FDiffDetailRow::ColumnId_ComparandValue("CID_Diff_ComparandValue");
const FName FDiffDetailRow::ColumnId_Exclude("CID_Diff_Exclude");

/** One row in the excluded-properties table. */
struct FExclusionRow
{
	/** Name of the excluded property. */
	FName PropertyName;
	/** Category of the excluded property. */
	EMaterialPropertyCategory Category = EMaterialPropertyCategory::Default;

	// Column Ids.
	static const FName ColumnId_Property;
	static const FName ColumnId_Category;
	static const FName ColumnId_Remove;
};

const FName FExclusionRow::ColumnId_Property("CID_Excl_Property");
const FName FExclusionRow::ColumnId_Category("CID_Excl_Category");
const FName FExclusionRow::ColumnId_Remove("CID_Excl_Remove");

/** Table row widget for one entry in the similarity list. */
class SSimilarityTableRow : public SMultiColumnTableRow<TSharedPtr<FSimilarityRow>>
{
	/** The row data being displayed. */
	TSharedPtr<FSimilarityRow> Item;
	/** Pool used to render the asset thumbnail. */
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;

public:
	SLATE_BEGIN_ARGS(SSimilarityTableRow) {}
		SLATE_ARGUMENT(TSharedPtr<FSimilarityRow>, Item)
		SLATE_ARGUMENT(TSharedPtr<FAssetThumbnailPool>, ThumbnailPool)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable)
	{
		Item = InArgs._Item;
		ThumbnailPool = InArgs._ThumbnailPool;
		SMultiColumnTableRow<TSharedPtr<FSimilarityRow>>::Construct(FSuperRowType::FArguments(), InOwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (!Item.IsValid())
		{
			return SNew(STextBlock).Text(FText::GetEmpty());
		}

		if (ColumnName == FSimilarityRow::ColumnId_Thumbnail)
		{
			const FAssetData AssetData = IAssetRegistry::Get()->GetAssetByObjectPath(Item->InstancePath);
			if (AssetData.IsValid() && ThumbnailPool.IsValid())
			{
				TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(AssetData, 32, 32, ThumbnailPool);
				FAssetThumbnailConfig ThumbnailConfig;
				return SNew(SBox).WidthOverride(32.f).HeightOverride(32.f)
				[
					Thumbnail->MakeThumbnailWidget(ThumbnailConfig)
				];
			}
			return SNew(SBox).WidthOverride(32.f).HeightOverride(32.f);
		}
		if (ColumnName == FSimilarityRow::ColumnId_AssetPath)
		{
			return GenerateAssetPathWidget(Item->InstancePath, FSlateColor::UseForeground());
		}
		if (ColumnName == FSimilarityRow::ColumnId_Distance)
		{
			FNumberFormattingOptions Options;
			Options.MinimumFractionalDigits = 0;
			Options.MaximumFractionalDigits = 2;
			return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::AsNumber(Item->Distance, &Options))
			];
		}
		// Per-category count columns
		for (int32 Category = 0; Category < (int32)EMaterialPropertyCategory::Count; ++Category)
		{
			if (ColumnName == FSimilarityRow::GetColumnIdForCategory((EMaterialPropertyCategory)Category))
			{
				const int32 Count = Item->CategoryCounts[Category];
				return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Count > 0 ? FText::AsNumber(Count) : FText::GetEmpty())
				];
			}
		}
		return SNew(STextBlock).Text(FText::GetEmpty());
	}
};

DECLARE_DELEGATE_TwoParams(FOnExcludeProperty, FName, EMaterialPropertyCategory);

/** Table row widget for one entry in the diff detail panel. */
class SDiffDetailTableRow : public SMultiColumnTableRow<TSharedPtr<FDiffDetailRow>>
{
	/** The row data being displayed. */
	TSharedPtr<FDiffDetailRow> Item;
	/** Fired when the user clicks the Exclude button for this row. */
	FOnExcludeProperty OnExclude;

public:
	SLATE_BEGIN_ARGS(SDiffDetailTableRow) {}
		SLATE_ARGUMENT(TSharedPtr<FDiffDetailRow>, Item)
		SLATE_EVENT(FOnExcludeProperty, OnExclude)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable)
	{
		Item = InArgs._Item;
		OnExclude = InArgs._OnExclude;
		SMultiColumnTableRow<TSharedPtr<FDiffDetailRow>>::Construct(FSuperRowType::FArguments(), InOwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (!Item.IsValid())
		{
			return SNullWidget::NullWidget;
		}
		if (ColumnName == FDiffDetailRow::ColumnId_Property)
		{
			return SNew(SBox).HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(4.f, 0.f)
			[
				SNew(STextBlock).Text(FText::FromName(Item->PropertyName))
			];
		}
		if (ColumnName == FDiffDetailRow::ColumnId_Category)
		{
			return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(GetCategoryDisplayName(Item->Category))
			];
		}
		if (ColumnName == FDiffDetailRow::ColumnId_CurrentValue)
		{
			return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(Item->CurrentValue))
			];
		}
		if (ColumnName == FDiffDetailRow::ColumnId_ComparandValue)
		{
			return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(Item->ComparandValue))
			];
		}
		if (ColumnName == FDiffDetailRow::ColumnId_Exclude)
		{
			const FName PropertyName = Item->PropertyName;
			const EMaterialPropertyCategory Category = Item->Category;
			return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(4.f)
				.ToolTipText(LOCTEXT("ExcludeTooltip", "Exclude this property from the similarity calculation."))
				.OnClicked_Lambda([this, PropertyName, Category]()
				{
					OnExclude.ExecuteIfBound(PropertyName, Category);
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Kismet.Interfaces.Remove"))
				]
			];
		}
		return SNullWidget::NullWidget;
	}
};

DECLARE_DELEGATE_OneParam(FOnRemoveExclusion, FName);

/** Table row widget for one entry in the excluded-properties panel. */
class SExclusionTableRow : public SMultiColumnTableRow<TSharedPtr<FExclusionRow>>
{
	/** The row data being displayed. */
	TSharedPtr<FExclusionRow> Item;
	/** Fired when the user clicks the Remove button for this row. */
	FOnRemoveExclusion OnRemove;

public:
	SLATE_BEGIN_ARGS(SExclusionTableRow) {}
		SLATE_ARGUMENT(TSharedPtr<FExclusionRow>, Item)
		SLATE_EVENT(FOnRemoveExclusion, OnRemove)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable)
	{
		Item = InArgs._Item;
		OnRemove = InArgs._OnRemove;
		SMultiColumnTableRow<TSharedPtr<FExclusionRow>>::Construct(FSuperRowType::FArguments(), InOwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (!Item.IsValid())
		{
			return SNullWidget::NullWidget;
		}
		if (ColumnName == FExclusionRow::ColumnId_Property)
		{
			return SNew(SBox).HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(4.f, 0.f)
			[
				SNew(STextBlock).Text(FText::FromName(Item->PropertyName))
			];
		}
		if (ColumnName == FExclusionRow::ColumnId_Category)
		{
			return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(GetCategoryDisplayName(Item->Category))
			];
		}
		if (ColumnName == FExclusionRow::ColumnId_Remove)
		{
			const FName PropertyName = Item->PropertyName;
			return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(4.f)
				.ToolTipText(LOCTEXT("RemoveExcludeTooltip", "Re-include this property in the similarity distance calculation."))
				.OnClicked_Lambda([this, PropertyName]()
				{
					OnRemove.ExecuteIfBound(PropertyName);
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Delete"))
				]
			];
		}
		return SNullWidget::NullWidget;
	}
};

/**
 * Compare the live instance against every entry in the hierarchy and return sorted similarity rows.
 * Excluded properties act as hard filters: if a comparand differs on any excluded property, the entire row is omitted (rather than just skipping that property's contribution to Distance).
 * The self-path skip prevents comparing the current instance against its own stale cached entry.
 */
static TArray<TSharedPtr<FSimilarityRow>> ComputeSimilarity(
	UMaterialValidationGroup const* InGroup,
	TMap<FName, EMaterialPropertyCategory> const& InExcludedProperties,
	USimilarityBrowserSettings const* InSettings)
{
	TArray<TSharedPtr<FSimilarityRow>> Rows;

	if (!InGroup || !InSettings || !InSettings->CurrentInstance.Get())
	{
		return Rows;
	}

	const FSoftObjectPath BaseMaterialPath = InSettings->BaseMaterial.ToSoftObjectPath();
	UMaterialInstanceConstant* const CurrentInstance = InSettings->CurrentInstance.Get();

	FMaterialDatabaseAssetHierarchyInfo HierarchyInfo;
	UMaterialValidationLibrary::GetMaterialHierarchyInfo(InGroup, BaseMaterialPath, HierarchyInfo);

	if (HierarchyInfo.MaterialPaths.IsEmpty())
	{
		return Rows;
	}

	TArray<FMaterialDatabaseAssetPropertyDesc> Properties;
	UMaterialValidationLibrary::GetMaterialProperties(HierarchyInfo, Properties);

	TArray<FMaterialDatabaseAssetPropertyValue> CurrentValues;
	UMaterialValidationLibrary::GetMaterialPropertyValues(HierarchyInfo, CurrentInstance, CurrentValues);

	// Hash helper: combines all value strings into a single uint32 for duplicate-permutation detection.
	auto HashValues = [](TArray<FMaterialDatabaseAssetPropertyValue> const& InValues) -> uint32
	{
		uint32 Hash = 0;
		for (FMaterialDatabaseAssetPropertyValue const& V : InValues)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(V.Value));
		}
		return Hash;
	};

	const uint32 CurrentHash = HashValues(CurrentValues);

	FSoftObjectPath const SelfPath(CurrentInstance);

	for (int32 i = 0; i < HierarchyInfo.MaterialPaths.Num(); ++i)
	{
		// Skip self (the current instance may already be cached in the hierarchy with stale data).
		if (HierarchyInfo.MaterialPaths[i] == SelfPath)
		{
			continue;
		}

		TArray<FMaterialDatabaseAssetPropertyValue> OtherValues;
		UMaterialValidationLibrary::GetMaterialPropertyValues(HierarchyInfo, i, OtherValues);

		TSharedPtr<FSimilarityRow> Row = MakeShared<FSimilarityRow>();
		Row->InstancePath = HierarchyInfo.MaterialPaths[i];
		Row->ValuesHash = HashValues(OtherValues);

		const int32 NumProperties = FMath::Min(Properties.Num(), FMath::Min(CurrentValues.Num(), OtherValues.Num()));
		bool bFilteredByExclusion = false;
		for (int32 j = 0; j < NumProperties; ++j)
		{
			if (InExcludedProperties.Contains(Properties[j].Name))
			{
				if (CurrentValues[j].Value != OtherValues[j].Value)
				{
					bFilteredByExclusion = true;
					break;
				}
				continue;
			}
			if (CurrentValues[j].Value != OtherValues[j].Value)
			{
				const EMaterialPropertyCategory Category = Properties[j].Category;
				Row->Distance += InSettings->GetWeightForCategory(Category);
				++Row->CategoryCounts[(int32)Category];
				Row->Diffs.Add({
					.PropertyName = Properties[j].Name,
					.Category = Category,
					.OldValue = OtherValues[j].Value, // comparand value
					.NewValue = CurrentValues[j].Value // current instance value
				});
			}
		}

		if (!bFilteredByExclusion
			&& (!InSettings->bUseMaxDistance || Row->Distance <= InSettings->MaxDistance))
		{
			Rows.Add(MoveTemp(Row));
		}
	}

	Rows.Sort([](TSharedPtr<FSimilarityRow> const& A, TSharedPtr<FSimilarityRow> const& B)
	{
		return A->Distance < B->Distance;
	});

	// Remove duplicate permutations: keep only the closest (lowest-distance) representative per unique
	// property hash, and drop any row whose hash matches the current instance (same permutation = no gain).
	if (InSettings->bShowUniqueOnly)
	{
		TSet<uint32> SeenHashes;
		SeenHashes.Add(CurrentHash); // seed: same permutation as current instance is always filtered out

		Rows = Rows.FilterByPredicate([&SeenHashes](TSharedPtr<FSimilarityRow> const& Row)
		{
			bool bAlreadySeen = false;
			SeenHashes.Add(Row->ValuesHash, &bAlreadySeen);
			return !bAlreadySeen;
		});
	}

	return Rows;
}

/** Main widget for the similarity browser; owns all state and layout. */
class SSimilarityBrowserWidget : public SCompoundWidget, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SSimilarityBrowserWidget) {}
		SLATE_ARGUMENT(UMaterialValidationGroup const*, Group)
		SLATE_ARGUMENT(FSoftObjectPath, BaseMaterialPath)
		SLATE_ARGUMENT(UMaterialInstanceConstant*, CurrentInstance)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Group = InArgs._Group;

		ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);
		Settings = NewObject<USimilarityBrowserSettings>(GetTransientPackage());
		Settings->BaseMaterial = TSoftObjectPtr<UMaterial>(InArgs._BaseMaterialPath);
		Settings->CurrentInstance = InArgs._CurrentInstance;

		// Create details view
		{
			FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
			FDetailsViewArgs Args;
			Args.bHideSelectionTip = true;
			Args.bAllowSearch = false;
			DetailsView = PropertyEditor.CreateDetailView(Args);
			DetailsView->RegisterInstancedCustomPropertyLayout(
				USimilarityBrowserSettings::StaticClass(),
				FOnGetDetailCustomizationInstance::CreateSP(
					this, &SSimilarityBrowserWidget::CreateSettingsCustomization));
			DetailsView->SetObject(Settings);
			OnObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(
				this, &SSimilarityBrowserWidget::HandleSettingsPropertyChanged);
		}

		RecomputeSimilarity();

		// Refresh when the live instance is modified
		OnObjectModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddSP(
			SharedThis(this),
			&SSimilarityBrowserWidget::HandleObjectModified);

		ChildSlot
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			// Left: details panel
			+ SSplitter::Slot()
			.Value(0.25f)
			[
				DetailsView.ToSharedRef()
			]

			// Right: main content
			+ SSplitter::Slot()
			.Value(0.75f)
			[
				SNew(SSplitter)
				.Orientation(Orient_Vertical)

				// Similarity list
				+ SSplitter::Slot()
				.Value(0.5f)
				[
					SAssignNew(SimilarityListView, SListView<TSharedPtr<FSimilarityRow>>)
				.HeaderRow(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(FSimilarityRow::ColumnId_Thumbnail)
						.DefaultLabel(FText::GetEmpty())
						.FixedWidth(36.f)
					+ SHeaderRow::Column(FSimilarityRow::ColumnId_AssetPath)
						.HAlignHeader(HAlign_Center).HAlignCell(HAlign_Left)
						.DefaultLabel(LOCTEXT("ColAssetPath", "Asset Path"))
						.ManualWidth_Lambda([]() { return GAssetNameColumnWidth; })
						.OnWidthChanged_Lambda([](float W) { GAssetNameColumnWidth = W; })
					+ SHeaderRow::Column(FSimilarityRow::ColumnId_Distance)
						.HAlignHeader(HAlign_Center).HAlignCell(HAlign_Center)
						.DefaultLabel(LOCTEXT("ColDistance", "Distance"))
						.FixedWidth(80.f)
					+ SHeaderRow::Column(FSimilarityRow::ColumnId_Cat_Default)
						.HAlignHeader(HAlign_Center).HAlignCell(HAlign_Center)
						.DefaultLabel(LOCTEXT("Cat_Default", "Default"))
						.FixedWidth(70.f)
					+ SHeaderRow::Column(FSimilarityRow::ColumnId_Cat_StaticProp)
						.HAlignHeader(HAlign_Center).HAlignCell(HAlign_Center)
						.DefaultLabel(LOCTEXT("Cat_Static", "Static Property"))
						.FixedWidth(100.f)
					+ SHeaderRow::Column(FSimilarityRow::ColumnId_Cat_UsageFlag)
						.HAlignHeader(HAlign_Center).HAlignCell(HAlign_Center)
						.DefaultLabel(LOCTEXT("Cat_Usage", "Usage Flag"))
						.FixedWidth(80.f)
					+ SHeaderRow::Column(FSimilarityRow::ColumnId_Cat_StaticSwitch)
						.HAlignHeader(HAlign_Center).HAlignCell(HAlign_Center)
						.DefaultLabel(LOCTEXT("Cat_Switch", "Static Switch"))
						.FixedWidth(90.f)
					+ SHeaderRow::Column(FSimilarityRow::ColumnId_Cat_ComponentMask)
						.HAlignHeader(HAlign_Center).HAlignCell(HAlign_Center)
						.DefaultLabel(LOCTEXT("Cat_Mask", "Component Mask"))
						.FixedWidth(110.f)
				)
				.ListItemsSource(&SimilarityRows)
				.OnGenerateRow_Lambda([this](TSharedPtr<FSimilarityRow> Item, const TSharedRef<STableViewBase>& Owner)
				{
					return SNew(SSimilarityTableRow, Owner)
						.Item(Item)
						.ThumbnailPool(ThumbnailPool);
				})
				.SelectionMode(ESelectionMode::Single)
				.OnSelectionChanged_Lambda([this](TSharedPtr<FSimilarityRow> Item, ESelectInfo::Type)
				{
					PopulateDiffPanel(Item);
				})
			]

				// Bottom panel: differences (left) + exclusions (right)
				+ SSplitter::Slot()
				.Value(0.5f)
				[
					SNew(SSplitter)
					.Orientation(Orient_Horizontal)

					// Differences list
					+ SSplitter::Slot()
					.Value(0.7f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4.f, 2.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0.f, 0.f, 4.f, 0.f)
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "SimpleButton")
								.ContentPadding(4.f)
								.ToolTipText(LOCTEXT("OpenDiffTooltip", "Open the material diff window for these two instances."))
								.Visibility_Lambda([this]() -> EVisibility
								{
									return SelectedSimilarityRow.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
								})
								.OnClicked_Lambda([this]()
								{
									if (SelectedSimilarityRow.IsValid())
									{
										UMaterialInstance* const Comparand = Cast<UMaterialInstance>(SelectedSimilarityRow->InstancePath.TryLoad());
										UMaterialInstance* const Current   = Cast<UMaterialInstance>(Settings->CurrentInstance.Get());
										if (Comparand && Current)
										{
											const FText Title = FText::Format(
												LOCTEXT("DiffWindowTitleFmt", "Diff: {0} vs {1}"),
												FText::FromString(Comparand->GetName()),
												FText::FromString(Current->GetName()));
											SMaterialDiff::CreateDiffWindow(Title, Comparand, Current, FRevisionInfo(), FRevisionInfo());
										}
									}
									return FReply::Handled();
								})
								[
									SNew(SImage)
									.Image(FAppStyle::GetBrush("SourceControl.Actions.Diff"))
								]
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.f)
							.VAlign(VAlign_Center)
							[
								SAssignNew(DiffPanelTitle, STextBlock)
								.Text(LOCTEXT("SelectRowHint", "Select a row above to see per-property differences."))
							]
						]
						+ SVerticalBox::Slot()
						.FillHeight(1.f)
						[
							SAssignNew(DiffDetailListView, SListView<TSharedPtr<FDiffDetailRow>>)
							.HeaderRow(
								SNew(SHeaderRow)
								+ SHeaderRow::Column(FDiffDetailRow::ColumnId_Property)
									.HAlignHeader(HAlign_Left).HAlignCell(HAlign_Left)
									.DefaultLabel(LOCTEXT("ColProperty", "Property"))
									.FixedWidth(200.f)
								+ SHeaderRow::Column(FDiffDetailRow::ColumnId_Category)
									.HAlignHeader(HAlign_Center).HAlignCell(HAlign_Center)
									.DefaultLabel(LOCTEXT("ColCategory", "Category"))
									.FixedWidth(80.f)
								+ SHeaderRow::Column(FDiffDetailRow::ColumnId_CurrentValue)
									.HAlignHeader(HAlign_Center).HAlignCell(HAlign_Center)
									.DefaultLabel(LOCTEXT("ColCurrentValue", "Current"))
									.FixedWidth(120.f)
								+ SHeaderRow::Column(FDiffDetailRow::ColumnId_ComparandValue)
									.HAlignHeader(HAlign_Center).HAlignCell(HAlign_Center)
									.DefaultLabel(LOCTEXT("ColComparandValue", "Comparand"))
									.FixedWidth(120.f)
								+ SHeaderRow::Column(FDiffDetailRow::ColumnId_Exclude)
									.HAlignHeader(HAlign_Center).HAlignCell(HAlign_Center)
									.DefaultLabel(LOCTEXT("ColExclude", "Exclude"))
									.FixedWidth(56.f)
							)
							.ListItemsSource(&DiffDetailRows)
							.OnGenerateRow_Lambda([this](TSharedPtr<FDiffDetailRow> Item, const TSharedRef<STableViewBase>& Owner)
							{
								return SNew(SDiffDetailTableRow, Owner)
									.Item(Item)
									.OnExclude(this, &SSimilarityBrowserWidget::OnExcludeProperty);
							})
							.SelectionMode(ESelectionMode::None)
						]
					]

					// Right panel: excluded properties
					+ SSplitter::Slot()
					.Value(0.3f)
					[
						SNew(SVerticalBox)

						// Title + Clear All
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4.f, 6.f, 4.f, 2.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("ExcludedPropsTitle", "Excluded Properties"))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.Text(LOCTEXT("ClearExclusions", "Clear All"))
								.ToolTipText(LOCTEXT("ClearExclusionsTooltip", "Remove all excluded properties."))
								.Visibility_Lambda([this]() { return ExcludedProperties.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
								.OnClicked_Lambda([this]()
								{
									ExcludedProperties.Empty();
									RebuildExclusionList();
									RecomputeSimilarity();
									return FReply::Handled();
								})
							]
						]
						+ SVerticalBox::Slot()
						.FillHeight(1.f)
						[
							SAssignNew(ExclusionListView, SListView<TSharedPtr<FExclusionRow>>)
							.HeaderRow(
								SNew(SHeaderRow)
								+ SHeaderRow::Column(FExclusionRow::ColumnId_Property)
									.HAlignHeader(HAlign_Left).HAlignCell(HAlign_Left)
									.DefaultLabel(LOCTEXT("ColExclProperty", "Property"))
									.FixedWidth(200.f)
								+ SHeaderRow::Column(FExclusionRow::ColumnId_Category)
									.HAlignHeader(HAlign_Center).HAlignCell(HAlign_Center)
									.DefaultLabel(LOCTEXT("ColExclCategory", "Category"))
									.FixedWidth(120.f)
								+ SHeaderRow::Column(FExclusionRow::ColumnId_Remove)
									.HAlignHeader(HAlign_Center).HAlignCell(HAlign_Center)
									.DefaultLabel(LOCTEXT("ColRemove", "Remove"))
									.FixedWidth(56.f)
							)
							.ListItemsSource(&ExclusionRows)
							.OnGenerateRow_Lambda([this](TSharedPtr<FExclusionRow> Item, const TSharedRef<STableViewBase>& Owner)
							{
								return SNew(SExclusionTableRow, Owner)
									.Item(Item)
									.OnRemove(this, &SSimilarityBrowserWidget::OnRemoveExcludedProperty);
							})
							.SelectionMode(ESelectionMode::None)
						]
					]
				]

			]
		];
	}

	virtual ~SSimilarityBrowserWidget() override
	{
		FCoreUObjectDelegates::OnObjectModified.Remove(OnObjectModifiedHandle);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnObjectPropertyChangedHandle);
	}

	/** Update the browser to compare a new instance. Called when reusing an existing window. */
	void SetComparands(
		UMaterialValidationGroup const* InGroup,
		FSoftObjectPath const& InBaseMaterialPath,
		UMaterialInstanceConstant* InCurrentInstance)
	{
		Group = InGroup;
		Settings->BaseMaterial    = TSoftObjectPtr<UMaterial>(InBaseMaterialPath);
		Settings->CurrentInstance = InCurrentInstance;

		ExcludedProperties.Empty();
		RebuildExclusionList();

		if (DetailsView.IsValid())
		{
			DetailsView->SetObject(Settings);
		}
		RecomputeSimilarity();
	}

private:
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(Settings);
	}
	virtual FString GetReferencerName() const override
	{
		return TEXT("SSimilarityBrowserWidget");
	}

	/** Selects an arbitrary MIC from the new base material hierarchy. */
	void AutoSelectFirstInstance()
	{
		TSet<FSoftObjectPath> Instances;
		CollectChildren(Settings->BaseMaterial.ToSoftObjectPath(), Instances);

		Settings->CurrentInstance = Instances.IsEmpty()
			? nullptr
			: Cast<UMaterialInstanceConstant>(Instances.begin()->TryLoad());
	}

	/** Responds to settings property changes; auto-selects an instance when BaseMaterial changes, then recomputes. */
	void HandleSettingsPropertyChanged(UObject* InObject, FPropertyChangedEvent& Event)
	{
		if (InObject != Settings)
		{
			return;
		}

		if (Event.Property && Event.Property->GetFName() == GET_MEMBER_NAME_CHECKED(USimilarityBrowserSettings, BaseMaterial))
		{
			AutoSelectFirstInstance();
			if (DetailsView.IsValid())
			{
				DetailsView->SetObject(Settings);
			}
		}

		RecomputeSimilarity();
	}

	TSharedRef<IDetailCustomization> CreateSettingsCustomization()
	{
		return FSimilarityBrowserSettingsCustomization::MakeInstance(Settings);
	}

	/** Re-run similarity computation and refresh the list. Clears the diff panel. */
	void RecomputeSimilarity()
	{
		SimilarityRows = ComputeSimilarity(
			Group.Get(),
			ExcludedProperties,
			Settings);

		if (SimilarityListView.IsValid())
		{
			SimilarityListView->RequestListRefresh();
		}
		PopulateDiffPanel(nullptr);
	}

	/** Populate the diff detail panel from the selected similarity row. */
	void PopulateDiffPanel(TSharedPtr<FSimilarityRow> InRow)
	{
		SelectedSimilarityRow = InRow;
		DiffDetailRows.Reset();

		if (InRow.IsValid())
		{
			if (DiffPanelTitle.IsValid())
			{
				DiffPanelTitle->SetText(FText::Format(
					LOCTEXT("DiffPanelTitleFmt", "{0} — {1} difference(s)"),
					FText::FromString(InRow->InstancePath.GetAssetName()),
					FText::AsNumber(InRow->Diffs.Num())));
			}

			for (FMaterialInstancePropertyDiff const& Diff : InRow->Diffs)
			{
				TSharedPtr<FDiffDetailRow> DetailRow = MakeShared<FDiffDetailRow>();
				DetailRow->PropertyName = Diff.PropertyName;
				DetailRow->Category = Diff.Category;
				DetailRow->CurrentValue = Diff.NewValue;   // NewValue = current instance
				DetailRow->ComparandValue = Diff.OldValue; // OldValue = comparand
				DiffDetailRows.Add(DetailRow);
			}
		}
		else if (DiffPanelTitle.IsValid())
		{
			DiffPanelTitle->SetText(LOCTEXT("SelectRowHint", "Select a row above to see per-property differences."));
		}

		if (DiffDetailListView.IsValid())
		{
			DiffDetailListView->RequestListRefresh();
		}
	}

	/** Called when the user clicks "Exclude" on a diff detail row. */
	void OnExcludeProperty(FName InPropertyName, EMaterialPropertyCategory InCategory)
	{
		ExcludedProperties.Add(InPropertyName, InCategory);
		RebuildExclusionList();
		RecomputeSimilarity();
	}

	/** Remove a property from the exclusion set when the user clicks × on a chip. */
	void OnRemoveExcludedProperty(FName InPropertyName)
	{
		ExcludedProperties.Remove(InPropertyName);
		RebuildExclusionList();
		RecomputeSimilarity();
	}

	/** Called when any UObject is modified; recomputes if it's our current instance. */
	void HandleObjectModified(UObject* InObject)
	{
		if (InObject == Settings->CurrentInstance.Get())
		{
			RecomputeSimilarity();
		}
	}

	/** Rebuild the exclusion list from ExcludedProperties. */
	void RebuildExclusionList()
	{
		ExclusionRows.Reset();
		for (auto const& [PropertyName, Category] : ExcludedProperties)
		{
			TSharedPtr<FExclusionRow> Row = MakeShared<FExclusionRow>();
			Row->PropertyName = PropertyName;
			Row->Category = Category;
			ExclusionRows.Add(MoveTemp(Row));
		}
		if (ExclusionListView.IsValid())
		{
			ExclusionListView->RequestListRefresh();
		}
	}

	/** The validation group supplying the material hierarchy. */
	TWeakObjectPtr<const UMaterialValidationGroup> Group;
	/** Pool used to render asset thumbnails in the similarity list. */
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;

	/** Settings object. Left panel is IDetailsView on this. */
	TObjectPtr<USimilarityBrowserSettings> Settings;
	/** Details panel showing the settings object (selection + weights). */
	TSharedPtr<IDetailsView> DetailsView;

	/** Excluded properties mapped to their category (session-scoped). */
	TMap<FName, EMaterialPropertyCategory> ExcludedProperties;

	/** Computed similarity rows, sorted by distance ascending. */
	TArray<TSharedPtr<FSimilarityRow>> SimilarityRows;
	/** Diff detail rows for the selected similarity row. */
	TArray<TSharedPtr<FDiffDetailRow>> DiffDetailRows;

	// Delegate handles
	FDelegateHandle OnObjectModifiedHandle;
	FDelegateHandle OnObjectPropertyChangedHandle;

	/** List view displaying similarity rows sorted by distance. */
	TSharedPtr<SListView<TSharedPtr<FSimilarityRow>>> SimilarityListView;
	/** List view showing per-property diffs for the selected similarity row. */
	TSharedPtr<SListView<TSharedPtr<FDiffDetailRow>>> DiffDetailListView;
	/** List view showing currently excluded properties. */
	TSharedPtr<SListView<TSharedPtr<FExclusionRow>>> ExclusionListView;
	/** Row data backing ExclusionListView. */
	TArray<TSharedPtr<FExclusionRow>> ExclusionRows;
	/** The currently selected row in SimilarityListView. */
	TSharedPtr<FSimilarityRow> SelectedSimilarityRow;
	/** Title text block above the diff detail panel. */
	TSharedPtr<STextBlock> DiffPanelTitle;
};

} // namespace MaterialValidation


float USimilarityBrowserSettings::GetWeightForCategory(EMaterialPropertyCategory InCategory) const
{
	switch (InCategory)
	{
	case EMaterialPropertyCategory::StaticProperty: return StaticPropertyWeight;
	case EMaterialPropertyCategory::UsageFlag: return UsageFlagWeight;
	case EMaterialPropertyCategory::StaticSwitch: return StaticSwitchWeight;
	case EMaterialPropertyCategory::ComponentMask: return ComponentMaskWeight;
	default: return 1.f;
	}
}

void SMaterialInstanceSimilarityBrowser::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SAssignNew(BrowserWidget, MaterialValidation::SSimilarityBrowserWidget)
			.Group(InArgs._Group)
			.BaseMaterialPath(InArgs._BaseMaterialPath)
			.CurrentInstance(InArgs._CurrentInstance)
	];
}

void SMaterialInstanceSimilarityBrowser::NavigateTo(UMaterialValidationGroup const* InGroup, FSoftObjectPath const& InBaseMaterialPath, UMaterialInstanceConstant* InCurrentInstance)
{
	if (BrowserWidget.IsValid())
	{
		BrowserWidget->SetComparands(InGroup, InBaseMaterialPath, InCurrentInstance);
	}
}

TSharedPtr<SWindow> SMaterialInstanceSimilarityBrowser::CreateWindow(UMaterialValidationGroup const* InGroup, FSoftObjectPath const& InBaseMaterialPath, UMaterialInstanceConstant* InCurrentInstance)
{
	// Reuse an existing browser window if one is already open.
	static const FName BrowserWidgetType("SMaterialInstanceSimilarityBrowser");
	for (TSharedRef<SWindow> const& TopLevel : FSlateApplication::Get().GetTopLevelWindows())
	{
		if (TopLevel->GetContent()->GetType() == BrowserWidgetType)
		{
			StaticCastSharedRef<SMaterialInstanceSimilarityBrowser>(TopLevel->GetContent())->NavigateTo(InGroup, InBaseMaterialPath, InCurrentInstance);
			TopLevel->BringToFront();
			return TopLevel;
		}
	}

	TSharedPtr<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "Find Similar Material Instances"))
		.ClientSize(FVector2f(1200.f, 700.f));

	Window->SetContent(
		SNew(SMaterialInstanceSimilarityBrowser)
			.Group(InGroup)
			.BaseMaterialPath(InBaseMaterialPath)
			.CurrentInstance(InCurrentInstance));

	const TSharedPtr<SWindow> ActiveModal = FSlateApplication::Get().GetActiveModalWindow();
	if (ActiveModal.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(Window.ToSharedRef(), ActiveModal.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(Window.ToSharedRef());
	}

	return Window;
}

#undef LOCTEXT_NAMESPACE
