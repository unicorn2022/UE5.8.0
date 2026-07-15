// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "MetaHumanCharacterEditorPipeline.h"
#include "MetaHumanPipelineBuiltDataCollection.h"

#define UE_API METAHUMANCHARACTERPALETTE_API

namespace UE::MetaHuman::UnpackUtilities
{

/**
 * Returns a set of objects that are referenced from the given USTRUCT instance and have 
 * OwnerObject as their direct Outer.
 * 
 * This is used to find generated assets that are subobjects of a MetaHuman Collection or Instance.
 * 
 * If bRecursive is true, references from objects (including those not under OwnerObject) will be 
 * recursively followed until all referenced objects have been found.
 * 
 * If bRecursive is false, only objects referenced from the USTRUCT will be included in the results.
 * 
 * In all cases, only the objects with OwnerObject as their direct Outer will be in the returned set.
 */
UE_API TSet<UObject*> GetDirectSubobjectsOfOwnerFromStruct(
	TNotNull<const UScriptStruct*> StructType, 
	TNotNull<const void*> StructMemory, 
	TNotNull<const UObject*> OwnerObject, 
	bool bRecursive);

/**
 * Returns a set of objects that are referenced from the given root objects and have 
 * OwnerObject as their direct Outer.
 * 
 * This is used to find generated assets that are subobjects of a MetaHuman Collection or Instance.
 * 
 * If bRecursive is true, references from objects (including those not under OwnerObject) will be 
 * recursively followed until all referenced objects have been found.
 * 
 * If bRecursive is false, only objects referenced from the root objects will be included in the 
 * results.
 * 
 * In all cases, only the objects with OwnerObject as their direct Outer will be in the returned set.
 */
UE_API TSet<UObject*> GetDirectSubobjectsOfOwnerFromRoots(const TArray<UObject*>& RootObjects, TNotNull<const UObject*> OwnerObject, bool bRecursive);

/**
 * Returns a set of objects that are referenced from the given root objects and are in
 * OwnerObject's outer chain (i.e. subobjects at any nesting depth).
 *
 * If bRecursive is true, references from objects (including those not under OwnerObject) will be
 * recursively followed until all referenced objects have been found.
 *
 * If bRecursive is false, only objects referenced from the root objects will be included in the
 * results.
 */
UE_API TSet<UObject*> GetAllSubobjectsOfOwnerFromRoots(const TArray<UObject*>& RootObjects, TNotNull<const UObject*> OwnerObject, bool bRecursive);

/**
 * Returns a set of objects that are referenced from the given USTRUCT instance and have
 * OwnerObject in their outer chain.
 *
 * If bRecursive is true, references from objects (including those not under OwnerObject) will be
 * recursively followed until all referenced objects have been found.
 *
 * If bRecursive is false, only objects referenced from the USTRUCT will be included in the
 * results.
 *
 * In all cases, only the objects with OwnerObject in their outer chain will be in the returned set.
 */
UE_API TSet<UObject*> GetAllSubobjectsOfOwnerFromStruct(
	TNotNull<const UScriptStruct*> StructType,
	TNotNull<const void*> StructMemory,
	TNotNull<const UObject*> OwnerObject,
	bool bRecursive);

/**
 * Finds all subobjects of OwnerObject (at any nesting depth) that are not referenced, directly
 * or indirectly, from OwnerObject itself, and marks them as RF_Transient to prevent them from
 * being saved.
 *
 * OwnerObject does not need to be a top level asset. When OwnerObject is itself a subobject of
 * some larger object (e.g. a UMetaHumanCollection embedded as a subobject of another asset),
 * this function will still correctly clean up only OwnerObject's own unreferenced subobjects
 * without touching siblings or parent objects.
 */
UE_API void MarkUnreferencedSubobjectsAsTransient(TNotNull<UObject*> OwnerObject);

/**
 * Attempt to move the given object out of TopLevelAsset's package and into its own package as a 
 * standalone asset.
 * 
 * InOutAssetPackageName must be set to the suggested package name that the new asset will be 
 * unpacked to. If there is a name conflict, TryUnpackObject will adjust the name to make it 
 * suitable.
 * 
 * If the function succeeds, InOutAssetPackageName will be set to the name of the actual package
 * that the asset was unpacked to.
 * 
 * OutUnpackedAssetPaths should be a set shared by all unpacking operations for the same top level
 * asset, to ensure that different assets don't overwrite each other as they're unpacked.
 */
UE_API bool TryUnpackObject(
	TNotNull<UObject*> Object,
	TNotNull<const UObject*> TopLevelAsset,
	FString& InOutAssetPackageName,
	TSet<FString>& OutUnpackedAssetPaths);

/**
 * Uses the metadata from the given item's built data to unpack the item's assets to their 
 * preferred locations.
 * 
 * TryUnpackObjectDelegate will be called to do the actual unpacking.
 */
UE_API bool TryUnpackItemAssetsFromMetaData(
	const FMetaHumanPaletteItemPath& BaseItemPath,
	FMetaHumanPipelineBuiltDataCollectionMutableView ItemBuiltData,
	const FString& UnpackFolder,
	const UMetaHumanCharacterEditorPipeline::FTryUnpackObjectDelegate& TryUnpackObjectDelegate);

} // namespace UE::MetaHuman::UnpackUtilities

#undef UE_API

#endif // WITH_EDITOR

