// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVImportStaticMeshParamsCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"

#include "Engine/StaticMesh.h"

#include "Params/PVImportStaticMeshParams.h"

#include "ScopedTransaction.h"

#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PVImportStaticMeshParamsCustomization"

namespace PVImportStaticMeshParamsCustomization
{
	TArray<FPVImportStaticMeshParams*> ResolveParams(TSharedRef<IPropertyHandle> Handle)
	{
		TArray<void*> RawData;
		Handle->AccessRawData(RawData);

		TArray<FPVImportStaticMeshParams*> Result;
		Result.Reserve(RawData.Num());
		for (int32 i = 0; i < RawData.Num(); ++i)
		{
			Result.Add(static_cast<FPVImportStaticMeshParams*>(RawData[i]));
		}
		return Result;
	}

	TArray<TPair<UObject*, FPVImportStaticMeshParams*>> ResolveObjectsAndParams(TSharedRef<IPropertyHandle> Handle)
	{
		TArray<void*> RawData;
		TArray<UObject*> Outers;
		Handle->AccessRawData(RawData);
		Handle->GetOuterObjects(Outers);

		if (RawData.Num() != Outers.Num())
		{
			return {};
		}

		TArray<TPair<UObject*, FPVImportStaticMeshParams*>> Result;
		Result.Reserve(RawData.Num());
		for (int32 i = 0; i < RawData.Num(); ++i)
		{
			Result.Add({ Outers[i], static_cast<FPVImportStaticMeshParams*>(RawData[i]) });
		}
		return Result;
	}

	// Returns true if all params are valid, non-null, and share the same StaticMeshAsset.
	bool AllShareSameMesh(const TArray<FPVImportStaticMeshParams*>& AllParams)
	{
		if (AllParams.IsEmpty())
		{
			return false;
		}

		const UStaticMesh* FirstMesh = AllParams[0] ? AllParams[0]->StaticMeshAsset : nullptr;
		if (!FirstMesh)
		{
			return false;
		}

		for (const FPVImportStaticMeshParams* Params : AllParams)
		{
			if (!Params || Params->StaticMeshAsset != FirstMesh)
			{
				return false;
			}
		}

		return true;
	}

	FText GetButtonSummaryText(const TArray<FPVImportStaticMeshParams*>& AllParams)
	{
		if (AllParams.IsEmpty() || !AllParams[0] || !AllParams[0]->StaticMeshAsset)
		{
			return LOCTEXT("NoMeshAssigned", "No mesh assigned");
		}

		// Check if all params have identical MaterialsToKeep
		const TArray<FName>& First = AllParams[0]->MaterialsToKeep;
		for (int32 i = 1; i < AllParams.Num(); ++i)
		{
			if (!AllParams[i] || AllParams[i]->MaterialsToKeep != First)
			{
				return LOCTEXT("MultipleValues", "Multiple Values");
			}
		}

		const int32 TotalSlots = AllParams[0]->StaticMeshAsset->GetStaticMaterials().Num();
		const int32 SelectedCount = First.Num();

		if (SelectedCount == 0)
		{
			return LOCTEXT("NoneSelected", "None selected");
		}

		if (SelectedCount == 1)
		{
			return FText::FromName(First[0]);
		}

		return FText::Format(
			LOCTEXT("SelectedCount", "{0} / {1} selected"),
			FText::AsNumber(SelectedCount),
			FText::AsNumber(TotalSlots));
	}

	ECheckBoxState IsSlotChecked(const TArray<FPVImportStaticMeshParams*>& AllParams, FName SlotName)
	{
		int32 CheckedCount = 0;
		for (const FPVImportStaticMeshParams* Params : AllParams)
		{
			if (Params && Params->MaterialsToKeep.Contains(SlotName))
			{
				++CheckedCount;
			}
		}

		if (CheckedCount == 0)
		{
			return ECheckBoxState::Unchecked;
		}
		if (CheckedCount == AllParams.Num())
		{
			return ECheckBoxState::Checked;
		}
		return ECheckBoxState::Undetermined;
	}

	void OnSlotCheckStateChanged(ECheckBoxState State, FName SlotName, TSharedRef<IPropertyHandle> StructHandle, TSharedRef<IPropertyHandle> PropertyHandle)
	{
		TArray<TPair<UObject*, FPVImportStaticMeshParams*>> AllParams = ResolveObjectsAndParams(StructHandle);
		if (AllParams.IsEmpty())
		{
			return;
		}

		FScopedTransaction Transaction(LOCTEXT("ToggleMaterialSlot", "Toggle Material Slot"));

		for (auto& [OuterObject, Params] : AllParams)
		{
			if (!OuterObject || !Params)
			{
				continue;
			}

			OuterObject->Modify();

			if (State == ECheckBoxState::Checked)
			{
				Params->MaterialsToKeep.AddUnique(SlotName);
			}
			else
			{
				Params->MaterialsToKeep.Remove(SlotName);
			}

			FPropertyChangedEvent PropertyChangedEvent(PropertyHandle->GetProperty(), EPropertyChangeType::ValueSet);
			OuterObject->PostEditChangeProperty(PropertyChangedEvent);
		}
	}

	TSharedRef<SWidget> BuildDropdownMenuContent(TSharedRef<IPropertyHandle> StructHandle, TSharedRef<IPropertyHandle> PropertyHandle)
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

		const TArray<FPVImportStaticMeshParams*> AllParams = ResolveParams(StructHandle);

		if (AllParams.IsEmpty() || !AllParams[0] || !AllParams[0]->StaticMeshAsset)
		{
			return Box;
		}

		for (const FStaticMaterial& Mat : AllParams[0]->StaticMeshAsset->GetStaticMaterials())
		{
			const FName SlotName = Mat.MaterialSlotName;

			Box->AddSlot()
				.AutoHeight()
				.Padding(2.0f, 1.0f)
				[
					SNew(SCheckBox)
						.IsChecked_Lambda([StructHandle, SlotName]() { return IsSlotChecked(ResolveParams(StructHandle), SlotName); })
						.OnCheckStateChanged_Static(&OnSlotCheckStateChanged, SlotName, StructHandle, PropertyHandle)
						[
							SNew(STextBlock).Text(FText::FromName(SlotName))
						]
				];
		}

		return Box;
	}

	TSharedRef<SWidget> BuildMaterialsDropdown(TSharedRef<IPropertyHandle> StructHandle, TSharedRef<IPropertyHandle> PropertyHandle)
	{
		return SNew(SComboButton)
			.IsEnabled_Lambda([StructHandle]() { return AllShareSameMesh(ResolveParams(StructHandle)); })
			.ButtonContent()
			[
				SNew(STextBlock).Text_Lambda([StructHandle]() { return GetButtonSummaryText(ResolveParams(StructHandle)); })
			]
			.OnGetMenuContent_Static(&BuildDropdownMenuContent, StructHandle, PropertyHandle);
	}

	void OnStaticMeshAssetChanged(TSharedRef<IPropertyHandle> StructHandle, TSharedPtr<IPropertyHandle> StaticMeshHandle)
	{
		const TArray<TPair<UObject*, FPVImportStaticMeshParams*>> AllParams = ResolveObjectsAndParams(StructHandle);
		if (AllParams.IsEmpty())
		{
			return;
		}

		FScopedTransaction Transaction(LOCTEXT("StaticMeshChanged", "Change Static Mesh Asset"));

		for (auto& [OuterObject, Params] : AllParams)
		{
			if (!OuterObject || !Params)
			{
				continue;
			}

			OuterObject->Modify();

			if (!Params->StaticMeshAsset)
			{
				Params->MaterialsToKeep.Empty();
			}
			else
			{
				TSet<FName> ValidSlots;
				for (const FStaticMaterial& Mat : Params->StaticMeshAsset->GetStaticMaterials())
				{
					ValidSlots.Add(Mat.MaterialSlotName);
				}

				Params->MaterialsToKeep.RemoveAll([&ValidSlots](const FName& Name)
				{
					return !ValidSlots.Contains(Name);
				});
			}

			FPropertyChangedEvent PropertyChangedEvent(StaticMeshHandle->GetProperty(), EPropertyChangeType::ValueSet);
			OuterObject->PostEditChangeProperty(PropertyChangedEvent);
		}
	}
}

TSharedRef<IPropertyTypeCustomization> FPVImportStaticMeshParamsCustomization::MakeInstance()
{
	return MakeShareable(new FPVImportStaticMeshParamsCustomization());
}

void FPVImportStaticMeshParamsCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> InPropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& InCustomizationUtils)
{}

void FPVImportStaticMeshParamsCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> InPropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	using namespace PVImportStaticMeshParamsCustomization;

	uint32 NumChildren;
	InPropertyHandle->GetNumChildren(NumChildren);

	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = InPropertyHandle->GetChildHandle(ChildIndex);
		const FName PropertyName = ChildHandle->GetProperty()->GetFName();

		if (PropertyName == GET_MEMBER_NAME_CHECKED(FPVImportStaticMeshParams, StaticMeshAsset))
		{
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
			ChildHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateStatic(&OnStaticMeshAssetChanged, InPropertyHandle, ChildHandle));
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(FPVImportStaticMeshParams, MaterialsToKeep))
		{
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef()).CustomWidget()
				.NameContent()
				[
					ChildHandle->CreatePropertyNameWidget()
				]
				.ValueContent()
				.HAlign(HAlign_Fill)
				[
					BuildMaterialsDropdown(InPropertyHandle, ChildHandle.ToSharedRef())
				];
		}
		else
		{
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}
}

#undef LOCTEXT_NAMESPACE
