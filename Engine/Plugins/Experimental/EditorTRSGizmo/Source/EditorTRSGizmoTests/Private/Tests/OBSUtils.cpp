// Copyright Epic Games, Inc. All Rights Reserved.

#include "OBSUtils.h"

#include "JsonUtils/JsonConversion.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace OBS
{
	FString JsonToString(const TSharedRef<FJsonObject>& InJsonObject)
	{
		FString JsonString;

		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString);
		if (!FJsonSerializer::Serialize(InJsonObject, Writer))
		{
			// Serialization failed, return empty string
			return FString();
		}

		return JsonString;
	}

	TSharedPtr<FJsonObject> StringToJson(const FString& InJsonString)
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJsonString);
		if (!FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			// Invalidate the object itself if deserialization fails
			JsonObject.Reset();
		}

		return JsonObject;
	}
}
