// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorCostumeToolView.h"

#include "Algo/Find.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterEditorWardrobeSettings.h"
#include "MetaHumanInstance.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionEditorPipeline.h"
#include "MetaHumanWardrobeItem.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Tools/MetaHumanCharacterEditorCostumeTools.h"
#include "UI/Widgets/SMetaHumanCharacterEditorToolPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "SWarningOrErrorBox.h"
#include "GroomBindingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "ChaosOutfitAsset/OutfitAsset.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorCostumeToolView"

namespace UE::MetaHuman::Private
{
	template<typename T>
	bool SlotSupportsType(TObjectPtr<UMetaHumanCollection> Collection, const FName& InSlotName)
	{
		if (const UMetaHumanCollectionEditorPipeline* EditorPipeline = Collection->GetEditorPipeline())
		{
			if (const FMetaHumanCharacterPipelineSlotEditorData* SlotEditorEntry = EditorPipeline->GetSpecification()->SlotEditorData.Find(InSlotName))
			{
				return SlotEditorEntry->SupportsAssetType(T::StaticClass());
			}
		}

		return false;
	}
} // UE::MetaHuman::Private

SMetaHumanCharacterEditorCostumeToolView::~SMetaHumanCharacterEditorCostumeToolView()
{
	if (FCoreUObjectDelegates::OnObjectPropertyChanged.IsBoundToObject(this))
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
	}
}

void SMetaHumanCharacterEditorCostumeToolView::Construct(const FArguments& InArgs, UMetaHumanCharacterEditorCostumeTool* InTool)
{
	SMetaHumanCharacterEditorToolView::Construct(SMetaHumanCharacterEditorToolView::FArguments(), InTool);
}

UInteractiveToolPropertySet* SMetaHumanCharacterEditorCostumeToolView::GetToolProperties() const
{
	const UMetaHumanCharacterEditorCostumeTool* CostumeTool = Cast<UMetaHumanCharacterEditorCostumeTool>(Tool);
	return IsValid(CostumeTool) ? CostumeTool->GetCostumeToolProperties() : nullptr;
}

void SMetaHumanCharacterEditorCostumeToolView::MakeToolView()
{
	if (ToolViewScrollBox.IsValid())
	{
		ToolViewScrollBox->ClearChildren();

		ToolViewScrollBox->AddSlot()
			.Padding(0.f, 4.f)
			.VAlign(VAlign_Top)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateCostumeToolViewWarningSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateCostumeToolViewGroomsSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateCostumeToolViewOutfitClothingSection()
				]

				+ SVerticalBox::Slot()
				.Padding(4.f)
				.AutoHeight()
				[
					CreateCostumeToolViewSkeletalMeshSection()
				]
			];
	}

	MakeCostumeItemsBoxes();
}

void SMetaHumanCharacterEditorCostumeToolView::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		Refresh();
	}
}

void SMetaHumanCharacterEditorCostumeToolView::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		Refresh();
	}
}

void SMetaHumanCharacterEditorCostumeToolView::Refresh()
{
	if (UMetaHumanCharacterEditorCostumeTool* CostumeTool = Cast<UMetaHumanCharacterEditorCostumeTool>(Tool))
	{
		CostumeTool->UpdateCostumeItems();

		// Re-apply all instance parameters in case they were touched by undo/redo
		if (UMetaHumanCharacterEditorCostumeToolProperties* CostumeToolProperties = CostumeTool->GetCostumeToolProperties())
		{
			TNotNull<UMetaHumanInstance*> Instance = CostumeToolProperties->Collection->GetMutableDefaultInstance();
			const TMap<FMetaHumanPaletteItemPath, FInstancedPropertyBag>& OverridenParams = Instance->GetOverriddenInstanceParameters();

			for (const TPair<FMetaHumanPaletteItemPath, FInstancedPropertyBag>& ParamIt : OverridenParams)
			{
				Instance->OverrideInstanceParameters(ParamIt.Key, ParamIt.Value);
			}
		}

		MakeToolView();
	}
}

TSharedRef<SWidget> SMetaHumanCharacterEditorCostumeToolView::CreateCostumeToolViewWarningSection()
{
	const TSharedRef<SWidget> WarningSectionWidget =
		SNew(SBox)
		.Padding(4.f)
		[
			SNew(SWarningOrErrorBox)
			.AutoWrapText(true)
			.MessageStyle(EMessageStyle::Warning)
			.Visibility(this, &SMetaHumanCharacterEditorCostumeToolView::GetWarningVisibility)
			.Message(LOCTEXT("CostumeToolViewWarningMessage", "No items available. Please, select items from the Wardrobe to enable Costume editing."))
		];

	return WarningSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorCostumeToolView::CreateCostumeToolViewGroomsSection()
{
	UMetaHumanCharacterEditorCostumeToolProperties* CostumeToolProperties = Cast<UMetaHumanCharacterEditorCostumeToolProperties>(GetToolProperties());
	if (!CostumeToolProperties)
	{
		return SNullWidget::NullWidget;
	}

	const TSharedRef<SWidget> CostumeGroomsSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("CostumeGroomsSectionLabel", "Grooms"))
		.Visibility(this, &SMetaHumanCharacterEditorCostumeToolView::GetGroomsBoxVisibility)
		.Content()
		[
			SAssignNew(GroomsBox, SVerticalBox)
		];

	return CostumeGroomsSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorCostumeToolView::CreateCostumeToolViewOutfitClothingSection()
{
	UMetaHumanCharacterEditorCostumeToolProperties* CostumeToolProperties = Cast<UMetaHumanCharacterEditorCostumeToolProperties>(GetToolProperties());
	if (!CostumeToolProperties)
	{
		return SNullWidget::NullWidget;
	}

	const TSharedRef<SWidget> CostumeOutfitClothingSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("CostumeOutfitClothingSectionLabel", "Outfit Clothing"))
		.Visibility(this, &SMetaHumanCharacterEditorCostumeToolView::GetOutfitClothingBoxVisibility)
		.Content()
		[
			SAssignNew(OutfitClothingBox, SVerticalBox)
		];

	return CostumeOutfitClothingSectionWidget;
}

TSharedRef<SWidget> SMetaHumanCharacterEditorCostumeToolView::CreateCostumeToolViewSkeletalMeshSection()
{
	UMetaHumanCharacterEditorCostumeToolProperties* CostumeToolProperties = Cast<UMetaHumanCharacterEditorCostumeToolProperties>(GetToolProperties());
	if (!CostumeToolProperties)
	{
		return SNullWidget::NullWidget;
	}

	const TSharedRef<SWidget> SkeletalMeshSectionWidget =
		SNew(SMetaHumanCharacterEditorToolPanel)
		.Label(LOCTEXT("CostumeSkelMeshClothingSectionLabel", "Skeletal Mesh"))
		.Visibility(this, &SMetaHumanCharacterEditorCostumeToolView::GetSkeletalMeshBoxVisibility)
		.Content()
		[
			SAssignNew(SkeletalMeshesBox, SVerticalBox)
		];

	return SkeletalMeshSectionWidget;
}

void SMetaHumanCharacterEditorCostumeToolView::MakeCostumeItemsBoxes()
{
	UMetaHumanCharacterEditorCostumeToolProperties* CostumeToolProperties = Cast<UMetaHumanCharacterEditorCostumeToolProperties>(GetToolProperties());
	if (!CostumeToolProperties || !GroomsBox.IsValid() || !OutfitClothingBox.IsValid())
	{
		return;
	}

	TArray<TObjectPtr<UMetaHumanCharacterEditorCostumeItem>>& CostumeItems = CostumeToolProperties->CostumeItems;

	TMap<FName, TArray<UMetaHumanCharacterEditorCostumeItem*>> SlotNameToItemsMap;
	for (TObjectPtr<UMetaHumanCharacterEditorCostumeItem>& Item : CostumeItems)
	{
		SlotNameToItemsMap.FindOrAdd(Item->SlotName).Add(Item);
	}

	const UMetaHumanCharacterEditorWardrobeSettings* WardrobeSettings = GetDefault<UMetaHumanCharacterEditorWardrobeSettings>();
	SlotNameToItemsMap.KeyStableSort([WardrobeSettings](const FName& ItemA, const FName& ItemB)
	{
		if (!WardrobeSettings)
		{
			return ItemA.LexicalLess(ItemB);
		}
		
		const int32 IndexA = WardrobeSettings->WardrobeSections.IndexOfByPredicate(
			[&ItemA](const FMetaHumanCharacterAssetsSection& Section)
			{
					return Section.SlotName == ItemA;
			});
		const int32 IndexB = WardrobeSettings->WardrobeSections.IndexOfByPredicate(
			[&ItemB](const FMetaHumanCharacterAssetsSection& Section)
			{
				return Section.SlotName == ItemB;
			});

		return (IndexA != INDEX_NONE && IndexB != INDEX_NONE) ? IndexA < IndexB : ItemA.LexicalLess(ItemB);
	});

	for (const TPair<FName, TArray<UMetaHumanCharacterEditorCostumeItem*>>& SlotNameToItems : SlotNameToItemsMap)
	{
		const FName& SlotName = SlotNameToItems.Key;
		const TArray<UMetaHumanCharacterEditorCostumeItem*>& Items = SlotNameToItems.Value;
		if (SlotName == UE::MetaHuman::CharacterPipelineSlots::Character)
		{
			continue;
		}

		TSharedPtr<SVerticalBox> ItemsSlotBox = nullptr;
		for (UMetaHumanCharacterEditorCostumeItem* Item : Items)
		{
			if (!Item || !Item->WardrobeItem.IsValid())
			{
				continue;
			}

			FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
			FDetailsViewArgs DetailsViewArgs;
			DetailsViewArgs.bUpdatesFromSelection = false;
			DetailsViewArgs.bLockable = false;
			DetailsViewArgs.bAllowSearch = false;
			DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

			const TSharedRef<IDetailsView> DetailView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
			DetailView->SetObject(Item);

			if (!ItemsSlotBox.IsValid())
			{
				SAssignNew(ItemsSlotBox, SVerticalBox);
			}

			FText AssetName = FText::FromString(Item->WardrobeItem->PrincipalAsset.GetAssetName());
			if (!Item->WardrobeItem->ThumbnailName.IsEmpty())
			{
				AssetName = Item->WardrobeItem->ThumbnailName;
			}

			const FText SlotLabel = FText::Format(LOCTEXT("CostumeSlotLabelText", "{0} ({1})"), AssetName, FText::FromName(SlotName));
			ItemsSlotBox->AddSlot()
				.AutoHeight()
				.Padding(2.f, 4.f)
				[
					SNew(SMetaHumanCharacterEditorToolPanel)
					.Label(SlotLabel)
					.Content()
					[
						SNew(SBox)
						.Padding(6.f, 10.f)
						[
							DetailView
						]
					]
				];
		}

		if (!ItemsSlotBox.IsValid())
		{
			continue;
		}

		if (UE::MetaHuman::Private::SlotSupportsType<UGroomBindingAsset>(CostumeToolProperties->Collection, SlotName))
		{
			GroomsBox->AddSlot()
				.AutoHeight()
				.Padding(4.f, 6.f)
				[
					ItemsSlotBox.ToSharedRef()
				];
		}
		else if (UE::MetaHuman::Private::SlotSupportsType<UChaosOutfitAsset>(CostumeToolProperties->Collection, SlotName))
		{
			OutfitClothingBox->AddSlot()
				.AutoHeight()
				.Padding(4.f, 6.f)
				[
					ItemsSlotBox.ToSharedRef()
				];
		}
		else if (UE::MetaHuman::Private::SlotSupportsType<USkeletalMesh>(CostumeToolProperties->Collection, SlotName))
		{
			SkeletalMeshesBox->AddSlot()
				.AutoHeight()
				.Padding(4.f, 6.f)
				[
					ItemsSlotBox.ToSharedRef()
				];
		}
	}

	if (!FCoreUObjectDelegates::OnObjectPropertyChanged.IsBoundToObject(this))
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SMetaHumanCharacterEditorCostumeToolView::OnObjectPropertyChanged);
	}
}

void SMetaHumanCharacterEditorCostumeToolView::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	UMetaHumanCharacterEditorCostumeItem* Item = Cast<UMetaHumanCharacterEditorCostumeItem>(Object);
	UMetaHumanCharacterEditorCostumeToolProperties* CostumeToolProperties = Cast<UMetaHumanCharacterEditorCostumeToolProperties>(GetToolProperties());
	if (!CostumeToolProperties || !CostumeToolProperties->Collection || !Item || !PropertyChangedEvent.Property)
	{
		return;
	}

	const FStructProperty* InstanceBagProperty = CastField<FStructProperty>(PropertyChangedEvent.MemberProperty);
	FInstancedPropertyBag* SourcePropertyBag = InstanceBagProperty ? InstanceBagProperty->ContainerPtrToValuePtr<FInstancedPropertyBag>(Item) : nullptr;
	const UPropertyBag* BagStruct = SourcePropertyBag ? SourcePropertyBag->GetPropertyBagStruct() : nullptr;
	if (!BagStruct)
	{
		return;
	}

	// Resolve the bag-level entry that contains the edited leaf property.
	// For atomic edits the leaf matches a desc directly. For struct sub-edits
	// (e.g. FLinearColor.R) the leaf is owned by the desc's struct.
	const FProperty* LeafProperty = PropertyChangedEvent.Property;
	const UStruct* LeafOwnerStruct = LeafProperty->GetOwnerStruct();

	const FPropertyBagPropertyDesc* BagEntryDesc = BagStruct->GetPropertyDescs().FindByPredicate(
		[LeafProperty, LeafOwnerStruct](const FPropertyBagPropertyDesc& Desc)
		{
			if (!Desc.CachedProperty)
			{
				return false;
			}
			if (Desc.CachedProperty == LeafProperty)
			{
				return true;
			}
			const FStructProperty* DescStructProp = CastField<FStructProperty>(Desc.CachedProperty);
			return DescStructProp && LeafOwnerStruct == DescStructProp->Struct;
		});

	if (!BagEntryDesc || !BagEntryDesc->CachedProperty)
	{
		return;
	}

	FInstancedPropertyBag InstanceParameterBag;
	const EPropertyBagAlterationResult AddResult = InstanceParameterBag.AddProperty(BagEntryDesc->Name, BagEntryDesc->CachedProperty);
	if (!ensure(AddResult == EPropertyBagAlterationResult::Success))
	{
		return;
	}

	const EPropertyBagResult SetResult = InstanceParameterBag.SetValue(
		BagEntryDesc->Name,
		BagEntryDesc->CachedProperty,
		SourcePropertyBag->GetValue().GetMemory());

	if (!ensure(SetResult == EPropertyBagResult::Success))
	{
		return;
	}

	if (!PropertyChangeTransaction.IsValid())
	{
		PropertyChangeTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("ApplyOverrideInstanceParameter", "Apply Parameter"));
	}

	TNotNull<UMetaHumanInstance*> Instance = CostumeToolProperties->Collection->GetMutableDefaultInstance();
	Instance->Modify();
	const EMetaHumanInstanceParameterOverrideResult OverrideResult = Instance->OverrideInstanceParameters(Item->ItemPath, InstanceParameterBag);

	// Apply the edit to the Character within the transaction, so that it will be rolled back on undo
	UMetaHumanCharacterEditorSubsystem::Get()->OnEditPreviewCollection(CostumeToolProperties->Character);

	const bool bIsInteractive = PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive;
	if (!bIsInteractive && PropertyChangeTransaction.IsValid() && PropertyChangeTransaction->IsOutstanding())
	{
		PropertyChangeTransaction.Reset();
	}

	if (EnumHasAnyFlags(OverrideResult, EMetaHumanInstanceParameterOverrideResult::ReassemblyRequiredToApplyNewParameters))
	{
		UMetaHumanCharacterEditorSubsystem::Get()->RunCharacterEditorPipelineForPreview(CostumeToolProperties->Character);
	}
}

EVisibility SMetaHumanCharacterEditorCostumeToolView::GetWarningVisibility() const
{
	const bool bIsVisible =
		GetGroomsBoxVisibility() == EVisibility::Collapsed
		&& GetOutfitClothingBoxVisibility() == EVisibility::Collapsed
		&& GetSkeletalMeshBoxVisibility() == EVisibility::Collapsed;
	return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SMetaHumanCharacterEditorCostumeToolView::GetGroomsBoxVisibility() const
{
	const bool bIsVisible = GroomsBox.IsValid() && GroomsBox->NumSlots() > 0;
	return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SMetaHumanCharacterEditorCostumeToolView::GetOutfitClothingBoxVisibility() const
{
	const bool bIsVisible = OutfitClothingBox.IsValid() && OutfitClothingBox->NumSlots() > 0;
	return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SMetaHumanCharacterEditorCostumeToolView::GetSkeletalMeshBoxVisibility() const
{
	const bool bIsVisible = SkeletalMeshesBox.IsValid() && SkeletalMeshesBox->NumSlots() > 0;
	return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

bool SMetaHumanCharacterEditorCostumeToolView::IsNameEnumValue(UEnum* EnumPtr, const FName& NameToCheck) const
{
	if (!EnumPtr)
	{
		return false;
	}

	const int32 NumEnums = EnumPtr->NumEnums();
	for (int32 Index = 0; Index < NumEnums - 1; ++Index)
	{
		FString EnumValueName = EnumPtr->GetDisplayNameTextByIndex(Index).ToString();
		if (EnumValueName == NameToCheck.ToString())
		{
			return true;
		}
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
