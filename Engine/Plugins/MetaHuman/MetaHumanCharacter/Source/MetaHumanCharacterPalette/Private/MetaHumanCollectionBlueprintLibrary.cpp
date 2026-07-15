// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCollectionBlueprintLibrary.h"

#include "MetaHumanCharacterPalette.h"
#include "MetaHumanCharacterPaletteItem.h"
#include "MetaHumanCharacterPaletteLog.h"
#include "MetaHumanCharacterPipeline.h"
#include "MetaHumanCharacterPipelineSpecification.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionPipeline.h"
#include "MetaHumanInstance.h"
#include "MetaHumanPipelineSlotSelectionData.h"
#include "MetaHumanWardrobeItem.h"

#include "Algo/RemoveIf.h"
#include "Algo/Transform.h"
#include "Internationalization/Text.h"

#include "EditorOnlyAssetReference.h"

#include "Logging/StructuredLog.h"
#include "UObject/SoftObjectPath.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanCollectionBlueprintLibrary)

namespace UE::MetaHuman::Private
{
	/**
	 * @brief Template function to get a value of an instance parameter from a property bag
	 * 
	 * @tparam ParamType the type of parameter to get
	 * @param InInstanceParam the instance parameter to get the parameter value for
	 * @param OutValue The value of the parameter
	 * @param InGetFunc a pointer to a Get function that can be called on a FInstancedPropertyBag object
	 * @returns true if the value can be obtained, false otherwise
	 */
	template<typename ParamType, typename PropertyBagGetFunc>
	bool GetInstanceParam(const FMetaHumanCharacterInstanceParameter& InInstanceParam, ParamType& OutValue, PropertyBagGetFunc InGetFunc)
	{
		if (!InInstanceParam.Instance.IsValid())
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "GetInstanceParam called with invalid instance");
			return false;
		}

		FInstancedPropertyBag PropertyBag = InInstanceParam.Instance->GetCurrentInstanceParametersForItem(InInstanceParam.ItemPath);

		if (!PropertyBag.IsValid())
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette,
					  Error,
					  "Failed to find parameters for item '{Item}' in instance '{Instance}'",
					  InInstanceParam.ItemPath.ToDebugString(),
					  InInstanceParam.Instance->GetPathName());
			return false;
		}

		const TValueOrError<ParamType, EPropertyBagResult> Result = InGetFunc(PropertyBag, InInstanceParam.Name);

		if (Result.HasError())
		{
			const EPropertyBagResult Error = Result.GetError();
			UE_LOGFMT(LogMetaHumanCharacterPalette,
					  Error,
					  "Failed to get '{Param}' of type '{Type}' for item '{Item}' in instance '{Instance}: {Error}'",
					  InInstanceParam.Name.ToString(),
					  UEnum::GetDisplayValueAsText(InInstanceParam.Type).ToString(),
					  InInstanceParam.ItemPath.ToDebugString(),
					  InInstanceParam.Instance->GetPathName(),
					  UEnum::GetDisplayValueAsText(Error).ToString());
			return false;
		}

		OutValue = Result.GetValue();

		return true;
	}

	/**
	 * @brief Template function to set a value of an instance parameter to a property bag and apply it back to the instance
	 * 
	 * @param InInstanceParam The instance parameter to set the value for
	 * @param InValue The value to be set
	 * @param InSetFunc Pointer to a Set function that can be called on a FInstancedPropertyBag object
	 * @return true if the value was set and false otherwise
	 */
	template<typename ParamType, typename PropertyBagSetFunc>
	bool SetInstanceParam(const FMetaHumanCharacterInstanceParameter& InInstanceParam, ParamType InValue, PropertyBagSetFunc InSetFunc)
	{
		if (!InInstanceParam.Instance.IsValid())
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "SetInstanceParam called with invalid instance");
			return false;
		}

		FInstancedPropertyBag PropertyBag = InInstanceParam.Instance->GetCurrentInstanceParametersForItem(InInstanceParam.ItemPath);

		if (!PropertyBag.IsValid())
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette,
					  Error,
					  "Failed to find parameters for item '{Item}' in instance '{Instance}'",
					  InInstanceParam.ItemPath.ToDebugString(),
					  InInstanceParam.Instance->GetPathName());
			return false;
		}

		const EPropertyBagResult Result = InSetFunc(PropertyBag, InInstanceParam.Name, InValue);

		if (Result != EPropertyBagResult::Success)
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette,
					  Error,
					  "Failed to set '{Param}' of type '{Type}' for item '{Item}' in instance '{Instance}: {Error}'",
					  InInstanceParam.Name.ToString(),
					  UEnum::GetDisplayValueAsText(InInstanceParam.Type).ToString(),
					  InInstanceParam.ItemPath.ToDebugString(),
					  InInstanceParam.Instance->GetPathName(),
					  UEnum::GetDisplayValueAsText(Result).ToString());
			return false;
		}

		InInstanceParam.Instance->OverrideInstanceParameters(InInstanceParam.ItemPath, PropertyBag);

		return true;
	}

	/** Returns the UClass constraint stored in PropertyDesc.ValueTypeObject for an Object/SoftObject parameter, or nullptr. */
	UClass* GetObjectClassFromPropertyDesc(const FPropertyBagPropertyDesc& PropertyDesc)
	{
		return Cast<UClass>(const_cast<UObject*>(PropertyDesc.ValueTypeObject.Get()));
	}
}

//-----------------------------------------------------------------------------
// UMetaHumanPaletteKeyBlueprintLibrary
//-----------------------------------------------------------------------------

bool UMetaHumanPaletteKeyBlueprintLibrary::ReferencesSameAsset(const FMetaHumanPaletteItemKey& InKey, const FMetaHumanPaletteItemKey& InOther)
{
	return InKey.ReferencesSameAsset(InOther);
}

FString UMetaHumanPaletteKeyBlueprintLibrary::ToAssetNameString(const FMetaHumanPaletteItemKey& InKey)
{
	return InKey.ToAssetNameString();
}

bool UMetaHumanPaletteKeyBlueprintLibrary::IsNull(const FMetaHumanPaletteItemKey& InKey)
{
	return InKey.IsNull();
}

//-----------------------------------------------------------------------------
// UMetaHumanPaletteItemPathBlueprintLibrary
//-----------------------------------------------------------------------------

FMetaHumanPaletteItemPath UMetaHumanPaletteItemPathBlueprintLibrary::MakeItemPath(const FMetaHumanPaletteItemKey& InItemKey)
{
	return FMetaHumanPaletteItemPath{ InItemKey };
}

//-----------------------------------------------------------------------------
// UMetaHumanCharacterInstanceParameterBlueprintLibrary
//-----------------------------------------------------------------------------

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::GetBoolInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, bool& OutValue)
{
	using namespace UE::MetaHuman::Private;
	return GetInstanceParam(InInstanceParam, OutValue, UE_PROJECTION_MEMBER(FInstancedPropertyBag, GetValueBool));
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::SetBoolInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, bool InValue)
{
	using namespace UE::MetaHuman::Private;
	return SetInstanceParam(InInstanceParam, InValue, UE_PROJECTION_MEMBER(FInstancedPropertyBag, SetValueBool));
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::GetFloatInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, float& OutValue)
{
	using namespace UE::MetaHuman::Private;
	return GetInstanceParam(InInstanceParam, OutValue, UE_PROJECTION_MEMBER(FInstancedPropertyBag, GetValueFloat));
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::SetFloatInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, float InValue)
{
	using namespace UE::MetaHuman::Private;
	return SetInstanceParam(InInstanceParam, InValue, UE_PROJECTION_MEMBER(FInstancedPropertyBag, SetValueFloat));
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::GetNameInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, FName& OutValue)
{
	using namespace UE::MetaHuman::Private;
	return GetInstanceParam(InInstanceParam, OutValue, UE_PROJECTION_MEMBER(FInstancedPropertyBag, GetValueName));
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::SetNameInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, FName InValue)
{
	using namespace UE::MetaHuman::Private;
	return SetInstanceParam(InInstanceParam, InValue, UE_PROJECTION_MEMBER(FInstancedPropertyBag, SetValueName));
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::GetStringInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, FString& OutValue)
{
	using namespace UE::MetaHuman::Private;
	return GetInstanceParam(InInstanceParam, OutValue, UE_PROJECTION_MEMBER(FInstancedPropertyBag, GetValueString));
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::SetStringInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, const FString& InValue)
{
	using namespace UE::MetaHuman::Private;
	return SetInstanceParam(InInstanceParam, InValue, UE_PROJECTION_MEMBER(FInstancedPropertyBag, SetValueString));
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::GetColorInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, FLinearColor& OutColor)
{
	using namespace UE::MetaHuman::Private;

	// Read the value while the property bag is still alive in this function's scope. Don't
	// thread through GetInstanceParam, because FInstancedPropertyBag::GetValueStruct<T>(Name)
	// returns a T* into the bag's storage, which would dangle once the bag goes out of scope.
	return GetInstanceParam(
		InInstanceParam,
		OutColor,
		[](const FInstancedPropertyBag& Bag, const FName Name) -> TValueOrError<FLinearColor, EPropertyBagResult>
		{
			const TValueOrError<FLinearColor*, EPropertyBagResult> Result = Bag.GetValueStruct<FLinearColor>(Name);
			if (Result.HasError())
			{
				return MakeError(Result.GetError());
			}
			if (FLinearColor* ColorPtr = Result.GetValue())
			{
				return MakeValue(*ColorPtr);
			}
			return MakeError(EPropertyBagResult::TypeMismatch);
		});
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::SetColorInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, const FLinearColor& InColor)
{
	using namespace UE::MetaHuman::Private;
	return SetInstanceParam(InInstanceParam, InColor, UE_PROJECTION_MEMBER(FInstancedPropertyBag, template SetValueStruct<FLinearColor>));
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::GetObjectInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, UObject*& OutValue)
{
	using namespace UE::MetaHuman::Private;
	return GetInstanceParam(
		InInstanceParam,
		OutValue,
		[](const FInstancedPropertyBag& Bag, const FName Name)
		{
			return Bag.GetValueObject(Name);
		});
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::SetObjectInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, UObject* InValue)
{
	using namespace UE::MetaHuman::Private;
	return SetInstanceParam(
		InInstanceParam,
		InValue,
		[](FInstancedPropertyBag& Bag, const FName Name, UObject* Value)
		{
			return Bag.SetValueObject(Name, Value);
		});
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::GetSoftObjectInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, TSoftObjectPtr<UObject>& OutValue)
{
	using namespace UE::MetaHuman::Private;
	FSoftObjectPath Path;
	if (!GetInstanceParam(
			InInstanceParam,
			Path,
			[](const FInstancedPropertyBag& Bag, const FName Name)
			{
				return Bag.GetValueSoftPath(Name);
			}))
	{
		return false;
	}
	OutValue = TSoftObjectPtr<UObject>(Path);
	return true;
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::SetSoftObjectInstanceParameter(const FMetaHumanCharacterInstanceParameter& InInstanceParam, const TSoftObjectPtr<UObject>& InValue)
{
	using namespace UE::MetaHuman::Private;
	return SetInstanceParam(
		InInstanceParam,
		InValue.ToSoftObjectPath(),
		[](FInstancedPropertyBag& Bag, const FName Name, const FSoftObjectPath& Value)
		{
			return Bag.SetValueSoftPath(Name, Value);
		});
}

//-----------------------------------------------------------------------------
// UMetaHumanPipelineSlotSelectionBlueprintLibrary
//-----------------------------------------------------------------------------

FMetaHumanPipelineSlotSelection UMetaHumanPipelineSlotSelectionBlueprintLibrary::MakeSlotSelection(FName SlotName, const FMetaHumanPaletteItemKey& SelectedItem, FMetaHumanPaletteItemPath ParentItemPath)
{
	return FMetaHumanPipelineSlotSelection(ParentItemPath, SlotName, SelectedItem);
}

FMetaHumanPaletteItemPath UMetaHumanPipelineSlotSelectionBlueprintLibrary::GetSelectedItemPath(const FMetaHumanPipelineSlotSelection& InSlotSelection)
{
	return InSlotSelection.GetSelectedItemPath();
}

FName UMetaHumanPipelineSlotSelectionBlueprintLibrary::GetSelectedSlotName(const FMetaHumanPipelineSlotSelection& InSlotSelection)
{
	return InSlotSelection.SlotName;
}

FMetaHumanPaletteItemKey UMetaHumanPipelineSlotSelectionBlueprintLibrary::GetSelectedItemKey(const FMetaHumanPipelineSlotSelection& InSlotSelection)
{
	return InSlotSelection.SelectedItem;
}

//-----------------------------------------------------------------------------
// UMetaHumanCollectionBlueprintLibrary
//-----------------------------------------------------------------------------

namespace UE::MetaHuman::Private
{
	/** Returns the pipeline specification for the palette, or nullptr. */
	const UMetaHumanCharacterPipelineSpecification* GetSpecForPalette(UMetaHumanCharacterPalette* Palette)
	{
		if (!Palette)
		{
			return nullptr;
		}

		const UMetaHumanCharacterPipeline* Pipeline = Palette->GetPalettePipeline();
		if (!Pipeline)
		{
			return nullptr;
		}

		return Pipeline->GetSpecification();
	}

	/** Returns true if A's PrincipalAsset matches the given object. */
	bool ItemKeyReferencesPrincipalAsset(const FMetaHumanPaletteItemKey& Key, const UObject* PrincipalAsset)
	{
		if (Key.IsNull() || PrincipalAsset == nullptr || Key.ReferencesExternalWardrobeItem())
		{
			return false;
		}
		FEditorOnlyAssetReference Ref;
		if (!Key.TryGetPrincipalAsset(Ref))
		{
			return false;
		}
		return Ref.ToSoftObjectPath() == FSoftObjectPath(PrincipalAsset);
	}

	bool ItemKeyReferencesWardrobeItem(const FMetaHumanPaletteItemKey& Key, const UMetaHumanWardrobeItem* WardrobeItem)
	{
		if (Key.IsNull() || WardrobeItem == nullptr || !Key.ReferencesExternalWardrobeItem())
		{
			return false;
		}
		FEditorOnlyAssetReference Ref;
		if (!Key.TryGetExternalWardrobeItem(Ref))
		{
			return false;
		}
		return Ref.ToSoftObjectPath() == FSoftObjectPath(WardrobeItem);
	}
}

UMetaHumanCharacterPipelineSpecification* UMetaHumanCollectionBlueprintLibrary::GetPipelineSpecification(UMetaHumanCharacterPalette* Palette)
{
	using namespace UE::MetaHuman::Private;
	return const_cast<UMetaHumanCharacterPipelineSpecification*>(GetSpecForPalette(Palette));
}

TArray<FName> UMetaHumanCollectionBlueprintLibrary::GetSlotNames(UMetaHumanCharacterPalette* Palette)
{
	using namespace UE::MetaHuman::Private;
	TArray<FName> Result;
	if (const UMetaHumanCharacterPipelineSpecification* Spec = GetSpecForPalette(Palette))
	{
		Result.Reserve(Spec->Slots.Num());
		for (const TPair<FName, FMetaHumanCharacterPipelineSlot>& Pair : Spec->Slots)
		{
			Result.Add(Pair.Key);
		}
	}
	return Result;
}

TArray<FMetaHumanPaletteItemKey> UMetaHumanCollectionBlueprintLibrary::GetAllItemKeys(UMetaHumanCharacterPalette* Palette)
{
	TArray<FMetaHumanPaletteItemKey> Result;
	if (!Palette)
	{
		return Result;
	}

	const TArray<FMetaHumanCharacterPaletteItem>& Items = Palette->GetItems();
	Result.Reserve(Items.Num());
	for (const FMetaHumanCharacterPaletteItem& Item : Items)
	{
		Result.Add(Item.GetItemKey());
	}
	return Result;
}

TArray<FMetaHumanPaletteItemKey> UMetaHumanCollectionBlueprintLibrary::GetItemKeysForSlot(UMetaHumanCharacterPalette* Palette, FName SlotName)
{
	using namespace UE::MetaHuman::Private;
	TArray<FMetaHumanPaletteItemKey> Result;
	if (!Palette)
	{
		return Result;
	}

	for (const FMetaHumanCharacterPaletteItem& Item : Palette->GetItems())
	{
		if (Item.SlotName == SlotName)
		{
			Result.Add(Item.GetItemKey());
		}
	}

	return Result;
}

TArray<FMetaHumanPaletteItemKey> UMetaHumanCollectionBlueprintLibrary::GetItemKeysForPrincipalAsset(UMetaHumanCharacterPalette* Palette, UObject* PrincipalAsset)
{
	using namespace UE::MetaHuman::Private;
	TArray<FMetaHumanPaletteItemKey> Result;
	if (!Palette || !PrincipalAsset)
	{
		return Result;
	}

	for (const FMetaHumanCharacterPaletteItem& Item : Palette->GetItems())
	{
		const FMetaHumanPaletteItemKey Key = Item.GetItemKey();
		if (ItemKeyReferencesPrincipalAsset(Key, PrincipalAsset))
		{
			Result.Add(Key);
		}
	}

	return Result;
}

TArray<FMetaHumanPaletteItemKey> UMetaHumanCollectionBlueprintLibrary::GetItemKeysForWardrobeItem(UMetaHumanCharacterPalette* Palette, UMetaHumanWardrobeItem* WardrobeItem)
{
	using namespace UE::MetaHuman::Private;
	TArray<FMetaHumanPaletteItemKey> Result;
	if (!Palette || !WardrobeItem)
	{
		return Result;
	}

	for (const FMetaHumanCharacterPaletteItem& Item : Palette->GetItems())
	{
		const FMetaHumanPaletteItemKey Key = Item.GetItemKey();
		if (ItemKeyReferencesWardrobeItem(Key, WardrobeItem))
		{
			Result.Add(Key);
		}
	}

	return Result;
}

FName UMetaHumanCollectionBlueprintLibrary::GetItemSlotName(UMetaHumanCharacterPalette* Palette, const FMetaHumanPaletteItemKey& ItemKey)
{
	if (!Palette)
	{
		return NAME_None;
	}

	FMetaHumanCharacterPaletteItem FoundItem;
	if (Palette->TryFindItem(ItemKey, FoundItem))
	{
		return FoundItem.SlotName;
	}

	return NAME_None;
}

#if WITH_EDITOR
FText UMetaHumanCollectionBlueprintLibrary::GetItemDisplayName(UMetaHumanCharacterPalette* Palette, const FMetaHumanPaletteItemKey& ItemKey)
{
	if (!Palette)
	{
		return FText::GetEmpty();
	}

	FMetaHumanCharacterPaletteItem FoundItem;
	if (Palette->TryFindItem(ItemKey, FoundItem))
	{
		return FoundItem.GetOrGenerateDisplayName();
	}

	return FText::GetEmpty();
}
#endif // WITH_EDITOR

//-----------------------------------------------------------------------------
// UMetaHumanCharacterInstanceBlueprintLibrary
//-----------------------------------------------------------------------------

UMetaHumanInstance* UMetaHumanCharacterInstanceBlueprintLibrary::DuplicateMetaHumanInstance(UMetaHumanInstance* Source, UObject* Outer)
{
	if (!Source)
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "DuplicateMetaHumanInstance called with a null source");
		return nullptr;
	}

	UObject* const NewOuter = Outer ? Outer : GetTransientPackage();
	UMetaHumanInstance* const NewInstance = NewObject<UMetaHumanInstance>(NewOuter);
	NewInstance->CopyContentsFrom(Source);
	return NewInstance;
}

TArray<FMetaHumanCharacterInstanceParameter> UMetaHumanCharacterInstanceBlueprintLibrary::GetInstanceParametersForItem(UMetaHumanInstance* InInstance, const FMetaHumanPaletteItemPath& ItemPath)
{
	using namespace UE::MetaHuman::Private;

	if (!InInstance)
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "GetInstanceParametersForItem called with an invalid instance");
		return {};
	}

	FInstancedPropertyBag InstanceParametersBag = InInstance->GetCurrentInstanceParametersForItem(ItemPath);

	if (!InstanceParametersBag.IsValid())
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette,
				  Error,
				  "Failed to find parameters for item '{Item}' in instance '{Instance}'",
				  ItemPath.ToDebugString(),
				  InInstance->GetPathName());
		return {};
	}

	const UPropertyBag* PropertyBagStruct = InstanceParametersBag.GetPropertyBagStruct();

	TArray<FMetaHumanCharacterInstanceParameter> InstanceParameters;
	InstanceParameters.Reserve(InstanceParametersBag.GetNumPropertiesInBag());

	for (const FPropertyBagPropertyDesc& PropertyDesc : PropertyBagStruct->GetPropertyDescs())
	{
		switch (PropertyDesc.ValueType)
		{
		case EPropertyBagPropertyType::Bool:
		case EPropertyBagPropertyType::Float:
		case EPropertyBagPropertyType::Name:
		case EPropertyBagPropertyType::String:
		case EPropertyBagPropertyType::Struct:
		case EPropertyBagPropertyType::Object:
		case EPropertyBagPropertyType::SoftObject:
			break;
		default:
			UE_LOGFMT(LogMetaHumanCharacterPalette,
					  Warning,
					  "Property '{Property}' of item '{Item}' is of an unsupported type. Make sure '{Type}' is defined in EMetaHumanCharacterInstanceParameterType",
					  PropertyDesc.Name.ToString(),
					  ItemPath.ToDebugString(),
					  UEnum::GetDisplayValueAsText(PropertyDesc.ValueType).ToString());
			continue;
		}

		FMetaHumanCharacterInstanceParameter& InstanceParam = InstanceParameters.AddDefaulted_GetRef();
		InstanceParam.Name = PropertyDesc.Name;
		InstanceParam.Type = static_cast<EMetaHumanCharacterInstanceParameterType>(PropertyDesc.ValueType);
		InstanceParam.ItemPath = ItemPath;
		InstanceParam.Instance = InInstance;
		InstanceParam.ObjectClass = GetObjectClassFromPropertyDesc(PropertyDesc);
	}

	return InstanceParameters;
}

TArray<FMetaHumanPaletteItemPath> UMetaHumanCharacterInstanceBlueprintLibrary::GetInstanceParameterItemPaths(UMetaHumanInstance* InInstance)
{
	TArray<FMetaHumanPaletteItemPath> Result;
	if (!InInstance)
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "GetInstanceParameterItemPaths called with an invalid instance");
		return Result;
	}

	const TMap<FMetaHumanPaletteItemPath, FInstancedPropertyBag> AssemblyParams = InInstance->GetAssemblyParameters();
	Result.Reserve(AssemblyParams.Num());
	for (const TPair<FMetaHumanPaletteItemPath, FInstancedPropertyBag>& Pair : AssemblyParams)
	{
		if (Pair.Value.IsValid() && Pair.Value.GetNumPropertiesInBag() > 0)
		{
			Result.Add(Pair.Key);
		}
	}
	return Result;
}

bool UMetaHumanCharacterInstanceBlueprintLibrary::TryGetInstanceParameter(UMetaHumanInstance* InInstance, const FMetaHumanPaletteItemPath& ItemPath, FName ParameterName, FMetaHumanCharacterInstanceParameter& OutParameter)
{
	using namespace UE::MetaHuman::Private;

	OutParameter = FMetaHumanCharacterInstanceParameter();

	if (!InInstance)
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "TryGetInstanceParameter called with an invalid instance");
		return false;
	}

	FInstancedPropertyBag PropertyBag = InInstance->GetCurrentInstanceParametersForItem(ItemPath);
	if (!PropertyBag.IsValid())
	{
		return false;
	}

	const UPropertyBag* PropertyBagStruct = PropertyBag.GetPropertyBagStruct();
	for (const FPropertyBagPropertyDesc& PropertyDesc : PropertyBagStruct->GetPropertyDescs())
	{
		if (PropertyDesc.Name != ParameterName)
		{
			continue;
		}

		OutParameter.Name = PropertyDesc.Name;
		OutParameter.Type = static_cast<EMetaHumanCharacterInstanceParameterType>(PropertyDesc.ValueType);
		OutParameter.ItemPath = ItemPath;
		OutParameter.Instance = InInstance;
		OutParameter.ObjectClass = GetObjectClassFromPropertyDesc(PropertyDesc);
		return true;
	}
	return false;
}


TArray<FMetaHumanPaletteItemKey> UMetaHumanCharacterInstanceBlueprintLibrary::GetAllowedItemKeysForSlot(UMetaHumanInstance* InInstance, FName SlotName)
{
	TArray<FMetaHumanPaletteItemKey> Result;

	if (!InInstance)
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "GetAllowedItemKeysForSlot called with an invalid instance");
		return Result;
	}

	UMetaHumanCollection* Collection = InInstance->GetMetaHumanCollection();
	if (!Collection)
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Warning,
			"GetAllowedItemKeysForSlot: instance {Instance} has no Collection",
			InInstance->GetPathName());
		return Result;
	}

	const UMetaHumanCollectionPipeline* Pipeline = Collection->GetPipeline();
	if (!Pipeline)
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Warning,
			"GetAllowedItemKeysForSlot: collection {Collection} has no pipeline",
			Collection->GetPathName());
		return Result;
	}

	// AreSlotSelectionsAllowed requires the Collection to have been built.
	if (!Collection->GetBuiltData().IsValid())
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Warning,
			"GetAllowedItemKeysForSlot: collection {Collection} has not been built; cannot evaluate slot compatibility",
			Collection->GetPathName());
		return Result;
	}

	const UMetaHumanCharacterPipelineSpecification* Spec = Pipeline->GetSpecification();
	if (!Spec)
	{
		return Result;
	}

	// Resolve virtual -> real slot.
	const TOptional<FName> RealSlotNameMaybe = Spec->ResolveRealSlotName(SlotName);
	if (!RealSlotNameMaybe.IsSet())
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Warning,
			"GetAllowedItemKeysForSlot: slot '{Slot}' not found on collection {Collection}'s pipeline",
			SlotName.ToString(), Collection->GetPathName());
		return Result;
	}
	const FName RealSlotName = RealSlotNameMaybe.GetValue();

	const FMetaHumanCharacterPipelineSlot* SlotSpec = Spec->Slots.Find(RealSlotName);
	if (!SlotSpec)
	{
		return Result;
	}

	// Build the baseline real-slot selections from the instance's current selections.
	const TArray<FMetaHumanPipelineSlotSelectionData> RealSlotSelectionData =
		Collection->PropagateVirtualSlotSelections(InInstance->GetSlotSelectionData());

	TArray<FMetaHumanPipelineSlotSelection> BaseRealSlotSelections;
	BaseRealSlotSelections.Reserve(RealSlotSelectionData.Num());
	Algo::Transform(RealSlotSelectionData, BaseRealSlotSelections,
		[](const FMetaHumanPipelineSlotSelectionData& Data) { return Data.Selection; });

	// For each candidate item targeting this slot, ask AreSlotSelectionsAllowed. Items match
	// against the slot name as authored on the item.
	for (const FMetaHumanCharacterPaletteItem& Item : Collection->GetItems())
	{
		if (Item.SlotName != SlotName)
		{
			continue;
		}

		const FMetaHumanPaletteItemKey ProposedItemKey = Item.GetItemKey();

		TArray<FMetaHumanPipelineSlotSelection> ProposedSelections = BaseRealSlotSelections;

		// Check whether the candidate is already in the baseline.
		bool bAlreadySelected = false;
		for (const FMetaHumanPipelineSlotSelection& Existing : BaseRealSlotSelections)
		{
			if (Existing.SlotName == RealSlotName && Existing.SelectedItem == ProposedItemKey)
			{
				bAlreadySelected = true;
				break;
			}
		}

		if (!bAlreadySelected)
		{
			// For single-select slots, replace any existing direct selection for this slot.
			if (!SlotSpec->bAllowsMultipleSelection)
			{
				const int32 NumToKeep = Algo::RemoveIf(ProposedSelections,
					[RealSlotName](const FMetaHumanPipelineSlotSelection& Selection)
					{
						return Selection.SlotName == RealSlotName && Selection.ParentItemPath.IsEmpty();
					});
				ProposedSelections.SetNum(NumToKeep);
			}

			ProposedSelections.Add(FMetaHumanPipelineSlotSelection(RealSlotName, ProposedItemKey));
		}

		FText DisallowedReason;
		if (Pipeline->AreSlotSelectionsAllowed(Collection, ProposedSelections, DisallowedReason))
		{
			Result.Add(ProposedItemKey);
		}
	}

	return Result;
}
