// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalShader.h"

class FXRCubemapPS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FXRCubemapPS, Global, XRBASE_API);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }

	FXRCubemapPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		InTexture.Bind(Initializer.ParameterMap, TEXT("InTextureCube"), SPF_Mandatory);
		InTextureSampler.Bind(Initializer.ParameterMap, TEXT("InTextureSampler"));
		InFaceToSampleParameter.Bind(Initializer.ParameterMap, TEXT("FaceToSample"));
	}
	FXRCubemapPS() {}

	void SetParameters(FRHIBatchedShaderParameters& BatchedParameters, const FTexture* Texture, const FMatrix44f&  FaceToSampleMatrix)
	{
		SetTextureParameter(BatchedParameters, InTexture, InTextureSampler, Texture);
		SetShaderValue(BatchedParameters, InFaceToSampleParameter, FaceToSampleMatrix);
	}

	void SetParameters(FRHIBatchedShaderParameters& BatchedParameters, FRHISamplerState* SamplerStateRHI, FRHITexture* TextureRHI, const FMatrix44f&  FaceToSampleMatrix)
	{
		SetTextureParameter(BatchedParameters, InTexture, InTextureSampler, SamplerStateRHI, TextureRHI);
		SetShaderValue(BatchedParameters, InFaceToSampleParameter, FaceToSampleMatrix);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, InTexture);
	LAYOUT_FIELD(FShaderResourceParameter, InTextureSampler);
	LAYOUT_FIELD(FShaderParameter, InFaceToSampleParameter);
};
