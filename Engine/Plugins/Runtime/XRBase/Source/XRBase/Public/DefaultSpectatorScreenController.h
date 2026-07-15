// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "ISpectatorScreenController.h"
#include "HeadMountedDisplayTypes.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "TextureResource.h"

#define UE_API XRBASE_API

class FHeadMountedDisplayBase;

/** 
 *	Default implementation of spectator screen controller.
 *
 */
class FDefaultSpectatorScreenController : public ISpectatorScreenController, public TSharedFromThis<FDefaultSpectatorScreenController, ESPMode::ThreadSafe>
{
public:
	UE_API FDefaultSpectatorScreenController(FHeadMountedDisplayBase* InHMDDevice);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS // for deprecated fields
	virtual ~FDefaultSpectatorScreenController() = default;
	FDefaultSpectatorScreenController(const FDefaultSpectatorScreenController&) = default;
	FDefaultSpectatorScreenController(FDefaultSpectatorScreenController&&) = default;
	FDefaultSpectatorScreenController& operator=(const FDefaultSpectatorScreenController&) = default;
	FDefaultSpectatorScreenController& operator=(FDefaultSpectatorScreenController&&) = default;
	UE_API PRAGMA_ENABLE_DEPRECATION_WARNINGS

	// ISpectatorScreenController
	virtual ESpectatorScreenMode GetSpectatorScreenMode() const override;
	UE_API virtual void SetSpectatorScreenMode(ESpectatorScreenMode Mode) override;
	UE_API virtual void SetSpectatorScreenTexture(UTexture* InTexture) override;
	UE_API virtual UTexture* GetSpectatorScreenTexture() const override;
	UE_API virtual void SetSpectatorScreenModeTexturePlusEyeLayout(const FSpectatorScreenModeTexturePlusEyeLayout& Layout) override;

	// Implementation methods called by HMD
	UE_API virtual void BeginRenderViewFamily(FSceneViewFamily& ViewFamily);

	ESpectatorScreenMode GetSpectatorScreenMode_RenderThread()
	{
		return SpectatorScreenMode_RenderThread;
	}

	UE_API virtual void RenderSpectatorScreen_RenderThread(class FRDGBuilder& GraphBuilder, FRDGTextureRef BackBuffer, FRDGTextureRef SrcTexture, FRDGTextureRef LayersTexture, FVector2f WindowSize);

protected:
	UE_API virtual FIntRect GetFullFlatEyeRect_RenderThread(const FRHITextureDesc& EyeTexture);
	UE_API virtual void AddSpectatorModePass(ESpectatorScreenMode SpectatorMode, class FRDGBuilder& GraphBuilder, FRDGTextureRef TargetTexture, FRDGTextureRef EyeTexture, FRDGTextureRef OtherTexture, FVector2f WindowSize);
	UE_API virtual FRDGTextureRef GetFallbackRDGTexture(FRDGBuilder& GraphBuilder) const;

	static constexpr ESpectatorScreenMode DefaultSpectatorMode = ESpectatorScreenMode::SingleEyeCroppedToFill;

	ESpectatorScreenMode SpectatorScreenMode_GameThread = DefaultSpectatorMode;
	TWeakObjectPtr<UTexture> SpectatorScreenTexture;

	ESpectatorScreenMode SpectatorScreenMode_RenderThread = DefaultSpectatorMode;
	FSpectatorScreenModeTexturePlusEyeLayout SpectatorScreenModeTexturePlusEyeLayout_RenderThread;
	FTextureResource* SpectatorScreenTexture_RenderThread = nullptr;
	ERHIFeatureLevel::Type FeatureLevel_RenderThread;
	EShaderPlatform ShaderPlatform_RenderThread;

	class Helpers
	{
	public:
		static UE_API FIntRect GetEyeCroppedToFitRect(FVector2D EyeCenterPoint, const FIntRect& EyeRect, const FIntRect& TargetRect);
		static UE_API FIntRect GetLetterboxedDestRect(const FIntRect& SrcRect, const FIntRect& TargetRect);
	};

private:
	UE_API void CopyEmulatedLayers(FRDGBuilder& GraphBuilder, FRDGTextureRef TargetTexture, const FIntRect SrcRect, const FIntRect DstRect);
	UE_API void AddSpectatorModePassTexturePlusEye(FRDGBuilder& GraphBuilder, FRDGTextureRef TargetTexture, FRDGTextureRef EyeTexture, FRDGTextureRef OtherTexture);

	FHeadMountedDisplayBase* HMDDevice;
	// Face locked stereo layers are composited to a single texture which has to be copied over to the spectator screen.
	FRDGTextureRef StereoLayersTextureRDG;
};

#undef UE_API
