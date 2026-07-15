// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCapWorkflowCustomization.h"
#include "PCapSettings.h"

TArray<UClass*> UPCapWorkflowCustomization::GetAllowedPanelWidgetClasses()
{
	UPerformanceCaptureSettings* Settings = UPerformanceCaptureSettings::GetPerformanceCaptureSettings();
	TArray<UClass*> AllowedClasses;
	AllowedClasses.Reserve(1);
	if (Settings != nullptr && !Settings->WorkflowPhaseUIBaseClass.IsNull())
	{
		AllowedClasses.Emplace(Settings->WorkflowPhaseUIBaseClass.LoadSynchronous());
	}
	return AllowedClasses;
}

#if WITH_EDITOR
void UPCapWorkflowCustomization::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	if(PropertyChangedEvent.Property != nullptr)
	{
		OnPCapCustomizationModified.Broadcast();
	}
}
#endif

