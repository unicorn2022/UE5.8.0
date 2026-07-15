// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaLightsVisualizationData.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Internationalization.h"

#define LOCTEXT_NAMESPACE "FMegaLightsVisualizationData"

static FMegaLightsVisualizationData GMegaLightsVisualizationData;

void FMegaLightsVisualizationData::Initialize()
{
	if (!bIsInitialized)
	{
		AddVisualizationMode(
			TEXT("Overview"),
			LOCTEXT("Overview", "Overview"),
			LOCTEXT("OverviewDesc", "All MegaLights view mode tiles overlayed on top"),
			EModeType::Overview,
			EModeID::Overview,
			true);

		AddVisualizationMode(
			TEXT("ShadowCasters"),
			LOCTEXT("ShadowCasters", "Shadow Casters"),
			LOCTEXT("ShadowCastersDesc", "Visualize shadow casters"),
			EModeType::General,
			EModeID::ShadowCasters,
			true);

		AddVisualizationMode(
			TEXT("ShadowCasterQuality"),
			LOCTEXT("ShadowCasterQuality", "Shadow Caster Quality"),
			LOCTEXT("ShadowCasterQualityDesc", "Visualize shadow caster mismatches with scene geometry"),
			EModeType::General,
			EModeID::ShadowCasterQuality,
			true);

		AddVisualizationMode(
			TEXT("LightComplexity_Opaque"),
			LOCTEXT("LumenComplexityOpaque", "Opaque"),
			LOCTEXT("LumenComplexityOpaqueDesc", "Visualizes MegaLights light complexity for opaque pixels"),
			EModeType::LightComplexity,
			EModeID::LightComplexity_Opaque,
			true);

		AddVisualizationMode(
			TEXT("LightComplexity_HairStrands"),
			LOCTEXT("LumenComplexityHairStrands", "Hair Strands"),
			LOCTEXT("LumenComplexityHairStrandsDesc", "Visualizes MegaLights light complexity for hair strands pixels"),
			EModeType::LightComplexity,
			EModeID::LightComplexity_HairStrands,
			true);

		AddVisualizationMode(
			TEXT("LightComplexity_FrontLayerTranslucency"),
			LOCTEXT("LumenComplexityFrontLayerTranslucency", "Front Layer Translucency"),
			LOCTEXT("LumenComplexityFrontLayerTranslucencyDesc", "Visualizes MegaLights light complexity for front layer translucency pixels"),
			EModeType::LightComplexity,
			EModeID::LightComplexity_FrontLayerTranslucency,
			true);

		AddVisualizationMode(
			TEXT("LightComplexity_Volume"),
			LOCTEXT("LumenComplexityVolume", "Volume"),
			LOCTEXT("LumenComplexityVolumeDesc", "Visualizes MegaLights light complexity for the froxel volume"),
			EModeType::LightComplexity,
			EModeID::LightComplexity_Volume,
			true);

		AddVisualizationMode(
			TEXT("LightComplexity_TranslucencyLightingVolume"),
			LOCTEXT("LumenComplexityTranslucencyLightingVolume", "Translucency Lighting Volume"),
			LOCTEXT("LumenComplexityTranslucencyLightingVolumeDesc", "Visualizes MegaLights light complexity for the translucency lighting volume"),
			EModeType::LightComplexity,
			EModeID::LightComplexity_TranslucencyLightingVolume,
			true);

		ConfigureConsoleCommand();

		bIsInitialized = true;
	}
}

void FMegaLightsVisualizationData::ConfigureConsoleCommand()
{
	FString AvailableVisualizationModes;
	for (TModeMap::TConstIterator It = ModeMap.CreateConstIterator(); It; ++It)
	{
		const FModeRecord& Record = It.Value();
		AvailableVisualizationModes += FString("\n  ");
		AvailableVisualizationModes += Record.ModeString;
	}

	ConsoleDocumentationVisualizationMode = TEXT("When the viewport view-mode is set to 'MegaLights Visualization', this command specifies which of the various channels to display. Values entered other than the allowed values shown below will be ignored.");
	ConsoleDocumentationVisualizationMode += AvailableVisualizationModes;

	IConsoleManager::Get().RegisterConsoleVariable(
		GetVisualizeConsoleCommandName(),
		TEXT(""),
		*ConsoleDocumentationVisualizationMode,
		ECVF_Cheat);
}

void FMegaLightsVisualizationData::AddVisualizationMode(
	const TCHAR* ModeString,
	const FText& ModeText,
	const FText& ModeDesc,
	EModeType ModeType,
	EModeID ModeID,
	bool DefaultComposited
)
{
	const FName ModeName = FName(ModeString);

	FModeRecord& Record	= ModeMap.Emplace(ModeName);
	Record.ModeString			= FString(ModeString);
	Record.ModeName				= ModeName;
	Record.ModeText				= ModeText;
	Record.ModeDesc				= ModeDesc;
	Record.ModeType				= ModeType;
	Record.ModeID				= ModeID;
	Record.DefaultComposited	= DefaultComposited;
}

FText FMegaLightsVisualizationData::GetModeDisplayName(const FName& InModeName) const
{
	if (const FModeRecord* Record = ModeMap.Find(InModeName))
	{
		return Record->ModeText;
	}
	else
	{
		return FText::GetEmpty();
	}
}

FMegaLightsVisualizationData::EModeID FMegaLightsVisualizationData::GetModeID(const FName& InModeName) const
{
	if (const FModeRecord* Record = ModeMap.Find(InModeName))
	{
		return Record->ModeID;
	}
	else
	{
		return EModeID::Invalid;
	}
}

bool FMegaLightsVisualizationData::GetModeDefaultComposited(const FName& InModeName) const
{
	if (const FModeRecord* Record = ModeMap.Find(InModeName))
	{
		return Record->DefaultComposited;
	}
	else
	{
		return false;
	}
}

FMegaLightsVisualizationData& GetMegaLightsVisualizationData()
{
	if (!GMegaLightsVisualizationData.IsInitialized())
	{
		GMegaLightsVisualizationData.Initialize();
	}

	return GMegaLightsVisualizationData;
}

#undef LOCTEXT_NAMESPACE
