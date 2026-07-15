// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/MetaHumanCharacterPaletteItemWrapperCustomization.h"

#include "MetaHumanCharacterPaletteItem.h"
#include "MetaHumanCharacterPaletteItemWrapper.h"
#include "MetaHumanWardrobeItem.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "UObject/UnrealType.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterPaletteEditor"

namespace UE::MetaHuman::Private
{
	// Helpers to give both overloads of AddExternalObjectProperty the same return type.
	//
	// This makes it possible to use them in the templated AddWardrobeItemPropertyRows below.
	static IDetailPropertyRow* AddExternalProperty(IDetailCategoryBuilder& Container, const TArray<UObject*>& Objects, FName PropertyName)
	{
		return Container.AddExternalObjectProperty(Objects, PropertyName, EPropertyLocation::Default, FAddPropertyParams());
	}

	static IDetailPropertyRow* AddExternalProperty(IDetailGroup& Container, const TArray<UObject*>& Objects, FName PropertyName)
	{
		return &Container.AddExternalObjectProperty(Objects, PropertyName, EPropertyLocation::Default, FAddPropertyParams());
	}

	template <typename ContainerT>
	static void AddWardrobeItemPropertyRows(
		ContainerT& OutContainer,
		const TArray<UObject*>& WardrobeItemObjects,
		TNotNull<const UClass*> WardrobeItemClass,
		bool bDisableAll,
		FName PropertyToDisable)
	{
		for (TFieldIterator<FProperty> PropertyIt(WardrobeItemClass); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
			{
				continue;
			}

			IDetailPropertyRow* Row = AddExternalProperty(OutContainer, WardrobeItemObjects, Property->GetFName());
			if (!Row)
			{
				continue;
			}

			const bool bShouldDisable = bDisableAll || (Property->GetFName() == PropertyToDisable);
			if (bShouldDisable)
			{
				Row->IsEnabled(false);
			}
		}
	}
}

TSharedRef<IDetailCustomization> FMetaHumanCharacterPaletteItemWrapperCustomization::MakeInstance()
{
	return MakeShared<FMetaHumanCharacterPaletteItemWrapperCustomization>();
}

void FMetaHumanCharacterPaletteItemWrapperCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> EditedObjects;
	DetailBuilder.GetObjectsBeingCustomized(EditedObjects);

	UMetaHumanCharacterPaletteItemWrapper* Wrapper = nullptr;
	if (EditedObjects.Num() == 1)
	{
		Wrapper = Cast<UMetaHumanCharacterPaletteItemWrapper>(EditedObjects[0].Get());
	}

	UMetaHumanWardrobeItem* WardrobeItem = Wrapper ? static_cast<UMetaHumanWardrobeItem*>(Wrapper->Item.WardrobeItem) : nullptr;

	const TSharedRef<IPropertyHandle> WardrobeItemPropertyHandle = DetailBuilder.GetProperty(
		GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterPaletteItem, WardrobeItem),
		FMetaHumanCharacterPaletteItem::StaticStruct());

	// Hide the default Wardrobe Item row, since we build one manually below
	DetailBuilder.HideProperty(WardrobeItemPropertyHandle);

	const bool bHasWardrobeItem = (WardrobeItem != nullptr);
	const bool bIsInternal = bHasWardrobeItem && !WardrobeItem->IsExternal();

	// Sort the Wardrobe Item section above the Character section (Variation, SlotName, etc.)
	IDetailCategoryBuilder& WardrobeItemCategory = DetailBuilder.EditCategory(
		TEXT("Wardrobe Item"),
		LOCTEXT("WardrobeItemCategory", "Wardrobe Item"),
		ECategoryPriority::Important);

	if (!bHasWardrobeItem)
	{
		return;
	}

	const TArray<UObject*> WardrobeItemObjects = { WardrobeItem };
	const FName PrincipalAssetPropertyName = GET_MEMBER_NAME_CHECKED(UMetaHumanWardrobeItem, PrincipalAsset);

	// The WI is presented as an expandable group in both cases, so the visual layout stays consistent
	IDetailGroup& WardrobeItemGroup = WardrobeItemCategory.AddGroup(
		TEXT("WardrobeItem"),
		LOCTEXT("WardrobeItemGroup", "Wardrobe Item"),
		/*bInStartExpanded=*/ false);

	if (bIsInternal)
	{
		// Internal WIs are subobjects of the Collection
		//
		// There's no asset to point at, so the header is a plain label rather than an asset row.
		WardrobeItemGroup.HeaderRow()
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InternalWardrobeItemHeader", "Internal Wardrobe Item"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];

		// PrincipalAsset is locked in at item-creation time, don't allow it to be edited here
		UE::MetaHuman::Private::AddWardrobeItemPropertyRows(
			WardrobeItemGroup,
			WardrobeItemObjects,
			UMetaHumanWardrobeItem::StaticClass(),
			/*bDisableAll=*/ false,
			/*PropertyToDisable=*/ PrincipalAssetPropertyName);
	}
	else
	{
		// External WIs are standalone assets
		//
		// Use the standard asset row for the WI, and show inner properties as read-only since 
		// editing them here would mutate a shared asset and surprise other consumers of it.
		WardrobeItemGroup.HeaderProperty(WardrobeItemPropertyHandle);

		UE::MetaHuman::Private::AddWardrobeItemPropertyRows(
			WardrobeItemGroup,
			WardrobeItemObjects,
			UMetaHumanWardrobeItem::StaticClass(),
			/*bDisableAll=*/ true,
			/*PropertyToDisable=*/ NAME_None);
	}
}

#undef LOCTEXT_NAMESPACE
