// Copyright Epic Games, Inc. All Rights Reserved.
#include "UAFAnimChooser.h"
#include "UObject/AssetRegistryTagsContext.h"

#if WITH_EDITOR
#include "AnimNextExports.h"
#include "WorkspaceAssetRegistryInfo.h"
#include "UAFAnimChooserOutlinerData.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(UAFAnimChooser)

UUAFAnimChooserTable::UUAFAnimChooserTable(const FObjectInitializer& Initializer)
	:Super(Initializer)
{

}

#if WITH_EDITOR
void AddExportsForChooser(const UChooserTable* Chooser, FWorkspaceOutlinerItemExports& Exports, FWorkspaceOutlinerItemExport ParentExport);

void AddExportForResult(const FInstancedStruct& ResultStruct, FWorkspaceOutlinerItemExports& Exports, FWorkspaceOutlinerItemExport ParentExport)
{
	if (const FObjectChooserBase* Result = ResultStruct.GetPtr<FObjectChooserBase>())
	{
		if (const UObject* Reference = Result->GetReferencedObject()) // todo: this should get a soft object reference!
		{
			FWorkspaceOutlinerItemExport& ReferencedAssetExport = Exports.Exports.Add_GetRef(FWorkspaceOutlinerItemExport(FName(Reference->GetPathName()), ParentExport));
			
			ReferencedAssetExport.GetData().InitializeAs<FWorkspaceOutlinerAssetReferenceItemData>();
			FWorkspaceOutlinerAssetReferenceItemData& RefData = ReferencedAssetExport.GetData().GetMutable<FWorkspaceOutlinerAssetReferenceItemData>();
			RefData.ReferredObjectPath = Reference;
			RefData.bRecursiveReference = false;
			RefData.bShouldExpandReference = true;
		}
	}
}

void AddExportsForChooser(const UChooserTable* Chooser, FWorkspaceOutlinerItemExports& Exports, FWorkspaceOutlinerItemExport ParentExport)
{
	for(const FInstancedStruct& ResultStruct : Chooser->ResultsStructs)
	{
		AddExportForResult(ResultStruct, Exports, ParentExport);
	}
	AddExportForResult(Chooser->FallbackResult, Exports, ParentExport);
}

namespace
{
	void MakeParentExport(UObject* Object, FWorkspaceOutlinerItemExport& RootExport, FWorkspaceOutlinerItemExport& Export)
	{
		if (RootExport.GetTopLevelAssetPath() == Object)
		{
			Export = RootExport;
		}
		else
		{
			FWorkspaceOutlinerItemExport ParentExport;
			MakeParentExport(Object->GetOuter(), RootExport, ParentExport);
		
			Export = FWorkspaceOutlinerItemExport(FName(Object->GetPathName()), ParentExport);
		}
	}
}
#endif

void UUAFAnimChooserTable::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);

#if WITH_EDITOR
	FWorkspaceOutlinerItemExports Exports;

	FWorkspaceOutlinerItemExport& RootChooserAssetExport = Exports.Exports.Add_GetRef(FWorkspaceOutlinerItemExport(GetFName(), this));

	RootChooserAssetExport.GetData().InitializeAsScriptStruct(FUAFChooserOutlinerItemData::StaticStruct());
	FUAFChooserOutlinerItemData& RootData = RootChooserAssetExport.GetData().GetMutable<FUAFChooserOutlinerItemData>();
	RootData.ObjectPath = this;
	RootData.bIsNestedObject = false;

	FWorkspaceOutlinerItemExport RootChooserExportCopy = RootChooserAssetExport;
	
	TArray<const UObject*> ExportedChoosers;
	AddExportsForChooser(this, Exports, RootChooserAssetExport);

	// export nested choosers
	for (const UObject* NestedObject : NestedObjects)
	{
		if (!ExportedChoosers.Contains(NestedObject))
		{
			ExportedChoosers.Add(NestedObject);

			FWorkspaceOutlinerItemExport ParentExport;
			MakeParentExport(NestedObject->GetOuter(), RootChooserExportCopy, ParentExport);
		
			FWorkspaceOutlinerItemExport& ChooserAssetExport = Exports.Exports.Add_GetRef(FWorkspaceOutlinerItemExport(FName(NestedObject->GetPathName()), ParentExport));
			ChooserAssetExport.GetData().InitializeAsScriptStruct(FUAFChooserOutlinerItemData::StaticStruct());
			FUAFChooserOutlinerItemData& ChooserData = ChooserAssetExport.GetData().GetMutable<FUAFChooserOutlinerItemData>();
			ChooserData.ObjectPath = NestedObject;
			ChooserData.bIsNestedObject = true;

			if (const UChooserTable* NestedChooser = Cast<UChooserTable>(NestedObject))
			{
				AddExportsForChooser(NestedChooser, Exports, ChooserAssetExport);
			}
			else
			{
				ChooserData.bExternalAsset= true;
			}
		}
	}

	FString TagValue;
	FWorkspaceOutlinerItemExports::StaticStruct()->ExportText(TagValue, &Exports, nullptr, nullptr, PPF_None, nullptr);
	Context.AddTag(FAssetRegistryTag(UE::Workspace::ExportsWorkspaceItemsRegistryTag, TagValue, FAssetRegistryTag::TT_Hidden));
#endif // WITH_EDITOR	
}

