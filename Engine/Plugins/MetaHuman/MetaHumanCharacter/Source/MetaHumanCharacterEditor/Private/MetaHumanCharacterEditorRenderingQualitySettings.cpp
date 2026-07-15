// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorRenderingQualitySettings.h"

#include "MetaHumanCharacterEditorSettings.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorRenderingQualitySettings"

FMetaHumanCharacterViewportRenderingFlags FMetaHumanCharacterViewportRenderingFlags::Epic = {
	.bTransmissionForAllLights		= true,
	.bTonemapper					= true,
	.bDepthOfField					= true,
	.bDynamicShadows				= true,
	.bSubsurfaceScattering			= true,
	.bGlobalIllumination			= true,
	.bTemporalAA					= true,
	.bLumenGlobalIllumination		= true,
	.bLumenReflections				= true,
	.bPreviewingScreenPercentage	= true,
	.PreviewScreenPercentage		= 100,
};

FMetaHumanCharacterViewportRenderingFlags FMetaHumanCharacterViewportRenderingFlags::High = {
	.bTransmissionForAllLights		= true,
	.bTonemapper					= true,
	.bDepthOfField					= true,
	.bDynamicShadows				= true,
	.bSubsurfaceScattering			= true,
	.bGlobalIllumination			= false,
	.bTemporalAA					= true,
	.bLumenGlobalIllumination		= false,
	.bLumenReflections				= false,
	.bPreviewingScreenPercentage	= true,
	.PreviewScreenPercentage		= 70,
};

FMetaHumanCharacterViewportRenderingFlags FMetaHumanCharacterViewportRenderingFlags::Medium = {};

namespace UE::MetaHuman::Private
{
	static FPostProcessSettings MakeDefaultPostProcessSettings()
	{
		FPostProcessSettings Settings;

		// Lens|Bloom
		Settings.bOverride_BloomIntensity = true;
		Settings.BloomIntensity = 0.0f;

		// Lens|Exposure
		Settings.bOverride_AutoExposureMethod = true;
		Settings.AutoExposureMethod = AEM_Manual;

		Settings.bOverride_AutoExposureBias = true;
		Settings.AutoExposureBias = 0.0f;

		Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		Settings.AutoExposureApplyPhysicalCameraExposure = false;

		// Lens|Flares
		Settings.bOverride_LensFlareIntensity = true;
		Settings.LensFlareIntensity = 0.0f;

		// Lens|Image Effects
		Settings.bOverride_VignetteIntensity = true;
		Settings.VignetteIntensity = 0.0f;

		// Color Grading|Misc
		Settings.bOverride_BlueCorrection = true;
		Settings.BlueCorrection = 0.0f;

		Settings.bOverride_ExpandGamut = true;
		Settings.ExpandGamut = 0.0f;

		Settings.bOverride_ToneCurveAmount = true;
		Settings.ToneCurveAmount = 1.0f;

		// Rendering Features|Motion Blur
		Settings.bOverride_MotionBlurAmount = true;
		Settings.MotionBlurAmount = 0.0f;

		Settings.bOverride_MotionBlurMax = true;
		Settings.MotionBlurMax = 0.0f;

		// Path Tracing
		Settings.bOverride_PathTracingMaxBounces = true;
		Settings.PathTracingMaxBounces = 4;

		return Settings;
	}
}

FMetaHumanCharacterRenderingQualityProfile::FMetaHumanCharacterRenderingQualityProfile(const FString& InProfileName)
	: ProfileName(InProfileName)
	, PostProcessSettings(UE::MetaHuman::Private::MakeDefaultPostProcessSettings())
{}

FMetaHumanCharacterRenderingQualityProfile::FMetaHumanCharacterRenderingQualityProfile(const FString& InProfileName, const bool bInMetaHumanCharacterDefault, const FMetaHumanCharacterViewportRenderingFlags& InViewportRenderingFlags)
	: ProfileName(InProfileName)
	, bMetaHumanCharacterDefault(bInMetaHumanCharacterDefault)
	, PostProcessSettings(UE::MetaHuman::Private::MakeDefaultPostProcessSettings())
	, ViewportRenderingFlags(InViewportRenderingFlags)
{}

const FMetaHumanCharacterRenderingQualityProfile FMetaHumanCharacterRenderingQualityProfile::Invalid;
FMetaHumanCharacterRenderingQualityProfile FMetaHumanCharacterRenderingQualityProfile::Epic = { TEXT("Epic"), true, FMetaHumanCharacterViewportRenderingFlags::Epic };
FMetaHumanCharacterRenderingQualityProfile FMetaHumanCharacterRenderingQualityProfile::High = { TEXT("High"), true, FMetaHumanCharacterViewportRenderingFlags::High };
FMetaHumanCharacterRenderingQualityProfile FMetaHumanCharacterRenderingQualityProfile::Medium = { TEXT("Medium"), true, FMetaHumanCharacterViewportRenderingFlags::Medium };

void UMetaHumanCharacterRenderingQualityProfileProxy::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterRenderingQualityProfile, ProfileName))
	{
		CachedProfileName = RenderingQualityProfile.ProfileName;
	}
}

void UMetaHumanCharacterRenderingQualityProfileProxy::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	UMetaHumanCharacterEditorSettings* Settings = GetMutableDefault<UMetaHumanCharacterEditorSettings>();
	check(Settings);

	const bool bProfileNameUpdated = PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterRenderingQualityProfile, ProfileName);

	if (bProfileNameUpdated)
	{
		const TArray<FString> ReservedNames =
		{
			FMetaHumanCharacterRenderingQualityProfile::Epic.ProfileName,
			FMetaHumanCharacterRenderingQualityProfile::High.ProfileName,
			FMetaHumanCharacterRenderingQualityProfile::Medium.ProfileName
		};

		if (ReservedNames.Contains(RenderingQualityProfile.ProfileName))
		{
			RenderingQualityProfile.ProfileName = CachedProfileName;
			Settings->ShowInvalidOperationError(LOCTEXT("ReservedRenderingQualityProfileName", "Epic, High and Medium are reserved rendering quality profile names."));
		}
	}

	if (Settings->IsValidRenderingQualityProfileIndex(ActiveProfileIndex))
	{
		Settings->SetRenderingQualityProfile(ActiveProfileIndex, RenderingQualityProfile);
		Settings->SaveConfig();
	}

	if (bProfileNameUpdated)
	{
		OnProfileNameUpdated.Broadcast();
	}
	else
	{
		OnProfileUpdated.Broadcast();
	}
}

void UMetaHumanCharacterRenderingQualityProfileProxy::SetActiveProfile(const FMetaHumanCharacterRenderingQualityProfile& InProfile)
{
	RenderingQualityProfile = InProfile;
}

#undef LOCTEXT_NAMESPACE
