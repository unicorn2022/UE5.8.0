// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FMegaLightsVisualizationData
{
public:
	enum class EModeType : uint8
	{
		Overview,
		General,
		LightComplexity
	};

	enum class EModeID : int32
	{
		Invalid = 0,
		Overview,
		ShadowCasters,
		ShadowCasterQuality,
		LightComplexity_Opaque,
		LightComplexity_HairStrands,
		LightComplexity_FrontLayerTranslucency,
		LightComplexity_Volume,
		LightComplexity_TranslucencyLightingVolume,

		// Which modes should map to r.MegaLights.Visualize
		MinVisualizeMode = Overview,
		MaxVisualizeMode = ShadowCasterQuality,
	};

	/** Describes a single available visualization mode. */
	struct FModeRecord
	{
		FString   ModeString;
		FName     ModeName;
		FText     ModeText;
		FText     ModeDesc;
		EModeType ModeType;
		EModeID     ModeID;

		// Whether or not this mode (by default) composites with regular scene depth.
		bool      DefaultComposited;
	};

	/** Mapping of FName to a visualization mode record. */
	typedef TMultiMap<FName, FModeRecord> TModeMap;

public:
	FMegaLightsVisualizationData()
	: bIsInitialized(false)
	{
	}

	/** Initialize the system. */
	void Initialize();

	/** Check if system was initialized. */
	inline bool IsInitialized() const { return bIsInitialized; }

	/** Check if visualization is active. */
	ENGINE_API bool IsActive() const;

	/** Get the display name of a named mode from the available mode map. **/
	ENGINE_API FText GetModeDisplayName(const FName& InModeName) const;

	ENGINE_API EModeID GetModeID(const FName& InModeName) const;

	ENGINE_API bool GetModeDefaultComposited(const FName& InModeName) const;

	inline const TModeMap& GetModeMap() const
	{
		return ModeMap;
	}

	/** Return the console command name for enabling single mode visualization. */
	static const TCHAR* GetVisualizeConsoleCommandName()
	{
		return TEXT("r.MegaLights.Visualize.ViewMode");
	}

private:
	/** Internal helper function for creating the Lumen visualization system console commands. */
	void ConfigureConsoleCommand();

	void AddVisualizationMode(
		const TCHAR* ModeString,
		const FText& ModeText,
		const FText& ModeDesc,
		EModeType ModeType,
		EModeID ModeID,
		bool DefaultComposited
	);

	//void SetActiveMode(int32 ModeID, const FName& ModeName, bool bDefaultComposited);

private:
	/** The name->mode mapping table */
	TModeMap ModeMap;

	/** Storage for console variable documentation strings. **/
	FString ConsoleDocumentationVisualizationMode;

	/** Flag indicating if system is initialized. **/
	bool bIsInitialized;
};

ENGINE_API FMegaLightsVisualizationData& GetMegaLightsVisualizationData();
