// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterPaletteUnpackHelpers.h"

#include "Materials/MaterialInstanceConstant.h"

namespace UE::MetaHuman::PaletteUnpackHelpers
{

void CopyMaterialParametersIfNeeded(EMaterialParameterType InParamType, TNotNull<const UMaterialInterface*> InSourceMaterial, TNotNull<UMaterialInstanceConstant*> InTargetMaterial)
{
	// Ideally we would use CopyMaterialUniformParametersEditorOnly, however, that function will override the parameters even if they are are the same.
	// This breaks the chain of material parameters for the LOD materials so we use a custom function to only copy parameters when they are actually different
	// from the material we are copying from

	TMap<FMaterialParameterInfo, FMaterialParameterMetadata> SourceParams;
	InSourceMaterial->GetAllParametersOfType(InParamType, SourceParams);

	TMap<FMaterialParameterInfo, FMaterialParameterMetadata> TargetParams;
	InTargetMaterial->GetAllParametersOfType(InParamType, TargetParams);

	for (const TPair<FMaterialParameterInfo, FMaterialParameterMetadata>& SourceParamPair : SourceParams)
	{
		const FMaterialParameterInfo& SourceParamInfo = SourceParamPair.Key;
		const FMaterialParameterMetadata& SourceParam = SourceParamPair.Value;

		if (const FMaterialParameterMetadata* TargetParam = TargetParams.Find(SourceParamInfo))
		{
			if (SourceParam.Value != TargetParam->Value)
			{
				switch (InParamType)
				{
					case EMaterialParameterType::Scalar:
						InTargetMaterial->SetScalarParameterValueEditorOnly(SourceParamInfo, SourceParam.Value.AsScalar());
						break;

					case EMaterialParameterType::Vector:
						InTargetMaterial->SetVectorParameterValueEditorOnly(SourceParamInfo, SourceParam.Value.AsLinearColor());
						break;

					case EMaterialParameterType::Texture:
						InTargetMaterial->SetTextureParameterValueEditorOnly(SourceParamInfo, SourceParam.Value.Texture);
						break;

					case EMaterialParameterType::StaticSwitch:
						InTargetMaterial->SetStaticSwitchParameterValueEditorOnly(SourceParamInfo, SourceParam.Value.AsStaticSwitch());
						break;

					default:
						break;
				}
			}
		}
	}
}

UMaterialInstanceConstant* CreateMaterialInstanceCopy(TNotNull<const UMaterialInstance*> InMaterialInstance, TNotNull<UObject*> InOuter)
{
	check(InMaterialInstance->Parent);

	const FString MaterialName = InMaterialInstance->GetName();

	constexpr FStringView PrefixMID = TEXTVIEW("MID_");
	constexpr FStringView PrefixMIC = TEXTVIEW("MIC_");

	FName MaterialConstantName;

	if (MaterialName.StartsWith(PrefixMID, ESearchCase::CaseSensitive))
	{
		MaterialConstantName = FName{ PrefixMIC + InMaterialInstance->GetName().RightChop(PrefixMID.Len()) };
	}
	else
	{
		MaterialConstantName = MakeUniqueObjectName(InOuter, InMaterialInstance->GetClass(), InMaterialInstance->GetFName());
	}

	UMaterialInstanceConstant* MaterialInstanceConstant = NewObject<UMaterialInstanceConstant>(InOuter, FName{ MaterialConstantName });
	MaterialInstanceConstant->SetParentEditorOnly(InMaterialInstance->Parent);

	CopyMaterialParametersIfNeeded(EMaterialParameterType::Scalar, InMaterialInstance, MaterialInstanceConstant);
	CopyMaterialParametersIfNeeded(EMaterialParameterType::Vector, InMaterialInstance, MaterialInstanceConstant);
	CopyMaterialParametersIfNeeded(EMaterialParameterType::Texture, InMaterialInstance, MaterialInstanceConstant);
	CopyMaterialParametersIfNeeded(EMaterialParameterType::StaticSwitch, InMaterialInstance, MaterialInstanceConstant);

	MaterialInstanceConstant->PostEditChange();

	return MaterialInstanceConstant;
}

} // UE::MetaHuman::PaletteUnpackHelpers
