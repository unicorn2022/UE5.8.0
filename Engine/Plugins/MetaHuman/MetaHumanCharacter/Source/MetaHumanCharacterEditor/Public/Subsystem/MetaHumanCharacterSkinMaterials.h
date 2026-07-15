// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Misc/EnumRange.h"
#include "Misc/NotNull.h"
#include "MetaHumanCharacterTeeth.h"
#include "MetaHumanCharacterMakeup.h"
#include "MetaHumanCharacterEyes.h"
#include "MetaHumanCharacterSkin.h"
#include "MetaHumanCharacterMaterialSet.h"
#include "MetaHumanTypes.h"
#include "Engine/DataTable.h"

#include "MetaHumanCharacterSkinMaterials.generated.h"

#define UE_API METAHUMANCHARACTEREDITOR_API

UENUM()
enum class EMetaHumanCharacterAccentRegion : uint8
{
	Scalp,
	Forehead,
	Nose,
	UnderEye,
	Cheeks,
	Lips,
	Chin,
	Ears,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterAccentRegion, EMetaHumanCharacterAccentRegion::Count);

UENUM()
enum class EMetaHumanCharacterAccentRegionParameter : uint8
{
	Redness,
	Saturation,
	Lightness,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterAccentRegionParameter, EMetaHumanCharacterAccentRegionParameter::Count);

UENUM()
enum class EMetaHumanCharacterFrecklesParameter : uint8
{
	Mask,
	Density,
	Strength,
	Saturation,
	ToneShift,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterFrecklesParameter, EMetaHumanCharacterFrecklesParameter::Count);

UENUM()
enum class EMetaHumanCharacterPalmParameters : uint8
{
	Lightness,
	Tint,
	CavityDarkness,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterPalmParameters, EMetaHumanCharacterPalmParameters::Count);

UENUM()
enum class EMetaHumanCharacterNailParameters : uint8
{
	TintColor,
	TintIntensity,
	Metallic,
	Roughness,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterNailParameters, EMetaHumanCharacterNailParameters::Count);

USTRUCT()
struct FMetaHumanCharacterSkinMaterialOverrideRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Scalar Parameters")
	TMap<FName, float> ScalarParameterValues;
};

struct FMetaHumanCharacterSkinMaterials
{
	/**
	 * Returns the material slot names for the skin materials
	 */
	UE_API static FName GetSkinMaterialSlotName(EMetaHumanCharacterSkinMaterialSlot InSlot);

	/**
	 * @brief Returns the material parameter name for the a given synthesized texture type
	 * 
	 * @param InTextureType the face texture type to get the parameter name for
	 * @param bInWithVTSupport Whether or not to return the virtual texture parameter name if supported
	 */
	UE_API static FName GetFaceTextureParameterName(EFaceTextureType InTextureType, bool bInWithVTSupport = false);

	/**
	 * @brief Returns the material parameter name for the a given body texture type
	 * 
	 * @param InTextureType the body texture type to get the parameter name for
	 * @param bInWithVTSupport Whether or not to return the virtual texture parameter name if supported
	 */
	UE_API static FName GetBodyTextureParameterName(EBodyTextureType InTextureType, bool bInWithVTSupport = false);

	UE_API static const FName EyeLeftSlotName;
	UE_API static const FName EyeRightSlotName;
	UE_API static const FName SalivaSlotName;
	UE_API static const FName EyeShellSlotName;
	UE_API static const FName EyeEdgeSlotName;
	UE_API static const FName TeethSlotName;
	UE_API static const FName EyelashesSlotName;
	UE_API static const FName EyelashesHiLodSlotName;
	UE_API static const FName BodySlotName;
	UE_API static const FName UseCavityParamName;
	UE_API static const FName UseAnimatedMapsParamName;
	UE_API static const FName UseTextureOverrideParamName;
	UE_API static const FName RoughnessUIMultiplyParamName;

	static void ForEachFaceMaterialSlot(const FMetaHumanCharacterFaceMaterialSet& InMaterialSet, TFunction<void(FName, const class UMaterialInterface*)> InCallback);

	UE_API static void SetHeadMaterialsOnMesh(const FMetaHumanCharacterFaceMaterialSet& InMaterialSet, TNotNull<class USkeletalMesh*> InMesh);
	UE_API static void SetBodyMaterialOnMesh(TNotNull<class UMaterialInterface*> InBodyMaterial, TNotNull<class USkeletalMesh*> InMesh);

	/**
	 * Creates a Face Material set from the materials in the given face mesh
	 */
	UE_API static FMetaHumanCharacterFaceMaterialSet GetHeadMaterialsFromMesh(TNotNull<class USkeletalMesh*> InFaceMesh);

	// Number of rows and columns to use when building the foundation color picker palette
	static constexpr int32 FoundationPaletteColumns = 7;
	static constexpr int32 FoundationPaletteRows = 5;
	static constexpr float FoundationSaturationShift = 0.1f;
	static constexpr float FoundationValueShift = 0.1f;

	/**
	 * Shifts the input color based on the preset index to calculate the final foundation color to apply.
	 * If InColorIndex is INDEX_NONE returns InColor so that no shift happens.
	 */
	UE_API static FLinearColor ShiftFoundationColor(const FLinearColor& InColor, int32 InColorIndex, int32 InShowColumns, int32 InShowRows, float InSaturationShift, float InValueShift);

	static void ApplyTextureOverrideParameterToMaterials(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, TNotNull<class UMaterialInstanceDynamic*> InBodyMaterial, const FMetaHumanCharacterSkinSettings& InSkinSettings);

	/**
	 * Replaces the default preview MIDs with new ones parented to override MICs.
	 * Skin and Body overrides only apply in Editable preview mode.
	 * Eye, teeth, and eyelash overrides apply in all preview modes.
	 */
	static void ApplyMaterialOverrides(
		FMetaHumanCharacterFaceMaterialSet& InOutFaceMaterialSet,
		UMaterialInstanceDynamic*& InOutBodyMaterial,
		const FMetaHumanCharacterSkinSettings& InSkinSettings,
		EMetaHumanCharacterSkinPreviewMaterial InPreviewMaterialType);

	/**
	 * Apply skin material parameter overrides based on the face texture index for better visuals
	 */
	UE_API static void ApplySkinParametersToMaterials(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, TNotNull<class UMaterialInstanceDynamic*> InBodyMID, const FMetaHumanCharacterSkinSettings& InSkinSettings);

	/**
	 * Apply the Roughness UI Multiply to the skin materials
	 */
	static void ApplyRoughnessMultiplyToMaterials(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, TNotNull<class UMaterialInstanceDynamic*> InBodyMaterial, const FMetaHumanCharacterSkinSettings& InSkinSettings);

	/**
	 * Apply skin palm parameters (Lightness, Tint, and Cavity Darkness) to the body material
	 */
	static void ApplyPalmParametersToMaterial(TNotNull<class UMaterialInstanceDynamic*> InBodyMaterial, const FMetaHumanCharacterSkinProperties& InSkinProps);

	/**
	 * Apply skin nail parameters (TintColor, TintIntensity, Metallic, and Roughness) to the body material
	 */
	static void ApplyNailParametersToMaterial(TNotNull<class UMaterialInstanceDynamic*> InBodyMaterial, const FMetaHumanCharacterSkinProperties& InSkinProps, const FString& InNailPrefix);

	/**
	 * Apply skin fingernail parameters (TintColor, TintIntensity, Metallic, and Roughness) to the body material
	 */
	static void ApplyFingernailParametersToMaterial(TNotNull<class UMaterialInstanceDynamic*> InBodyMaterial, const FMetaHumanCharacterSkinProperties& InSkinProps);

	/**
	 * Apply skin toenail parameters (TintColor, TintIntensity, Metallic, and Roughness) to the body material
	 */
	static void ApplyToenailParametersToMaterial(TNotNull<class UMaterialInstanceDynamic*> InBodyMaterial, const FMetaHumanCharacterSkinProperties& InSkinProps);

	/**
	 * Update the preview material parameter value of the given accent region.
	 */
	static void ApplySkinAccentParameterToMaterial(
		const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet,
		EMetaHumanCharacterAccentRegion InRegion,
		EMetaHumanCharacterAccentRegionParameter InParameter,
		float InValue);

	/**
	 * Updates the accent region parameters in the given face material set
	 */
	static void ApplySkinAccentsToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterAccentRegions& InAccentProperties);

	/**
	 * Updates the freckles mask in the given face material set
	 */
	static void ApplyFrecklesMaskToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, EMetaHumanCharacterFrecklesMask InMask);

	/**
	 * Updates one of the freckles material parameters in the given face material set
	 */
	static void ApplyFrecklesParameterToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, EMetaHumanCharacterFrecklesParameter InParam, float InValue);

	/**
	 * Updates all freckle parameters in the given face material set
	 */
	static void ApplyFrecklesToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterFrecklesProperties& InFrecklesProperties);

	/**
	 * @brief Apply makeup settings to the given face material set
	 * 
	 * @param InFaceMaterialSet the set of face materials to where the foundation properties are going to be applied
	 * @param bInWithVTSupport Whether or not apply textures to virtual texture slots when supported
	 */
	UE_API static void ApplyFoundationMakeupToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterFoundationMakeupProperties& InFoundationMakeupProperties, bool bInWithVTSupport);
	UE_API static void ApplyEyeMakeupToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterEyeMakeupProperties& InEyeMakeupProperties, bool bInWithVTSupport);
	UE_API static void ApplyBlushMakeupToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterBlushMakeupProperties& InBlushMakeupProperties, bool bInWithVTSupport);
	UE_API static void ApplyLipsMakeupToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterLipsMakeupProperties& InLipsMakeupProperties, bool bInWithVTSupport);

	/**
	 * Helper to apply update the MH face material so that it references the (transient) synthesized textures
	 */
	UE_API static void ApplySynthesizedTexturesToFaceMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& InSynthesizedFaceTextures);

	/**
	 * Helper to apply all eye material settings to the given face material set
	 */
	UE_API static void ApplyEyeSettingsToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterEyesSettings& InEyeSettings);

	/**
	 * Compute the Sclera tint based on the current Skin Tone
	 */
	static FLinearColor GetScleraTintBasedOnSkinTone(const FMetaHumanCharacterSkinSettings& InSkinSettings);

	/**
	 * Read the eye settings from the default eye material
	 */
	static void GetDefaultEyeSettings(FMetaHumanCharacterEyesSettings& OutEyeSettings);

	/**
	* Applies eyelashes material properties to given face material set
	*/
	UE_API static void ApplyEyelashesPropertiesToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterEyelashesProperties& InEyelashesProperties, const bool bAlwaysUseCards);

	/**
	 * Toggles opacity for eyelashes material set for lower LODs which can have both strands and cards.
	 */
	UE_API static void ToggleEyelashesMaterialOpacity(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, bool bEyelashesGroomEnabled, bool bAlwaysUseHairCards);

	/**
	* Applies teeth material properties to given face material set
	*/
	UE_API static void ApplyTeethPropertiesToMaterial(const FMetaHumanCharacterFaceMaterialSet& InFaceMaterialSet, const FMetaHumanCharacterTeethProperties& InTeethProperties);

	/**
	 * @brief Returns a new material instance for the head for a given preview material type
	 * 
	 * @param bInWithVTSupport Returns the materials that have support for virtual textures
	 */
	UE_API static FMetaHumanCharacterFaceMaterialSet GetHeadPreviewMaterialInstance(EMetaHumanCharacterSkinPreviewMaterial InPreviewMaterialType, bool bInWithVTSupport);

	/**
	 * Returns a new material instance for the body for a given preview material type
	 * 
	 * @param bInWithVTSupport Returns the materials that have support for virtual texures
	 */
	UE_API static class UMaterialInstanceDynamic* GetBodyPreviewMaterialInstance(EMetaHumanCharacterSkinPreviewMaterial InPreviewMaterialType, bool bInWithVTSupport);

	/**
	 * Set the parent of InMaterial to InNewParent preserving overrides and static switches
	 */
	UE_API static void SetMaterialInstanceParent(TNotNull<class UMaterialInstanceConstant*> InMaterial, TNotNull<class UMaterialInterface*> InNewParent);

	/**
	* Returns the active mask texture used for the eyelashes mesh given the input eyelashes properties
	*/
	static class UTexture2D* GetEyelashesMask(const FMetaHumanCharacterEyelashesProperties& InEyelashesProperties);
};

#undef UE_API
