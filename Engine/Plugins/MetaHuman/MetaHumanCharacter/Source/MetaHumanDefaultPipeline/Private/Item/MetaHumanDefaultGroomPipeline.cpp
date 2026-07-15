// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/MetaHumanDefaultGroomPipeline.h"

#include "MetaHumanDefaultPipelineLog.h"

#include "Engine/Texture.h"
#include "Logging/StructuredLog.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "StructUtils/PropertyBag.h"

namespace UE::MetaHuman::DefaultGroomPipeline::Private
{
	namespace CategoryName
	{
		static constexpr FStringView Regions = TEXTVIEW("Regions");
		static constexpr FStringView Ombre = TEXTVIEW("Ombre");
		static constexpr FStringView Highlights = TEXTVIEW("Highlights");
	}

	namespace MetaDataKey
	{
		static const FName GroomCategory = FName("GroomCategory");
		static const FName MaterialParamName = FName("MaterialParamName");
	}
}

UMetaHumanDefaultGroomPipeline::UMetaHumanDefaultGroomPipeline()
	: UMetaHumanGroomPipeline()
{
	UpdateParameters();
}

#if WITH_EDITOR
void UMetaHumanDefaultGroomPipeline::PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	const FName PropertyName = InPropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipeline, bSupportsRegions)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipeline, bSupportsOmbre)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipeline, bSupportsHightlights)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipeline, SlotTarget)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipeline, SlotNames)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipeline, SlotIndices))
	{
		UpdateParameters();
	}
}

void UMetaHumanDefaultGroomPipeline::AddRuntimeParameter(TNotNull<FProperty*> InProperty, const FName& InMaterialParameterName)
{
	using namespace UE::MetaHuman::MaterialUtils;

	FMetaHumanMaterialParameter& Param = RuntimeMaterialParameters.AddDefaulted_GetRef();
	Param.InstanceParameterName = InProperty->GetFName();
	Param.SlotTarget = SlotTarget;
	Param.SlotNames = SlotNames;
	Param.SlotIndices = SlotIndices;
	Param.MaterialParameter.Name = InMaterialParameterName;
	Param.ParameterType = PropertyToParameterType(InProperty);
	Param.PropertyMetadata = CopyMetadataFromProperty(InProperty);
}
#endif

void UMetaHumanDefaultGroomPipeline::UpdateParameters()
{
#if WITH_EDITOR
	using namespace UE::MetaHuman::DefaultGroomPipeline::Private;

	RuntimeMaterialParameters.Empty();

	for (TFieldIterator<FProperty> PropertyIterator(UMetaHumanDefaultGroomPipelineMaterialParameters::StaticClass()); PropertyIterator; ++PropertyIterator)
	{
		FProperty* Property = *PropertyIterator;

		if (!Property || Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			continue;
		}

		const FString GroomCategory = Property->GetMetaData(MetaDataKey::GroomCategory);

		if (GroomCategory.IsEmpty()
			|| (!bSupportsOmbre && GroomCategory == CategoryName::Ombre)
			|| (!bSupportsRegions && GroomCategory == CategoryName::Regions)
			|| (!bSupportsHightlights && GroomCategory == CategoryName::Highlights))
		{
			continue;
		}

		const FString MaterialParamName = Property->GetMetaData(MetaDataKey::MaterialParamName);

		if (MaterialParamName.IsEmpty())
		{
			continue;
		}

		AddRuntimeParameter(Property, FName(MaterialParamName));
	}
#endif
}

#if WITH_EDITOR
void UMetaHumanDefaultGroomPipeline::SetFaceMaterialParameters(
	const TArray<UMaterialInstance*>& FaceMaterials,
	const TArray<int32>& LODToMaterial,
	FName SlotName, 
	const FInstancedPropertyBag& InstanceParameters,
	bool bIsProduction,
	bool bHideHair,
	bool bForceCards,
	int32& OutFirstLODBaked) const
{
	check(FaceMaterials.Num() > 0);
	check(FaceMaterials[0]);

	OutFirstLODBaked = INDEX_NONE;

	UTexture* DefaultTexture = nullptr;
	bool bParameterExists = FaceMaterials[0]->GetTextureParameterDefaultValue(*(SlotName.ToString() + TEXT("AttributeMap")), DefaultTexture);
	// If there's no matching texture parameter this groom should not be represented as a texture
	if (!bParameterExists)
	{
		return;
	}

	UTexture* LoadedBakedGroomTexture = BakedGroomTexture.LoadSynchronous();
	
	if (LoadedBakedGroomTexture == nullptr || bHideHair)
	{
		// Not baking the texture, but the material parameter exists, so set it to the default

		for (UMaterialInstance* Material : FaceMaterials)
		{
			SetFaceMaterialParametersForLOD(Material, SlotName, InstanceParameters, DefaultTexture);
		}

		return;
	}

	OutFirstLODBaked = FMath::Max(GroomTextureMinLOD, 0);

	if (!bIsProduction 
		&& bForceCards 
		&& (LODTransition == EHairLODTransition::StrandsToCardsAndTextureToTexture 
			|| LODTransition == EHairLODTransition::StrandsToCardsAndTextureToMeshToTexture 
			|| LODTransition == EHairLODTransition::StrandsToTexture))
	{
		OutFirstLODBaked = 0;
	}

	for (int32 LODIndex = 0; LODIndex < LODToMaterial.Num(); LODIndex++)
	{
		const int32 MaterialIndex = LODToMaterial[LODIndex];
		if (MaterialIndex == INDEX_NONE)
		{
			// This LOD has no material (e.g. the LOD has been removed from the mesh), so silently
			// skip it.
			continue;
		}

		if (!FaceMaterials.IsValidIndex(MaterialIndex))
		{
			UE_LOGFMT(LogMetaHumanDefaultPipeline, Error, "SetFaceMaterialParameters: Index from LODToMaterial out of range of FaceMaterials array ({Index}/{FaceMaterials})", MaterialIndex, FaceMaterials.Num());
			continue;
		}

		UMaterialInstance* Material = FaceMaterials[MaterialIndex];
		check(Material);

		if (LODIndex < OutFirstLODBaked)
		{
			SetFaceMaterialParametersForLOD(Material, SlotName, InstanceParameters, DefaultTexture);
		}
		else
		{
			SetFaceMaterialParametersForLOD(Material, SlotName, InstanceParameters, LoadedBakedGroomTexture);
		}
	}
}

static void SetScalarParameterValue(TNotNull<UMaterialInstance*> Material, FName ParameterName, float Value)
{
	if (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Material))
	{
		MIC->SetScalarParameterValueEditorOnly(ParameterName, Value);
	}
	else if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Material))
	{
		MID->SetScalarParameterValue(ParameterName, Value);
	}
}

static void SetVectorParameterValue(TNotNull<UMaterialInstance*> Material, FName ParameterName, FLinearColor Value)
{
	if (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Material))
	{
		MIC->SetVectorParameterValueEditorOnly(ParameterName, Value);
	}
	else if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Material))
	{
		MID->SetVectorParameterValue(ParameterName, Value);
	}
}

static void SetTextureParameterValue(TNotNull<UMaterialInstance*> Material, FName ParameterName, UTexture* Texture)
{
	if (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Material))
	{
		MIC->SetTextureParameterValueEditorOnly(ParameterName, Texture);
	}
	else if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Material))
	{
		MID->SetTextureParameterValue(ParameterName, Texture);
	}
}

static void TrySetScalarParameterFromPropertyBag(UMaterialInstance* Material, const FInstancedPropertyBag& InstanceParameters, FName ParameterName, FName PropertyName)
{
	TValueOrError<float, EPropertyBagResult> ValueFloat = InstanceParameters.GetValueFloat(PropertyName);
	if (const float* Value = ValueFloat.TryGetValue())
	{
		SetScalarParameterValue(Material, ParameterName, *Value);
	}
}

static void TrySetBooleanScalarParameterFromPropertyBag(UMaterialInstance* Material, const FInstancedPropertyBag& InstanceParameters, FName ParameterName, FName PropertyName)
{
	TValueOrError<bool, EPropertyBagResult> ValueBool = InstanceParameters.GetValueBool(PropertyName);
	if (const bool* Value = ValueBool.TryGetValue())
	{
		SetScalarParameterValue(Material, ParameterName, *Value ? 1.0f : 0.0f);
	}
}

static void TrySetVectorParameterFromPropertyBag(UMaterialInstance* Material, const FInstancedPropertyBag& InstanceParameters, FName ParameterName, FName PropertyName)
{
	TValueOrError<FStructView, EPropertyBagResult> ValueStruct = InstanceParameters.GetValueStruct(PropertyName, TBaseStructure<FLinearColor>::Get());
	if (const FStructView* Value = ValueStruct.TryGetValue())
	{
		SetVectorParameterValue(Material, ParameterName, Value->Get<FLinearColor>());
	}
}

static void TrySetTextureParameterFromPropertyBag(UMaterialInstance* Material, const FInstancedPropertyBag& InstanceParameters, FName ParameterName, FName PropertyName)
{
	TValueOrError<UObject*, EPropertyBagResult> ValueObject = InstanceParameters.GetValueObject(PropertyName, UTexture::StaticClass());
	if (UObject* const* Value = ValueObject.TryGetValue())
	{
		SetTextureParameterValue(Material, ParameterName, CastChecked<UTexture>(*Value));
	}
}

void UMetaHumanDefaultGroomPipeline::SetFaceMaterialParametersForLOD(
	UMaterialInstance* FaceMaterial, 
	FName SlotName, 
	const FInstancedPropertyBag& InstanceParameters,
	UTexture* Texture) const
{
	SetTextureParameterValue(FaceMaterial, FName(SlotName.ToString() + TEXT("AttributeMap")), Texture);
	TrySetScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, *(SlotName.ToString() + TEXT("Melanin")), GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, Melanin));
	TrySetScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, *(SlotName.ToString() + TEXT("Redness")), GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, Redness));
	TrySetScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, *(SlotName.ToString() + TEXT("WhiteAmount")), GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, Whiteness));
	TrySetVectorParameterFromPropertyBag(FaceMaterial, InstanceParameters, *(SlotName.ToString() + TEXT("DyeColor")), GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, DyeColor));
	SetScalarParameterValue(FaceMaterial, FName("ShowBakedGroomTextures"), 1.0f);

	// For now, only hair slot can have secondary colors
	if (SlotName == UE::MetaHuman::CharacterPipelineSlots::Hair)
	{
		// Color regions
		if (bSupportsRegions)
		{
			TrySetBooleanScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, "Region", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, bUseRegions));
			TrySetVectorParameterFromPropertyBag(FaceMaterial, InstanceParameters, "RegionhairDye", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, RegionsColor));
			TrySetScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, "RegionMelanin", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, RegionsU));
			TrySetScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, "RegionRedness", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, RegionsV));
		}

		// Ombre
		if (bSupportsOmbre)
		{
			TrySetBooleanScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, "Ombre", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, bUseOmbre));
			TrySetVectorParameterFromPropertyBag(FaceMaterial, InstanceParameters, "OmbrehairDye", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, OmbreColor));
			TrySetScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, "OmbreMelanin", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, OmbreU));
			TrySetScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, "OmbreRedness", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, OmbreV));
			TrySetScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, "OmbreShift", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, OmbreShift));
			TrySetScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, "OmbreContrast", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, OmbreContrast));
			TrySetScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, "OmbreIntensity", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, OmbreIntensity));
		}

		// Highlights
		if (bSupportsHightlights)
		{
			TrySetBooleanScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, "Highlights", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, bUseHighlights));
			TrySetVectorParameterFromPropertyBag(FaceMaterial, InstanceParameters, "HighlightshairDye", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, HighlightsColor));
			TrySetScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, "HighlightsMelanin", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, HighlightsU));
			TrySetScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, "HighlightsRedness", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, HighlightsV));
			TrySetScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, "HighlightsBlending", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, HighlightsBlending));
			TrySetScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, "HighlightsIntensity", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, HighlightsIntensity));
			TrySetScalarParameterFromPropertyBag(FaceMaterial, InstanceParameters, "HighlightsVariationNumber", GET_MEMBER_NAME_CHECKED(UMetaHumanDefaultGroomPipelineMaterialParameters, HighlightsVariation));
			SetScalarParameterValue(FaceMaterial, FName("HighlightsRootDistance"), HighlightsRootDistance);
		}

		if (UTexture* LoadedHighlightsMask = HighlightsMask.LoadSynchronous())
		{
			SetTextureParameterValue(FaceMaterial, FName("HighlightsMask"), LoadedHighlightsMask);
		}
		else
		{
			UTexture* DefaultHighlightsMask = nullptr;
			if (FaceMaterial->GetTextureParameterDefaultValue(FName("HighlightsMask"), DefaultHighlightsMask))
			{
				SetTextureParameterValue(FaceMaterial, FName("HighlightsMask"), DefaultHighlightsMask);
			}
		}
	}
}

void UMetaHumanDefaultGroomPipeline::SetPostAssemblyParameters(const FSetPostAssemblyParametersParams& Params, FInstancedStruct& InOutItemAssemblyOutput) const
{
	Super::SetPostAssemblyParameters(Params, InOutItemAssemblyOutput);

	const FMetaHumanGroomPipelineAssemblyOutput* GroomAssemblyOutput = InOutItemAssemblyOutput.GetPtr<FMetaHumanGroomPipelineAssemblyOutput>();
	if (!GroomAssemblyOutput || GroomAssemblyOutput->FaceMeshOverrideMaterials.IsEmpty())
	{
		return;
	}

	// Get the groom slot name (e.g. "Hair", "Eyebrows") for param name prefixing
	const FMetaHumanPipelineBuiltData* BuiltData = Params.ItemBuiltData.Find(Params.BaseItemPath);
	if (!BuiltData)
	{
		return;
	}
	const FName SlotName = BuiltData->SlotName;

	UTexture* BakedTex = BakedGroomTexture.LoadSynchronous();

	if (!BakedTex)
	{
		return;
	}

	// Update face material because we have the baked texture
	for (const TPair<FName, TObjectPtr<UMaterialInstanceDynamic>>& Pair : GroomAssemblyOutput->FaceMeshOverrideMaterials)
	{
		if (UMaterialInstanceDynamic* MID = Pair.Value.Get())
		{
			SetFaceMaterialParametersForLOD(MID, SlotName, Params.ModifiedPostAssemblyParameters, BakedTex);
		}
	}
}
#endif

void UMetaHumanDefaultGroomPipeline::OverrideInitialMaterialValues(TNotNull<UMaterialInstanceDynamic*> InMID, FName InSlotName) const
{
	InMID->SetScalarParameterValue(FName("HighlightsRootDistance"), HighlightsRootDistance);
	InMID->SetTextureParameterValue(FName("HighlightsMask"), HighlightsMask.LoadSynchronous());
}
