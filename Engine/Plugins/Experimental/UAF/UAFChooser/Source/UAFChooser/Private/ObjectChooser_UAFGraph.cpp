// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectChooser_UAFGraph.h"

#include "ChooserPlayerTraitData.h"
#include "UAF/UAFAssetFactory.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ObjectChooser_UAFGraph)

#if WITH_EDITORONLY_DATA
bool FUAFGraphChooser::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	return false;
}

void FUAFGraphChooser::PostSerialize(const FArchive& Ar)
{
#if WITH_EDITORONLY_DATA
	if (Ar.IsLoading() && Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::UAFAssetData)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (AssetData.IsValid() == false && Asset_DEPRECATED != nullptr)
		{
			AssetData = UE::UAF::FAssetDataFactory::CreateUAFAssetDataFromObject<FUAFGraphFactoryAsset>(Asset_DEPRECATED);
		}
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
#endif // WITH_EDITORONLY_DATA
}
#endif // #if WITH_EDITORONLY_DATA

UObject* FUAFGraphChooser::ChooseObject(FChooserEvaluationContext& Context) const
{
	if (Context.Params.Num() >= 2)
	{
		if (FUAFChooserPlayerSettings* Settings = Context.Params[1].GetPtr<FUAFChooserPlayerSettings>())
		{
			Settings->AssetData = AssetData;
		}
	}

	// We really choose the asset data, which is not a UObject, but we have to return a non-null object ptr
	return GetObjectFromAssetData();
}

FObjectChooserBase::EIteratorStatus FUAFGraphChooser::IterateObjects(FChooserEvaluationContext& Context, FObjectChooserIteratorCallback Callback) const
{
	UObject* MutableObject = GetObjectFromAssetData();
	return Callback.Execute(MutableObject);
}

UObject* FUAFGraphChooser::GetObjectFromAssetData() const
{
	if (AssetData.IsValid() == false)
	{
		return nullptr;
	}

	TArray<const UObject*> ReferencedObjects;
	AssetData.GetPtr()->GetObjectReferences(ReferencedObjects);
	
	// Todo: how to avoid const cast?
	return const_cast<UObject*>(ReferencedObjects.Num() != 0 ? ReferencedObjects[0] : nullptr); 
}

#if WITH_EDITOR
UObject* FUAFGraphChooser::GetReferencedObject() const
{
	return GetObjectFromAssetData();
}
#endif