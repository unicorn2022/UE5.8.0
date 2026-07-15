// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

#include "EditorOnlyAssetReference.generated.h"

struct FPropertyTag;

#define UE_API METAHUMANCHARACTERPALETTE_API

/**
 * A reference to a UObject asset that can be used as an identifier in cooked builds without
 * causing the referenced asset to be cooked as a dependency.
 *
 * In editor builds the reference uses a TSoftObjectPtr, so moving or renaming the referenced 
 * asset updates this property as it would any other soft pointer.
 *
 * The API is intentionally very similar to TSoftObjectPtr, so that it can be used as a drop-in
 * replacement for it in many cases. 
 *
 * This struct can even be used to replace an existing TSoftObjectPtr UPROPERTY. Existing saved
 * data that wrote the property as a TSoftObjectPtr migrates automatically on load via
 * SerializeFromMismatchedTag. No core redirect, rename, or deprecation is required, but note 
 * that the conversion only works from TSoftObjectPtr to FEditorOnlyAssetReference. Once the 
 * FEditorOnlyAssetReference is saved into the object, it can't be automatically converted back 
 * to TSoftObjectPtr if the code change is reverted.
 */
USTRUCT(BlueprintType)
struct FEditorOnlyAssetReference
{
	GENERATED_BODY()

public:
	UE_API FEditorOnlyAssetReference();
	UE_API explicit FEditorOnlyAssetReference(const TSoftObjectPtr<UObject>& InAsset);
	UE_API explicit FEditorOnlyAssetReference(const FSoftObjectPath& InPath);
	UE_API explicit FEditorOnlyAssetReference(const UObject* InObject);

	UE_API FEditorOnlyAssetReference& operator=(const TSoftObjectPtr<UObject>& InAsset);
	UE_API FEditorOnlyAssetReference& operator=(const FSoftObjectPath& InPath);
	UE_API FEditorOnlyAssetReference& operator=(const UObject* InObject);

	/** Returns the stored asset path. Available in all builds */
	UE_API FSoftObjectPath ToSoftObjectPath() const;

#if WITH_EDITORONLY_DATA
	/** Returns the editor-only TSoftObjectPtr */
	UE_API TSoftObjectPtr<UObject> ToSoftObjectPtr() const;
#endif

	/** These functions operate on the reference as an identifier and can be used in any build */
	UE_API bool IsNull() const;
	UE_API void Reset();
	UE_API FString ToString() const;
	UE_API FString GetAssetName() const;

#if WITH_EDITORONLY_DATA
	/** These functions involve loading or fetching the referenced object and are therefore editor-only */
	UE_API UObject* LoadSynchronous() const;
	UE_API bool IsValid() const;
	UE_API UObject* Get() const;
#endif

	UE_API bool Serialize(FArchive& Ar);
	UE_API bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

	UE_API friend bool operator==(const FEditorOnlyAssetReference& A, const FEditorOnlyAssetReference& B);
	UE_API friend bool operator!=(const FEditorOnlyAssetReference& A, const FEditorOnlyAssetReference& B);

	/** Coercing equality operators to match the API of TSoftObjectPtr */
	UE_API friend bool operator==(const FEditorOnlyAssetReference& A, const TSoftObjectPtr<UObject>& B);
	UE_API friend bool operator!=(const FEditorOnlyAssetReference& A, const TSoftObjectPtr<UObject>& B);
	UE_API friend bool operator==(const TSoftObjectPtr<UObject>& A, const FEditorOnlyAssetReference& B);
	UE_API friend bool operator!=(const TSoftObjectPtr<UObject>& A, const FEditorOnlyAssetReference& B);

	UE_API friend bool operator==(const FEditorOnlyAssetReference& A, const FSoftObjectPath& B);
	UE_API friend bool operator!=(const FEditorOnlyAssetReference& A, const FSoftObjectPath& B);
	UE_API friend bool operator==(const FSoftObjectPath& A, const FEditorOnlyAssetReference& B);
	UE_API friend bool operator!=(const FSoftObjectPath& A, const FEditorOnlyAssetReference& B);

	UE_API friend bool operator==(const FEditorOnlyAssetReference& A, const UObject* B);
	UE_API friend bool operator!=(const FEditorOnlyAssetReference& A, const UObject* B);
	UE_API friend bool operator==(const UObject* A, const FEditorOnlyAssetReference& B);
	UE_API friend bool operator!=(const UObject* A, const FEditorOnlyAssetReference& B);

	UE_API friend uint32 GetTypeHash(const FEditorOnlyAssetReference& Ref);

private:
#if WITH_EDITORONLY_DATA
	/** Authoritative editor-only reference */
	UPROPERTY(EditAnywhere, Category = "Asset")
	TSoftObjectPtr<UObject> Asset;
#endif

	/**
	 * Runtime-available identifier. Refreshed from Asset inside Serialize() when saving.
	 *
	 * The Untracked metadata tells the Asset Registry to ignore this reference, so that it 
	 * won't influence cooking.
	 */
	UPROPERTY(meta=(Untracked))
	FSoftObjectPath AssetIdentifier;
};

template<>
struct TStructOpsTypeTraits<FEditorOnlyAssetReference> : TStructOpsTypeTraitsBase2<FEditorOnlyAssetReference>
{
	enum
	{
		WithSerializer = true,
		WithStructuredSerializeFromMismatchedTag = true,
		WithIdenticalViaEquality = true,
	};
};

#undef UE_API
