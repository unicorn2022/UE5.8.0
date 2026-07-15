// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieGraphCommon.h"
#include "StructUtils/PropertyBag.h"
#include "Templates/ValueOrError.h"

#include "MovieGraphValueView.generated.h"

#define UE_API MOVIERENDERPIPELINECORE_API

namespace UE::MovieGraph::Private
{
	template<typename ReturnType>
	bool GetOptionalValue(TValueOrError<ReturnType, EPropertyBagResult>& PropertyBagValue, ReturnType& OutValue)
	{
		// Convert the property bag-provided TValueOrError, which contains an EPropertyBagResult, to an output value and
		// a bool (signifying if there was an error or not). EPropertyBagResult shouldn't be exposed on the MRQ API.
		if (PropertyBagValue.HasValue())
		{
			OutValue = PropertyBagValue.StealValue();
			return true;
		}

		return false;
	}
}

/** Represents a property that should be added to a property bag via UMovieGraphValueView. */
USTRUCT(BlueprintType)
struct FMovieGraphValueViewProperty
{
	GENERATED_BODY()

	FMovieGraphValueViewProperty() = default;

	FMovieGraphValueViewProperty(
		const FName& InPropertyName,
		const EMovieGraphValueType ValueType,
		const TObjectPtr<UObject>& InValueTypeObject,
		const TMap<FName, FString>& InMetadata);

	/** The name of the property as it will appear in the property bag. */
	UPROPERTY(BlueprintReadWrite, Category = "Property")
	FName PropertyName;

	/** The type of value the property holds. */
	UPROPERTY(BlueprintReadWrite, Category = "Property")
	EMovieGraphValueType ValueType = EMovieGraphValueType::None;

	/** The value type object for object, struct, and enum-typed properties (nullptr for other types). */
	UPROPERTY(BlueprintReadWrite, Category = "Property")
	TObjectPtr<UObject> ValueTypeObject = nullptr;

	/** Metadata to apply to the property. */
	UPROPERTY(BlueprintReadWrite, Category = "Property")
	TMap<FName, FString> Metadata;
};

/**
 * Provides a scripting-friendly way to get/set values, and add/remove properties, in a property bag.
 *
 * Does not protect against invalid property bags. When using, ensure that the property bag that the view is initialized with is valid. The view
 * will also not take ownership of the property bag.
 */
UCLASS(Abstract)
class UMovieGraphValueView : public UObject
{
	GENERATED_BODY()

public:
	UMovieGraphValueView() = default; 

	UE_API explicit UMovieGraphValueView(FInstancedPropertyBag* InPropertyBag);

	/** Update the view to use the specified property bag. */
	UE_API void ViewProperties(FInstancedPropertyBag* InPropertyBag);

	/** Determines if this view is valid (meaning it points to a non-null property bag, and the bag is valid itself). */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool IsValid() const;

	/**
	 * Adds a new property to the container. If a property by the given name already exists, no add operation will occur. Returns true
	 * on successful add, otherwise returns false.
	 */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool AddProperty(const FName& InPropertyName, const EMovieGraphValueType ValueType, UObject* InValueTypeObject, const TMap<FName, FString>& InMetadata);

	/**
	 * Bulk adds properties to the container. Much more efficient than calling AddProperty() in a loop. Returns true if all properties were
	 * successfully added, else false.
	 */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool AddProperties(const TArray<FMovieGraphValueViewProperty>& InProperties);

	/** Removes a property by the given name. Returns true on successful remove, otherwise returns false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool RemoveProperty(const FName& InPropertyName);

	/** Removes all properties and their values, effectively resetting the property bag. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API void RemoveAllProperties();

	/** Returns true if a property by the given name exists in the container, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool HasProperty(const FName& InPropertyName) const;

	/** Gets the number of properties that this container holds. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API int32 GetNumProperties() const;

	/** Gets the names of properties in this container. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API TArray<FName> GetPropertyNames() const;

#if WITH_EDITOR
	/** Gets the metadata for the given property. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API TMap<FName, FString> GetPropertyMetadata(const FName& InPropertyName) const;
#endif	// WITH_EDITOR

	/** Gets the bool value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool GetValueBool(const FName& InPropertyName, bool& bOutValue) const;

	/** Gets the byte value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool GetValueByte(const FName& InPropertyName, uint8& OutValue) const;

	/** Gets the int32 value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool GetValueInt32(const FName& InPropertyName, int32& OutValue) const;

	/** Gets the int64 value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool GetValueInt64(const FName& InPropertyName, int64& OutValue) const;

	/** Gets the float value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool GetValueFloat(const FName& InPropertyName, float& OutValue) const;

	/** Gets the double value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool GetValueDouble(const FName& InPropertyName, double& OutValue) const;

	/** Gets the FName value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool GetValueName(const FName& InPropertyName, FName& OutValue) const;

	/** Gets the FString value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool GetValueString(const FName& InPropertyName, FString& OutValue) const;

	/** Gets the FText value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool GetValueText(const FName& InPropertyName, FText& OutValue) const;

	/** Gets the enum value (for a specific enum) of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool GetValueEnum(const FName& InPropertyName, uint8& OutValue, const UEnum* RequestedEnum = nullptr) const;

	/** Gets the struct value (for a specific struct) of the held data. Returns true on success, else false. */
	UE_API bool GetValueStruct(const FName& InPropertyName, FStructView& OutValue, const UScriptStruct* RequestedStruct = nullptr) const;

	/** Gets the object value (for a specific class) of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool GetValueObject(const FName& InPropertyName, UObject*& OutValue, const UClass* RequestedClass = nullptr) const;

	/** Gets the UClass value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool GetValueClass(const FName& InPropertyName, UClass*& OutValue) const;

	/** Gets the serialized string value of the held data. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API FString GetValueSerializedString(const FName& InPropertyName) const;

	/** Gets the enum value of the held data. Returns true on success, else false. */
	template <typename T>
	bool GetValueEnum(const FName& InPropertyName, T& OutValue) const
	{
		TValueOrError<T, EPropertyBagResult> Result = PropertyBag->GetValueEnum<T>(InPropertyName);
		return UE::MovieGraph::Private::GetOptionalValue<T>(Result, OutValue);
	}

	/** Gets the struct value of the held data. Returns true on success, else false. */
	template <typename T>
	bool GetValueStruct(const FName& InPropertyName, T*& OutValue) const
	{
		TValueOrError<T*, EPropertyBagResult> Result = PropertyBag->GetValueStruct<T*>(InPropertyName);
		return UE::MovieGraph::Private::GetOptionalValue<T*>(Result, OutValue);
	}

	/** Gets the object value of the held data. Returns true on success, else false. */
	template <typename T>
	bool GetValueObject(const FName& InPropertyName, T*& OutValue) const
	{
		TValueOrError<T*, EPropertyBagResult> Result = PropertyBag->GetValueObject<T*>(InPropertyName);
		return UE::MovieGraph::Private::GetOptionalValue<T*>(Result, OutValue);
	}

	/** Sets the bool value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool SetValueBool(const FName& InPropertyName, const bool bInValue);

	/** Sets the byte value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool SetValueByte(const FName& InPropertyName, const uint8 InValue);

	/** Sets the int32 value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool SetValueInt32(const FName& InPropertyName, const int32 InValue);

	/** Sets the int64 value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool SetValueInt64(const FName& InPropertyName, const int64 InValue);

	/** Sets the float value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool SetValueFloat(const FName& InPropertyName, const float InValue);

	/** Sets the double value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool SetValueDouble(const FName& InPropertyName, const double InValue);

	/** Sets the FName value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool SetValueName(const FName& InPropertyName, const FName InValue);

	/** Sets the FString value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool SetValueString(const FName& InPropertyName, const FString& InValue);

	/** Sets the FText value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool SetValueText(const FName& InPropertyName, const FText& InValue);

	/** Sets the enum value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool SetValueEnum(const FName& InPropertyName, const uint8 InValue, const UEnum* Enum);

	/** Sets the struct value of the held data. Returns true on success, else false. */
	UE_API bool SetValueStruct(const FName& InPropertyName, FConstStructView InValue);

	/** Sets the object value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool SetValueObject(const FName& InPropertyName, UObject* InValue);

	/** Sets the class value of the held data. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool SetValueClass(const FName& InPropertyName, UClass* InValue);

	/** Sets the serialized value of the held data. The string should be the serialized representation of the value. Returns true on success, else false. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API bool SetValueSerializedString(const FName& InPropertyName, const FString& NewValue);

	/** Sets the enum value of the held data. Returns true on success, else false. */
	template <typename T>
	bool SetValueEnum(const FName& InPropertyName, const T InValue)
	{
		return PropertyBag->SetValueEnum<T>(InPropertyName, InValue) == EPropertyBagResult::Success;
	}

	/** Sets the struct value of the held data. Returns true on success, else false. */
	template <typename T>
	bool SetValueStruct(const FName& InPropertyName, const T& InValue)
	{
		return PropertyBag->SetValueStruct<T>(InPropertyName, InValue) == EPropertyBagResult::Success;
	}

	/** Sets the object value of the held data. Returns true on success, else false. */
	template <typename T>
	bool SetValueObject(const FName& InPropertyName, T* InValue)
	{
		return PropertyBag->SetValueObject<T>(InPropertyName, InValue) == EPropertyBagResult::Success;
	}

	/** Gets the type of the stored data. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API EMovieGraphValueType GetValueType(const FName& InPropertyName) const;

	/** Sets the type of the stored data. Enums, structs, and classes must specify a value type object. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API void SetValueType(const FName& InPropertyName, EMovieGraphValueType ValueType, UObject* InValueTypeObject = nullptr);

	/** Gets the object that defines the enum, struct, or class. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API const UObject* GetValueTypeObject(const FName& InPropertyName) const;

	/** Sets the object that defines the enum, struct, or class. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API void SetValueTypeObject(const FName& InPropertyName, const UObject* ValueTypeObject);

	/** Gets the container type of the stored value. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API EMovieGraphContainerType GetValueContainerType(const FName& InPropertyName) const;

	/** Sets the container type of the stored value. */
	UFUNCTION(BlueprintCallable, Category = "Properties")
	UE_API void SetValueContainerType(const FName& InPropertyName, EMovieGraphContainerType ContainerType);

	/**
	 * Gets a reference to the array backing the value, if any. GetValueContainerType() will return
	 * EMovieGraphContainerType::Array if the value is holding an array.
	 */
	UE_API TValueOrError<FPropertyBagArrayRef, EPropertyBagResult> GetArrayRef(const FName& InPropertyName);

protected:
	/** Whether this view should allow properties to be added/removed from the property bag. */
	bool bAllowPropertyAddRemove = false;

private:
	FInstancedPropertyBag* PropertyBag = nullptr;
};

/**
 * Provides a scripting-friendly way to get/set values in a property bag. Does not allow adding/removing properties.
 */
UCLASS(BlueprintType)
class UMovieGraphFixedValueView final : public UMovieGraphValueView
{
	GENERATED_BODY()

public:
	UMovieGraphFixedValueView();

	UE_API explicit UMovieGraphFixedValueView(FInstancedPropertyBag* InPropertyBag);

	// The fixed view doesn't allow for adding/removing properties. Deleting these methods isn't foolproof, since a user could cast to the base class
	// and call these methods there. However, these methods also have internal logic that cue off bAllowPropertyAddRemove to prevent these methods
	// from performing any work.
	bool AddProperty(
		const FName& InPropertyName,
		const EMovieGraphValueType ValueType,
		UObject* InValueTypeObject,
		const TMap<FName, FString>& InMetadata) = delete;
	bool AddProperties(const TArray<FMovieGraphValueViewProperty>& InProperties) = delete;
	bool RemoveProperty(const FName& InPropertyName) = delete;
	void RemoveAllProperties() = delete;
};

/**
 * Provides a scripting-friendly way to get/set values, as well as add/remove properties, in a property bag.
 */
UCLASS(BlueprintType)
class UMovieGraphMutableValueView final : public UMovieGraphValueView
{
	GENERATED_BODY()

public:
	UMovieGraphMutableValueView();

	UE_API explicit UMovieGraphMutableValueView(FInstancedPropertyBag* InPropertyBag);
};

#undef UE_API