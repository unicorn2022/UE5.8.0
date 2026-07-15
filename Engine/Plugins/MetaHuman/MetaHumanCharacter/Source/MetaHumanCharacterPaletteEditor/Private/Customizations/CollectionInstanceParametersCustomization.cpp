// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/CollectionInstanceParametersCustomization.h"

#include "PaletteEditor/MetaHumanCharacterPaletteEditorToolkit.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterPaletteEditor"

TSharedRef<IDetailCustomization> FCollectionInstanceParametersCustomization::MakeInstance()
{
	return MakeShared<FCollectionInstanceParametersCustomization>();
}

void FCollectionInstanceParametersCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> EditedObjects;
	DetailBuilder.GetObjectsBeingCustomized(EditedObjects);

	UMetaHumanPaletteEditorCollectionInstanceParameters* Wrapper = nullptr;
	if (EditedObjects.Num() == 1)
	{
		Wrapper = Cast<UMetaHumanPaletteEditorCollectionInstanceParameters>(EditedObjects[0].Get());
	}

	const TSharedRef<IPropertyHandle> ItemsHandle = DetailBuilder.GetProperty(
		GET_MEMBER_NAME_CHECKED(UMetaHumanPaletteEditorCollectionInstanceParameters, UMetaHumanPaletteEditorCollectionInstanceParameters_Items));

	// Hide the default array; we re-add each element's InstanceParameters into its own category below.
	DetailBuilder.HideProperty(ItemsHandle);

	if (!Wrapper)
	{
		return;
	}

	const TSharedPtr<IPropertyHandleArray> ItemsArrayHandle = ItemsHandle->AsArray();
	if (!ItemsArrayHandle.IsValid())
	{
		return;
	}

	uint32 NumElements = 0;
	ItemsArrayHandle->GetNumElements(NumElements);

	for (uint32 ElementIndex = 0; ElementIndex < NumElements; ++ElementIndex)
	{
		if (!Wrapper->UMetaHumanPaletteEditorCollectionInstanceParameters_Items.IsValidIndex(ElementIndex))
		{
			continue;
		}

		const FMetaHumanPaletteEditorItemInstanceParameters& Element =
			Wrapper->UMetaHumanPaletteEditorCollectionInstanceParameters_Items[ElementIndex];

		const TSharedRef<IPropertyHandle> ElementHandle = ItemsArrayHandle->GetElement(ElementIndex);
		const TSharedPtr<IPropertyHandle> InstanceParametersHandle = ElementHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FMetaHumanPaletteEditorItemInstanceParameters, InstanceParameters));

		if (!InstanceParametersHandle.IsValid())
		{
			continue;
		}

		// Unique FName per element so categories don't collide; display text comes from the item's
		// friendly display name (populated by the toolkit), falling back to the path.
		const FName CategoryName(*FString::Printf(TEXT("ItemInstanceParameters_%u"), ElementIndex));
		const FText CategoryDisplay = !Element.DisplayName.IsEmpty()
			? Element.DisplayName
			: FText::FromString(Element.ItemPath.ToDebugString());

		IDetailCategoryBuilder& ItemCategory = DetailBuilder.EditCategory(
			CategoryName,
			CategoryDisplay,
			ECategoryPriority::Default);

		ItemCategory.AddProperty(InstanceParametersHandle.ToSharedRef());
	}
}

#undef LOCTEXT_NAMESPACE
