// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorOnlyAssetReference.h"

#include "UObject/PropertyTag.h"
#include "UObject/UnrealType.h"

FEditorOnlyAssetReference::FEditorOnlyAssetReference() = default;

FEditorOnlyAssetReference::FEditorOnlyAssetReference(const TSoftObjectPtr<UObject>& InAsset)
{
	*this = InAsset;
}

FEditorOnlyAssetReference::FEditorOnlyAssetReference(const FSoftObjectPath& InPath)
{
	*this = InPath;
}

FEditorOnlyAssetReference::FEditorOnlyAssetReference(const UObject* InObject)
{
	*this = InObject;
}

FEditorOnlyAssetReference& FEditorOnlyAssetReference::operator=(const TSoftObjectPtr<UObject>& InAsset)
{
#if WITH_EDITORONLY_DATA
	Asset = InAsset;
#endif
	AssetIdentifier = InAsset.ToSoftObjectPath();
	return *this;
}

FEditorOnlyAssetReference& FEditorOnlyAssetReference::operator=(const FSoftObjectPath& InPath)
{
#if WITH_EDITORONLY_DATA
	Asset = TSoftObjectPtr<UObject>(InPath);
#endif
	AssetIdentifier = InPath;
	return *this;
}

FEditorOnlyAssetReference& FEditorOnlyAssetReference::operator=(const UObject* InObject)
{
#if WITH_EDITORONLY_DATA
	// TSoftObjectPtr's constructor takes const UObject* via implicit conversion, but the member
	// requires a mutable UObject* in practice. Go via FSoftObjectPath to keep this const-correct.
	const FSoftObjectPath Path(InObject);
	Asset = TSoftObjectPtr<UObject>(Path);
	AssetIdentifier = Path;
#else
	AssetIdentifier = FSoftObjectPath(InObject);
#endif
	return *this;
}

FSoftObjectPath FEditorOnlyAssetReference::ToSoftObjectPath() const
{
#if WITH_EDITORONLY_DATA
	// In editor, the TSoftObjectPtr is the authoritative source -- rename / redirector fix-up
	// updates it directly without going through the mirror.
	return Asset.ToSoftObjectPath();
#else
	return AssetIdentifier;
#endif
}

#if WITH_EDITORONLY_DATA
TSoftObjectPtr<UObject> FEditorOnlyAssetReference::ToSoftObjectPtr() const
{
	return Asset;
}
#endif

bool FEditorOnlyAssetReference::IsNull() const
{
	return ToSoftObjectPath().IsNull();
}

void FEditorOnlyAssetReference::Reset()
{
#if WITH_EDITORONLY_DATA
	Asset.Reset();
#endif
	AssetIdentifier.Reset();
}

FString FEditorOnlyAssetReference::ToString() const
{
	return ToSoftObjectPath().ToString();
}

FString FEditorOnlyAssetReference::GetAssetName() const
{
	return ToSoftObjectPath().GetAssetName();
}

#if WITH_EDITORONLY_DATA
UObject* FEditorOnlyAssetReference::LoadSynchronous() const
{
	return Asset.LoadSynchronous();
}

bool FEditorOnlyAssetReference::IsValid() const
{
	return Asset.IsValid();
}

UObject* FEditorOnlyAssetReference::Get() const
{
	return Asset.Get();
}
#endif // WITH_EDITORONLY_DATA

bool FEditorOnlyAssetReference::Serialize(FArchive& Ar)
{
#if WITH_EDITORONLY_DATA
	if (Ar.IsSaving())
	{
		// Refresh the runtime mirror from the editor-authoritative TSoftObjectPtr before the
		// default per-property serialization runs.
		AssetIdentifier = Asset.ToSoftObjectPath();
	}
#endif

	// Return false so the tagged-property system falls through to its default serialization for
	// each UPROPERTY on this struct.
	return false;
}

bool FEditorOnlyAssetReference::SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot)
{
	// Legacy case: the outer property used to be declared as TSoftObjectPtr<SomeClass>, which
	// is tagged as a SoftObjectProperty and has an FSoftObjectPtr payload. We accept that here
	// so callers can change the declared type from TSoftObjectPtr<T> to FEditorOnlyAssetReference
	// without renaming the property or leaving a core redirect behind.
	if (Tag.Type == NAME_SoftObjectProperty)
	{
		FSoftObjectPtr Legacy;
		Slot << Legacy;

		*this = Legacy.ToSoftObjectPath();
		return true;
	}

	return false;
}

uint32 GetTypeHash(const FEditorOnlyAssetReference& Ref)
{
	return GetTypeHash(Ref.ToSoftObjectPath());
}

bool operator==(const FEditorOnlyAssetReference& A, const FEditorOnlyAssetReference& B)
{
	return A.ToSoftObjectPath() == B.ToSoftObjectPath();
}

bool operator!=(const FEditorOnlyAssetReference& A, const FEditorOnlyAssetReference& B)
{
	return !(A == B);
}

bool operator==(const FEditorOnlyAssetReference& A, const TSoftObjectPtr<UObject>& B)
{
	return A.ToSoftObjectPath() == B.ToSoftObjectPath();
}

bool operator!=(const FEditorOnlyAssetReference& A, const TSoftObjectPtr<UObject>& B)
{
	return !(A == B);
}

bool operator==(const TSoftObjectPtr<UObject>& A, const FEditorOnlyAssetReference& B)
{
	return A.ToSoftObjectPath() == B.ToSoftObjectPath();
}

bool operator!=(const TSoftObjectPtr<UObject>& A, const FEditorOnlyAssetReference& B)
{
	return !(A == B);
}

bool operator==(const FEditorOnlyAssetReference& A, const FSoftObjectPath& B)
{
	return A.ToSoftObjectPath() == B;
}

bool operator!=(const FEditorOnlyAssetReference& A, const FSoftObjectPath& B)
{
	return !(A == B);
}

bool operator==(const FSoftObjectPath& A, const FEditorOnlyAssetReference& B)
{
	return A == B.ToSoftObjectPath();
}

bool operator!=(const FSoftObjectPath& A, const FEditorOnlyAssetReference& B)
{
	return !(A == B);
}

bool operator==(const FEditorOnlyAssetReference& A, const UObject* B)
{
	return A.ToSoftObjectPath() == FSoftObjectPath(B);
}

bool operator!=(const FEditorOnlyAssetReference& A, const UObject* B)
{
	return !(A == B);
}

bool operator==(const UObject* A, const FEditorOnlyAssetReference& B)
{
	return FSoftObjectPath(A) == B.ToSoftObjectPath();
}

bool operator!=(const UObject* A, const FEditorOnlyAssetReference& B)
{
	return !(A == B);
}
