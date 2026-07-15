// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Subsystem/MetaHumanCharacterSkinMaterials.h"
#include "MetaHumanCharacterSkin.h"
#include "MetaHumanCharacterMakeup.h"
#include "MetaHumanCharacterEyes.h"
#include "MetaHumanCharacterTeeth.h"
#include "MetaHumanCharacterMaterialSet.h"
#include "Materials/MaterialInstanceDynamic.h"

#if WITH_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FMetaHumanCharacterSkinMaterialsTest, "MetaHumanCreator.SkinMaterials", EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)
	
	/** Snapshot all scalar parameter overrides on a material instance */
	TMap<FName, float> SnapshotScalarParameters(TNotNull<UMaterialInstanceDynamic*> InMaterial) const
	{
		TMap<FName, float> Result;
		TMap<FMaterialParameterInfo, FMaterialParameterMetadata> Parameters;
		InMaterial->GetAllParametersOfType(EMaterialParameterType::Scalar, Parameters);
		for (const TPair<FMaterialParameterInfo, FMaterialParameterMetadata>& Pair : Parameters)
		{
			float Value = 0.0f;
			if (InMaterial->GetScalarParameterValue(Pair.Key.Name, Value))
			{
				Result.Add(Pair.Key.Name, Value);
			}
		}
		return Result;
	}

	/** Snapshot all vector parameter overrides on a material instance */
	TMap<FName, FLinearColor> SnapshotVectorParameters(TNotNull<UMaterialInstanceDynamic*> InMaterial) const
	{
		TMap<FName, FLinearColor> Result;
		TMap<FMaterialParameterInfo, FMaterialParameterMetadata> Parameters;
		InMaterial->GetAllParametersOfType(EMaterialParameterType::Vector, Parameters);
		for (const TPair<FMaterialParameterInfo, FMaterialParameterMetadata>& Pair : Parameters)
		{
			FLinearColor Value = FLinearColor::Black;
			if (InMaterial->GetVectorParameterValue(Pair.Key.Name, Value))
			{
				Result.Add(Pair.Key.Name, Value);
			}
		}
		return Result;
	}

	/** Count how many scalar parameters differ between two snapshots */
	int32 CountScalarDifferences(
		const TMap<FName, float>& InSnapshotA,
		const TMap<FName, float>& InSnapshotB,
		float InTolerance = KINDA_SMALL_NUMBER) const
	{
		int32 DifferenceCount = 0;
		for (const TPair<FName, float>& Pair : InSnapshotA)
		{
			if (const float* OtherValue = InSnapshotB.Find(Pair.Key))
			{
				if (!FMath::IsNearlyEqual(Pair.Value, *OtherValue, InTolerance))
				{
					++DifferenceCount;
				}
			}
		}
		return DifferenceCount;
	}

	/**
	 * Perturb all float properties in a UScriptStruct by a delta, clamping within
	 * the property's metadata range if available. Returns the number of properties modified.
	 */
	int32 PerturbFloatProperties(const UScriptStruct* InStruct, void* InStructData, float InDelta = 0.15f) const
	{
		int32 ModifiedCount = 0;
		for (TFieldIterator<FProperty> PropertyIt(InStruct); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;

			if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
			{
				float& Value = *FloatProperty->ContainerPtrToValuePtr<float>(InStructData);
				const FString ClampMin = FloatProperty->GetMetaData(TEXT("ClampMin"));
				const FString ClampMax = FloatProperty->GetMetaData(TEXT("ClampMax"));
				const float Min = ClampMin.IsEmpty() ? 0.0f : FCString::Atof(*ClampMin);
				const float Max = ClampMax.IsEmpty() ? 1.0f : FCString::Atof(*ClampMax);
				Value = FMath::Clamp(Value + InDelta, Min, Max);
				++ModifiedCount;
			}
			else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
				{
					FLinearColor& Color = *StructProperty->ContainerPtrToValuePtr<FLinearColor>(InStructData);
					Color = FLinearColor(
						FMath::Clamp(Color.R + InDelta, 0.0f, 1.0f),
						FMath::Clamp(Color.G + InDelta, 0.0f, 1.0f),
						FMath::Clamp(Color.B + InDelta, 0.0f, 1.0f),
						Color.A);
					++ModifiedCount;
				}
				else if (StructProperty->Struct->IsNative())
				{
					// Recurse into nested USTRUCTs
					void* NestedData = StructProperty->ContainerPtrToValuePtr<void>(InStructData);
					ModifiedCount += PerturbFloatProperties(StructProperty->Struct, NestedData, InDelta);
				}
			}
			// Skip bool, enum, int32, and other non-visual properties intentionally
		}
		return ModifiedCount;
	}

	// Mapping from struct property name to material parameter name
	struct FPropertyMapping
	{
		FName PropertyName;
		FName MaterialParameterName;
	};

	// Preview material instances — populated by the outer BeforeEach each test
	FMetaHumanCharacterFaceMaterialSet FaceMaterialSet;
	UMaterialInstanceDynamic* BodyMaterial = nullptr;

	// Per-section snapshot state — populated by the inner BeforeEach before each It() block.
	TMap<FName, float> SectionScalarsBefore;
	TMap<FName, float> SectionScalarsAfter;
	TMap<FName, FLinearColor> SectionVectorsBefore;
	TMap<FName, FLinearColor> SectionVectorsAfter;

	/**
	 * Verify that every property on a UScriptStruct is accounted for in either the mapped
	 * or skipped set. Recurses into nested native USTRUCTs.
	 */
	void VerifyAllPropertiesMapped(TNotNull<const UScriptStruct*> InStruct, const TSet<FName>& InMappedNames, const TSet<FName>& InSkippedProperties)
	{
		for (TFieldIterator<FProperty> PropertyIt(InStruct); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			const FName PropertyName = Property->GetFName();

			if (CastField<FFloatProperty>(Property))
			{
				TestTrue(FString::Printf(TEXT("Float property '%s' is in PropertyMap or SkippedProperties"), *PropertyName.ToString()), InMappedNames.Contains(PropertyName) || InSkippedProperties.Contains(PropertyName));
			}
			else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
				{
					TestTrue(FString::Printf(TEXT("FLinearColor property '%s' is in PropertyMap or SkippedProperties"), *PropertyName.ToString()), InMappedNames.Contains(PropertyName) || InSkippedProperties.Contains(PropertyName));
				}
				else if (StructProperty->Struct->GetFName() == TEXT("Vector3f"))
				{
					TestTrue(FString::Printf(TEXT("FVector3f property '%s' is in SkippedProperties"), *PropertyName.ToString()), InSkippedProperties.Contains(PropertyName));
				}
				else if (StructProperty->Struct->IsNative())
				{
					// Recurse into nested native USTRUCTs
					VerifyAllPropertiesMapped(StructProperty->Struct, InMappedNames, InSkippedProperties);
				}
			}
			else if (CastField<FBoolProperty>(Property)
				|| CastField<FIntProperty>(Property)
				|| CastField<FEnumProperty>(Property)
				|| CastField<FByteProperty>(Property))
			{
				// Known non-material types — must be in skip list
				TestTrue(FString::Printf(TEXT("Non-material property '%s' is in SkippedProperties"), *PropertyName.ToString()), InSkippedProperties.Contains(PropertyName));
			}
			else
			{
				TestTrue(FString::Printf(TEXT("Property '%s' has unexpected type '%s'"), *PropertyName.ToString(), *Property->GetCPPType()), false);
			}
		}
	}

	/**
	 * Verify that a mapped material parameter changed between the before and after snapshots.
	 * Automatically determines vector vs scalar from the source struct property type.
	 */
	void VerifyPropertyChanged(const FPropertyMapping& InMapping, TNotNull<const UScriptStruct*> InStruct)
	{
		const FProperty* Prop = InStruct->FindPropertyByName(InMapping.PropertyName);
		const FStructProperty* StructProp = CastField<FStructProperty>(Prop);
		const bool bIsVector = StructProp && StructProp->Struct == TBaseStructure<FLinearColor>::Get();

		if (bIsVector)
		{
			const FLinearColor* BeforeValue = SectionVectorsBefore.Find(InMapping.MaterialParameterName);
			const FLinearColor* AfterValue = SectionVectorsAfter.Find(InMapping.MaterialParameterName);
			TestNotNull(FString::Printf(TEXT("Before value for '%s'"), *InMapping.MaterialParameterName.ToString()), BeforeValue);
			TestNotNull(FString::Printf(TEXT("After value for '%s'"), *InMapping.MaterialParameterName.ToString()), AfterValue);
			if (BeforeValue && AfterValue)
			{
				TestFalse(FString::Printf(TEXT("'%s' changed after perturbation"), *InMapping.MaterialParameterName.ToString()),
					BeforeValue->Equals(*AfterValue));
			}
		}
		else
		{
			const float* BeforeValue = SectionScalarsBefore.Find(InMapping.MaterialParameterName);
			const float* AfterValue = SectionScalarsAfter.Find(InMapping.MaterialParameterName);
			TestNotNull(FString::Printf(TEXT("Before value for '%s'"), *InMapping.MaterialParameterName.ToString()), BeforeValue);
			TestNotNull(FString::Printf(TEXT("After value for '%s'"), *InMapping.MaterialParameterName.ToString()), AfterValue);
			if (BeforeValue && AfterValue)
			{
				TestFalse(FString::Printf(TEXT("'%s' changed after perturbation"), *InMapping.MaterialParameterName.ToString()),
					FMath::IsNearlyEqual(*BeforeValue, *AfterValue));
			}
		}
	}

END_DEFINE_SPEC(FMetaHumanCharacterSkinMaterialsTest)

void FMetaHumanCharacterSkinMaterialsTest::Define()
{
	BeforeEach([this]()
	{
		FaceMaterialSet = FMetaHumanCharacterSkinMaterials::GetHeadPreviewMaterialInstance(EMetaHumanCharacterSkinPreviewMaterial::Editable, /*bInWithVTSupport*/ false);
		BodyMaterial = FMetaHumanCharacterSkinMaterials::GetBodyPreviewMaterialInstance(EMetaHumanCharacterSkinPreviewMaterial::Editable, /*bInWithVTSupport*/ false);
	});

	// NOTE: The following tests follow a similar pattern of validating that a modification to the MHC asset editable properties is being applied to the respective material property.
	// On a high level the steps are:
	// - Setup modified editable properties based on a hard coded property map between editable prop name and material prop name
	// - Validate that no editable properties were missed, if yes the test needs updating
	// - Run tests validating each editable property separately (follow SPEC pattern for reporting)

	// =========================================================================
	// Skin Properties — Body Material 
	// =========================================================================
	Describe("SkinPropertiesOnBodyMaterial", [this]()
	{
		// Mapping from FMetaHumanCharacterSkinProperties field names to material parameter names.
		// Material parameter names match FName::NameToDisplayString (CamelCase -> "Spaced Name").
		// Based on FMetaHumanCharacterSkinMaterials::ApplyPalm/Fingernail/ToenailParametersToMaterial
		// TODO: these should be defined with the FMetaHumanCharacterSkinMaterials, at the moment everything is hard coded in the source files
		static const TArray<FPropertyMapping> PropertyMap = {
			{ TEXT("PalmLightness"),           TEXT("Palm Lightness")           },
			{ TEXT("PalmTint"),                TEXT("Palm Tint")                },
			{ TEXT("PalmCavityDarkness"),      TEXT("Palm Cavity Darkness")     },
			{ TEXT("FingernailTintColor"),     TEXT("Fingernail Tint Color")    },
			{ TEXT("FingernailTintIntensity"), TEXT("Fingernail Tint Intensity") },
			{ TEXT("FingernailMetallic"),      TEXT("Fingernail Metallic")      },
			{ TEXT("FingernailRoughness"),     TEXT("Fingernail Roughness")     },
			{ TEXT("ToenailTintColor"),        TEXT("Toenail Tint Color")       },
			{ TEXT("ToenailTintIntensity"),    TEXT("Toenail Tint Intensity")   },
			{ TEXT("ToenailMetallic"),         TEXT("Toenail Metallic")         },
			{ TEXT("ToenailRoughness"),        TEXT("Toenail Roughness")        },
		};

		// Properties that exist on the struct but do not map to material parameters
		static const TSet<FName> SkippedProperties = {
			TEXT("U"),
			TEXT("V"),
			TEXT("BodyBias"),
			TEXT("BodyGain"),
			TEXT("bShowTopUnderwear"),
			TEXT("BodyTextureIndex"),
			TEXT("FaceTextureIndex"),
			TEXT("Roughness"),  // Tested separately in SkinPropertiesRoughness
		};

		BeforeEach([this]()
		{
			TestNotNull(TEXT("Body material"), BodyMaterial);
			if (!BodyMaterial)
			{
				return;
			}

			SectionScalarsBefore.Reset();
			SectionScalarsAfter.Reset();
			SectionVectorsBefore.Reset();
			SectionVectorsAfter.Reset();

			// Apply default properties and snapshot
			FMetaHumanCharacterSkinProperties DefaultProperties;
			FMetaHumanCharacterSkinMaterials::ApplyPalmParametersToMaterial(BodyMaterial, DefaultProperties);
			FMetaHumanCharacterSkinMaterials::ApplyFingernailParametersToMaterial(BodyMaterial, DefaultProperties);
			FMetaHumanCharacterSkinMaterials::ApplyToenailParametersToMaterial(BodyMaterial, DefaultProperties);
			SectionScalarsBefore = SnapshotScalarParameters(BodyMaterial);
			SectionVectorsBefore = SnapshotVectorParameters(BodyMaterial);

			// Perturb all properties via reflection and apply
			FMetaHumanCharacterSkinProperties ModifiedProperties = DefaultProperties;
			PerturbFloatProperties(
				FMetaHumanCharacterSkinProperties::StaticStruct(), &ModifiedProperties, 0.15f);
			FMetaHumanCharacterSkinMaterials::ApplyPalmParametersToMaterial(BodyMaterial, ModifiedProperties);
			FMetaHumanCharacterSkinMaterials::ApplyFingernailParametersToMaterial(BodyMaterial, ModifiedProperties);
			FMetaHumanCharacterSkinMaterials::ApplyToenailParametersToMaterial(BodyMaterial, ModifiedProperties);
			SectionScalarsAfter = SnapshotScalarParameters(BodyMaterial);
			SectionVectorsAfter = SnapshotVectorParameters(BodyMaterial);


		});

		// Validate that all reflected properties are accounted for in the map or skip list
		// If a property is added or remove, this test will fail and the PropertyMap and SkippedProperties will need to be updated
		It("should have mappings for all float and color properties", [this]()
		{
			TSet<FName> MappedNames;
			for (const FPropertyMapping& Entry : PropertyMap)
			{
				MappedNames.Add(Entry.PropertyName);
			}
			VerifyAllPropertiesMapped(FMetaHumanCharacterSkinProperties::StaticStruct(), MappedNames, SkippedProperties);
		});

		// One It() per mapped property — verify material parameter changed
		for (const FPropertyMapping& Mapping : PropertyMap)
		{
			It(FString::Printf(TEXT("should apply %s to material parameter %s"),
				*Mapping.PropertyName.ToString(), *Mapping.MaterialParameterName.ToString()),
			[this, Mapping]()
			{
				VerifyPropertyChanged(Mapping, FMetaHumanCharacterSkinProperties::StaticStruct());
			});
		}
	});

	// =========================================================================
	// Skin Properties — Roughness (face + body)
	// =========================================================================
	Describe("SkinPropertiesRoughness", [this]()
	{
		It("should apply roughness to both face skin and body materials", [this]()
		{
			const TObjectPtr<UMaterialInstance>* FoundSkinMaterial = FaceMaterialSet.Skin.Find(EMetaHumanCharacterSkinMaterialSlot::LOD0);
			UMaterialInstanceDynamic* SkinMaterial = FoundSkinMaterial ? Cast<UMaterialInstanceDynamic>(*FoundSkinMaterial) : nullptr;
			TestNotNull(TEXT("Skin LOD0 material"), SkinMaterial);
			TestNotNull(TEXT("Body material"), BodyMaterial);
			if (!SkinMaterial || !BodyMaterial)
			{
				return;
			}

			// Apply defaults and snapshot
			FMetaHumanCharacterSkinSettings DefaultSettings;
			FMetaHumanCharacterSkinMaterials::ApplyRoughnessMultiplyToMaterials(FaceMaterialSet, BodyMaterial, DefaultSettings);
			const TMap<FName, float> FaceScalarsBefore = SnapshotScalarParameters(SkinMaterial);
			const TMap<FName, float> BodyScalarsBefore = SnapshotScalarParameters(BodyMaterial);

			// Perturb roughness via reflection on the whole settings struct
			FMetaHumanCharacterSkinSettings ModifiedSettings = DefaultSettings;
			PerturbFloatProperties(FMetaHumanCharacterSkinProperties::StaticStruct(), &ModifiedSettings.Skin, 0.05f);
			FMetaHumanCharacterSkinMaterials::ApplyRoughnessMultiplyToMaterials(FaceMaterialSet, BodyMaterial, ModifiedSettings);
			const TMap<FName, float> FaceScalarsAfter = SnapshotScalarParameters(SkinMaterial);
			const TMap<FName, float> BodyScalarsAfter = SnapshotScalarParameters(BodyMaterial);

			TestTrue(TEXT("Face skin material roughness parameter changed"),
				CountScalarDifferences(FaceScalarsBefore, FaceScalarsAfter) > 0);
			TestTrue(TEXT("Body material roughness parameter changed"),
				CountScalarDifferences(BodyScalarsBefore, BodyScalarsAfter) > 0);

			// Round-trip: applying defaults restores original state
			FMetaHumanCharacterSkinMaterials::ApplyRoughnessMultiplyToMaterials(FaceMaterialSet, BodyMaterial, DefaultSettings);
			const TMap<FName, float> FaceScalarsRestored = SnapshotScalarParameters(SkinMaterial);
			TestEqual(TEXT("Face parameters restored after round-trip"),
				CountScalarDifferences(FaceScalarsBefore, FaceScalarsRestored), 0);

			const TMap<FName, float> BodyScalarsRestored = SnapshotScalarParameters(BodyMaterial);
			TestEqual(TEXT("Body parameters restored after round-trip"),
				CountScalarDifferences(BodyScalarsBefore, BodyScalarsRestored), 0);
		});
	});

	// =========================================================================
	// Skin Accents
	// =========================================================================
	Describe("SkinAccents", [this]()
	{
		It("should apply all accent region parameters to face skin material", [this]()
		{
			const TObjectPtr<UMaterialInstance>* FoundSkinMaterial = FaceMaterialSet.Skin.Find(EMetaHumanCharacterSkinMaterialSlot::LOD0);
			UMaterialInstanceDynamic* SkinMaterial = FoundSkinMaterial ? Cast<UMaterialInstanceDynamic>(*FoundSkinMaterial) : nullptr;
			TestNotNull(TEXT("Skin LOD0 material"), SkinMaterial);
			if (!SkinMaterial)
			{
				return;
			}

			FMetaHumanCharacterAccentRegions DefaultAccents;
			FMetaHumanCharacterSkinMaterials::ApplySkinAccentsToMaterial(FaceMaterialSet, DefaultAccents);
			const TMap<FName, float> ScalarsBefore = SnapshotScalarParameters(SkinMaterial);

			FMetaHumanCharacterAccentRegions ModifiedAccents;
			PerturbFloatProperties(FMetaHumanCharacterAccentRegions::StaticStruct(), &ModifiedAccents, 0.2f);
			FMetaHumanCharacterSkinMaterials::ApplySkinAccentsToMaterial(FaceMaterialSet, ModifiedAccents);
			const TMap<FName, float> ScalarsAfter = SnapshotScalarParameters(SkinMaterial);

			// 8 regions x 3 parameters = 24 expected changes
			const int32 ScalarDifferences = CountScalarDifferences(ScalarsBefore, ScalarsAfter);
			TestTrue(TEXT("At least 20 accent parameters changed on face material"), ScalarDifferences >= 20);

			// Round-trip
			FMetaHumanCharacterSkinMaterials::ApplySkinAccentsToMaterial(FaceMaterialSet, DefaultAccents);
			const TMap<FName, float> ScalarsRestored = SnapshotScalarParameters(SkinMaterial);
			TestEqual(TEXT("Accent parameters restored after round-trip"),
				CountScalarDifferences(ScalarsBefore, ScalarsRestored), 0);
		});
	});

	// =========================================================================
	// Skin Freckles
	// =========================================================================
	Describe("SkinFreckles", [this]()
	{
		static const TArray<FPropertyMapping> PropertyMap = {
			{ TEXT("Density"),   TEXT("FrecklesDensity")    },
			{ TEXT("Strength"),  TEXT("FrecklesStrength")   },
			{ TEXT("Saturation"),TEXT("FrecklesSaturation") },
			{ TEXT("ToneShift"), TEXT("FrecklesToneShift")  },
		};

		static const TSet<FName> SkippedProperties = {
			TEXT("Mask"),
		};

		BeforeEach([this]()
		{
			SectionScalarsBefore.Reset();
			SectionScalarsAfter.Reset();
			SectionVectorsBefore.Reset();
			SectionVectorsAfter.Reset();

			const TObjectPtr<UMaterialInstance>* FoundSkinMaterial = FaceMaterialSet.Skin.Find(EMetaHumanCharacterSkinMaterialSlot::LOD0);
			UMaterialInstanceDynamic* SkinMaterial = FoundSkinMaterial ? Cast<UMaterialInstanceDynamic>(*FoundSkinMaterial) : nullptr;
			TestNotNull(TEXT("Skin LOD0 material"), SkinMaterial);
			if (!SkinMaterial)
			{
				return;
			}

			FMetaHumanCharacterFrecklesProperties DefaultFreckles;
			FMetaHumanCharacterSkinMaterials::ApplyFrecklesToMaterial(FaceMaterialSet, DefaultFreckles);
			SectionScalarsBefore = SnapshotScalarParameters(SkinMaterial);
			SectionVectorsBefore = SnapshotVectorParameters(SkinMaterial);

			FMetaHumanCharacterFrecklesProperties ModifiedFreckles;
			ModifiedFreckles.Mask = EMetaHumanCharacterFrecklesMask::Type1;
			PerturbFloatProperties(FMetaHumanCharacterFrecklesProperties::StaticStruct(), &ModifiedFreckles, 0.2f);
			FMetaHumanCharacterSkinMaterials::ApplyFrecklesToMaterial(FaceMaterialSet, ModifiedFreckles);
			SectionScalarsAfter = SnapshotScalarParameters(SkinMaterial);
			SectionVectorsAfter = SnapshotVectorParameters(SkinMaterial);
		});

		It("should have mappings for all freckles properties", [this]()
		{
			TSet<FName> MappedNames;
			for (const FPropertyMapping& Entry : PropertyMap)
			{
				MappedNames.Add(Entry.PropertyName);
			}
			VerifyAllPropertiesMapped(FMetaHumanCharacterFrecklesProperties::StaticStruct(), MappedNames, SkippedProperties);
		});

		for (const FPropertyMapping& Mapping : PropertyMap)
		{
			It(FString::Printf(TEXT("should apply %s to material parameter %s"),
				*Mapping.PropertyName.ToString(), *Mapping.MaterialParameterName.ToString()),
			[this, Mapping]()
			{
				VerifyPropertyChanged(Mapping, FMetaHumanCharacterFrecklesProperties::StaticStruct());
			});
		}
	});

	// =========================================================================
	// Makeup Parameters
	// =========================================================================
	Describe("MakeupFoundation", [this]()
	{
		static const TArray<FPropertyMapping> PropertyMap = {
			{ TEXT("Color"),     TEXT("Makeup Foundation Color")   },
			{ TEXT("Intensity"), TEXT("Makeup Foundation Opacity")  },
			{ TEXT("Roughness"), TEXT("Makeup Foundation Roughness") },
			{ TEXT("Concealer"), TEXT("Makeup Concealer Opacity")   },
		};

		static const TSet<FName> SkippedProperties = {
			TEXT("bApplyFoundation"),
			TEXT("ConcealerColor"),  // Derived internally from Color
			TEXT("PresetIndex"),
		};

		BeforeEach([this]()
		{
			SectionScalarsBefore.Reset();
			SectionScalarsAfter.Reset();
			SectionVectorsBefore.Reset();
			SectionVectorsAfter.Reset();

			const TObjectPtr<UMaterialInstance>* FoundSkinMaterial = FaceMaterialSet.Skin.Find(EMetaHumanCharacterSkinMaterialSlot::LOD0);
			UMaterialInstanceDynamic* SkinMaterial = FoundSkinMaterial ? Cast<UMaterialInstanceDynamic>(*FoundSkinMaterial) : nullptr;
			TestNotNull(TEXT("Skin LOD0 material"), SkinMaterial);
			if (!SkinMaterial)
			{
				return;
			}

			FMetaHumanCharacterFoundationMakeupProperties DefaultFoundation;
			FMetaHumanCharacterSkinMaterials::ApplyFoundationMakeupToMaterial(FaceMaterialSet, DefaultFoundation, /*bInWithVTSupport*/ false);
			SectionScalarsBefore = SnapshotScalarParameters(SkinMaterial);
			SectionVectorsBefore = SnapshotVectorParameters(SkinMaterial);

			FMetaHumanCharacterFoundationMakeupProperties ModifiedFoundation;
			ModifiedFoundation.bApplyFoundation = true;
			PerturbFloatProperties(
				FMetaHumanCharacterFoundationMakeupProperties::StaticStruct(), &ModifiedFoundation, 0.2f);
			FMetaHumanCharacterSkinMaterials::ApplyFoundationMakeupToMaterial(FaceMaterialSet, ModifiedFoundation, /*bInWithVTSupport*/ false);
			SectionScalarsAfter = SnapshotScalarParameters(SkinMaterial);
			SectionVectorsAfter = SnapshotVectorParameters(SkinMaterial);


		});

		It("should have mappings for all foundation properties", [this]()
		{
			TSet<FName> MappedNames;
			for (const FPropertyMapping& Entry : PropertyMap)
			{
				MappedNames.Add(Entry.PropertyName);
			}
			VerifyAllPropertiesMapped(FMetaHumanCharacterFoundationMakeupProperties::StaticStruct(), MappedNames, SkippedProperties);
		});

		for (const FPropertyMapping& Mapping : PropertyMap)
		{
			It(FString::Printf(TEXT("should apply %s to material parameter %s"),
				*Mapping.PropertyName.ToString(), *Mapping.MaterialParameterName.ToString()),
			[this, Mapping]()
			{
				VerifyPropertyChanged(Mapping, FMetaHumanCharacterFoundationMakeupProperties::StaticStruct());
			});
		}
	});

	Describe("MakeupEyeMakeup", [this]()
	{
		static const TArray<FPropertyMapping> PropertyMap = {
			{ TEXT("PrimaryColor"),       TEXT("Makeup Eye Primary Color")       },
			{ TEXT("Opacity"),            TEXT("Makeup Eye Primary Opacity")     },
			{ TEXT("Roughness"),          TEXT("Makeup Eye Primary Roughness")   },
			{ TEXT("Metalness"),          TEXT("Makeup Eye Primary Metallic")    },
			{ TEXT("SecondaryColor"),     TEXT("Makeup Eye Secondary Color")     },
			{ TEXT("SecondaryOpacity"),   TEXT("Makeup Eye Secondary Opacity")   },
			{ TEXT("SecondaryRoughness"), TEXT("Makeup Eye Secondary Roughness") },
			{ TEXT("SecondaryMetalness"), TEXT("Makeup Eye Secondary Metallic")  },
		};

		static const TSet<FName> SkippedProperties = {
			TEXT("Type"),
		};

		BeforeEach([this]()
		{
			SectionScalarsBefore.Reset();
			SectionScalarsAfter.Reset();
			SectionVectorsBefore.Reset();
			SectionVectorsAfter.Reset();

			const TObjectPtr<UMaterialInstance>* FoundSkinMaterial = FaceMaterialSet.Skin.Find(EMetaHumanCharacterSkinMaterialSlot::LOD0);
			UMaterialInstanceDynamic* SkinMaterial = FoundSkinMaterial ? Cast<UMaterialInstanceDynamic>(*FoundSkinMaterial) : nullptr;
			TestNotNull(TEXT("Skin LOD0 material"), SkinMaterial);
			if (!SkinMaterial)
			{
				return;
			}

			FMetaHumanCharacterEyeMakeupProperties DefaultEyeMakeup;
			FMetaHumanCharacterSkinMaterials::ApplyEyeMakeupToMaterial(FaceMaterialSet, DefaultEyeMakeup, /*bInWithVTSupport*/ false);
			SectionScalarsBefore = SnapshotScalarParameters(SkinMaterial);
			SectionVectorsBefore = SnapshotVectorParameters(SkinMaterial);

			FMetaHumanCharacterEyeMakeupProperties ModifiedEyeMakeup;
			ModifiedEyeMakeup.Type = EMetaHumanCharacterEyeMakeupType::ThinLiner;
			PerturbFloatProperties(
				FMetaHumanCharacterEyeMakeupProperties::StaticStruct(), &ModifiedEyeMakeup, 0.15f);
			FMetaHumanCharacterSkinMaterials::ApplyEyeMakeupToMaterial(FaceMaterialSet, ModifiedEyeMakeup, /*bInWithVTSupport*/ false);
			SectionScalarsAfter = SnapshotScalarParameters(SkinMaterial);
			SectionVectorsAfter = SnapshotVectorParameters(SkinMaterial);
		});

		It("should have mappings for all eye makeup properties", [this]()
		{
			TSet<FName> MappedNames;
			for (const FPropertyMapping& Entry : PropertyMap)
			{
				MappedNames.Add(Entry.PropertyName);
			}
			VerifyAllPropertiesMapped(FMetaHumanCharacterEyeMakeupProperties::StaticStruct(), MappedNames, SkippedProperties);
		});

		for (const FPropertyMapping& Mapping : PropertyMap)
		{
			It(FString::Printf(TEXT("should apply %s to material parameter %s"),
				*Mapping.PropertyName.ToString(), *Mapping.MaterialParameterName.ToString()),
			[this, Mapping]()
			{
				VerifyPropertyChanged(Mapping, FMetaHumanCharacterEyeMakeupProperties::StaticStruct());
			});
		}
	});

	Describe("MakeupBlush", [this]()
	{
		static const TArray<FPropertyMapping> PropertyMap = {
			{ TEXT("Color"),     TEXT("Makeup Blusher Color")     },
			{ TEXT("Intensity"), TEXT("Makeup Blusher Opacity")   },
			{ TEXT("Roughness"), TEXT("Makeup Blusher Roughness") },
		};

		static const TSet<FName> SkippedProperties = {
			TEXT("Type"),
		};

		BeforeEach([this]()
		{
			SectionScalarsBefore.Reset();
			SectionScalarsAfter.Reset();
			SectionVectorsBefore.Reset();
			SectionVectorsAfter.Reset();

			const TObjectPtr<UMaterialInstance>* FoundSkinMaterial = FaceMaterialSet.Skin.Find(EMetaHumanCharacterSkinMaterialSlot::LOD0);
			UMaterialInstanceDynamic* SkinMaterial = FoundSkinMaterial ? Cast<UMaterialInstanceDynamic>(*FoundSkinMaterial) : nullptr;
			TestNotNull(TEXT("Skin LOD0 material"), SkinMaterial);
			if (!SkinMaterial)
			{
				return;
			}

			FMetaHumanCharacterBlushMakeupProperties DefaultBlush;
			FMetaHumanCharacterSkinMaterials::ApplyBlushMakeupToMaterial(FaceMaterialSet, DefaultBlush, /*bInWithVTSupport*/ false);
			SectionScalarsBefore = SnapshotScalarParameters(SkinMaterial);
			SectionVectorsBefore = SnapshotVectorParameters(SkinMaterial);

			FMetaHumanCharacterBlushMakeupProperties ModifiedBlush;
			ModifiedBlush.Type = EMetaHumanCharacterBlushMakeupType::Angled;
			PerturbFloatProperties(
				FMetaHumanCharacterBlushMakeupProperties::StaticStruct(), &ModifiedBlush, 0.2f);
			FMetaHumanCharacterSkinMaterials::ApplyBlushMakeupToMaterial(FaceMaterialSet, ModifiedBlush, /*bInWithVTSupport*/ false);
			SectionScalarsAfter = SnapshotScalarParameters(SkinMaterial);
			SectionVectorsAfter = SnapshotVectorParameters(SkinMaterial);
		});

		It("should have mappings for all blush properties", [this]()
		{
			TSet<FName> MappedNames;
			for (const FPropertyMapping& Entry : PropertyMap)
			{
				MappedNames.Add(Entry.PropertyName);
			}
			VerifyAllPropertiesMapped(FMetaHumanCharacterBlushMakeupProperties::StaticStruct(), MappedNames, SkippedProperties);
		});

		for (const FPropertyMapping& Mapping : PropertyMap)
		{
			It(FString::Printf(TEXT("should apply %s to material parameter %s"),
				*Mapping.PropertyName.ToString(), *Mapping.MaterialParameterName.ToString()),
			[this, Mapping]()
			{
				VerifyPropertyChanged(Mapping, FMetaHumanCharacterBlushMakeupProperties::StaticStruct());
			});
		}
	});

	Describe("MakeupLips", [this]()
	{
		static const TArray<FPropertyMapping> PropertyMap = {
			{ TEXT("Color"),     TEXT("Makeup Lipstick Color")     },
			{ TEXT("Opacity"),   TEXT("Makeup Lipstick Opacity")   },
			{ TEXT("Roughness"), TEXT("Makeup Lipstick Roughness") },
			{ TEXT("Metalness"), TEXT("Makeup Lipstick Metallic")  },
		};

		static const TSet<FName> SkippedProperties = {
			TEXT("Type"),
		};

		BeforeEach([this]()
		{
			SectionScalarsBefore.Reset();
			SectionScalarsAfter.Reset();
			SectionVectorsBefore.Reset();
			SectionVectorsAfter.Reset();

			const TObjectPtr<UMaterialInstance>* FoundSkinMaterial = FaceMaterialSet.Skin.Find(EMetaHumanCharacterSkinMaterialSlot::LOD0);
			UMaterialInstanceDynamic* SkinMaterial = FoundSkinMaterial ? Cast<UMaterialInstanceDynamic>(*FoundSkinMaterial) : nullptr;
			TestNotNull(TEXT("Skin LOD0 material"), SkinMaterial);
			if (!SkinMaterial)
			{
				return;
			}

			FMetaHumanCharacterLipsMakeupProperties DefaultLips;
			FMetaHumanCharacterSkinMaterials::ApplyLipsMakeupToMaterial(FaceMaterialSet, DefaultLips, /*bInWithVTSupport*/ false);
			SectionScalarsBefore = SnapshotScalarParameters(SkinMaterial);
			SectionVectorsBefore = SnapshotVectorParameters(SkinMaterial);

			FMetaHumanCharacterLipsMakeupProperties ModifiedLips;
			ModifiedLips.Type = EMetaHumanCharacterLipsMakeupType::Natural;
			PerturbFloatProperties(
				FMetaHumanCharacterLipsMakeupProperties::StaticStruct(), &ModifiedLips, -0.15f);
			FMetaHumanCharacterSkinMaterials::ApplyLipsMakeupToMaterial(FaceMaterialSet, ModifiedLips, /*bInWithVTSupport*/ false);
			SectionScalarsAfter = SnapshotScalarParameters(SkinMaterial);
			SectionVectorsAfter = SnapshotVectorParameters(SkinMaterial);
		});

		It("should have mappings for all lips properties", [this]()
		{
			TSet<FName> MappedNames;
			for (const FPropertyMapping& Entry : PropertyMap)
			{
				MappedNames.Add(Entry.PropertyName);
			}
			VerifyAllPropertiesMapped(FMetaHumanCharacterLipsMakeupProperties::StaticStruct(), MappedNames, SkippedProperties);
		});

		for (const FPropertyMapping& Mapping : PropertyMap)
		{
			It(FString::Printf(TEXT("should apply %s to material parameter %s"),
				*Mapping.PropertyName.ToString(), *Mapping.MaterialParameterName.ToString()),
			[this, Mapping]()
			{
				VerifyPropertyChanged(Mapping, FMetaHumanCharacterLipsMakeupProperties::StaticStruct());
			});
		}
	});

	// =========================================================================
	// Eye Parameters
	// =========================================================================
	Describe("EyeIris", [this]()
	{
		static const TArray<FPropertyMapping> PropertyMap = {
			{ TEXT("IrisRotation"),     TEXT("Iris Rotation")                      },
			{ TEXT("PrimaryColorU"),    TEXT("Iris Primary Color Hue")             },
			{ TEXT("PrimaryColorV"),    TEXT("Iris Primary Color Value")           },
			{ TEXT("SecondaryColorU"),  TEXT("Iris Secondary Color Hue")           },
			{ TEXT("SecondaryColorV"),  TEXT("Iris Secondary Color Value")         },
			{ TEXT("ColorBlend"),       TEXT("Iris Color Blend Coverage")          },
			{ TEXT("ColorBlendSoftness"),TEXT("Iris Color Blend Coverage Softness") },
			{ TEXT("ShadowDetails"),    TEXT("Iris Shadow Details Amount")         },
			{ TEXT("LimbalRingSize"),   TEXT("Limbal Ring Size")                   },
			{ TEXT("LimbalRingSoftness"),TEXT("Limbal Ring Softness")               },
			{ TEXT("LimbalRingColor"),  TEXT("Limbal Ring Color (Mult)")           },
			{ TEXT("GlobalSaturation"), TEXT("Iris Global Saturation")             },
			{ TEXT("GlobalTint"),       TEXT("Iris Color Multiply")                },
		};

		static const TSet<FName> SkippedProperties = {
			TEXT("IrisPattern"),  // Enum, controls texture selection
			TEXT("BlendMethod"), // Enum, cast to float for "Iris Color Blend Switch"
		};

		BeforeEach([this]()
		{
			SectionScalarsBefore.Reset();
			SectionScalarsAfter.Reset();
			SectionVectorsBefore.Reset();
			SectionVectorsAfter.Reset();

			UMaterialInstanceDynamic* LeftEyeMaterial = Cast<UMaterialInstanceDynamic>(FaceMaterialSet.EyeLeft);
			TestNotNull(TEXT("Left eye material"), LeftEyeMaterial);
			if (!LeftEyeMaterial)
			{
				return;
			}

			FMetaHumanCharacterEyeIrisProperties MidRangeIris;
			MidRangeIris.LimbalRingColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
			MidRangeIris.GlobalTint = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);

			FMetaHumanCharacterEyesSettings DefaultSettings;
			DefaultSettings.EyeLeft.Iris = MidRangeIris;
			DefaultSettings.EyeRight.Iris = MidRangeIris;
			FMetaHumanCharacterSkinMaterials::ApplyEyeSettingsToMaterial(FaceMaterialSet, DefaultSettings);
			SectionScalarsBefore = SnapshotScalarParameters(LeftEyeMaterial);
			SectionVectorsBefore = SnapshotVectorParameters(LeftEyeMaterial);

			FMetaHumanCharacterEyesSettings ModifiedSettings = DefaultSettings;
			PerturbFloatProperties(
				FMetaHumanCharacterEyeIrisProperties::StaticStruct(),
				&ModifiedSettings.EyeLeft.Iris, 0.1f);
			PerturbFloatProperties(
				FMetaHumanCharacterEyeIrisProperties::StaticStruct(),
				&ModifiedSettings.EyeRight.Iris, 0.2f);
			FMetaHumanCharacterSkinMaterials::ApplyEyeSettingsToMaterial(FaceMaterialSet, ModifiedSettings);
			SectionScalarsAfter = SnapshotScalarParameters(LeftEyeMaterial);
			SectionVectorsAfter = SnapshotVectorParameters(LeftEyeMaterial);


		});

		It("should have mappings for all iris properties", [this]()
		{
			TSet<FName> MappedNames;
			for (const FPropertyMapping& Entry : PropertyMap)
			{
				MappedNames.Add(Entry.PropertyName);
			}
			VerifyAllPropertiesMapped(FMetaHumanCharacterEyeIrisProperties::StaticStruct(), MappedNames, SkippedProperties);
		});

		for (const FPropertyMapping& Mapping : PropertyMap)
		{
			It(FString::Printf(TEXT("should apply %s to material parameter %s"),
				*Mapping.PropertyName.ToString(), *Mapping.MaterialParameterName.ToString()),
			[this, Mapping]()
			{
				VerifyPropertyChanged(Mapping, FMetaHumanCharacterEyeIrisProperties::StaticStruct());
			});
		}

		It("should apply different iris values to left and right eyes", [this]()
		{
			UMaterialInstanceDynamic* LeftEyeMaterial = Cast<UMaterialInstanceDynamic>(FaceMaterialSet.EyeLeft);
			UMaterialInstanceDynamic* RightEyeMaterial = Cast<UMaterialInstanceDynamic>(FaceMaterialSet.EyeRight);
			TestNotNull(TEXT("Left eye material"), LeftEyeMaterial);
			TestNotNull(TEXT("Right eye material"), RightEyeMaterial);
			if (!LeftEyeMaterial || !RightEyeMaterial)
			{
				return;
			}

			// Apply different iris perturbations to each eye independently — do not rely on shared BeforeEach state
			FMetaHumanCharacterEyeIrisProperties MidRangeIris;
			MidRangeIris.LimbalRingColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
			MidRangeIris.GlobalTint = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);

			FMetaHumanCharacterEyesSettings Settings;
			Settings.EyeLeft.Iris = MidRangeIris;
			Settings.EyeRight.Iris = MidRangeIris;
			PerturbFloatProperties(FMetaHumanCharacterEyeIrisProperties::StaticStruct(), &Settings.EyeLeft.Iris, 0.1f);
			PerturbFloatProperties(FMetaHumanCharacterEyeIrisProperties::StaticStruct(), &Settings.EyeRight.Iris, 0.2f);
			FMetaHumanCharacterSkinMaterials::ApplyEyeSettingsToMaterial(FaceMaterialSet, Settings);

			const TMap<FName, float> LeftScalars = SnapshotScalarParameters(LeftEyeMaterial);
			const TMap<FName, float> RightScalars = SnapshotScalarParameters(RightEyeMaterial);
			TestTrue(TEXT("Left and right eye have different iris parameters"), CountScalarDifferences(LeftScalars, RightScalars) > 0);
		});
	});

	Describe("EyePupilCorneaSclera", [this]()
	{
		static const TArray<FPropertyMapping> PropertyMap = {
			// Pupil
			{ TEXT("Dilation"),             TEXT("Pupil Dilation")                   },
			{ TEXT("Feather"),              TEXT("Pupil Feather Strength")           },
			// Cornea
			{ TEXT("Size"),                 TEXT("Cornea Size")                      },
			{ TEXT("LimbusSoftness"),       TEXT("Corneal Limbus Softness")          },
			{ TEXT("LimbusColor"),          TEXT("Corneal Limbus Color (Mult)")      },
			// Sclera
			{ TEXT("Rotation"),             TEXT("Sclera Rotation")                  },
			{ TEXT("Tint"),                 TEXT("Sclera Color Multiply")            },
			{ TEXT("TransmissionSpread"),   TEXT("Sclera Transmission Spread")       },
			{ TEXT("TransmissionColor"),    TEXT("Sclera Transmission Color (Mult)") },
			{ TEXT("VascularityIntensity"), TEXT("Sclera Irritation Veins Opacity")  },
			{ TEXT("VascularityCoverage"),  TEXT("Sclera Irritation Area Size")      },
		};

		static const TSet<FName> SkippedProperties = {
			TEXT("bUseCustomTint"), // Bool, conditional sclera tint
		};

		BeforeEach([this]()
		{
			SectionScalarsBefore.Reset();
			SectionScalarsAfter.Reset();
			SectionVectorsBefore.Reset();
			SectionVectorsAfter.Reset();

			UMaterialInstanceDynamic* LeftEyeMaterial = Cast<UMaterialInstanceDynamic>(FaceMaterialSet.EyeLeft);
			TestNotNull(TEXT("Left eye material"), LeftEyeMaterial);
			if (!LeftEyeMaterial)
			{
				return;
			}

			// Pre-set boundary colors to mid-range so perturbation has room to move them
			FMetaHumanCharacterEyesSettings DefaultSettings;
			DefaultSettings.EyeLeft.Cornea.LimbusColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
			DefaultSettings.EyeLeft.Sclera.Tint = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
			DefaultSettings.EyeLeft.Sclera.TransmissionColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
			DefaultSettings.EyeLeft.Sclera.Rotation = 0.5f;
			FMetaHumanCharacterSkinMaterials::ApplyEyeSettingsToMaterial(FaceMaterialSet, DefaultSettings);
			SectionScalarsBefore = SnapshotScalarParameters(LeftEyeMaterial);
			SectionVectorsBefore = SnapshotVectorParameters(LeftEyeMaterial);

			FMetaHumanCharacterEyesSettings ModifiedSettings = DefaultSettings;
			PerturbFloatProperties(
				FMetaHumanCharacterEyePupilProperties::StaticStruct(),
				&ModifiedSettings.EyeLeft.Pupil, -0.05f);
			PerturbFloatProperties(
				FMetaHumanCharacterEyeCorneaProperties::StaticStruct(),
				&ModifiedSettings.EyeLeft.Cornea, 0.02f);
			PerturbFloatProperties(
				FMetaHumanCharacterEyeScleraProperties::StaticStruct(),
				&ModifiedSettings.EyeLeft.Sclera, 0.1f);
			FMetaHumanCharacterSkinMaterials::ApplyEyeSettingsToMaterial(FaceMaterialSet, ModifiedSettings);
			SectionScalarsAfter = SnapshotScalarParameters(LeftEyeMaterial);
			SectionVectorsAfter = SnapshotVectorParameters(LeftEyeMaterial);


		});

		It("should have mappings for all pupil, cornea, and sclera properties", [this]()
		{
			TSet<FName> MappedNames;
			for (const FPropertyMapping& Entry : PropertyMap)
			{
				MappedNames.Add(Entry.PropertyName);
			}
			VerifyAllPropertiesMapped(FMetaHumanCharacterEyePupilProperties::StaticStruct(), MappedNames, SkippedProperties);
			VerifyAllPropertiesMapped(FMetaHumanCharacterEyeCorneaProperties::StaticStruct(), MappedNames, SkippedProperties);
			VerifyAllPropertiesMapped(FMetaHumanCharacterEyeScleraProperties::StaticStruct(), MappedNames, SkippedProperties);
		});

		for (const FPropertyMapping& Mapping : PropertyMap)
		{
			It(FString::Printf(TEXT("should apply %s to material parameter %s"),
				*Mapping.PropertyName.ToString(), *Mapping.MaterialParameterName.ToString()),
			[this, Mapping]()
			{
				// Search the three sub-structs to find the property and determine its type
				for (const UScriptStruct* SubStruct : {
					FMetaHumanCharacterEyePupilProperties::StaticStruct(),
					FMetaHumanCharacterEyeCorneaProperties::StaticStruct(),
					FMetaHumanCharacterEyeScleraProperties::StaticStruct() })
				{
					if (SubStruct->FindPropertyByName(Mapping.PropertyName))
					{
						VerifyPropertyChanged(Mapping, SubStruct);
						return;
					}
				}
				TestTrue(FString::Printf(TEXT("Property '%s' is found in a known eye sub-struct"), *Mapping.PropertyName.ToString()), false);
			});
		}
	});

	// =========================================================================
	// Head Model Parameters
	// =========================================================================
	Describe("Eyelashes", [this]()
	{
		static const TArray<FPropertyMapping> PropertyMap = {
			{ TEXT("DyeColor"),  TEXT("DyeColor")    },
			{ TEXT("Roughness"), TEXT("Roughness")   },
			{ TEXT("Melanin"),   TEXT("HairMelanin") },
			{ TEXT("Redness"),   TEXT("HairRedness") },
		};

		static const TSet<FName> SkippedProperties = {
			TEXT("Type"),
			TEXT("bEnableGrooms"),
			TEXT("SaltAndPepper"),
			TEXT("Lightness"),
		};

		BeforeEach([this]()
		{
			SectionScalarsBefore.Reset();
			SectionScalarsAfter.Reset();
			SectionVectorsBefore.Reset();
			SectionVectorsAfter.Reset();

			UMaterialInstanceDynamic* EyelashesMaterial = Cast<UMaterialInstanceDynamic>(FaceMaterialSet.Eyelashes);
			TestNotNull(TEXT("Eyelashes material"), EyelashesMaterial);
			if (!EyelashesMaterial)
			{
				return;
			}

			FMetaHumanCharacterEyelashesProperties DefaultProperties;
			DefaultProperties.Type = EMetaHumanCharacterEyelashesType::ShortSparse;
			FMetaHumanCharacterSkinMaterials::ApplyEyelashesPropertiesToMaterial(FaceMaterialSet, DefaultProperties, /*bAlwaysUseCards*/ false);
			SectionScalarsBefore = SnapshotScalarParameters(EyelashesMaterial);
			SectionVectorsBefore = SnapshotVectorParameters(EyelashesMaterial);

			FMetaHumanCharacterEyelashesProperties ModifiedProperties = DefaultProperties;
			PerturbFloatProperties(
				FMetaHumanCharacterEyelashesProperties::StaticStruct(), &ModifiedProperties, -0.15f);
			FMetaHumanCharacterSkinMaterials::ApplyEyelashesPropertiesToMaterial(FaceMaterialSet, ModifiedProperties, /*bAlwaysUseCards*/ false);
			SectionScalarsAfter = SnapshotScalarParameters(EyelashesMaterial);
			SectionVectorsAfter = SnapshotVectorParameters(EyelashesMaterial);


		});

		It("should have mappings for all eyelashes properties", [this]()
		{
			TSet<FName> MappedNames;
			for (const FPropertyMapping& Entry : PropertyMap)
			{
				MappedNames.Add(Entry.PropertyName);
			}
			VerifyAllPropertiesMapped(FMetaHumanCharacterEyelashesProperties::StaticStruct(), MappedNames, SkippedProperties);
		});

		for (const FPropertyMapping& Mapping : PropertyMap)
		{
			It(FString::Printf(TEXT("should apply %s to material parameter %s"),
				*Mapping.PropertyName.ToString(), *Mapping.MaterialParameterName.ToString()),
			[this, Mapping]()
			{
				VerifyPropertyChanged(Mapping, FMetaHumanCharacterEyelashesProperties::StaticStruct());
			});
		}
	});

	Describe("Teeth", [this]()
	{
		static const TArray<FPropertyMapping> PropertyMap = {
			{ TEXT("TeethColor"),  TEXT("Teeth Basecolor Multiply")  },
			{ TEXT("GumColor"),    TEXT("Gums Basecolor Multiply")   },
			{ TEXT("PlaqueColor"), TEXT("Plaque Basecolor Multiply") },
			{ TEXT("PlaqueAmount"),TEXT("Plaque Amount")             },
		};

		static const TSet<FName> SkippedProperties = {
			TEXT("JawOpen"),                   // Morph target, not material param
			TEXT("TeethGap"),                  // Morph target, not material param
			TEXT("ToothLength"),               // Morph target, not material param
			TEXT("ToothSpacing"),              // Morph target, not material param
			TEXT("UpperShift"),                // Morph target, not material param
			TEXT("LowerShift"),                // Morph target, not material param
			TEXT("Overbite"),                  // Morph target, not material param
			TEXT("Overjet"),                   // Morph target, not material param
			TEXT("WornDown"),                  // Morph target, not material param
			TEXT("Polycanine"),                // Morph target, not material param
			TEXT("RecedingGums"),              // Morph target, not material param
			TEXT("Narrowness"),                // Morph target, not material param
			TEXT("Variation"),                 // Morph target, not material param
			TEXT("EnableShowTeethExpression"), // Bool, not material param
		};

		BeforeEach([this]()
		{
			SectionScalarsBefore.Reset();
			SectionScalarsAfter.Reset();
			SectionVectorsBefore.Reset();
			SectionVectorsAfter.Reset();

			UMaterialInstanceDynamic* TeethMaterial = Cast<UMaterialInstanceDynamic>(FaceMaterialSet.Teeth);
			TestNotNull(TEXT("Teeth material"), TeethMaterial);
			if (!TeethMaterial)
			{
				return;
			}

			// Pre-set boundary values to mid-range so perturbation has room in both directions
			FMetaHumanCharacterTeethProperties DefaultProperties;
			DefaultProperties.PlaqueAmount = 0.5f;
			DefaultProperties.TeethColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
			DefaultProperties.GumColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
			DefaultProperties.PlaqueColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
			FMetaHumanCharacterSkinMaterials::ApplyTeethPropertiesToMaterial(FaceMaterialSet, DefaultProperties);
			SectionScalarsBefore = SnapshotScalarParameters(TeethMaterial);
			SectionVectorsBefore = SnapshotVectorParameters(TeethMaterial);

			FMetaHumanCharacterTeethProperties ModifiedProperties = DefaultProperties;
			PerturbFloatProperties(
				FMetaHumanCharacterTeethProperties::StaticStruct(), &ModifiedProperties, -0.15f);
			FMetaHumanCharacterSkinMaterials::ApplyTeethPropertiesToMaterial(FaceMaterialSet, ModifiedProperties);
			SectionScalarsAfter = SnapshotScalarParameters(TeethMaterial);
			SectionVectorsAfter = SnapshotVectorParameters(TeethMaterial);


		});

		It("should have mappings for all teeth properties", [this]()
		{
			TSet<FName> MappedNames;
			for (const FPropertyMapping& Entry : PropertyMap)
			{
				MappedNames.Add(Entry.PropertyName);
			}
			VerifyAllPropertiesMapped(FMetaHumanCharacterTeethProperties::StaticStruct(), MappedNames, SkippedProperties);
		});

		for (const FPropertyMapping& Mapping : PropertyMap)
		{
			It(FString::Printf(TEXT("should apply %s to material parameter %s"),
				*Mapping.PropertyName.ToString(), *Mapping.MaterialParameterName.ToString()),
			[this, Mapping]()
			{
				VerifyPropertyChanged(Mapping, FMetaHumanCharacterTeethProperties::StaticStruct());
			});
		}
	});
}

#endif // WITH_AUTOMATION_TESTS
