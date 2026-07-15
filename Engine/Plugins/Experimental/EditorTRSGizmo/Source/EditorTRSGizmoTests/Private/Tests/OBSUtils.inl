// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OBSUtils.h"
#include "Tessellation/Affine.h"

namespace OBS
{
	template <>
	inline void EncodeOptional<FString>(TSharedPtr<FJsonObject>& InObject, const FString& InFieldName, const TOptional<FString>& InFieldValue)
	{
		if (InFieldValue.IsSet())
		{
			InObject->SetStringField(InFieldName, InFieldValue.GetValue());
		}
	}

	template <typename NumericValueType UE_REQUIRES(std::is_integral_v<NumericValueType> || std::is_floating_point_v<NumericValueType>)>
	void EncodeOptional(TSharedPtr<FJsonObject>& InObject, const FString& InFieldName, const TOptional<NumericValueType>& InFieldValue)
	{
		if (InFieldValue.IsSet())
		{
			InObject->SetNumberField(InFieldName, InFieldValue.GetValue());
		}
	}

	template <typename VectorType UE_REQUIRES(std::is_base_of_v<UE::Math::TVector2<typename VectorType::FReal>, VectorType>)>
	void EncodeOptional(TSharedPtr<FJsonObject>& InObject, const FString& InFieldNameX, const FString& InFieldNameY, const TOptional<VectorType>& InFieldValue)
	{
		if (InFieldValue.IsSet())
		{
			InObject->SetNumberField(InFieldNameX, InFieldValue.GetValue().X);
			InObject->SetNumberField(InFieldNameY, InFieldValue.GetValue().Y);
		}
	}

	template <>
	inline void DecodeOptional<FString>(const TSharedPtr<FJsonObject>& InObject, const FString& InFieldName, TOptional<FString>& OutFieldValue)
	{
		FString Value;
		if (InObject->TryGetStringField(InFieldName, Value))
		{
			OutFieldValue = Value;
		}
	}

	template <typename NumericValueType UE_REQUIRES(std::is_integral_v<NumericValueType> || std::is_floating_point_v<NumericValueType>)>
	void DecodeOptional(const TSharedPtr<FJsonObject>& InObject, const FString& InFieldName, TOptional<NumericValueType>& OutFieldValue)
	{
		NumericValueType Value;
		if (InObject->TryGetNumberField(InFieldName, Value))
		{
			OutFieldValue = Value;
		}
	}

	template <typename VectorType UE_REQUIRES(std::is_base_of_v<UE::Math::TVector2<typename VectorType::FReal>, VectorType>)>
	void DecodeOptional(const TSharedPtr<FJsonObject>& InObject, const FString& InFieldNameX, const FString& InFieldNameY, TOptional<VectorType>& OutFieldValue)
	{
		bool bHasValue = false;
		UE::Math::TVector2<typename VectorType::FReal> Value(EForceInit::ForceInitToZero);

		typename VectorType::FReal ValueX;
		if (InObject->TryGetNumberField(InFieldNameX, ValueX))
		{
			bHasValue = true;
			Value.X = ValueX;
		}

		typename VectorType::FReal ValueY;
		if (InObject->TryGetNumberField(InFieldNameY, ValueY))
		{
			bHasValue = true;
			Value.Y = ValueY;
		}

		if (bHasValue)
		{
			OutFieldValue = Value;
		}
	}
};
