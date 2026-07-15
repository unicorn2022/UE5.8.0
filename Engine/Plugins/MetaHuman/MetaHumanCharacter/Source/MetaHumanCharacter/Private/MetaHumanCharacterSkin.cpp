// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterSkin.h"

#include "Engine/Texture2D.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanCharacterSkin)

bool FMetaHumanCharacterSkinProperties::EqualForTextureSynthesis(const FMetaHumanCharacterSkinProperties& InOther) const
{
	return U == InOther.U &&
		V == InOther.V &&
		FaceTextureIndex == InOther.FaceTextureIndex &&
		BodyTextureIndex == InOther.BodyTextureIndex;
}

bool FMetaHumanCharacterSkinProperties::operator==(const FMetaHumanCharacterSkinProperties& InOther) const
{
	return U == InOther.U &&
		V == InOther.V &&
		BodyTextureIndex == InOther.BodyTextureIndex &&
		FaceTextureIndex == InOther.FaceTextureIndex &&
		Roughness == InOther.Roughness &&
		PalmLightness == InOther.PalmLightness &&
		PalmTint == InOther.PalmTint &&
		PalmCavityDarkness == InOther.PalmCavityDarkness &&
		FingernailTintColor == InOther.FingernailTintColor &&
		FingernailTintIntensity == InOther.FingernailTintIntensity &&
		FingernailMetallic == InOther.FingernailMetallic &&
		FingernailRoughness == InOther.FingernailRoughness &&
		ToenailTintColor == InOther.ToenailTintColor &&
		ToenailTintIntensity == InOther.ToenailTintIntensity &&
		ToenailMetallic == InOther.ToenailMetallic &&
		ToenailRoughness == InOther.ToenailRoughness;
}

bool FMetaHumanCharacterSkinProperties::operator!=(const FMetaHumanCharacterSkinProperties& InOther) const
{
	return !(*this == InOther);
}

bool FMetaHumanCharacterMaterialOverrideSet::operator==(const FMetaHumanCharacterMaterialOverrideSet& InOther) const
{
	return Skin.OrderIndependentCompareEqual(InOther.Skin)
		&& TeethAndEyes.OrderIndependentCompareEqual(InOther.TeethAndEyes)
		&& Body == InOther.Body;
}

bool FMetaHumanCharacterMaterialOverrideSet::operator!=(const FMetaHumanCharacterMaterialOverrideSet& InOther) const
{
	return !(*this == InOther);
}

namespace UE::MetaHuman
{
	template<typename TEnum>
	static TMap<TEnum, TObjectPtr<UTexture2D>> LoadTextures(const TMap<TEnum, TSoftObjectPtr<UTexture2D>>& InSoftTextures)
	{
		TMap<TEnum, TObjectPtr<UTexture2D>> LoadedTextures;
		for (const TPair<TEnum, TSoftObjectPtr<UTexture2D>>& SoftTexturePair : InSoftTextures)
		{
			const TEnum TextureType = SoftTexturePair.Key;
			const TSoftObjectPtr<UTexture2D> Texture = SoftTexturePair.Value;

			if (!Texture.IsNull())
			{
				if (UTexture2D* LoadedTexture = Texture.LoadSynchronous())
				{
					LoadedTextures.Add(TextureType, LoadedTexture);
				}
			}
		}
		return LoadedTextures;
	}
}

void FMetaHumanCharacterSkinTextureSet::Append(const FMetaHumanCharacterSkinTextureSet& InOther)
{
	Face.Append(InOther.Face);
	Body.Append(InOther.Body);
}

FMetaHumanCharacterSkinTextureSet FMetaHumanCharacterSkinTextureSoftSet::LoadTextureSet() const
{
	return FMetaHumanCharacterSkinTextureSet
	{
		.Face = UE::MetaHuman::LoadTextures(Face),
		.Body = UE::MetaHuman::LoadTextures(Body)
	};
}

FMetaHumanCharacterSkinTextureSet FMetaHumanCharacterSkinSettings::GetFinalSkinTextureSet(const FMetaHumanCharacterSkinTextureSet& InSkinTextureSet) const
{
	FMetaHumanCharacterSkinTextureSet FinalSkinTextureSet = InSkinTextureSet;

	if (TextureMaterialOverrides.bEnableTextureOverrides)
	{
		FMetaHumanCharacterSkinTextureSet LoadedOverrides = TextureMaterialOverrides.TextureOverrides.LoadTextureSet();

		FinalSkinTextureSet.Append(LoadedOverrides);
	}

	return FinalSkinTextureSet;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void FMetaHumanCharacterSkinSettings::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading())
	{
		// Migrate deprecated bEnableTextureOverrides to TextureMaterialOverrides
		if (bEnableTextureOverrides)
		{
			TextureMaterialOverrides.bEnableTextureOverrides = true;
			bEnableTextureOverrides = false;
		}

		// Migrate deprecated TextureOverrides to TextureMaterialOverrides
		if (TextureOverrides.Face.Num() > 0 || TextureOverrides.Body.Num() > 0)
		{
			TextureMaterialOverrides.TextureOverrides = MoveTemp(TextureOverrides);
			TextureOverrides = FMetaHumanCharacterSkinTextureSoftSet();
		}
	}
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void FMetaHumanCharacterTextureSourceResolutions::SetAllResolutionsTo(ERequestTextureResolution InResolution)
{
	for (TFieldIterator<FEnumProperty> It(StaticStruct()); It; ++It)
	{
		if (FEnumProperty* ResolutionProperty = *It)
		{
			ResolutionProperty->SetValue_InContainer(this, &InResolution);
		}
	}
}

bool FMetaHumanCharacterTextureSourceResolutions::AreAllResolutionsEqualTo(ERequestTextureResolution InResolution) const
{
	for (TFieldIterator<FEnumProperty> It(StaticStruct()); It; ++It)
	{
		if (const FEnumProperty* ResolutionProperty = *It)
		{
			ERequestTextureResolution Resolution = ERequestTextureResolution::Res2k;
			ResolutionProperty->GetValue_InContainer(this, &Resolution);

			if (InResolution != Resolution)
			{
				return false;
			}
		}
	}

	return true;
}