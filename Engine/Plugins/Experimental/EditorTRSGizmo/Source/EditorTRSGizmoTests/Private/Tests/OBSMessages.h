// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Future.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "OBSTypes.h"

class IWebSocket;

namespace OBS
{
	enum class EOperationCode : uint8
	{
		Hello = 0,
		Identify = 1,
		Identified = 2,
		Reidentify = 3,
		// @note: The missed number here is intentional and matches the spec
		Event = 5,
		Request = 6,
		RequestResponse = 7,
		RequestBatch = 8,
		RequestBatchResponse = 9,

		None = 99
	};

	/** An operation is usually a request, but can be Identify, etc. */
	struct FOperation
	{
		explicit FOperation(const EOperationCode InOperationCode, const FString& InOperationName);
		virtual ~FOperation() = default;

		int32 Id = INDEX_NONE;
		EOperationCode OperationCode = EOperationCode::None;
		FString OperationName;

		virtual FString Encode() const;

		/** Adds/Updates fields in the given JsonObject. */
		virtual void EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const;

	protected:
		friend struct FBatchRequest;

		/** Adds to the given JsonObject or creates a new one if not valid. */
		[[maybe_unused]] virtual TSharedPtr<FJsonObject> EncodeInternal(const TSharedPtr<FJsonObject>& InRootObject) const;

		/** Adds to the given JsonObject or creates a new one if not valid. Outputs a PayloadDataObject (d::requestData, etc.) */
		[[maybe_unused]] virtual TSharedPtr<FJsonObject> EncodeInternal(const TSharedPtr<FJsonObject>& InRootObject, TSharedPtr<FJsonObject>& OutPayloadDataObject) const;
	};

	template <typename RequestType UE_REQUIRES(std::is_base_of_v<FOperation, RequestType>)>
	struct TBatchRequest : FOperation
	{
		explicit TBatchRequest(const TArray<RequestType>& InRequests);

		TArray<RequestType> Requests;

		virtual FString Encode() const override;
	};

	struct FGetSceneListRequest : FOperation
	{
		FGetSceneListRequest();
	};

	/**
	 * A response containing a list of scenes.
	 */
	struct FGetSceneListResponse
	{
		FScene CurrentScene;
		TArray<FScene> Scenes;

		static FGetSceneListResponse Decode(const TSharedPtr<FJsonObject>& InJsonObject);
	};

	struct FGetProfileParameterRequest : FOperation
	{
		explicit FGetProfileParameterRequest(const FString& InParameterCategory, const FString& InParameterName);

		FParameter Parameter;

		virtual void EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const override;
	};

	struct FSetProfileParameterRequest : FOperation
	{
		explicit FSetProfileParameterRequest(const FString& InParameterCategory, const FString& InParameterName, const FString& InParameterValue);

		FParameter Parameter;

		virtual void EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const override;
	};

	struct FCreateSceneRequest : FOperation
	{
		explicit FCreateSceneRequest(const FString& InSceneName);

		FString SceneName;

		virtual void EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const override;
	};

	struct FGetSceneItemListRequest : FOperation
	{
		explicit FGetSceneItemListRequest(const FScene& InScene);

		FScene Scene;

		virtual void EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const override;
	};

	struct FGetRecordStatusRequest : FOperation
	{
		explicit FGetRecordStatusRequest();
	};

	struct FGetRecordStatusResponse
	{
		bool bIsOutputActive = false;
		bool bIsOutputPaused = false;
		FString OutputTimecode;
		int64 OutputDuration = 0.0f;
		uint64 OutputBytes = 0;

		static FGetRecordStatusResponse Decode(const TSharedPtr<FJsonObject>& InJsonObject);
	};

	struct FStartRecordRequest : FOperation
	{
		explicit FStartRecordRequest();
	};

	struct FStopRecordRequest : FOperation
	{
		explicit FStopRecordRequest();
	};

	struct FPauseRecordRequest : FOperation
	{
		explicit FPauseRecordRequest();
	};

	struct FResumeRecordRequest : FOperation
	{
		explicit FResumeRecordRequest();
	};

	struct FGetInputListRequest : FOperation
	{
		explicit FGetInputListRequest(const FString& InInputKind);

		virtual void EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const override;

	private:
		FString InputKind;
	};

	struct FGetInputSettingsRequest : FOperation
	{
		explicit FGetInputSettingsRequest(const FString& InInputName, const FString& InInputUniqueId);

		virtual void EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const override;

	private:
		FString InputName;
		FString InputUniqueId;
	};

	struct FGetInputDefaultSettingsRequest : FOperation
	{
		explicit FGetInputDefaultSettingsRequest(const FString& InInputKind);

		virtual void EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const override;

	private:
		FString InputKind;
	};

	struct FSetInputSettingsRequest : FOperation
	{
		explicit FSetInputSettingsRequest(const FString& InInputName, const FString& InInputUniqueId, const TSharedPtr<FJsonObject>& InInputSettings);

		virtual void EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const override;

	private:
		FString InputName;
		FString InputUniqueId;
		TSharedPtr<FJsonObject> InputSettings = nullptr;
	};

	struct FCreateInputRequest : FOperation
	{
		using FResponse = FSceneItem;

		explicit FCreateInputRequest(
			const FScene& InScene, const FString& InInputName, const FString& InInputKind, const TSharedPtr<FJsonObject>& InInputSettings, const bool bIsSceneItemEnabled = true);

		virtual void EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const override;

	private:
		FScene Scene;
		FString InputName;
		FString InputKind;
		TSharedPtr<FJsonObject> InputSettings;
		bool bIsSceneItemEnabled = true;
	};

	struct FGetCurrentProgramSceneRequest : FOperation
	{
		using FResponse = FScene;

		explicit FGetCurrentProgramSceneRequest();
	};

	struct FSetCurrentProgramSceneRequest : FOperation
	{
		explicit FSetCurrentProgramSceneRequest(const FScene& InScene);

		virtual void EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const override;

	private:
		FScene Scene;
	};

	struct FSetSceneItemTransformRequest : FOperation
	{
		explicit FSetSceneItemTransformRequest(const FScene& InScene, const FSceneItem& InSceneItem, const FSceneItem::FSceneItemTransform& InSceneItemTransform);

		virtual void EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const override;

	private:
		FScene Scene;
		FSceneItem SceneItem;
		FSceneItem::FSceneItemTransform SceneItemTransform;
	};

	struct FCreateRecordChapterRequest : FOperation
	{
		explicit FCreateRecordChapterRequest(const FString& InChapterName);

		virtual void EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const override;

	private:
		FString ChapterName;
	};

	struct FIdentifyRequest : FOperation
	{
		FIdentifyRequest();

		virtual void EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const override;

	private:
		static constexpr int32 RPCVersion = 1;

		/** Bitflags, taken from the JS client example. */
		static constexpr int32 EventSubscriptions = 131071;
	};
}
