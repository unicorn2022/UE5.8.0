// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/MetaHumanMaterialPipelineCommon.h"

#include "StructUtils/PropertyBag.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/Texture.h"
#include "Algo/Find.h"

namespace UE::MetaHuman::MaterialUtils
{
	FFetchSlotNameDelegate MakeFetchSlotNameDelegate(TConstArrayView<FSkeletalMaterial> SkeletalMaterials)
	{
		return UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate::CreateLambda([SkeletalMaterials](int32 Index) -> FName
			{
				return SkeletalMaterials.IsValidIndex(Index) ? SkeletalMaterials[Index].MaterialSlotName : NAME_None;
			});
	}

	FFetchSlotMaterialDelegate MakeFetchSlotMaterialDelegate(TConstArrayView<FSkeletalMaterial> SkeletalMaterials)
	{
		return UE::MetaHuman::MaterialUtils::FFetchSlotMaterialDelegate::CreateLambda([SkeletalMaterials](int32 Index)
			{
				return SkeletalMaterials.IsValidIndex(Index) ? SkeletalMaterials[Index].MaterialInterface : nullptr;
			});
	}

	TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> CastMaterialMapToDynamic(const TMap<FName, TObjectPtr<UMaterialInterface>>& MaterialMap)
	{
		TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> Result;
		Result.Reserve(MaterialMap.Num());

		for (const TPair<FName, TObjectPtr<UMaterialInterface>>& Pair : MaterialMap)
		{
			if (UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(Pair.Value))
			{
				Result.Add(Pair.Key, DynamicMaterial);
			}
		}

		return Result;
	}

#if WITH_EDITOR
	void GenerateAssemblyParameters(
		const TMap<FName, TObjectPtr<UMaterialInterface>>& OverrideMaterials,
		TConstArrayView<FMetaHumanMaterialParameter> RuntimeMaterialParameters,
		int32 NumMaterialSlots,
		const FFetchSlotNameDelegate& FetchSlotName,
		const FFetchSlotMaterialDelegate& FetchSlotMaterial,
		FInstancedPropertyBag& OutAssemblyParameters)
	{
		TSet<FName> ProcessedMaterialSlots;
		ProcessedMaterialSlots.Reserve(NumMaterialSlots);

		for (int32 SlotIndex = 0; SlotIndex < NumMaterialSlots; ++SlotIndex)
		{
			const FName SlotName = FetchSlotName.Execute(SlotIndex);

			if (ProcessedMaterialSlots.Contains(SlotName))
			{
				// A slot with the same name has already been processed.
				//
				// We can only support one slot for each slot name.
				continue;
			}

			const UMaterialInterface* AssemblyMaterial = FetchSlotMaterial.Execute(SlotIndex);

			const TObjectPtr<UMaterialInterface>* PipelineMaterialOverride = OverrideMaterials.Find(SlotName);
			if (PipelineMaterialOverride != nullptr)
			{
				AssemblyMaterial = *PipelineMaterialOverride;
			}

			if (!AssemblyMaterial)
			{
				// No material is assigned to this slot
				continue;
			}

			const TArray<FMetaHumanMaterialParameter> MaterialParamsForThisSlot = RuntimeMaterialParameters.FilterByPredicate(
				[SlotName, SlotIndex](const FMetaHumanMaterialParameter& Parameter)
				{
					switch (Parameter.SlotTarget)
					{
					case EMetaHumanRuntimeMaterialParameterSlotTarget::SlotNames:
						return Parameter.SlotNames.Contains(SlotName);
					case EMetaHumanRuntimeMaterialParameterSlotTarget::SlotIndices:
						return Parameter.SlotIndices.Contains(SlotIndex);
					case EMetaHumanRuntimeMaterialParameterSlotTarget::AllSlots:
						return true;
					default:
						return false;
					}
				});

			const bool bSuccessful = ParametersToPropertyBag(
				AssemblyMaterial,
				MaterialParamsForThisSlot,
				OutAssemblyParameters);

			if (bSuccessful)
			{
				ProcessedMaterialSlots.Add(SlotName);
			}
		}
	}
#endif // WITH_EDITOR

	UObject* CreateUniqueNamedObject(
		UObject* InOuter,
		FName& InOutGeneratedName,
		TFunctionRef<UObject*(FName)> InMakeObject)
	{
		// Start with no number, then increment if there's a collision.
		InOutGeneratedName.SetNumber(0);

		// Names must be deterministic to ensure the same object created in different
		// contexts has the same name. This is critical to avoid cook errors.
		//
		// Creation functions such as UMaterialInstanceDynamic::Create or NewObject
		// can pick a unique name, but often not in a deterministic way, so we have to
		// use FindObjectFast here to find a name that's not already taken.
		while (FindObjectFast<UObject>(InOuter, InOutGeneratedName) != nullptr)
		{
			InOutGeneratedName.SetNumber(InOutGeneratedName.GetNumber() + 1);
		}

		UObject* CreatedObject = InMakeObject(InOutGeneratedName);
		ensure(!CreatedObject || CreatedObject->GetFName() == InOutGeneratedName);
		return CreatedObject;
	}

	UMaterialInstanceDynamic* CreateUniqueNamedMaterialInstanceDynamic(
		TNotNull<const UMaterialInterface*> InParentMaterial,
		UObject* InOuter,
		FName& InOutGeneratedName)
	{
		// Const cast is needed here, because UMaterialInstanceDynamic::Create requires a
		// non-const parent material, in case it hasn't been PostLoaded yet.
		//
		// In practice, InParentMaterial will not be modified.
		const UMaterialInterface* ConstParentMaterial = InParentMaterial;
		UMaterialInterface* MutableParentMaterial = const_cast<UMaterialInterface*>(ConstParentMaterial);

		UMaterialInstanceDynamic* MaterialDynamic = Cast<UMaterialInstanceDynamic>(CreateUniqueNamedObject(
			InOuter,
			InOutGeneratedName,
			[MutableParentMaterial, InOuter](FName InResolvedName) -> UObject*
			{
				return UMaterialInstanceDynamic::Create(MutableParentMaterial, InOuter, InResolvedName);
			}));

		if (MaterialDynamic)
		{
			MaterialDynamic->SetFlags(RF_Public);
			MaterialDynamic->ClearFlags(RF_Transient);
		}

		return MaterialDynamic;
	}

	void ProcessAssemblyParameters(
		const TMap<FName, TObjectPtr<UMaterialInterface>>& SourceOverrideMaterials,
		TConstArrayView<FMetaHumanMaterialParameter> RuntimeMaterialParameters,
		const FString& ItemFriendlyName,
		const FString& PreferredSubfolderPathForGeneratedAssets,
		int32 NumMaterialSlots,
		const FFetchSlotNameDelegate& FetchSlotName,
		const FFetchSlotMaterialDelegate& FetchSlotMaterial,
		UObject* OuterForGeneratedObjects,
		TMap<FName, TObjectPtr<UMaterialInterface>>& OutOverrideMaterials,
		FInstancedPropertyBag& OutPostAssemblyParameters,
		TArray<FMetaHumanGeneratedAssetMetadata>& OutMetadata)
	{
		for (int32 SlotIndex = 0; SlotIndex < NumMaterialSlots; ++SlotIndex)
		{
			const FName SlotName = FetchSlotName.Execute(SlotIndex);

			if (OutOverrideMaterials.Contains(SlotName))
			{
				// A slot with the same name has already been processed.
				//
				// We can only support one slot for each slot name.
				continue;
			}
			
			const UMaterialInterface* AssemblyMaterial = FetchSlotMaterial.Execute(SlotIndex);

			const TObjectPtr<UMaterialInterface>* PipelineMaterialOverride = SourceOverrideMaterials.Find(SlotName);
			if (PipelineMaterialOverride != nullptr)
			{
				AssemblyMaterial = *PipelineMaterialOverride;

				// Add this override now in case it doesn't have any material parameters that are 
				// set from Post-Assembly Parameters. 
				//
				// If it does have settable parameters, the dynamic material instance will replace 
				// this entry below.
				//
				// The const cast is needed here because this data is visible to blueprints, which
				// don't support const properties very well.
				OutOverrideMaterials.Add(SlotName, const_cast<UMaterialInterface*>(AssemblyMaterial));
			}

			if (!AssemblyMaterial)
			{
				// No material is assigned to this slot
				continue;
			}

			const TArray<FMetaHumanMaterialParameter> MaterialParamsForThisSlot = RuntimeMaterialParameters.FilterByPredicate(
				[SlotName, SlotIndex](const FMetaHumanMaterialParameter& Parameter)
				{
					switch (Parameter.SlotTarget)
					{
					case EMetaHumanRuntimeMaterialParameterSlotTarget::SlotNames:
						return Parameter.SlotNames.Contains(SlotName);
					case EMetaHumanRuntimeMaterialParameterSlotTarget::SlotIndices:
						return Parameter.SlotIndices.Contains(SlotIndex);
					case EMetaHumanRuntimeMaterialParameterSlotTarget::AllSlots:
						return true;
					default:
						return false;
					}
				});

			const bool bSuccessful = UE::MetaHuman::MaterialUtils::ParametersToPropertyBag(
				AssemblyMaterial,
				MaterialParamsForThisSlot,
				OutPostAssemblyParameters);

			if (!bSuccessful)
			{
				// No material parameters can be set from Post-Assembly Parameters, so no need to 
				// use a dynamic material instance.
				continue;
			}

			// Generate unique name for the material instance to be unpacked as
			FName GeneratedName;
			{
				FString StrippedMaterialName;
				{
					StrippedMaterialName = AssemblyMaterial->GetName();
						
					constexpr FStringView PrefixMID = TEXTVIEW("MID_");
					constexpr FStringView PrefixMI = TEXTVIEW("MI_");

					// UMaterialInstanceDynamic::Create, called above, adds "MID_" to the front 
					// of the parent material name, so a well named Material Instance Constant
					// will end up as "MID_MI_...".
					//
					// This code aims to strip both of these prefixes.

					if (StrippedMaterialName.StartsWith(PrefixMID))
					{
						StrippedMaterialName = StrippedMaterialName.RightChop(PrefixMID.Len());
					}

					if (StrippedMaterialName.StartsWith(PrefixMI))
					{
						StrippedMaterialName = StrippedMaterialName.RightChop(PrefixMI.Len());
					}
				}

				GeneratedName = FName(FString::Format(TEXT("MI_{0}_{1}"), { ItemFriendlyName, StrippedMaterialName }));
			}

			UMaterialInstanceDynamic* AssemblyMaterialDynamic = CreateUniqueNamedMaterialInstanceDynamic(
				AssemblyMaterial,
				OuterForGeneratedObjects,
				GeneratedName);

			OutOverrideMaterials.Add(SlotName, AssemblyMaterialDynamic);
			OutMetadata.Emplace(AssemblyMaterialDynamic, PreferredSubfolderPathForGeneratedAssets, GeneratedName.ToString());
		}
	}

	void SetInstanceParameters(
		const TArray<FMetaHumanMaterialParameter>& InMaterialParameters,
		const TMap<FName, TObjectPtr<class UMaterialInstanceDynamic>>& InMaterialInstanceMapping,
		const FFetchSlotNameDelegate& InFetchSlotName,
		const FInstancedPropertyBag& InPropertyBag)
	{
		const UPropertyBag* PropertyBag = InPropertyBag.GetPropertyBagStruct();
		if (!PropertyBag)
		{
			return;
		}

		for (const FPropertyBagPropertyDesc& PropertyDesc : PropertyBag->GetPropertyDescs())
		{
			const FMetaHumanMaterialParameter* Parameter = Algo::FindBy(InMaterialParameters, PropertyDesc.Name, &FMetaHumanMaterialParameter::InstanceParameterName);
			if (!Parameter)
			{
				// TODO: log error as this suggests InMaterialParameters have changed since assembly
				continue;
			}

			// Slot names are collected either from slot indices or explicit slot names defined on the parameter
			TArray<FName> SlotNames;

			switch (Parameter->SlotTarget)
			{
				case EMetaHumanRuntimeMaterialParameterSlotTarget::SlotNames:
				{
					SlotNames = Parameter->SlotNames;
					break;
				}
				case EMetaHumanRuntimeMaterialParameterSlotTarget::SlotIndices:
				{
					check(InFetchSlotName.IsBound());

					for (int32 SlotIndex : Parameter->SlotIndices)
					{
						SlotNames.Add(InFetchSlotName.Execute(SlotIndex));
					}
					break;
				}
				case EMetaHumanRuntimeMaterialParameterSlotTarget::AllSlots:
				{
					InMaterialInstanceMapping.GetKeys(SlotNames);
					break;
				}
				default:
					break;
			}

			for (const FName SlotName : SlotNames)
			{
				UMaterialInstanceDynamic* MaterialInstance = nullptr;

				if (const TObjectPtr<UMaterialInstanceDynamic>* FoundMaterialInstance = InMaterialInstanceMapping.Find(SlotName))
				{
					MaterialInstance = *FoundMaterialInstance;
				}

				if (!MaterialInstance)
				{
					// TODO: log error. This shouldn't happen
					continue;
				}

				switch (Parameter->ParameterType)
				{
					case EMetaHumanRuntimeMaterialParameterType::Toggle:
					{
						const TValueOrError<bool, EPropertyBagResult> Result = InPropertyBag.GetValueBool(PropertyDesc);
						if (!Result.HasValue())
						{
							break;
						}

						MaterialInstance->SetScalarParameterValueByInfo(Parameter->MaterialParameter, Result.GetValue() ? 1.0f : 0.0f);
					}
					break;

					case EMetaHumanRuntimeMaterialParameterType::Scalar:
					{
						const TValueOrError<float, EPropertyBagResult> Result = InPropertyBag.GetValueFloat(PropertyDesc);
						if (!Result.HasValue())
						{
							break;
						}

						MaterialInstance->SetScalarParameterValueByInfo(Parameter->MaterialParameter, Result.GetValue());
					}
					break;

					case EMetaHumanRuntimeMaterialParameterType::Vector:
					{
						const TValueOrError<FLinearColor*, EPropertyBagResult> Result = InPropertyBag.GetValueStruct<FLinearColor>(PropertyDesc);
						if (!Result.HasValue()
							|| Result.GetValue() == nullptr)
						{
							break;
						}

						MaterialInstance->SetVectorParameterValueByInfo(Parameter->MaterialParameter, *Result.GetValue());
					}
					break;

					case EMetaHumanRuntimeMaterialParameterType::DoubleVector:

					case EMetaHumanRuntimeMaterialParameterType::Texture:
					{
						const TValueOrError<UObject*, EPropertyBagResult> Result = InPropertyBag.GetValueObject(PropertyDesc, UTexture::StaticClass());
						if (!Result.HasValue())
						{
							break;
						}

						MaterialInstance->SetTextureParameterValueByInfo(Parameter->MaterialParameter, Cast<UTexture>(Result.GetValue()));
						break;
					}

					case EMetaHumanRuntimeMaterialParameterType::TextureCollection:

					case EMetaHumanRuntimeMaterialParameterType::Font:

					case EMetaHumanRuntimeMaterialParameterType::RuntimeVirtualTexture:

					case EMetaHumanRuntimeMaterialParameterType::SparseVolumeTexture:
						break;
				}
			}
		}
	}
	
	void SetInstanceParameters(
		const TArray<FMetaHumanMaterialParameter>& InMaterialParameters,
		const TMap<FName, TObjectPtr<class UMaterialInstanceDynamic>>& InMaterialInstanceMapping,
		const TArray<FName>& InAvailableSlots,
		const FInstancedPropertyBag& InPropertyBag)
	{
		const FFetchSlotNameDelegate FetchSlotName = FFetchSlotNameDelegate::CreateLambda([&InAvailableSlots](int32 Index) -> FName
			{
				if (!InAvailableSlots.IsValidIndex(Index))
				{
					return NAME_None;
				}

				return InAvailableSlots[Index];
			});

		SetInstanceParameters(InMaterialParameters, InMaterialInstanceMapping, FetchSlotName, InPropertyBag);
	}

	bool ParametersToPropertyBag(
		TNotNull<const UMaterialInterface*> InMaterial,
		const TArray<FMetaHumanMaterialParameter>& InMaterialParameters,
		FInstancedPropertyBag& InOutPropertyBag)
	{
		int32 Count = 0;

		for (const FMetaHumanMaterialParameter& MaterialParameter : InMaterialParameters)
		{
			FPropertyBagPropertyDesc PropertyDesc;
			PropertyDesc.Name = MaterialParameter.InstanceParameterName;

	#if WITH_EDITORONLY_DATA
			for (const TPair<FName, FString>& ItPair : MaterialParameter.PropertyMetadata)
			{
				PropertyDesc.MetaData.Add(FPropertyBagPropertyDescMetaData(ItPair.Key, ItPair.Value));
			}
	#endif
			// Get current parameter value and set property value
			switch (MaterialParameter.ParameterType)
			{
				case EMetaHumanRuntimeMaterialParameterType::Toggle:
				{
					PropertyDesc.ValueType = EPropertyBagPropertyType::Bool;
					FMaterialParameterMetadata MaterialValue;

					if (InMaterial->GetParameterValue(EMaterialParameterType::Scalar, MaterialParameter.MaterialParameter, MaterialValue))
					{
						float Value = MaterialValue.Value.AsScalar();
						verify(MaterialValue.Value.Type == EMaterialParameterType::Scalar);
						InOutPropertyBag.AddProperties({ PropertyDesc }); // TODO: Better way to add desc?
						InOutPropertyBag.SetValueBool(MaterialParameter.InstanceParameterName, Value > 0.0f);
					}
				}
				break;

				case EMetaHumanRuntimeMaterialParameterType::Scalar:
				{
					PropertyDesc.ValueType = EPropertyBagPropertyType::Float;
					FMaterialParameterMetadata MaterialValue;

					if (InMaterial->GetParameterValue(EMaterialParameterType::Scalar, MaterialParameter.MaterialParameter, MaterialValue))
					{
						float Value = MaterialValue.Value.AsScalar();
						verify(MaterialValue.Value.Type == EMaterialParameterType::Scalar);
						InOutPropertyBag.AddProperties({ PropertyDesc });
						InOutPropertyBag.SetValueFloat(MaterialParameter.InstanceParameterName, Value);
					}
				}
				break;

				case EMetaHumanRuntimeMaterialParameterType::Vector:
				{
					PropertyDesc.ValueType = EPropertyBagPropertyType::Struct;
					PropertyDesc.ValueTypeObject = TBaseStructure<FLinearColor>::Get();
					FMaterialParameterMetadata MaterialValue;

					if (InMaterial->GetParameterValue(EMaterialParameterType::Vector, MaterialParameter.MaterialParameter, MaterialValue))
					{
						FLinearColor Value = MaterialValue.Value.AsLinearColor();
						verify(MaterialValue.Value.Type == EMaterialParameterType::Vector);
						InOutPropertyBag.AddProperties({ PropertyDesc });
						InOutPropertyBag.SetValueStruct(MaterialParameter.InstanceParameterName, Value);
					}
				}
				break;

				case EMetaHumanRuntimeMaterialParameterType::DoubleVector:

				case EMetaHumanRuntimeMaterialParameterType::Texture:
				{
					PropertyDesc.ValueType = EPropertyBagPropertyType::Object;
					PropertyDesc.ValueTypeObject = UTexture::StaticClass();
					FMaterialParameterMetadata MaterialValue;

					if (InMaterial->GetParameterValue(EMaterialParameterType::Texture, MaterialParameter.MaterialParameter, MaterialValue))
					{
						UTexture* Value = Cast<UTexture>(MaterialValue.Value.AsTextureObject());
						verify(MaterialValue.Value.Type == EMaterialParameterType::Texture);
						InOutPropertyBag.AddProperties({ PropertyDesc });
						InOutPropertyBag.SetValueObject(MaterialParameter.InstanceParameterName, Value);
					}
				}
				break;

				case EMetaHumanRuntimeMaterialParameterType::TextureCollection:

				case EMetaHumanRuntimeMaterialParameterType::Font:

				case EMetaHumanRuntimeMaterialParameterType::RuntimeVirtualTexture:

				case EMetaHumanRuntimeMaterialParameterType::SparseVolumeTexture:
					break;

			}

			if (InOutPropertyBag.FindPropertyDescByName(MaterialParameter.InstanceParameterName))
			{
				Count++;
			}
		}

		return Count > 0;
	}

	EMetaHumanRuntimeMaterialParameterType PropertyToParameterType(TNotNull<FProperty*> InProperty)
	{
		if (InProperty->IsA(FBoolProperty::StaticClass()))
		{
			return EMetaHumanRuntimeMaterialParameterType::Toggle;
		}
		else if (InProperty->IsA(FFloatProperty::StaticClass()))
		{
			return EMetaHumanRuntimeMaterialParameterType::Scalar;
		}
		else if (InProperty->IsA(FStructProperty::StaticClass()))
		{
			if (FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
			{
				if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get()
					|| StructProperty->Struct == TBaseStructure<FColor>::Get())
				{
					return EMetaHumanRuntimeMaterialParameterType::Vector;
				}
			}
		}
		else if (InProperty->IsA(FSoftObjectProperty::StaticClass()))
		{
			if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(InProperty))
			{
				if (SoftObjectProperty->PropertyClass->IsChildOf<UTexture>())
				{
					return EMetaHumanRuntimeMaterialParameterType::Texture;
				}
			}
		}
		else if (InProperty->IsA(FObjectProperty::StaticClass()))
		{
			if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(InProperty))
			{
				if (ObjectProperty->PropertyClass->IsChildOf<UTexture>())
				{
					return EMetaHumanRuntimeMaterialParameterType::Texture;
				}
			}
		}

		// TODO: Unsupported type
		checkNoEntry();
		return EMetaHumanRuntimeMaterialParameterType::Scalar;
	}

#if WITH_EDITOR
	TMap<FName, FString> CopyMetadataFromProperty(TNotNull<FProperty*> InProperty)
	{
		TMap<FName, FString> Result;

		if (const TMap<FName, FString>* FoundMetaDataMap = InProperty->GetMetaDataMap())
		{
			Result = *FoundMetaDataMap;
			Result.Remove(FName("ModuleRelativePath"));
		}

		return Result;
	}
#endif

} // namespace UE::MetaHuman::MaterialUtils