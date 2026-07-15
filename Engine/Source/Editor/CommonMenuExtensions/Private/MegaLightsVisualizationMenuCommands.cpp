// Copyright Epic Games, Inc. All Rights Reserved.

#include "MegaLightsVisualizationMenuCommands.h"

#include "Delegates/Delegate.h"
#include "EditorViewportClient.h"
#include "SLevelViewport.h"
#include "Framework/Commands/InputChord.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Text.h"
#include "MegaLightsVisualizationData.h"
#include "Misc/AssertionMacros.h"
#include "Styling/AppStyle.h"
#include "UObject/UnrealNames.h"

#define LOCTEXT_NAMESPACE "MegaLightsVisualizationMenuCommands"

UE_DEFINE_TCOMMANDS(FMegaLightsVisualizationMenuCommands)

FMegaLightsVisualizationMenuCommands::FMegaLightsVisualizationMenuCommands()
	: TCommands<FMegaLightsVisualizationMenuCommands>
	(
		TEXT("MegaLightsVisualizationMenu"), // Context name for fast lookup
		NSLOCTEXT("Contexts", "MegaLightsVisualizationMenu", "MegaLights"), // Localized context name for displaying
		NAME_None, // Parent context name.  
		FAppStyle::GetAppStyleSetName() // Icon Style Set
	),
	CommandMap()
{
	for (int32 Index = 0; Index < int32(EMegaLightsVisualizationType::Count); ++Index)
	{
		TypeCommandCounts[Index] = 0;
	}
}

void FMegaLightsVisualizationMenuCommands::BuildCommandMap()
{
	const FMegaLightsVisualizationData& VisualizationData = GetMegaLightsVisualizationData();
	const FMegaLightsVisualizationData::TModeMap& ModeMap = VisualizationData.GetModeMap();

	CommandMap.Empty();
	for (int32 Index = 0; Index < int32(EMegaLightsVisualizationType::Count); ++Index)
	{
		TypeCommandCounts[Index] = 0;
	}

	for (FMegaLightsVisualizationData::TModeMap::TConstIterator It = ModeMap.CreateConstIterator(); It; ++It)
	{
		const FMegaLightsVisualizationData::FModeRecord& Entry = It.Value();
		FMegaLightsVisualizationRecord& Record = CommandMap.Add(Entry.ModeName, FMegaLightsVisualizationRecord());
		Record.Name = Entry.ModeName;
		Record.Command = FUICommandInfoDecl(
			this->AsShared(),
			Entry.ModeName,
			Entry.ModeText,
			Entry.ModeDesc)
			.UserInterfaceType(EUserInterfaceActionType::RadioButton)
			.DefaultChord(FInputChord()
		);

		switch (Entry.ModeType)
		{
		default:
		case FMegaLightsVisualizationData::EModeType::Overview:
			Record.Type = EMegaLightsVisualizationType::Overview;
			break;

		case FMegaLightsVisualizationData::EModeType::General:
			Record.Type = EMegaLightsVisualizationType::General;
			break;

		case FMegaLightsVisualizationData::EModeType::LightComplexity:
			Record.Type = EMegaLightsVisualizationType::LightComplexity;
			break;
		}

		++TypeCommandCounts[int32(Record.Type)];
	}
}

void FMegaLightsVisualizationMenuCommands::BuildVisualisationSubMenu(FMenuBuilder& Menu, TWeakPtr<SLevelViewport> WeakViewport)
{
	const FMegaLightsVisualizationMenuCommands& Commands = FMegaLightsVisualizationMenuCommands::Get();
	if (Commands.IsPopulated())
	{
		// Overview
		{
			Menu.BeginSection("LevelViewportMegaLightsVisualizationOverview", LOCTEXT("MegaLightsVisualizationOverviewHeader", "Overview"));
			Commands.AddCommandTypeToMenu(Menu, EMegaLightsVisualizationType::Overview, WeakViewport);
			Menu.EndSection();
		}
		
		// General
		{
			Menu.BeginSection("LevelViewportMegaLightsVisualizationGeneral", LOCTEXT("MegaLightsVisualizationGeneralHeader", "General"));
			Commands.AddCommandTypeToMenu(Menu, EMegaLightsVisualizationType::General, WeakViewport);
			Menu.EndSection();
		}

		// Performance
		{
			Menu.BeginSection("LevelViewportMegaLightsVisualizationPerformance", LOCTEXT("MegaLightsVisualizationPerformanceHeader", "Performance"));
			Commands.AddCommandTypeToMenu(Menu, EMegaLightsVisualizationType::LightComplexity, WeakViewport);
			Menu.EndSection();
		}
	}
}

bool FMegaLightsVisualizationMenuCommands::AddCommandTypeToMenu(FMenuBuilder& Menu, const EMegaLightsVisualizationType Type, TWeakPtr<SLevelViewport> WeakViewport) const
{
	bool bAddedCommands = false;

	if (TypeCommandCounts[int32(Type)] > 0)
	{
		if (Type == EMegaLightsVisualizationType::LightComplexity)
		{
			FName SubMenuName = "LightComplexity";
			FText SubMenuLabel = LOCTEXT("MegaLightsLightComplexitySubMenuLabel", "Light Complexity");
			FText SubMenuTooltip = LOCTEXT("MegaLightsLightComplexitySubMenuToolTip", "Light complexity visualization.");

			Menu.AddSubMenu(
				SubMenuLabel,
				SubMenuTooltip,
				FNewMenuDelegate::CreateLambda([this, Type, SubMenuLabel](FMenuBuilder& SubMenu)
				{
					SubMenu.BeginSection(NAME_None, SubMenuLabel);

					for (TCommandConstIterator It = CreateCommandConstIterator(); It; ++It)
					{
						const FMegaLightsVisualizationRecord& Record = It.Value();
						if (Record.Type == Type)
						{
							SubMenu.AddMenuEntry(Record.Command, NAME_None, Record.Command->GetLabel());
						}
					}

					SubMenu.EndSection();
				}),
				FUIAction(
					FExecuteAction(),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([WeakViewport, SubMenuName]()
					{
						const TSharedPtr<SLevelViewport> Viewport = WeakViewport.Pin();
						if (!Viewport.IsValid())
						{
							return false;
						}
						const FLevelEditorViewportClient& ViewportClient = Viewport->GetLevelViewportClient();
						return ViewportClient.IsViewModeEnabled(VMI_VisualizeMegaLights)
							&& ViewportClient.CurrentMegaLightsVisualizationMode.ToString().StartsWith(SubMenuName.ToString());
					})),
				NAME_None,
				EUserInterfaceActionType::RadioButton);
		}
		else
		{
			for (TCommandConstIterator It = CreateCommandConstIterator(); It; ++It)
			{
				const FMegaLightsVisualizationRecord& Record = It.Value();
				if (Record.Type == Type)
				{
					Menu.AddMenuEntry(Record.Command, NAME_None, Record.Command->GetLabel());
				}
			}
		}

		bAddedCommands = true;
	}

	return bAddedCommands;
}

FMegaLightsVisualizationMenuCommands::TCommandConstIterator FMegaLightsVisualizationMenuCommands::CreateCommandConstIterator() const
{
	return CommandMap.CreateConstIterator();
}

void FMegaLightsVisualizationMenuCommands::RegisterCommands()
{
	BuildCommandMap();
}

void FMegaLightsVisualizationMenuCommands::BindCommands(FUICommandList& CommandList, const TSharedPtr<FEditorViewportClient>& Client) const
{
	// Map MegaLights visualization mode actions
	for (FMegaLightsVisualizationMenuCommands::TCommandConstIterator It = FMegaLightsVisualizationMenuCommands::Get().CreateCommandConstIterator(); It; ++It)
	{
		const FMegaLightsVisualizationMenuCommands::FMegaLightsVisualizationRecord& Record = It.Value();
		CommandList.MapAction(
			Record.Command,
			FExecuteAction::CreateStatic(&FMegaLightsVisualizationMenuCommands::ChangeMegaLightsVisualizationMode, Client.ToWeakPtr(), Record.Name),
			FCanExecuteAction(),
			FIsActionChecked::CreateStatic(&FMegaLightsVisualizationMenuCommands::IsMegaLightsVisualizationModeSelected, Client.ToWeakPtr(), Record.Name)
		);
	}
}

void FMegaLightsVisualizationMenuCommands::ChangeMegaLightsVisualizationMode(TWeakPtr<FEditorViewportClient> WeakClient, FName InName)
{
	if (TSharedPtr<FEditorViewportClient> Client = WeakClient.Pin())
	{
		Client->ChangeMegaLightsVisualizationMode(InName);
	}
}

bool FMegaLightsVisualizationMenuCommands::IsMegaLightsVisualizationModeSelected(TWeakPtr<FEditorViewportClient> WeakClient, FName InName)
{
	if (TSharedPtr<FEditorViewportClient> Client = WeakClient.Pin())
	{
		return Client->IsMegaLightsVisualizationModeSelected(InName);
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
