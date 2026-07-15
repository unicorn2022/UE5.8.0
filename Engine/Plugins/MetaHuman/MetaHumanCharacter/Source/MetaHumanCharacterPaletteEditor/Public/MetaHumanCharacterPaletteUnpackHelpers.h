// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/NotNull.h"

class UMaterialInterface;
class UMaterialInstance;
class UMaterialInstanceConstant;
enum class EMaterialParameterType : uint8;

#define UE_API METAHUMANCHARACTERPALETTEEDITOR_API

namespace UE::MetaHuman::PaletteUnpackHelpers
{
	/**
	 * Copy the material parameters of a given type from the source to the target material only if the values are not the same. This ensures
	 * the target material will only have the overrides for parameters that have different values.
	 */
	UE_API void CopyMaterialParametersIfNeeded(EMaterialParameterType InParamType, TNotNull<const UMaterialInterface*> InSourceMaterial, TNotNull<UMaterialInstanceConstant*> InTargetMaterial);

	/**
	 * Creates a material instance constant from the given material instance. Parameters are only copied if they differ from the base material.
	 * This prevents the new material from having all of its parameters overridden
	 */
	UE_API UMaterialInstanceConstant* CreateMaterialInstanceCopy(TNotNull<const UMaterialInstance*> InMaterialInstance, TNotNull<UObject*> InOuter);
}

#undef UE_API