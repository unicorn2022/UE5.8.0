// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanFaceContourTrackerAsset.h"
#include "MetaHumanAuthoringObjects.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "NNE.h"
#include "NNEModelData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanFaceContourTrackerAsset)

#define LOCTEXT_NAMESPACE "FaceContourTracker"

TAutoConsoleVariable<FString> CVarMetaHumanFaceTrackerBackend
{
	TEXT("mh.FaceTracker.Backend"),
#if PLATFORM_WINDOWS
	"NNERuntimeORTDml",
	TEXT("Controls which NNE backend is used. Tested options are \"NNERuntimeORTDml\" or \"NNERuntimeORTCpu\""),
#elif PLATFORM_LINUX
	"NNERuntimeORTCpu",
	TEXT("Controls which NNE backend is used. Tested options are \"NNERuntimeORTCpu\""),
#elif PLATFORM_MAC
	"NNERuntimeORTCpu",
	TEXT("Controls which NNE backend is used. Tested options are \"NNERuntimeORTCpu\""),
#else // Console
	"NNERuntimeORTCpu",
	TEXT("Controls which NNE backend is used. Tested options are \"NNERuntimeORTCpu\""),
#endif
	ECVF_Default
};

#if WITH_EDITOR
TObjectPtr<UMetaHumanFaceContourTrackerAsset> UMetaHumanFaceContourTrackerAsset::LoadDefaultTracker()
{
	TObjectPtr<UMetaHumanFaceContourTrackerAsset> DefaultTracker;
	static constexpr const TCHAR* GenericTrackerPath = TEXT("/" UE_PLUGIN_NAME "/GenericTracker/GenericFaceContourTracker.GenericFaceContourTracker");
	if (UMetaHumanFaceContourTrackerAsset* Tracker = LoadObject<UMetaHumanFaceContourTrackerAsset>(GetTransientPackage(), GenericTrackerPath))
	{
		DefaultTracker = Tracker;
	}
	return DefaultTracker;
}

void UMetaHumanFaceContourTrackerAsset::PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	LoadedTrackerModels.Reset();
}

void UMetaHumanFaceContourTrackerAsset::UMetaHumanFaceContourTrackerAsset::PostTransacted(const FTransactionObjectEvent& InTransactionEvent)
{
	Super::PostTransacted(InTransactionEvent);

	LoadedTrackerModels.Reset();
}
#endif

void UMetaHumanFaceContourTrackerAsset::PostLoad()
{
	Super::PostLoad();

	// Find the tracking model objects. These could be in the plugin specified by the TSoftObjectPtr
	// path that was loaded, but they could also be in a different plugin. Update TSoftObjectPtr path
	// accordingly. This supports freely moving these large tracking models between plugins, ie 
	// from /MetaHuman/GenericTracker/Chin to /MetaHumanAuthoring/GenericTracker/Chin
	FMetaHumanAuthoringObjects::FindObject<UNNEModelData>(FaceDetectorModelData);
	FMetaHumanAuthoringObjects::FindObject<UNNEModelData>(FullFaceTrackerModelData);
	FMetaHumanAuthoringObjects::FindObject<UNNEModelData>(BrowsDenseTrackerModelData);
	FMetaHumanAuthoringObjects::FindObject<UNNEModelData>(EyesDenseTrackerModelData);
	FMetaHumanAuthoringObjects::FindObject<UNNEModelData>(MouthDenseTrackerModelData);
	FMetaHumanAuthoringObjects::FindObject<UNNEModelData>(LipzipDenseTrackerModelData);
	FMetaHumanAuthoringObjects::FindObject<UNNEModelData>(NasioLabialsDenseTrackerModelData);
	FMetaHumanAuthoringObjects::FindObject<UNNEModelData>(ChinDenseTrackerModelData);
	FMetaHumanAuthoringObjects::FindObject<UNNEModelData>(TeethDenseTrackerModelData);
	FMetaHumanAuthoringObjects::FindObject<UNNEModelData>(TeethConfidenceTrackerModelData);
}

TArray<TSoftObjectPtr<UNNEModelData>> UMetaHumanFaceContourTrackerAsset::GetTrackerModelData() const
{
	return
	{
		FaceDetectorModelData,
		FullFaceTrackerModelData,
		BrowsDenseTrackerModelData,
		EyesDenseTrackerModelData,
		MouthDenseTrackerModelData,
		LipzipDenseTrackerModelData,
		NasioLabialsDenseTrackerModelData,
		ChinDenseTrackerModelData,
		TeethDenseTrackerModelData,
		TeethConfidenceTrackerModelData
	};
}

TArray<TSharedPtr<UE::NNE::IModelInstanceRunSync>> UMetaHumanFaceContourTrackerAsset::GetTrackerModels() const
{
	return
	{
		FaceDetectorModel,
		FullFaceTrackerModel,
		BrowsDenseTrackerModel,
		EyesDenseTrackerModel,
		MouthDenseTrackerModel,
		LipzipDenseTrackerModel,
		NasioLabialsDenseTrackerModel,
		ChinDenseTrackerModel,
		TeethDenseTrackerModel,
		TeethConfidenceTrackerModel
	};
}

bool UMetaHumanFaceContourTrackerAsset::SetTrackerModels()
{
	if (LoadedTrackerModels.Num() != GetTrackerModels().Num())
	{
		return false;
	}

	FaceDetectorModel = LoadedTrackerModels[0];
	FullFaceTrackerModel = LoadedTrackerModels[1];
	BrowsDenseTrackerModel = LoadedTrackerModels[2];
	EyesDenseTrackerModel = LoadedTrackerModels[3];
	MouthDenseTrackerModel = LoadedTrackerModels[4];
	LipzipDenseTrackerModel = LoadedTrackerModels[5];
	NasioLabialsDenseTrackerModel = LoadedTrackerModels[6];
	ChinDenseTrackerModel = LoadedTrackerModels[7];
	TeethDenseTrackerModel = LoadedTrackerModels[8];
	TeethConfidenceTrackerModel = LoadedTrackerModels[9];

	return true;
}

TArray<FSoftObjectPath> UMetaHumanFaceContourTrackerAsset::GetTrackerModelDataAsSoftObjectPaths() const
{
	TArray<TSoftObjectPtr<UNNEModelData>> TrackerModelData = GetTrackerModelData();
	TArray<FSoftObjectPath> TrackerModelDataSoftObjectPaths;
	TrackerModelDataSoftObjectPaths.Reserve(TrackerModelData.Num());

	for (const TSoftObjectPtr<UNNEModelData>& ModelData : TrackerModelData)
	{
		TrackerModelDataSoftObjectPaths.Emplace(ModelData.ToSoftObjectPath());
	}

	return TrackerModelDataSoftObjectPaths;
}

bool UMetaHumanFaceContourTrackerAsset::CanProcess() const
{
	// TODO we want to add more validation here that the NNE models have the right number of outputs if possible
	// but needs extra functionality adding to the Pipeline HyprSense node
	for (const TSoftObjectPtr<UNNEModelData>& ModelData : GetTrackerModelData())
	{
		if (ModelData.IsNull())
		{
			return false;
		}
	}

	// we don't need to check the tracker models are valid, just the tracker model data

	return true;
}

void UMetaHumanFaceContourTrackerAsset::LoadTrackers(bool bInShowProgressNotification, TFunction<void(bool)>&& Callback)
{
	// Show a progress indicator if requested.
	if (bInShowProgressNotification)
	{
		// Only show if the trackers aren't loaded already.
		if (!AreTrackerModelsLoaded())
		{
			FNotificationInfo Info(LOCTEXT("LoadTrackersNotification", "Loading trackers..."));
			Info.bFireAndForget = false;
			LoadNotification = FSlateNotificationManager::Get().AddNotification(Info);
			if (LoadNotification.IsValid())
			{
				LoadNotification.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
			}
		}
	}

	FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
	TrackersLoadHandle = StreamableManager.RequestAsyncLoad(GetTrackerModelDataAsSoftObjectPaths(), [this, Callback]()
	{
		bool bLoadSucceeded = true;

		for (const TSoftObjectPtr<UNNEModelData>& ModelData : GetTrackerModelData())
		{
			if (ModelData.IsValid())
			{
				LoadedTrackerModelData.Add(ModelData.Get());
			}
			else
			{
				bLoadSucceeded = false;
			}
		}

		bLoadSucceeded &= CreateTrackerModels();

		if (LoadNotification.IsValid())
		{
			LoadNotification.Pin()->SetCompletionState(SNotificationItem::CS_None);
			LoadNotification.Pin()->ExpireAndFadeout();
		}

		Callback(bLoadSucceeded);
	});
}

void UMetaHumanFaceContourTrackerAsset::CancelLoadTrackers()
{
	if (TrackersLoadHandle.IsValid())
	{
		TrackersLoadHandle->CancelHandle();
	}

	if (LoadNotification.IsValid() && LoadNotification.Pin().IsValid())
	{
		LoadNotification.Pin()->SetCompletionState(SNotificationItem::CS_Fail);
		LoadNotification.Pin()->ExpireAndFadeout();
	}
}

bool UMetaHumanFaceContourTrackerAsset::LoadTrackersSynchronous()
{
	FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
	StreamableManager.RequestSyncLoad(GetTrackerModelDataAsSoftObjectPaths());
	bool bLoadSucceeded = true;

	for (const TSoftObjectPtr<UNNEModelData>& ModelData : GetTrackerModelData())
	{
		if (ModelData.IsValid())
		{
			LoadedTrackerModelData.Add(ModelData.Get());
		}
		else
		{
			bLoadSucceeded = false;
		}
	}

	bLoadSucceeded &= CreateTrackerModels();

	return bLoadSucceeded;
}

bool UMetaHumanFaceContourTrackerAsset::AreTrackerModelsLoaded() const
{
	TArray<TSoftObjectPtr<UNNEModelData>> ModelData = GetTrackerModelData();
	TArray<TSharedPtr<UE::NNE::IModelInstanceRunSync>> Models = GetTrackerModels();
	for (int i = 0; i < ModelData.Num(); i++)
	{
		if (!ModelData[i].IsNull() && (!ModelData[i].IsValid() || !Models[i].IsValid()))
		{
			return false;
		}
	}

	return true;
}

bool UMetaHumanFaceContourTrackerAsset::IsLoadingTrackers() const
{
	return TrackersLoadHandle.IsValid() && TrackersLoadHandle->IsLoadingInProgress();
}

bool UMetaHumanFaceContourTrackerAsset::CreateTrackerModels()
{
	if (LoadedTrackerModels.IsEmpty())
	{
		using namespace UE::NNE;

		TArray<TSoftObjectPtr<UNNEModelData>> ModelDataArray = GetTrackerModelData();
		LoadedTrackerModels.Empty();
		LoadedTrackerModels.Reserve(ModelDataArray.Num());

		const FString Backend = GetNNEBackend();

		if (Backend == "NNERuntimeORTDml") // GPU cases
		{
			TWeakInterfacePtr<INNERuntimeGPU> Runtime = GetRuntime<INNERuntimeGPU>(Backend);
			if (!Runtime.IsValid())
			{
				return false;
			}

			TArray<TSharedPtr<UE::NNE::IModelInstanceGPU>> GPUModels;
			GPUModels.Reserve(ModelDataArray.Num());

			for (const TSoftObjectPtr<UNNEModelData>& ModelData : ModelDataArray)
			{
				if (!ModelData.IsNull() && ModelData.IsValid())
				{
					TSharedPtr<IModelGPU> UniqueModel = Runtime->CreateModelGPU(ModelData.Get());
					if (!UniqueModel.IsValid())
					{
						return false;
					}

					TSharedPtr<IModelInstanceGPU> GPUModel = UniqueModel->CreateModelInstanceGPU();
					GPUModels.Add(GPUModel);
					LoadedTrackerModels.Emplace(GPUModel);
				}
			}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
			if (GPUModels.Num() != GetTrackerModels().Num())
			{
				return false;
			}

			FaceDetector = GPUModels[0];
			FullFaceTracker = GPUModels[1];
			BrowsDenseTracker = GPUModels[2];
			EyesDenseTracker = GPUModels[3];
			MouthDenseTracker = GPUModels[4];
			LipzipDenseTracker = GPUModels[5];
			NasioLabialsDenseTracker = GPUModels[6];
			ChinDenseTracker = GPUModels[7];
			TeethDenseTracker = GPUModels[8];
			TeethConfidenceTracker = GPUModels[9];
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
		else
		{
			TWeakInterfacePtr<INNERuntimeCPU> Runtime = GetRuntime<INNERuntimeCPU>(Backend);
			if (!Runtime.IsValid())
			{
				return false;
			}

			for (const TSoftObjectPtr<UNNEModelData>& ModelData : ModelDataArray)
			{
				if (!ModelData.IsNull() && ModelData.IsValid())
				{
					TSharedPtr<IModelCPU> UniqueModel = Runtime->CreateModelCPU(ModelData.Get());
					if (!UniqueModel.IsValid())
					{
						return false;
					}
					LoadedTrackerModels.Emplace(TSharedPtr<IModelInstanceRunSync>(UniqueModel->CreateModelInstanceCPU()));
				}
			}
		}
	}

	return SetTrackerModels();
}

void UMetaHumanFaceContourTrackerAsset::SetNNEBackend(const FString& InNNEBackend)
{
	NNEBackend = InNNEBackend;
}

FString UMetaHumanFaceContourTrackerAsset::GetNNEBackend(void) const
{
	if (NNEBackend.IsEmpty())
	{
		return CVarMetaHumanFaceTrackerBackend.GetValueOnAnyThread();
	}
	else
	{
		return NNEBackend;
	}
}

#undef LOCTEXT_NAMESPACE

