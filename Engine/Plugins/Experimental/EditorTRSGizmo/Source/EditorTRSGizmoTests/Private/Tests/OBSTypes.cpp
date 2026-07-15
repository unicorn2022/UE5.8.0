// Copyright Epic Games, Inc. All Rights Reserved.

#include "OBSTypes.h"

#include "JsonUtils/JsonConversion.h"
#include "OBSUtils.h"
#include "OBSUtils.inl"
#include "Serialization/JsonSerializer.h"
#include "WebSocketsModule.h"

namespace OBS
{
	namespace Private
	{
		FString BoundsTypeToString(const FSceneItem::FSceneItemTransform::EBoundsType InBoundsType)
		{
			switch (InBoundsType)
			{
			case FSceneItem::FSceneItemTransform::EBoundsType::StretchToBounds:
				return TEXT("OBS_BOUNDS_STRETCH");

			case FSceneItem::FSceneItemTransform::EBoundsType::ScaleToInnerBounds:
				return TEXT("OBS_BOUNDS_SCALE_INNER");

			case FSceneItem::FSceneItemTransform::EBoundsType::ScaleToOuterBounds:
				return TEXT("OBS_BOUNDS_SCALE_OUTER");

			case FSceneItem::FSceneItemTransform::EBoundsType::ScaleToWidthOfBounds:
				return TEXT("OBS_BOUNDS_SCALE_TO_WIDTH");

			case FSceneItem::FSceneItemTransform::EBoundsType::ScaleToHeightOfBounds:
				return TEXT("OBS_BOUNDS_SCALE_TO_HEIGHT");

			case FSceneItem::FSceneItemTransform::EBoundsType::MaximumSizeOnly:
				return TEXT("OBS_BOUNDS_MAX_ONLY");

			case FSceneItem::FSceneItemTransform::EBoundsType::None:
			default:
				return TEXT("OBS_BOUNDS_NONE");
			}
		}
	
		FSceneItem::FSceneItemTransform::EBoundsType BoundsTypeFromString(const FString& InBoundsTypeString)
		{
			if (InBoundsTypeString == TEXT("OBS_BOUNDS_STRETCH"))
			{
				return FSceneItem::FSceneItemTransform::EBoundsType::StretchToBounds;
			}
			else if (InBoundsTypeString == TEXT("OBS_BOUNDS_SCALE_INNER"))
			{
				return FSceneItem::FSceneItemTransform::EBoundsType::ScaleToInnerBounds;
			}
			else if (InBoundsTypeString == TEXT("OBS_BOUNDS_SCALE_OUTER"))
			{
				return FSceneItem::FSceneItemTransform::EBoundsType::ScaleToOuterBounds;
			}
			else if (InBoundsTypeString == TEXT("OBS_BOUNDS_SCALE_TO_WIDTH"))
			{
				return FSceneItem::FSceneItemTransform::EBoundsType::ScaleToWidthOfBounds;
			}
			else if (InBoundsTypeString == TEXT("OBS_BOUNDS_SCALE_TO_HEIGHT"))
			{
				return FSceneItem::FSceneItemTransform::EBoundsType::ScaleToHeightOfBounds;
			}
			else if (InBoundsTypeString == TEXT("OBS_BOUNDS_MAX_ONLY"))
			{
				return FSceneItem::FSceneItemTransform::EBoundsType::MaximumSizeOnly;
			}
		
			return FSceneItem::FSceneItemTransform::EBoundsType::None;
		}
	}

	FScene FScene::Decode(const TSharedPtr<FJsonObject>& InJsonObject)
	{
		FScene Scene;

		if (InJsonObject.IsValid())
		{
			FString SceneName;
			InJsonObject->TryGetStringField(TEXT("sceneName"), SceneName);

			FString SceneId;
			InJsonObject->TryGetStringField(TEXT("sceneUuid"), SceneId);

			Scene.Name = SceneName;
			Scene.UniqueId = SceneId;
		}

		return Scene;
	}

	TSharedPtr<FJsonObject> FSceneItem::FSceneItemTransform::Encode() const
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

		if (Alignment.IsSet())
		{
			JsonObject->SetNumberField(TEXT("alignment"), static_cast<uint8>(Alignment.GetValue()));	
		}

		if (BoundsAlignment.IsSet())
		{
			JsonObject->SetNumberField(TEXT("boundsAlignment"), static_cast<uint8>(BoundsAlignment.GetValue()));	
		}

		EncodeOptional(JsonObject, TEXT("boundsWidth"), BoundsWidth);
		EncodeOptional(JsonObject, TEXT("boundsHeight"), BoundsHeight);

		if (BoundsType.IsSet())
		{
			JsonObject->SetStringField(TEXT("boundsType"), OBS::Private::BoundsTypeToString(BoundsType.GetValue()));	
		}

		EncodeOptional(JsonObject, TEXT("height"), Height);
		EncodeOptional(JsonObject, TEXT("width"), Width);

		EncodeOptional(JsonObject, TEXT("positionX"), TEXT("positionY"), Position);
		EncodeOptional(JsonObject, TEXT("scaleX"), TEXT("scaleY"), Scale);

		return JsonObject;
	}

	FSceneItem::FSceneItemTransform FSceneItem::FSceneItemTransform::Decode(const TSharedPtr<FJsonObject>& InJsonObject)
	{
		FSceneItemTransform Transform;
		if (InJsonObject.IsValid())
		{
			uint8 AlignmentValue = 0;
			if (InJsonObject->TryGetNumberField(TEXT("alignment"), AlignmentValue))
			{
				Transform.Alignment = static_cast<EAlignment>(AlignmentValue);
			}

			uint8 BoundsAlignmentValue = 0;
			if (InJsonObject->TryGetNumberField(TEXT("boundsAlignment"), BoundsAlignmentValue))
			{
				Transform.BoundsAlignment = static_cast<EAlignment>(BoundsAlignmentValue);
			}

			DecodeOptional(InJsonObject, TEXT("boundsWidth"), Transform.BoundsWidth);
			DecodeOptional(InJsonObject, TEXT("boundsHeight"), Transform.BoundsHeight);
		
			FString BoundsTypeValue;
			if (InJsonObject->TryGetStringField(TEXT("boundsType"), BoundsTypeValue))
			{
				Transform.BoundsType = OBS::Private::BoundsTypeFromString(BoundsTypeValue);
			}

			DecodeOptional(InJsonObject, TEXT("height"), Transform.Height);
			DecodeOptional(InJsonObject, TEXT("width"), Transform.Width);

			DecodeOptional(InJsonObject, TEXT("positionX"), TEXT("positionY"), Transform.Position);
			DecodeOptional(InJsonObject, TEXT("scaleX"), TEXT("scaleY"), Transform.Scale);
		}

		return Transform;
	}

	FSceneItem FSceneItem::Decode(const TSharedPtr<FJsonObject>& InJsonObject)
	{
		FSceneItem SceneItem;

		if (InJsonObject.IsValid())
		{
			FString InputKind;
			InJsonObject->TryGetStringField(TEXT("inputKind"), InputKind);

			bool bIsSceneItemEnabled = false;
			InJsonObject->TryGetBoolField(TEXT("sceneItemEnabled"), bIsSceneItemEnabled);

			int32 SceneItemId = 0;
			InJsonObject->TryGetNumberField(TEXT("sceneItemId"), SceneItemId);

			int32 SceneItemIndex = 0;
			InJsonObject->TryGetNumberField(TEXT("sceneItemIndex"), SceneItemIndex);

			bool bIsSceneItemLocked = false;
			InJsonObject->TryGetBoolField(TEXT("sceneItemLocked"), bIsSceneItemLocked);

			const TSharedPtr<FJsonObject>* SceneItemTransformObject = nullptr;
			if (InJsonObject->TryGetObjectField(TEXT("sceneItemTransform"), SceneItemTransformObject))
			{
				SceneItem.SceneItemTransform = FSceneItemTransform::Decode(*SceneItemTransformObject);
			}

			FString SourceName;
			InJsonObject->TryGetStringField(TEXT("sourceName"), SourceName);

			FString SourceType;
			InJsonObject->TryGetStringField(TEXT("sourceType"), SourceType);

			FString SourceId;
			InJsonObject->TryGetStringField(TEXT("sourceUuid"), SourceId);

			SceneItem.InputKind = InputKind;
			SceneItem.bIsSceneItemEnabled = bIsSceneItemEnabled;
			SceneItem.SceneItemId = SceneItemId;
			SceneItem.SceneItemIndex = SceneItemIndex;
			SceneItem.bIsSceneItemLocked = bIsSceneItemLocked;

			SceneItem.SourceName = SourceName;
			SceneItem.SourceType = SourceType;
			SceneItem.SourceUniqueId = SourceId;
		}

		return SceneItem;
	}
}
