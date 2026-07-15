// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultSpectatorScreenController.h"

#include "Engine/Engine.h"
#include "Engine/Texture.h"
#include "GlobalRenderResources.h"
#include "HeadMountedDisplayBase.h"
#include "HeadMountedDisplayTypes.h"
#include "IStereoLayers.h"
#include "Misc/CoreDelegates.h"
#include "PipelineStateCache.h"
#include "RenderGraphResources.h"
#include "RenderGraphUtils.h"
#include "SceneUtils.h" // for SCOPED_DRAW_EVENT()
#include "SceneView.h"
#include "TextureResource.h"
#include "XRCopyTexture.h"

FDefaultSpectatorScreenController::FDefaultSpectatorScreenController(FHeadMountedDisplayBase* InHMDDevice)
	: FeatureLevel_RenderThread(GMaxRHIFeatureLevel)
	, ShaderPlatform_RenderThread(GMaxRHIShaderPlatform)
	, HMDDevice(InHMDDevice)
{}

ESpectatorScreenMode FDefaultSpectatorScreenController::GetSpectatorScreenMode() const
{
	check(IsInGameThread());

	return SpectatorScreenMode_GameThread;
}

void FDefaultSpectatorScreenController::SetSpectatorScreenMode(ESpectatorScreenMode Mode)
{
	check(IsInGameThread());

	UE_LOGF(LogHMD, Log, "SetSpectatorScreenMode(%i).", static_cast<int32>(Mode));

	SpectatorScreenMode_GameThread = Mode;

	ENQUEUE_RENDER_COMMAND(SetSpectatorScreenMode)(
		[this, Mode](FRHICommandListImmediate&)
		{
			SpectatorScreenMode_RenderThread = Mode;
		});
}

void FDefaultSpectatorScreenController::SetSpectatorScreenTexture(UTexture* SrcTexture)
{
	SpectatorScreenTexture = SrcTexture;
}

UTexture* FDefaultSpectatorScreenController::GetSpectatorScreenTexture() const
{
	if (SpectatorScreenTexture.IsValid())
	{
		return SpectatorScreenTexture.Get();
	}
	return nullptr;
}

void FDefaultSpectatorScreenController::SetSpectatorScreenModeTexturePlusEyeLayout(const FSpectatorScreenModeTexturePlusEyeLayout& Layout)
{
	if (Layout.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(SetSpectatorScreenModeTexturePlusEyeLayout)(
			[this, Layout](FRHICommandListImmediate&)
			{
				SpectatorScreenModeTexturePlusEyeLayout_RenderThread = Layout;
			});
	}
	else
	{
		UE_LOGF(LogHMD, Warning, "SetSpectatorScreenModeTexturePlusEyeLayout called with invalid Layout.  Ignoring it.  See warnings above.")
	}
}

void FDefaultSpectatorScreenController::BeginRenderViewFamily(FSceneViewFamily& ViewFamily)
{
	check(IsInGameThread());

	UTexture* Texture = SpectatorScreenTexture.Get();
	FTextureResource* TextureResource = Texture ? Texture->GetResource() : nullptr;
	ERHIFeatureLevel::Type FeatureLevel = ViewFamily.GetFeatureLevel();
	EShaderPlatform ShaderPlatform = ViewFamily.GetShaderPlatform();

	ENQUEUE_RENDER_COMMAND(SetSpectatorScreenTexture)(
		[this, TextureResource, FeatureLevel, ShaderPlatform](FRHICommandListImmediate& RHICmdList)
		{
			FeatureLevel_RenderThread = FeatureLevel;
			ShaderPlatform_RenderThread = ShaderPlatform;
			SpectatorScreenTexture_RenderThread = TextureResource;
		});
}

void FDefaultSpectatorScreenController::RenderSpectatorScreen_RenderThread(FRDGBuilder& GraphBuilder, FRDGTextureRef BackBuffer, FRDGTextureRef SrcTexture, FRDGTextureRef LayersTexture, FVector2f WindowSize)
{
	SCOPED_NAMED_EVENT_TEXT("RenderSocialScreen_RenderThread()", FColor::Magenta);

	check(IsInRenderingThread());

	StereoLayersTextureRDG = LayersTexture;
	ON_SCOPE_EXIT{ StereoLayersTextureRDG = nullptr; };

	FRDGTextureRef OtherTexture = nullptr;
	if (SpectatorScreenTexture_RenderThread)
	{
		OtherTexture = RegisterExternalTexture(GraphBuilder, SpectatorScreenTexture_RenderThread->GetTextureRHI(), TEXT("DefaultSpectatorScreen_OtherTexture"));
	}
	AddSpectatorModePass(SpectatorScreenMode_RenderThread, GraphBuilder, BackBuffer, SrcTexture, OtherTexture, WindowSize);

	// Apply the debug canvas layer.
	IStereoLayers* StereoLayers = HMDDevice->GetStereoLayers();
	if (StereoLayers && !LayersTexture)
	{
		const FIntRect DstRect(0, 0, BackBuffer->Desc.GetSize().X, BackBuffer->Desc.GetSize().Y);

		FXRCopyTextureOptions Options(FeatureLevel_RenderThread, ShaderPlatform_RenderThread);
		Options.BlendMod = EXRCopyTextureBlendModifier::PremultipliedAlphaBlend;
		Options.SetDisplayMappingOptions(HMDDevice->GetRenderTargetManager());
		for (FTextureRHIRef LayerTexture : StereoLayers->GetDebugLayerTextures_RenderThread())
		{
			FTextureRHIRef LayerTexture2D = LayerTexture->GetTexture2D();
			check(LayerTexture2D.IsValid());  // Debug canvas layer should be a 2d layer
			const FIntRect LayerRect(0, 0, LayerTexture2D->GetSizeX(), LayerTexture2D->GetSizeY());
			const FIntRect DstRectLetterboxed = Helpers::GetLetterboxedDestRect(LayerRect, DstRect);
			FRDGTextureRef RDGLayerTexture = RegisterExternalTexture(GraphBuilder, LayerTexture2D, TEXT("OpenXRSpectatorDebugLayerTexture"));
			AddXRCopyTexturePass(GraphBuilder, RDG_EVENT_NAME("DefaultSpectatorScreen_DebugLayers"), RDGLayerTexture, LayerRect, BackBuffer, DstRectLetterboxed, Options);
		}
	}
}

FIntRect FDefaultSpectatorScreenController::GetFullFlatEyeRect_RenderThread(const FRHITextureDesc& EyeTexture)
{
	return HMDDevice->GetFullFlatEyeRect_RenderThread(EyeTexture);
}

void FDefaultSpectatorScreenController::CopyEmulatedLayers(FRDGBuilder& GraphBuilder, FRDGTextureRef TargetTexture, const FIntRect SrcRect, const FIntRect DstRect)
{
	if (StereoLayersTextureRDG)
	{
		FXRCopyTextureOptions Options(FeatureLevel_RenderThread, ShaderPlatform_RenderThread);
		Options.bClearBlack = false;
		Options.BlendMod = EXRCopyTextureBlendModifier::PremultipliedAlphaBlend;
		Options.SetDisplayMappingOptions(HMDDevice->GetRenderTargetManager());
		AddXRCopyTexturePass(GraphBuilder, RDG_EVENT_NAME("DefaultSpectatorScreen_CopyEmulatedLayers"), StereoLayersTextureRDG, SrcRect, TargetTexture, DstRect, Options);
	}
	StereoLayersTextureRDG = nullptr;
}

void FDefaultSpectatorScreenController::AddSpectatorModePassTexturePlusEye(FRDGBuilder& GraphBuilder, FRDGTextureRef TargetTexture,
	FRDGTextureRef EyeTexture, FRDGTextureRef OtherTexture)
{
	FRDGTextureRef OtherTextureLocal = OtherTexture ? OtherTexture : GetFallbackRDGTexture(GraphBuilder);

	const FIntRect EyeDstRect = SpectatorScreenModeTexturePlusEyeLayout_RenderThread.GetScaledEyeRect(TargetTexture->Desc.GetSize().X, TargetTexture->Desc.GetSize().Y);
	const FIntRect EyeSrcRect = GetFullFlatEyeRect_RenderThread(EyeTexture->Desc);
	const FIntRect CroppedEyeSrcRect = Helpers::GetEyeCroppedToFitRect(HMDDevice->GetEyeCenterPoint_RenderThread(EStereoscopicEye::eSSE_LEFT_EYE), EyeSrcRect, EyeDstRect);

	const FIntRect OtherDstRect = SpectatorScreenModeTexturePlusEyeLayout_RenderThread.GetScaledTextureRect(TargetTexture->Desc.GetSize().X, TargetTexture->Desc.GetSize().Y);
	const FIntRect OtherSrcRect(0, 0, OtherTextureLocal->Desc.GetSize().X, OtherTextureLocal->Desc.GetSize().Y);

	FXRCopyTextureOptions Options(FeatureLevel_RenderThread, ShaderPlatform_RenderThread);
	Options.bClearBlack = SpectatorScreenModeTexturePlusEyeLayout_RenderThread.bClearBlack;
	Options.SetDisplayMappingOptions(HMDDevice->GetRenderTargetManager());

	if (SpectatorScreenModeTexturePlusEyeLayout_RenderThread.bDrawEyeFirst)
	{
		AddXRCopyTexturePass(GraphBuilder, RDG_EVENT_NAME("DefaultSpectatorScreen_TexturePlusEye_EyeTexture1st"), EyeTexture, CroppedEyeSrcRect, TargetTexture, EyeDstRect, Options);
		CopyEmulatedLayers(GraphBuilder, TargetTexture, CroppedEyeSrcRect, EyeDstRect);
		Options.BlendMod = SpectatorScreenModeTexturePlusEyeLayout_RenderThread.bUseAlpha ?
			EXRCopyTextureBlendModifier::PremultipliedAlphaBlend :
			EXRCopyTextureBlendModifier::Opaque;
		AddXRCopyTexturePass(GraphBuilder, RDG_EVENT_NAME("DefaultSpectatorScreen_TexturePlusEye_OtherTexture2nd"), OtherTextureLocal, OtherSrcRect, TargetTexture, OtherDstRect, Options);
	}
	else
	{
		AddXRCopyTexturePass(GraphBuilder, RDG_EVENT_NAME("DefaultSpectatorScreen_TexturePlusEye_OtherTexture1st"), OtherTextureLocal, OtherSrcRect, TargetTexture, OtherDstRect, Options);
		Options.bClearBlack = false;
		AddXRCopyTexturePass(GraphBuilder, RDG_EVENT_NAME("DefaultSpectatorScreen_TexturePlusEye_EyeTexture2nd"), EyeTexture, CroppedEyeSrcRect, TargetTexture, EyeDstRect, Options);
		CopyEmulatedLayers(GraphBuilder, TargetTexture, CroppedEyeSrcRect, EyeDstRect);
	}
}

void FDefaultSpectatorScreenController::AddSpectatorModePass(ESpectatorScreenMode SpectatorMode, FRDGBuilder& GraphBuilder, FRDGTextureRef TargetTexture, FRDGTextureRef EyeTexture, FRDGTextureRef OtherTexture, FVector2f WindowSize)
{
	// Special cases
	if (SpectatorMode == ESpectatorScreenMode::Disabled)
	{
		return;
	}

	if (SpectatorMode == ESpectatorScreenMode::TexturePlusEye)
	{
		AddSpectatorModePassTexturePlusEye(GraphBuilder, TargetTexture, EyeTexture, OtherTexture);
		return;
	}

	// Standard path
	FIntRect SrcRect;
	FRDGTextureRef SrcTexture = EyeTexture;
	FIntRect DstRect = FIntRect(0, 0, TargetTexture->Desc.GetSize().X, TargetTexture->Desc.GetSize().Y);
	FXRCopyTextureOptions Options(FeatureLevel_RenderThread, ShaderPlatform_RenderThread);
	Options.bClearBlack = false;
	Options.SetDisplayMappingOptions(HMDDevice->GetRenderTargetManager());
	bool bCopyEmulatedLayers = true;
	
	switch (SpectatorMode)
	{
	case ESpectatorScreenMode::SingleEyeLetterboxed:
		SrcRect = GetFullFlatEyeRect_RenderThread(EyeTexture->Desc);
		DstRect = Helpers::GetLetterboxedDestRect(SrcRect, DstRect);
		Options.bClearBlack = true;
		break;
	case ESpectatorScreenMode::Undistorted:
		SrcRect = FIntRect(0, 0, EyeTexture->Desc.GetSize().X, EyeTexture->Desc.GetSize().Y);
		break;
	case ESpectatorScreenMode::SingleEye:
		SrcRect = FIntRect(0, 0, EyeTexture->Desc.GetSize().X / 2, EyeTexture->Desc.GetSize().Y);
		break;
	case ESpectatorScreenMode::Texture:
		SrcTexture = OtherTexture ? OtherTexture : GetFallbackRDGTexture(GraphBuilder);
		SrcRect = FIntRect(0, 0, SrcTexture->Desc.GetSize().X, SrcTexture->Desc.GetSize().Y);
		bCopyEmulatedLayers = false;
		break;
	default:
		// Some modes are only supported by certain plugins
		// The default implementation falls back to SingleEyeCroppedToFill.
		if(GEngine)
		{
			GEngine->AddOnScreenDebugMessage((uint64) this, 2.0, FColor::Red,
				FString::Printf(TEXT("ESpectatorScreenMode %d is not available in the default spectator controller."), (int)SpectatorMode));
		}
		// fall-through
	case ESpectatorScreenMode::SingleEyeCroppedToFill:
		SrcRect = Helpers::GetEyeCroppedToFitRect(HMDDevice->GetEyeCenterPoint_RenderThread(EStereoscopicEye::eSSE_LEFT_EYE),
			GetFullFlatEyeRect_RenderThread(EyeTexture->Desc),
			FIntRect(0, 0, static_cast<int32>(WindowSize.X), static_cast<int32>(WindowSize.Y)));
		break;
	}

	AddXRCopyTexturePass(GraphBuilder, RDG_EVENT_NAME("DefaultSpectatorScreen_CopyTexture"), SrcTexture, SrcRect, TargetTexture, DstRect, Options);
	if (bCopyEmulatedLayers)
	{
		CopyEmulatedLayers(GraphBuilder, TargetTexture, SrcRect, DstRect);
	}
}

FRDGTextureRef FDefaultSpectatorScreenController::GetFallbackRDGTexture(FRDGBuilder& GraphBuilder) const
{
	FRHITexture* FallbackTexture = GBlackTexture->TextureRHI->GetTexture2D();
	return RegisterExternalTexture(GraphBuilder, FallbackTexture, TEXT("DefaultSpectatorScreen_Fallback"));
}

FIntRect FDefaultSpectatorScreenController::Helpers::GetEyeCroppedToFitRect(FVector2D EyeCenterPoint, const FIntRect& SrcRect, const FIntRect& TargetRect)
{
	// Return a SubRect of EyeRect which has the same aspect ratio as TargetRect
	// such that drawing that SubRect of the eye texture into TargetRect of some other texture
	// will give a nice single eye cropped to fit view.

	// If EyeCenterPoint can be put in the center of the screen by shifting the crop up/down or left/right
	// shift it as far as we can without cropping further.  This means if we are cropping
	// vertically we can shift to a vertical center other than 0.5, and if we are cropping horizontally
	// we can shift to a horizontal center other than 0.5.

	// Eye rect is the subrect of the eye texture that we want to crop to fit TargetRect.
	// Eye rect should already have been cropped to only contain pixels we might want to show on TargetRect.
	// So it ought to be cropped to the reasonably flat-looking part of the rendered area.

	FIntRect OutRect = SrcRect;

	// Assuming neither rect is zero size in any dimension.
	check(SrcRect.Area() != 0);
	check(TargetRect.Area() != 0);

	const float SrcRectAspect = (float)SrcRect.Width() / (float)SrcRect.Height();
	const float TargetRectAspect = (float)TargetRect.Width() / (float)TargetRect.Height();

	if (SrcRectAspect < TargetRectAspect)
	{
		// Source is taller than destination
		// Crop top/bottom
		const float DesiredSrcHeight = SrcRect.Height() * (SrcRectAspect / TargetRectAspect);
		const int32 HalfHeightDiff = FMath::TruncToInt(((float)SrcRect.Height() - DesiredSrcHeight) * 0.5f);
		OutRect.Min.Y += HalfHeightDiff;
		OutRect.Max.Y -= HalfHeightDiff;
		const int32 DesiredCenterAdjustment = FMath::TruncToInt(((float)EyeCenterPoint.Y - 0.5f) * (float)SrcRect.Height());
		const int32 ActualCenterAdjustment = FMath::Clamp(DesiredCenterAdjustment, -HalfHeightDiff, HalfHeightDiff);
		OutRect.Min.Y += ActualCenterAdjustment;
		OutRect.Max.Y += ActualCenterAdjustment;
	}
	else
	{
		// Source is wider than destination
		// Crop left/right
		const float DesiredSrcWidth = SrcRect.Width() * (TargetRectAspect / SrcRectAspect);
		const int32 HalfWidthDiff = FMath::TruncToInt(((float)SrcRect.Width() - DesiredSrcWidth) * 0.5f);
		OutRect.Min.X += HalfWidthDiff;
		OutRect.Max.X -= HalfWidthDiff;
		const int32 DesiredCenterAdjustment = FMath::TruncToInt(((float)EyeCenterPoint.X - 0.5f) * (float)SrcRect.Width());
		const int32 ActualCenterAdjustment = FMath::Clamp(DesiredCenterAdjustment, -HalfWidthDiff, HalfWidthDiff);
		OutRect.Min.X += ActualCenterAdjustment;
		OutRect.Max.X += ActualCenterAdjustment;
	}

	return OutRect;
}

FIntRect FDefaultSpectatorScreenController::Helpers::GetLetterboxedDestRect(const FIntRect& SrcRect, const FIntRect& TargetRect)
{
	FIntRect OutRect = TargetRect;

	// Assuming neither rect is zero size in any dimension.
	check(SrcRect.Area() != 0);
	check(TargetRect.Area() != 0);

	const float SrcRectAspect = (float)SrcRect.Width() / (float)SrcRect.Height();
	const float TargetRectAspect = (float)TargetRect.Width() / (float)TargetRect.Height();

	if (SrcRectAspect < TargetRectAspect)
	{
		// Source is taller than destination
		// Column-boxing
		const float DesiredTgtWidth = TargetRect.Width() * (SrcRectAspect / TargetRectAspect);
		const int32 HalfWidthDiff = FMath::TruncToInt(((float)TargetRect.Width() - DesiredTgtWidth) * 0.5f);
		OutRect.Min.X += HalfWidthDiff;
		OutRect.Max.X -= HalfWidthDiff;
	}
	else
	{
		// Source is wider than destination
		// Letter-boxing
		const float DesiredTgtHeight = TargetRect.Height() * (TargetRectAspect / SrcRectAspect);
		const int32 HalfHeightDiff = FMath::TruncToInt(((float)TargetRect.Height() - DesiredTgtHeight) * 0.5f);
		OutRect.Min.Y += HalfHeightDiff;
		OutRect.Max.Y -= HalfHeightDiff;
	}

	return OutRect;
}
