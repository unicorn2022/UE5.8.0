// Copyright Epic Games, Inc. All Rights Reserved.

#include "OBSMessages.h"

#include "OBSUtils.h"

namespace OBS
{
	FOperation::FOperation(const EOperationCode InOperationCode, const FString& InOperationName)
		: OperationCode(InOperationCode)
		, OperationName(InOperationName)
	{
	}

	FString FOperation::Encode() const
	{
		TSharedPtr<FJsonObject> RequestDataObject = nullptr;
		const TSharedPtr<FJsonObject> RootObject = FOperation::EncodeInternal(nullptr, RequestDataObject);

		EncodeToObject(RequestDataObject);

		return JsonToString(RootObject.ToSharedRef());
	}

	void FOperation::EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const
	{
	}

	TSharedPtr<FJsonObject> FOperation::EncodeInternal(const TSharedPtr<FJsonObject>& InRootObject) const
	{
		TSharedPtr<FJsonObject> OutUnusedPayloadObject = nullptr;
		return EncodeInternal(InRootObject, OutUnusedPayloadObject);
	}

	TSharedPtr<FJsonObject> FOperation::EncodeInternal(const TSharedPtr<FJsonObject>& InRootObject, TSharedPtr<FJsonObject>& OutPayloadDataObject) const
	{
		TSharedPtr<FJsonObject> JsonObject = InRootObject.IsValid() ? InRootObject : MakeShared<FJsonObject>();

		auto GetOrCreateJsonObject = [](const TSharedPtr<FJsonObject>& InParentObject, FStringView InFieldName) -> TSharedPtr<FJsonObject>
		{
			const TSharedPtr<FJsonObject>* ExistingObject = nullptr;
			TSharedPtr<FJsonObject> NewOrExistingObject = nullptr;
			if (InParentObject->TryGetObjectField(InFieldName, ExistingObject))
			{
				NewOrExistingObject = *ExistingObject;
			}
			else
			{
				NewOrExistingObject = MakeShared<FJsonObject>();
				InParentObject->SetObjectField(FString(InFieldName), NewOrExistingObject);
			}

			return NewOrExistingObject;
		};

		JsonObject->SetNumberField(TEXT("op"), static_cast<uint8>(OperationCode));

		const FString DataFieldName = TEXT("d");
		const TSharedPtr<FJsonObject> DataObject = GetOrCreateJsonObject(JsonObject, DataFieldName);

		// Identify operations do not use requestId and requestType fields
		if (OperationCode != EOperationCode::Identify)
		{
			DataObject->SetNumberField(TEXT("requestId"), Id);
			DataObject->SetStringField(TEXT("requestType"), OperationName);	
		}

		if (OperationCode == EOperationCode::Request)
		{
			const FString RequestDataFieldName = TEXT("requestData");
			OutPayloadDataObject = GetOrCreateJsonObject(DataObject, RequestDataFieldName);
		}
		else
		{
			OutPayloadDataObject = DataObject;
		}

		return JsonObject;
	}

	FGetSceneListRequest::FGetSceneListRequest()
		: FOperation(EOperationCode::Request, TEXT("GetSceneList"))
	{
	}

	FGetRecordStatusResponse FGetRecordStatusResponse::Decode(const TSharedPtr<FJsonObject>& InJsonObject)
	{
		FGetRecordStatusResponse Response;
		if (InJsonObject.IsValid())
		{
			InJsonObject->TryGetBoolField(TEXT("outputActive"), Response.bIsOutputActive);
			InJsonObject->TryGetBoolField(TEXT("outputPaused"), Response.bIsOutputPaused);
			InJsonObject->TryGetStringField(TEXT("outputTimecode"), Response.OutputTimecode);
			InJsonObject->TryGetNumberField(TEXT("outputDuration"), Response.OutputDuration);
			InJsonObject->TryGetNumberField(TEXT("outputBytes"), Response.OutputBytes);
		}

		return Response;
	}

	FGetSceneListResponse FGetSceneListResponse::Decode(const TSharedPtr<FJsonObject>& InJsonObject)
	{
		FGetSceneListResponse Response;
		if (InJsonObject.IsValid())
		{
			FString CurrentSceneName;
			InJsonObject->TryGetStringField(TEXT("currentProgramSceneName"), CurrentSceneName);

			FString CurrentSceneId;
			InJsonObject->TryGetStringField(TEXT("currentProgramSceneUuid"), CurrentSceneId);

			Response.CurrentScene = FScene{ CurrentSceneName, CurrentSceneId };

			const TArray<TSharedPtr<FJsonValue>>* ResponseScenes;
			if (InJsonObject->TryGetArrayField(TEXT("scenes"), ResponseScenes))
			{
				Response.Scenes.Reserve(ResponseScenes->Num());
				for (const TSharedPtr<FJsonValue>& ResponseScene : *ResponseScenes)
				{
					FScene Scene = FScene::Decode(ResponseScene->AsObject());
					Response.Scenes.Emplace(Scene);
				}
			}
		}

		return Response;
	}

	FGetProfileParameterRequest::FGetProfileParameterRequest(
		const FString& InParameterCategory,
		const FString& InParameterName)
		: FOperation(EOperationCode::Request, TEXT("GetProfileParameter"))
		, Parameter(InParameterCategory, InParameterName)
	{
	}

	void FGetProfileParameterRequest::EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const
	{
		InJsonObject->SetStringField(TEXT("parameterCategory"), Parameter.Category);
		InJsonObject->SetStringField(TEXT("parameterName"), Parameter.Name);
	}

	FSetProfileParameterRequest::FSetProfileParameterRequest(
		const FString& InParameterCategory,
		const FString& InParameterName,
		const FString& InParameterValue)
		: FOperation(EOperationCode::Request, TEXT("SetProfileParameter"))
		, Parameter(InParameterCategory, InParameterName, InParameterValue)
	{
	}

	void FSetProfileParameterRequest::EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const
	{
		InJsonObject->SetStringField(TEXT("parameterCategory"), Parameter.Category);
		InJsonObject->SetStringField(TEXT("parameterName"), Parameter.Name);
		InJsonObject->SetStringField(TEXT("parameterValue"), Parameter.Value.Get(FString()));
	}

	FCreateSceneRequest::FCreateSceneRequest(const FString& InSceneName)
		: FOperation(EOperationCode::Request, TEXT("CreateScene"))
		, SceneName(InSceneName)
	{
	}

	void FCreateSceneRequest::EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const
	{
		InJsonObject->SetStringField(TEXT("sceneName"), SceneName);
	}

	FGetSceneItemListRequest::FGetSceneItemListRequest(const FScene& InScene)
		: FOperation(EOperationCode::Request, TEXT("GetSceneItemList"))
		, Scene(InScene)
	{
	}

	void FGetSceneItemListRequest::EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const
	{
		InJsonObject->SetStringField(TEXT("sceneName"), Scene.Name);
		InJsonObject->SetStringField(TEXT("sceneUuid"), Scene.UniqueId);
	}

	FGetRecordStatusRequest::FGetRecordStatusRequest()
		: FOperation(EOperationCode::Request, TEXT("GetRecordStatus"))
	{
	}

	FStartRecordRequest::FStartRecordRequest()
		: FOperation(EOperationCode::Request, TEXT("StartRecord"))
	{
	}

	FStopRecordRequest::FStopRecordRequest()
		: FOperation(EOperationCode::Request, TEXT("StopRecord"))
	{
	}

	FPauseRecordRequest::FPauseRecordRequest()
		: FOperation(EOperationCode::Request, TEXT("PauseRecord"))
	{
	}

	FResumeRecordRequest::FResumeRecordRequest()
		: FOperation(EOperationCode::Request, TEXT("ResumeRecord"))
	{
	}

	FGetInputListRequest::FGetInputListRequest(const FString& InInputKind)
		: FOperation(EOperationCode::Request, TEXT("GetInputList"))
		, InputKind(InInputKind)
	{
	}

	void FGetInputListRequest::EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const
	{
		InJsonObject->SetStringField(TEXT("inputKind"), InputKind);
	}

	FGetInputSettingsRequest::FGetInputSettingsRequest(const FString& InInputName, const FString& InInputUniqueId)
		: FOperation(EOperationCode::Request, TEXT("GetInputSettings"))
		, InputName(InInputName)
		, InputUniqueId(InInputUniqueId)
	{
	}

	void FGetInputSettingsRequest::EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const
	{
		InJsonObject->SetStringField(TEXT("inputName"), InputName);
		InJsonObject->SetStringField(TEXT("inputUuid"), InputUniqueId);
	}

	FGetInputDefaultSettingsRequest::FGetInputDefaultSettingsRequest(const FString& InInputKind)
		: FOperation(EOperationCode::Request, TEXT("GetInputDefaultSettings"))
		, InputKind(InInputKind)
	{
	}

	void FGetInputDefaultSettingsRequest::EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const
	{
		InJsonObject->SetStringField(TEXT("inputKind"), InputKind);
	}

	FSetInputSettingsRequest::FSetInputSettingsRequest(
		const FString& InInputName,
		const FString& InInputUniqueId,
		const TSharedPtr<FJsonObject>& InInputSettings)
		: FOperation(EOperationCode::Request, TEXT("SetInputSettings"))
		, InputName(InInputName)
		, InputUniqueId(InInputUniqueId)
		, InputSettings(InInputSettings)
	{
	}

	void FSetInputSettingsRequest::EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const
	{
		InJsonObject->SetStringField(TEXT("inputName"), InputName);
		InJsonObject->SetStringField(TEXT("inputUuid"), InputUniqueId);
		InJsonObject->SetObjectField(TEXT("inputSettings"), InputSettings);
	}

	FCreateInputRequest::FCreateInputRequest(
		const FScene& InScene,
		const FString& InInputName,
		const FString& InInputKind,
		const TSharedPtr<FJsonObject>& InInputSettings,
		const bool bIsSceneItemEnabled)
		: FOperation(EOperationCode::Request, TEXT("CreateInput"))
		, Scene(InScene)
		, InputName(InInputName)
		, InputKind(InInputKind)
		, InputSettings(InInputSettings)
		, bIsSceneItemEnabled(bIsSceneItemEnabled)
	{
	}

	void FCreateInputRequest::EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const
	{
		InJsonObject->SetStringField(TEXT("sceneName"), Scene.Name);
		InJsonObject->SetStringField(TEXT("sceneUuid"), Scene.UniqueId);
		InJsonObject->SetStringField(TEXT("inputName"), InputName);
		InJsonObject->SetStringField(TEXT("inputKind"), InputKind);
		InJsonObject->SetObjectField(TEXT("inputSettings"), InputSettings);
		InJsonObject->SetBoolField(TEXT("sceneItemEnabled"), bIsSceneItemEnabled);
	}

	FGetCurrentProgramSceneRequest::FGetCurrentProgramSceneRequest()
		: FOperation(EOperationCode::Request, TEXT("GetCurrentProgramScene"))
	{
	}

	FSetCurrentProgramSceneRequest::FSetCurrentProgramSceneRequest(const FScene& InScene)
		: FOperation(EOperationCode::Request, TEXT("SetCurrentProgramScene"))
		, Scene(InScene)
	{
	}

	void FSetCurrentProgramSceneRequest::EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const
	{
		InJsonObject->SetStringField(TEXT("sceneName"), Scene.Name);
		InJsonObject->SetStringField(TEXT("sceneUuid"), Scene.UniqueId);
	}

	FSetSceneItemTransformRequest::FSetSceneItemTransformRequest(
		const FScene& InScene,
		const FSceneItem& InSceneItem,
		const FSceneItem::FSceneItemTransform& InSceneItemTransform)
		: FOperation(EOperationCode::Request, TEXT("SetSceneItemTransform"))
		, Scene(InScene)
		, SceneItem(InSceneItem)
		, SceneItemTransform(InSceneItemTransform)
	{
	}

	void FSetSceneItemTransformRequest::EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const
	{
		InJsonObject->SetStringField(TEXT("sceneName"), Scene.Name);
		InJsonObject->SetStringField(TEXT("sceneUuid"), Scene.UniqueId);
		InJsonObject->SetNumberField(TEXT("sceneItemId"), SceneItem.SceneItemId);

		const TSharedPtr<FJsonObject> SceneItemTransformObject = SceneItemTransform.Encode();
		InJsonObject->SetObjectField(TEXT("sceneItemTransform"), SceneItemTransformObject);
	}

	FCreateRecordChapterRequest::FCreateRecordChapterRequest(const FString& InChapterName)
		: FOperation(EOperationCode::Request, TEXT("CreateRecordChapter"))
		, ChapterName(InChapterName)
	{
	}

	void FCreateRecordChapterRequest::EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const
	{
		InJsonObject->SetStringField(TEXT("chapterName"), ChapterName);
	}

	FIdentifyRequest::FIdentifyRequest()
		: FOperation(EOperationCode::Identify, TEXT("Identify"))
	{
	}

	void FIdentifyRequest::EncodeToObject(const TSharedPtr<FJsonObject>& InJsonObject) const
	{
		InJsonObject->SetNumberField(TEXT("rpcVersion"), RPCVersion);
		InJsonObject->SetNumberField(TEXT("eventSubscriptions"), EventSubscriptions);
	}
}
