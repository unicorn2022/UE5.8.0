// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanUnpackUtilities.h"

#if WITH_EDITOR

#include "MetaHumanCharacterPaletteLog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Internationalization/Regex.h"
#include "Logging/StructuredLog.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace UE::MetaHuman::UnpackUtilities
{
namespace Private
{

void SetBlankThumbnailForAsset(UObject* Asset)
{
    if (!Asset || !Asset->GetPackage()) 
	{
		return;
	}

    // Set a thumbnail with a single black pixel, so that it doesn't get re-rendered on save
    FObjectThumbnail Thumb;
    Thumb.SetImageSize(1, 1);
    TArray<uint8>& Bytes = Thumb.AccessImageData();
    Bytes.SetNumZeroed(4);

    ThumbnailTools::CacheThumbnail(
        Asset->GetFullName(),
        &Thumb,
        Asset->GetPackage());
}

bool TryMoveObjectToAssetPackage(TNotNull<UObject*> Object, FStringView NewAssetPackageName)
{
	UPackage* AssetPackage = UPackageTools::FindOrCreatePackageForAssetType(FName(NewAssetPackageName), Object->GetClass());
	const FString AssetName = FPackageName::GetShortName(AssetPackage);

	// Attempt to load an object from this package to see if one already exists
	const FString AssetPath = AssetPackage->GetName() + TEXT(".") + AssetName;
	UObject* ExistingAsset = LoadObject<UObject>(nullptr, *AssetPath, nullptr, LOAD_NoWarn);

	// Rename any existing object out of the way
	if (ExistingAsset)
	{
		if (UBlueprint* ExistingBlueprintAsset = Cast<UBlueprint>(ExistingAsset))
		{
			if (!ExistingAsset->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_SkipGeneratedClasses))
			{
				return false;
			}
			const FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), ExistingBlueprintAsset->StaticClass()); 
			ExistingBlueprintAsset->RenameGeneratedClasses(*UniqueName.ToString(), GetTransientPackage(), REN_DontCreateRedirectors);
		}
		else if (!ExistingAsset->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional))
		{
			return false;
		}
	}

	if (!Object->Rename(*AssetName, AssetPackage, REN_DontCreateRedirectors))
	{
		return false;
	}

	Object->ClearFlags(RF_Transient);
	Object->SetFlags(RF_Public | RF_Transactional | RF_Standalone);
	Object->MarkPackageDirty();

	// Set a blank thumbnail on every unpacked asset.
	// 
	// Without this, when the unpacked assets are saved, all the thumbnails are rendered at once,
	// causing a GPU driver crash. In the long term we should fix the crash, but this works around
	// it for now.
	SetBlankThumbnailForAsset(Object);

	// Notify the asset registry so that the asset appears in the Content Browser
	if (!ExistingAsset)
	{
		FAssetRegistryModule::AssetCreated(Object);
	}

	return true;
}

// Helper archive to walk through all object dependencies
// Based on the implementation FPackageReferenceFinder and FImportExportCollector
struct FObjectDependencyFinder : public FArchiveUObject
{
	FObjectDependencyFinder(TNotNull<const UObject*> InOwnerObject)
		: OwnerObject(InOwnerObject)
	{
		// Skip transient references, as these won't be duplicated.
		SetIsPersistent(true);

		// Serialization code should write to this archive rather than read from it
		SetIsSaving(true);

		// Serialize all properties, even ones that are the same as their defaults
		ArNoDelta = true;

		// Signal to custom serialize functions that we're only interested in object references.
		// This allows them to skip potentially time consuming serialization of other data.
		ArIsObjectReferenceCollector = true;

		// Bulk data never contains object references, so it can safely be skipped.
		ArShouldSkipBulkData = true;

		// DuplicateTransient properties *will* be followed. This is the behavior we want when the
		// references aren't being used to find objects to duplicate.
		ArPortFlags = PPF_None;
	}

	virtual FArchive& operator<<(UObject*& ObjRef) override
	{
		if (IsValidObject(ObjRef))
		{
			References.Add(ObjRef);
		}

		return *this;
	}

	virtual FArchive& operator<<(FSoftObjectPath& Value) override
	{
		if (UObject* Obj = Value.TryLoad())
		{
			if (IsValidObject(Obj))
			{
				References.Add(Obj);
			}
		}

		return *this;
	}

	bool IsValidObject(const UObject* InObject)
	{
		return InObject != nullptr
			&& InObject->IsInOuter(OwnerObject);
	}

	TArray<UObject*> References;
	TNotNull<const UObject*> OwnerObject;
};

// Shared implementation for GetDirectSubobjectsOfOwnerFromRoots and
// GetAllSubobjectsOfOwnerFromRoots. The AddToResult callback controls
// whether the outer chain is walked to find the direct subobject, or the
// object is added as-is.
//
// OwnerObject does not need to be a top level asset.
TSet<UObject*> GetSubobjectsOfOwnerFromRootsImpl(
	const TArray<UObject*>& RootObjects,
	TNotNull<const UObject*> OwnerObject,
	bool bRecursive,
	TFunctionRef<void(TSet<UObject*>& Result, UObject* Obj, TNotNull<const UObject*> OwnerObject)> AddToResult)
{
	TSet<UObject*> Result;

	// Initialize with the root object
	TArray<UObject*> PendingRefs;
	PendingRefs.Append(RootObjects);

	// Keep track of all visited objects
	TSet<UObject*> RefsProcessed;

	// Iterate on all referenced objects recursively
	while (PendingRefs.Num())
	{
		UObject* Iter = PendingRefs.Pop();
		RefsProcessed.Add(Iter);

		FObjectDependencyFinder DependencyFinder(OwnerObject);
		Iter->Serialize(DependencyFinder);
		for (UObject* Obj : DependencyFinder.References)
		{
			if (!RefsProcessed.Contains(Obj))
			{
				if (bRecursive)
				{
					PendingRefs.Add(Obj);
				}

				AddToResult(Result, Obj, OwnerObject);
			}
		}
	}

	return Result;
}

} // namespace Private

TSet<UObject*> GetDirectSubobjectsOfOwnerFromStruct(
	TNotNull<const UScriptStruct*> StructType, 
	TNotNull<const void*> StructMemory, 
	TNotNull<const UObject*> OwnerObject, 
	bool bRecursive)
{
	Private::FObjectDependencyFinder DependencyFinder(OwnerObject);
	
	// Const cast needed because SerializeItem is non-const. 
	//
	// Neither StructType nor StructMemory will be modified.
	UScriptStruct* NonConstStructType = const_cast<UScriptStruct*>(static_cast<const UScriptStruct*>(StructType));
	void* NonConstStructMemory = const_cast<void*>(static_cast<const void*>(StructMemory));
	NonConstStructType->SerializeItem(DependencyFinder, NonConstStructMemory, nullptr);

	TSet<UObject*> Result;

	if (bRecursive)
	{
		// Initialize Result with the recursive expansion from the references.
		Result = GetDirectSubobjectsOfOwnerFromRoots(DependencyFinder.References, OwnerObject, bRecursive);
	}

	// Walk up each reference's outer chain to find the ancestor that has OwnerObject as its outer.
	Result.Reserve(Result.Num() + DependencyFinder.References.Num());
	for (UObject* Obj : DependencyFinder.References)
	{
		check(Obj);

		UObject* DirectSubobjectOfOwner = Obj;
		while (DirectSubobjectOfOwner && DirectSubobjectOfOwner->GetOuter() != OwnerObject)
		{
			DirectSubobjectOfOwner = DirectSubobjectOfOwner->GetOuter();
		}

		if (DirectSubobjectOfOwner)
		{
			Result.Add(DirectSubobjectOfOwner);
		}
	}

	return Result;
}

TSet<UObject*> GetDirectSubobjectsOfOwnerFromRoots(const TArray<UObject*>& RootObjects, TNotNull<const UObject*> OwnerObject, bool bRecursive)
{
	return Private::GetSubobjectsOfOwnerFromRootsImpl(RootObjects, OwnerObject, bRecursive,
		[](TSet<UObject*>& Result, UObject* Obj, TNotNull<const UObject*> OwnerObject)
		{
			UObject* DirectSubobjectOfOwner = Obj;
			// Walk up the outer chain to find the object that is the direct subobject of OwnerObject
			while (DirectSubobjectOfOwner && DirectSubobjectOfOwner->GetOuter() != OwnerObject)
			{
				DirectSubobjectOfOwner = DirectSubobjectOfOwner->GetOuter();
			}

			if (DirectSubobjectOfOwner)
			{
				Result.Add(DirectSubobjectOfOwner);
			}
		});
}

TSet<UObject*> GetAllSubobjectsOfOwnerFromRoots(const TArray<UObject*>& RootObjects, TNotNull<const UObject*> OwnerObject, bool bRecursive)
{
	return Private::GetSubobjectsOfOwnerFromRootsImpl(RootObjects, OwnerObject, bRecursive,
		[](TSet<UObject*>& Result, UObject* Obj, TNotNull<const UObject*> OwnerObject)
		{
			Result.Add(Obj);
		});
}

TSet<UObject*> GetAllSubobjectsOfOwnerFromStruct(
	TNotNull<const UScriptStruct*> StructType,
	TNotNull<const void*> StructMemory,
	TNotNull<const UObject*> OwnerObject,
	bool bRecursive)
{
	Private::FObjectDependencyFinder DependencyFinder(OwnerObject);

	// Const cast needed because SerializeItem is non-const.
	//
	// Neither StructType nor StructMemory will be modified.
	UScriptStruct* NonConstStructType = const_cast<UScriptStruct*>(static_cast<const UScriptStruct*>(StructType));
	void* NonConstStructMemory = const_cast<void*>(static_cast<const void*>(StructMemory));
	NonConstStructType->SerializeItem(DependencyFinder, NonConstStructMemory, nullptr);

	TSet<UObject*> Result;

	if (bRecursive)
	{
		Result = GetAllSubobjectsOfOwnerFromRoots(DependencyFinder.References, OwnerObject, bRecursive);
	}

	// Include the references walked from the struct itself. FObjectDependencyFinder already
	// filtered to objects in OwnerObject's outer chain. In the non-recursive case this is the
	// only contribution to the result.
	Result.Reserve(Result.Num() + DependencyFinder.References.Num());
	for (UObject* Obj : DependencyFinder.References)
	{
		check(Obj);
		Result.Add(Obj);
	}

	return Result;
}

void MarkUnreferencedSubobjectsAsTransient(TNotNull<UObject*> OwnerObject)
{
	UObject* Owner = OwnerObject;

	// Collect all objects in OwnerObject's outer chain (i.e. all nested subobjects at any depth).
	// This doesn't include OwnerObject itself.
	TSet<UObject*> AllSubobjects;
	ForEachObjectWithOuter(Owner, [&AllSubobjects](UObject* Object)
	{
		AllSubobjects.Add(Object);
	}, EGetObjectsFlags::IncludeNestedObjects);

	// Find all objects referenced, directly or indirectly, from OwnerObject itself.
	TArray<UObject*> Roots;
	Roots.Add(Owner);
	const TSet<UObject*> ReferencedObjects = GetAllSubobjectsOfOwnerFromRoots(Roots, OwnerObject, /*bRecursive=*/ true);

	// Anything nested under OwnerObject but not referenced from it is orphaned and shouldn't
	// be saved with the package.
	const TSet<UObject*> UnreferencedObjects = AllSubobjects.Difference(ReferencedObjects);

	for (UObject* Object : UnreferencedObjects)
	{
		Object->SetFlags(RF_Transient);
		Object->ClearFlags(RF_Public | RF_Standalone);
	}
}

bool TryUnpackObject(
	TNotNull<UObject*> Object,
	TNotNull<const UObject*> TopLevelAsset,
	FString& InOutAssetPackageName,
	TSet<FString>& OutUnpackedAssetPaths)
{
	if (!Object->IsInPackage(TopLevelAsset->GetPackage()))
	{
		// Can't unpack this object, as the asset being unpacked doesn't own it
		return false;
	}

	if (InOutAssetPackageName.Len() == 0)
	{
		// Target package name is required
		return false;
	}

	bool bIsUnpackedPathAlreadyUsed = false;
	OutUnpackedAssetPaths.Add(InOutAssetPackageName, &bIsUnpackedPathAlreadyUsed);

	if (bIsUnpackedPathAlreadyUsed)
	{
		const FRegexPattern Pattern(TEXT("^(.*)_(\\d+)$"));

		while (bIsUnpackedPathAlreadyUsed)
		{
			FRegexMatcher Matcher(Pattern, InOutAssetPackageName);

			if (Matcher.FindNext())
			{
				// The asset name is already in the format Name_Index, and so we can simply increment
				// the index
				const int32 ExistingNameIndex = FCString::Atoi(*Matcher.GetCaptureGroup(2));

				InOutAssetPackageName = FString::Format(TEXT("{0}_{1}"), { Matcher.GetCaptureGroup(1), ExistingNameIndex + 1 });
			}
			else
			{
				// Append a new index to the name, starting at 2
				InOutAssetPackageName = InOutAssetPackageName + TEXT("_2");
			}

			// Try to add the new name to see if it's unique
			OutUnpackedAssetPaths.Add(InOutAssetPackageName, &bIsUnpackedPathAlreadyUsed);
		}
	}

	return Private::TryMoveObjectToAssetPackage(Object, InOutAssetPackageName);
}

bool TryUnpackItemAssetsFromMetaData(
	const FMetaHumanPaletteItemPath& BaseItemPath,
	FMetaHumanPipelineBuiltDataCollectionMutableView ItemBuiltData,
	const FString& UnpackFolder,
	const UMetaHumanCharacterEditorPipeline::FTryUnpackObjectDelegate& TryUnpackObjectDelegate)
{
	for (const FMetaHumanGeneratedAssetMetadata& AssetMetadata : ItemBuiltData[BaseItemPath].Metadata)
	{
		if (!AssetMetadata.Object
			|| AssetMetadata.Object->GetOuter()->IsA<UPackage>())
		{
			// Ignore objects that are already at the top level of a package
			continue;
		}

		FString AssetPackagePath = UnpackFolder;

		if (!AssetMetadata.PreferredSubfolderPath.IsEmpty())
		{
			if (AssetMetadata.bSubfolderIsAbsolute)
			{
				AssetPackagePath = AssetMetadata.PreferredSubfolderPath;
			}
			else
			{
				AssetPackagePath = AssetPackagePath / AssetMetadata.PreferredSubfolderPath;
			}
		}

		if (!AssetMetadata.PreferredName.IsEmpty())
		{
			AssetPackagePath = AssetPackagePath / AssetMetadata.PreferredName;
		}
		else
		{
			AssetPackagePath = AssetPackagePath / AssetMetadata.Object->GetName();
		}

		if (!TryUnpackObjectDelegate.Execute(AssetMetadata.Object, AssetPackagePath))
		{
			return false;
		}
	}

	return true;
}

} // namespace UE::MetaHuman::UnpackUtilities

#endif // WITH_EDITOR

