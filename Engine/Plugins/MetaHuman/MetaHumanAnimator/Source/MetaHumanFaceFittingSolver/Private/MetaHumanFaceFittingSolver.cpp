// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanFaceFittingSolver.h"
#include "MetaHumanFaceAnimationSolver.h"
#include "MetaHumanConfig.h"
#include "MetaHumanConfigLog.h"
#include "UObject/Package.h"
#include "Interfaces/IPluginManager.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanFaceFittingSolver)



#if WITH_EDITOR

void UMetaHumanFaceFittingSolver::PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);
	NotifyInternalsChanged();
}

void UMetaHumanFaceFittingSolver::PostTransacted(const FTransactionObjectEvent& InTransactionEvent)
{
	Super::PostTransacted(InTransactionEvent);
	NotifyInternalsChanged();
}

#endif

void UMetaHumanFaceFittingSolver::LoadFaceFittingSolvers()
{
	FaceAnimationSolver = LoadObject<UMetaHumanFaceAnimationSolver>(nullptr, TEXT("/" UE_PLUGIN_NAME "/Solver/GenericFaceAnimationSolver.GenericFaceAnimationSolver"));
}

void UMetaHumanFaceFittingSolver::LoadPredictiveSolver()
{
	static const FString DepthProcessingPluginName = TEXT("MetaHumanDepthProcessing");
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(DepthProcessingPluginName);

	if (Plugin.IsValid() && Plugin->IsEnabled())
	{
		// Use LoadObject with a direct path instead of querying the AssetRegistry.
		// The AssetRegistry may not have finished discovering plugin content when this
		// is called during asset deserialization (e.g. from the DDC commandlet), causing
		// GetAssetsByPackageName to return empty results and leaving PredictiveSolver null.
		// LoadObject resolves the package path directly and works reliably in all contexts.
		const FString PredictiveSolverPath = FString::Printf(TEXT("/%s/Solver/GenericPredictiveSolver.GenericPredictiveSolver"), *DepthProcessingPluginName);
		PredictiveSolver = LoadObject<UMetaHumanConfig>(nullptr, *PredictiveSolverPath);

		if (!PredictiveSolver)
		{
			UE_LOGF(LogMetaHumanConfig, Error, "Failed to load predictive solver from %ls", *PredictiveSolverPath);
		}
	}
	else
	{
		UE_LOGF(LogMetaHumanConfig, Error, "Unable to load predictive solver. Please make sure the Depth Processing plugin is enabled. (Available on Fab)");
	}
}

bool UMetaHumanFaceFittingSolver::CanProcess() const
{
	return (bOverrideDeviceConfig == false || DeviceConfig != nullptr) && FaceAnimationSolver != nullptr && FaceAnimationSolver->CanProcess();
}

bool UMetaHumanFaceFittingSolver::GetConfigDisplayName(UCaptureData* InCaptureData, FString& OutName) const
{
	bool bSpecifiedCaptureData = true;

	if (bOverrideDeviceConfig && DeviceConfig)
	{
		OutName = DeviceConfig->Name;
	}
	else
	{
		bSpecifiedCaptureData = FMetaHumanConfig::GetInfo(InCaptureData, TEXT(""), OutName);
	}

	return bSpecifiedCaptureData;
}

FString UMetaHumanFaceFittingSolver::GetFittingTemplateData(UCaptureData* InCaptureData) const
{
	return GetEffectiveConfig(InCaptureData)->GetFittingTemplateData();
}

FString UMetaHumanFaceFittingSolver::GetFittingConfigData(UCaptureData* InCaptureData) const
{
	return GetEffectiveConfig(InCaptureData)->GetFittingConfigData();
}

FString UMetaHumanFaceFittingSolver::GetFittingConfigTeethData(UCaptureData* InCaptureData) const
{
	return GetEffectiveConfig(InCaptureData)->GetFittingConfigTeethData();
}

FString UMetaHumanFaceFittingSolver::GetFittingIdentityModelData(UCaptureData* InCaptureData) const
{
	return GetEffectiveConfig(InCaptureData)->GetFittingIdentityModelData();
}

FString UMetaHumanFaceFittingSolver::GetFittingControlsData(UCaptureData* InCaptureData) const
{
	return GetEffectiveConfig(InCaptureData)->GetFittingControlsData();
}

TArray<uint8> UMetaHumanFaceFittingSolver::GetPredictiveGlobalTeethTrainingData() const
{
	return PredictiveSolver->GetPredictiveGlobalTeethTrainingData();
}

TArray<uint8> UMetaHumanFaceFittingSolver::GetPredictiveTrainingData() const
{
	return PredictiveSolver->GetPredictiveTrainingData();
}

UMetaHumanFaceFittingSolver::FOnInternalsChanged& UMetaHumanFaceFittingSolver::OnInternalsChanged()
{
	return OnInternalsChangedDelegate;
}

UMetaHumanConfig* UMetaHumanFaceFittingSolver::GetEffectiveConfig(UCaptureData* InCaptureData) const
{
	if (bOverrideDeviceConfig && DeviceConfig)
	{
		return DeviceConfig;
	}
	else
	{
		check(InCaptureData);

		UMetaHumanConfig* Config;
		FMetaHumanConfig::GetInfo(InCaptureData, TEXT(""), Config);

		if (Config)
		{
			return Config;
		}
		else
		{
			check(false);
		}
	}

	return nullptr;
}

FString UMetaHumanFaceFittingSolver::JsonObjectAsString(TSharedPtr<FJsonObject> InJsonObject) const
{
	FString JsonString;

	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);

	if (!InJsonObject || !FJsonSerializer::Serialize(InJsonObject.ToSharedRef(), JsonWriter))
	{
		UE_LOGF(LogMetaHumanConfig, Fatal, "Failed to serialize json");
	}

	return JsonString;
}

void UMetaHumanFaceFittingSolver::NotifyInternalsChanged()
{
	OnInternalsChangedDelegate.Broadcast();
}
