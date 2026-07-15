// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterPaletteEditorAnalytics.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionPipeline.h"
#include "MetaHumanInstance.h"
#include "MetaHumanItemPipeline.h"
#include "MetaHumanWardrobeItem.h"

#include "AssetRegistry/AssetData.h"
#include "EngineAnalytics.h"
#include "AnalyticsEventAttribute.h"
#include "GeneralProjectSettings.h"
#include "Misc/SecureHash.h"
#include "UObject/PrimaryAssetId.h"

namespace UE::MetaHuman::Analytics
{
	namespace Private
	{
		const FString EventNamePrefix = TEXT("Editor.MetaHumanCharacterPalette.");

		FString AnonymizeString(const FString& String)
		{
			FSHA1 Sha1;
			Sha1.UpdateWithString(*String, String.Len());
			const FSHAHash HashedName = Sha1.Finalize();
			return HashedName.ToString();
		}

		FString AnonymizeName(const FName& Name)
		{
			return AnonymizeString(Name.ToString());
		}

		FString MakeAnonymizedAssetId(const UObject* InAsset)
		{
			FPrimaryAssetId PrimaryAssetId(*InAsset->GetPathName(), InAsset->GetFName());
			const FString PrimaryAssetIdStr = PrimaryAssetId.PrimaryAssetType.GetName().ToString() / PrimaryAssetId.PrimaryAssetName.ToString();
			const UGeneralProjectSettings* GeneralProjectSettings = GetDefault<UGeneralProjectSettings>();
			const FString ProjectInfoStr = GeneralProjectSettings->ProjectID.ToString();
			return AnonymizeString(ProjectInfoStr / PrimaryAssetIdStr);
		}

		void StartRecordEvent(TArray<FAnalyticsEventAttribute>& EventAttributes, const UMetaHumanCollection* InCollection)
		{
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("CollectionId"), MakeAnonymizedAssetId(InCollection)));
		}

		void AddInstanceIdAttribute(TArray<FAnalyticsEventAttribute>& EventAttributes, const UMetaHumanInstance* InInstance)
		{
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("InstanceId"), MakeAnonymizedAssetId(InInstance)));
		}

		void FinishRecordEvent(const FString& EventName, const TArray<FAnalyticsEventAttribute>& EventAttributes)
		{
			check(FEngineAnalytics::IsAvailable());
			const FString FullEventName = EventNamePrefix + EventName;
			FEngineAnalytics::GetProvider().RecordEvent(FullEventName, EventAttributes);
		}
	}

#define NO_ANALYTICS_CIRCUIT_BREAK()\
if (!FEngineAnalytics::IsAvailable()) return

#define EVENT_BEGIN_BODY(EventName)\
	NO_ANALYTICS_CIRCUIT_BREAK();\
	const FString EventNameStr = TEXT(#EventName);\
	TArray<FAnalyticsEventAttribute> EventAttributes;\
	StartRecordEvent(EventAttributes, InCollection)

#define EVENT_END_BODY()\
	FinishRecordEvent(EventNameStr, EventAttributes)

#define BEGIN_RECORD_EVENT(EventName,FuncName,...)\
void Record##FuncName##Event(TNotNull<const UMetaHumanCollection*> InCollection, __VA_ARGS__)\
{\
	EVENT_BEGIN_BODY(EventName);

#define END_RECORD_EVENT()\
	EVENT_END_BODY();\
}

#define DEFINE_RECORD_EVENT(EventName,FuncName)\
void Record##FuncName##Event(TNotNull<const UMetaHumanCollection*> InCollection)\
{\
	EVENT_BEGIN_BODY(EventName);\
	EVENT_END_BODY();\
}

	// Event implementations ==============================================================================================================================
	using namespace Private;

	DEFINE_RECORD_EVENT(CreateCollection, CreateCollection);
	DEFINE_RECORD_EVENT(OpenCollectionEditor, OpenCollectionEditor);

	BEGIN_RECORD_EVENT(CreateInstance, CreateInstance, TNotNull<const UMetaHumanInstance*> InInstance)
	{
		AddInstanceIdAttribute(EventAttributes, InInstance);
	}
	END_RECORD_EVENT();

	BEGIN_RECORD_EVENT(OpenInstanceEditor, OpenInstanceEditor, TNotNull<const UMetaHumanInstance*> InInstance)
	{
		AddInstanceIdAttribute(EventAttributes, InInstance);
	}
	END_RECORD_EVENT();

	void RecordBuildCollectionEvent(TNotNull<const UMetaHumanCollection*> InCollection)
	{
		// Skip the event entirely if the Collection has no Pipeline; the Collection cannot be built in this state.
		const UMetaHumanCollectionPipeline* Pipeline = InCollection->GetPipeline();
		if (Pipeline == nullptr)
		{
			return;
		}

		EVENT_BEGIN_BODY(BuildCollection);

		const UClass* PipelineClass = Pipeline->GetClass();
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("PipelineID"), AnonymizeString(PipelineClass->GetPathName())));

		if (const UClass* PipelineParentClass = PipelineClass->GetSuperClass())
		{
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("PipelineParentID"), AnonymizeString(PipelineParentClass->GetPathName())));
		}

		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("TotalItems"), InCollection->GetItems().Num()));

		EVENT_END_BODY();
	}

	void RecordDropItemsOnCollectionEvent(
		TNotNull<const UMetaHumanCollection*> InCollection,
		FName InTargetSlotName,
		int32 InNumItemsAdded,
		const FAssetData& InSampleAsset,
		const UMetaHumanWardrobeItem* InSampleWardrobeItem)
	{
		EVENT_BEGIN_BODY(DropItemsOnCollection);

		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("NumItemsAdded"), InNumItemsAdded));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("TotalItems"), InCollection->GetItems().Num()));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("SlotName"), AnonymizeName(InTargetSlotName)));

		// Sample asset (the first successfully added asset from the drop).
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("SampleAssetID"), AnonymizeString(InSampleAsset.GetSoftObjectPath().ToString())));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("SampleAssetClassID"), AnonymizeString(InSampleAsset.AssetClassPath.ToString())));

		const bool bIsExternalWardrobeItem = InSampleWardrobeItem != nullptr;
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("SampleAssetIsExternalWardrobeItem"), bIsExternalWardrobeItem));

		if (bIsExternalWardrobeItem)
		{
			if (const UMetaHumanItemPipeline* WardrobeItemPipeline = InSampleWardrobeItem->GetPipeline())
			{
				const UClass* PipelineClass = WardrobeItemPipeline->GetClass();
				EventAttributes.Add(FAnalyticsEventAttribute(TEXT("SampleWardrobeItemPipelineID"), AnonymizeString(PipelineClass->GetPathName())));

				if (const UClass* PipelineParentClass = PipelineClass->GetSuperClass())
				{
					EventAttributes.Add(FAnalyticsEventAttribute(TEXT("SampleWardrobeItemPipelineParentID"), AnonymizeString(PipelineParentClass->GetPathName())));
				}
			}
		}

		EVENT_END_BODY();
	}
}
