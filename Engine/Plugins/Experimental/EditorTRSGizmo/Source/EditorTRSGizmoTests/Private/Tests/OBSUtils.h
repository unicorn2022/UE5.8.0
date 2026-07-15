// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dom/JsonObject.h"

namespace OBS
{
	/** Converts a JSON object to its string representation. */
	FString JsonToString(const TSharedRef<FJsonObject>& InJsonObject);

	/** Parses a JSON string and converts it to a JSON object. */
	TSharedPtr<FJsonObject> StringToJson(const FString& InJsonString);

	/** Sets the field only if the provided optional is set. */
	template <typename ValueType>
	void EncodeOptional(TSharedPtr<FJsonObject>& InObject, const FString& InFieldName, const TOptional<ValueType>& InFieldValue);

	template <>
	void EncodeOptional<FString>(TSharedPtr<FJsonObject>& InObject, const FString& InFieldName, const TOptional<FString>& InFieldValue);

	template <typename NumericValueType UE_REQUIRES(std::is_integral_v<NumericValueType> || std::is_floating_point_v<NumericValueType>)>
	void EncodeOptional(TSharedPtr<FJsonObject>& InObject, const FString& InFieldName, const TOptional<NumericValueType>& InFieldValue);

	template <typename VectorType UE_REQUIRES(std::is_base_of_v<UE::Math::TVector2<typename VectorType::FReal>, VectorType>)>
	void EncodeOptional(TSharedPtr<FJsonObject>& InObject, const FString& InFieldNameX, const FString& InFieldNameY, const TOptional<VectorType>& InFieldValue);

	template <typename ValueType>
	void DecodeOptional(const TSharedPtr<FJsonObject>& InObject, const FString& InFieldName, TOptional<ValueType>& OutFieldValue);

	template <>
	void DecodeOptional<FString>(const TSharedPtr<FJsonObject>& InObject, const FString& InFieldName, TOptional<FString>& OutFieldValue);
	
	template <typename NumericValueType UE_REQUIRES(std::is_integral_v<NumericValueType> || std::is_floating_point_v<NumericValueType>)>
	void DecodeOptional(const TSharedPtr<FJsonObject>& InObject, const FString& InFieldName, TOptional<NumericValueType>& OutFieldValue);
	
	template <typename VectorType UE_REQUIRES(std::is_base_of_v<UE::Math::TVector2<typename VectorType::FReal>, VectorType>)>
	void DecodeOptional(const TSharedPtr<FJsonObject>& InObject, const FString& InFieldNameX, const FString& InFieldNameY, TOptional<VectorType>& OutFieldValue);
};
