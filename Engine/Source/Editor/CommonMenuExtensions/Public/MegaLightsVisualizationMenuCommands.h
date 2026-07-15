// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/Platform.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"

#define UE_API COMMONMENUEXTENSIONS_API

class SLevelViewport;
class FEditorViewportClient;
class FMenuBuilder;
class FUICommandInfo;
class FUICommandList;

UE_DECLARE_TCOMMANDS(class FMegaLightsVisualizationMenuCommands, UE_API)

class FMegaLightsVisualizationMenuCommands : public TCommands<FMegaLightsVisualizationMenuCommands>
{
public:
	enum class EMegaLightsVisualizationType : uint8
	{
		Overview,
		General,
		LightComplexity,
		Count
	};

	struct FMegaLightsVisualizationRecord
	{
		FName Name;
		TSharedPtr<FUICommandInfo> Command;
		EMegaLightsVisualizationType Type;

		FMegaLightsVisualizationRecord()
		: Name()
		, Command()
		, Type(EMegaLightsVisualizationType::Overview)
		{
		}
	};

	typedef TMultiMap<FName, FMegaLightsVisualizationRecord> TMegaLightsVisualizationModeCommandMap;
	typedef TMegaLightsVisualizationModeCommandMap::TConstIterator TCommandConstIterator;

	UE_API FMegaLightsVisualizationMenuCommands();

	UE_API TCommandConstIterator CreateCommandConstIterator() const;

	static UE_API void BuildVisualisationSubMenu(FMenuBuilder& Menu, TWeakPtr<SLevelViewport> WeakViewport);

	UE_API virtual void RegisterCommands() override;

	UE_API void BindCommands(FUICommandList& CommandList, const TSharedPtr<FEditorViewportClient>& Client) const;
	
	inline bool IsPopulated() const
	{
		return CommandMap.Num() > 0;
	}

private:
	void BuildCommandMap();
	bool AddCommandTypeToMenu(FMenuBuilder& Menu, const EMegaLightsVisualizationType Type, TWeakPtr<SLevelViewport> WeakViewport) const;

	static void ChangeMegaLightsVisualizationMode(TWeakPtr<FEditorViewportClient> WeakClient, FName InName);
	static bool IsMegaLightsVisualizationModeSelected(TWeakPtr<FEditorViewportClient> WeakClient, FName InName);

private:
	TMegaLightsVisualizationModeCommandMap CommandMap;
	int32 TypeCommandCounts[int32(EMegaLightsVisualizationType::Count)];
};

#undef UE_API