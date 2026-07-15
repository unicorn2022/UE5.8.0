// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMBuildData.h"
#include "RigVMEditorAsset.h"
#include "RigVMBlueprintGeneratedClass.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "RigVMModel/RigVMFunctionLibrary.h"
#include "UObject/UObjectIterator.h"
#include "RigVMModel/RigVMClient.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Subsystems/EditorAssetSubsystem.h"
#endif

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/NamePermissionList.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigVMBuildData)

static const FLazyName FunctionReferenceNodeDataName = TEXT("FunctionReferenceNodeData");
static const FLazyName PublicFunctionsPropertyName = TEXT("PublicGraphFunctions");

// When the object system has been completely loaded, collect all the references between RigVM graphs 
static FDelayedAutoRegisterHelper GRigVMBuildDataSingletonHelper(EDelayedRegisterRunPhase::EndOfEngineInit, []() -> void
{
	URigVMBuildData::Get()->InitializeIfNeeded();
	FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");
	AssetToolsModule->Get().GetFolderPermissionList()->OnFilterChanged().AddLambda([](){ URigVMBuildData::Get()->Reset(); });
});

bool URigVMBuildData::bInitialized = false; 

URigVMBuildData::URigVMBuildData()
: UObject()
, bIsRunningUnitTest(false)
{
}

TArray<UClass*> URigVMBuildData::FindAllRigVMAssetClasses()
{
	// Find all classes which implement IRigVMClientHost
	TArray<UClass*> ImplementedClasses;
	for (TObjectIterator<UClass> ClassIterator; ClassIterator; ++ClassIterator)
	{
		if (ClassIterator->ImplementsInterface(URigVMClientHost::StaticClass()) ||
			ClassIterator->ImplementsInterface(URigVMGraphFunctionHost::StaticClass()))
		{
			ImplementedClasses.AddUnique(*ClassIterator);
		}
	}
	return ImplementedClasses;
}

void URigVMBuildData::SetupRigVMGraphFunctionPointers()
{
	FRigVMGraphFunctionIdentifier::GetVariantRefsByGuidFunc = [this](const FGuid& InGuid)
	{
		return FindFunctionVariantRefs(InGuid);
	};
	FRigVMGraphFunctionHeader::FindFunctionHeaderFromPathFunc = [this](const FSoftObjectPath& InObjectPath, const FName& InFunctionName, bool *bOutIsPublic)
	{
		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> Assets;
		FString AssetPath = InObjectPath.GetLongPackageName();
		AssetRegistryModule.GetRegistry().GetAssetsByPackageName(*AssetPath, Assets, true);
		if(!Assets.IsEmpty())
		{
			const FString FunctionNameString = InFunctionName.ToString();
			const TArray<FRigVMGraphFunctionHeader> Headers = GetFunctionHeadersForAsset(Assets[0]);
			for(const FRigVMGraphFunctionHeader& Header : Headers)
			{
				if(Header.LibraryPointer.GetFunctionName() == FunctionNameString)
				{
					if(bOutIsPublic)
					{
						// if this isn't loaded - it means that the asset is not loaded thus the function can only be public
						*bOutIsPublic = true;
						
						const FSoftObjectPath LibraryNodeSoftPath = Header.LibraryPointer.GetNodeSoftPath();
						if(const URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(LibraryNodeSoftPath.ResolveObject()))
						{
							if(const URigVMFunctionLibrary* FunctionLibrary = LibraryNode->GetTypedOuter<URigVMFunctionLibrary>())
							{
								*bOutIsPublic = FunctionLibrary->IsFunctionPublic(Header.LibraryPointer.GetFunctionFName());
							}
						}
					}
					return Header;
				}
			}
		}
		if(bOutIsPublic)
		{
			*bOutIsPublic = false;
		}
		return FRigVMGraphFunctionHeader();
	};
	FRigVMGraphFunctionData::GetFunctionHostFromObjectFunc = [this](UObject* InObject) -> IRigVMGraphFunctionHost*
	{
		if(IRigVMGraphFunctionHost* FunctionHost = Cast<IRigVMGraphFunctionHost>(InObject))
		{
			return FunctionHost;
		}
		if(IRigVMGraphFunctionHost* OuterFunctionHost = InObject->GetImplementingOuter<IRigVMGraphFunctionHost>())
		{
			return OuterFunctionHost;
		}
		if(IRigVMClientHost* ClientHost = Cast<IRigVMClientHost>(InObject))
		{
			if(TScriptInterface<IRigVMGraphFunctionHost> FunctionHost = ClientHost->GetRigVMGraphFunctionHost())
			{
				return FunctionHost.GetInterface();
			}
		}
		if(IRigVMClientHost* OuterClientHost = InObject->GetImplementingOuter<IRigVMClientHost>())
		{
			if(TScriptInterface<IRigVMGraphFunctionHost> FunctionHost = OuterClientHost->GetRigVMGraphFunctionHost())
			{
				return FunctionHost.GetInterface();
			}
		}
		if(IRigVMClientHost* Blueprint = Cast<IRigVMClientHost>(InObject))
		{
			return Blueprint->GetRigVMGraphFunctionHost().GetInterface();
		}
		if(URigVMClientHost* Blueprint = InObject->GetTypedOuter<URigVMClientHost>())
		{
			return Cast<IRigVMClientHost>(Blueprint)->GetRigVMGraphFunctionHost().GetInterface();
		}
		return nullptr;
	};
}

void URigVMBuildData::TearDownRigVMGraphFunctionPointers()
{
	FRigVMGraphFunctionIdentifier::GetVariantRefsByGuidFunc.Reset();
	FRigVMGraphFunctionHeader::FindFunctionHeaderFromPathFunc.Reset();
	FRigVMGraphFunctionData::GetFunctionHostFromObjectFunc.Reset();
}

TArray<FRigVMGraphFunctionHeader> URigVMBuildData::GetFunctionHeadersForAsset(const FAssetData& InAssetData)
{
	TArray<FRigVMGraphFunctionHeader> Result;
	if (!InAssetData.GetClass()->ImplementsInterface(URigVMRuntimeAssetInterface::StaticClass()) &&
		!InAssetData.GetClass()->ImplementsInterface(URigVMEditorAssetInterface::StaticClass()))
	{
		return Result;
	}

	// If the asset is loaded, gather the function variants from the function store
	// which will include private functions
	if (InAssetData.IsAssetLoaded())
	{
		UObject* AssetObject = InAssetData.GetAsset();
		TScriptInterface<IRigVMGraphFunctionHost> GeneratedClass = AssetObject;
		if (!GeneratedClass)
		{
			if (TScriptInterface<IRigVMClientHost> Client = AssetObject)
			{
				GeneratedClass = Client->GetRigVMGraphFunctionHost();
			}
		}

		if (GeneratedClass)
		{
			for (int32 Pass=0; Pass<2; Pass++)
			{
				TArray<FRigVMGraphFunctionData>& Functions = (Pass == 0)
					? GeneratedClass->GetRigVMGraphFunctionStore()->PrivateFunctions
					: GeneratedClass->GetRigVMGraphFunctionStore()->PublicFunctions;
				
				for (FRigVMGraphFunctionData& Data : Functions)
				{
					Result.Add(Data.Header);
				}
			}

			return Result;
		}
	}

	// If asset is not loaded, gather function variants from metadata
	const FString PublicGraphFunctionsString = InAssetData.GetTagValueRef<FString>(TEXT("PublicGraphFunctions"));
	if(!PublicGraphFunctionsString.IsEmpty())
	{
		const FArrayProperty* PublicGraphFunctionsProperty = CastField<FArrayProperty>(FRigVMGraphFunctionHeaderArray::StaticStruct()->FindPropertyByName(TEXT("Headers")));
		PublicGraphFunctionsProperty->ImportText_Direct(*PublicGraphFunctionsString, &Result, nullptr, EPropertyPortFlags::PPF_None);
	}
	return Result;
}

URigVMBuildData* URigVMBuildData::Get()
{
	// static in a function scope ensures that the GC system is initiated before 
	// the build data constructor is called
	static TStrongObjectPtr<URigVMBuildData> sBuildData;
	if(!sBuildData.IsValid() && IsInGameThread())
	{
		sBuildData = TStrongObjectPtr<URigVMBuildData>(
			NewObject<URigVMBuildData>(
				GetTransientPackage(), 
				TEXT("RigVMBuildData"), 
				RF_Transient));
	}
	
	sBuildData->InitializeIfNeeded();
	return sBuildData.Get();

}

void URigVMBuildData::InitializeIfNeeded()
{
	if (bInitialized)
	{
		return;
	}
	bInitialized = true;

	TArray<UClass*> ImplementedClasses = FindAllRigVMAssetClasses();

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetTools& AssetTools = IAssetTools::Get();
	
	// Loop the classes
	for (UClass* Class : ImplementedClasses)
	{
		// find all assets of this class in the project
		TArray<FAssetData> AssetDatas;
		FARFilter AssetFilter;
		AssetFilter.ClassPaths.Add(Class->GetClassPathName());
		AssetRegistryModule.Get().GetAssets(AssetFilter, AssetDatas);

		// loop over all found assets
		for(const FAssetData& AssetData : AssetDatas)
		{
			// In UEFN, there are some assets that are not visible, and should not be registered
			if (!AssetTools.IsAssetVisible(AssetData))
			{
				continue;
			}
			
			RegisterReferencesFromAsset(AssetData);
			RegisterPublicFunctionsFromAsset(AssetData);
		}
	}
	
	AssetRegistryModule.Get().OnAssetAdded().AddStatic(&URigVMBuildData::RegisterReferencesFromAsset);
	AssetRegistryModule.Get().OnAssetAdded().AddStatic(&URigVMBuildData::RegisterPublicFunctionsFromAsset);
}

void URigVMBuildData::RegisterReferencesFromAsset(const FAssetData& InAssetData)
{
	URigVMBuildData* BuildData = URigVMBuildData::Get();

	// EditorOnlyCollect prevents the cooker from treating these tag-parsed soft paths as
	// EInstigator::StartupSoftObjectPath references that bypass cook exclusion.
	FSoftObjectPathSerializationScope EditorOnlyScope(ESoftObjectPathCollectType::EditorOnlyCollect);

	// It's faster to check for a key directly then trying to get the class
	FAssetDataTagMapSharedView::FFindTagResult FoundValue = InAssetData.TagsAndValues.FindTag(FunctionReferenceNodeDataName);

	if (FoundValue.IsSet())
	{
		if (UClass* Class = InAssetData.GetClass())
		{
			const FArrayProperty* ReferenceNodeDataProperty =
				  CastField<FArrayProperty>(Class->FindPropertyByName(FunctionReferenceNodeDataName));
			if(ReferenceNodeDataProperty)
			{
				const FString ReferenceNodeDataString = FoundValue.AsString();
				if(ReferenceNodeDataString.IsEmpty())
				{
					return;
				}

				// See if it has reference node data, and register the references
				TArray<FRigVMReferenceNodeData> ReferenceNodeDatas;
				ReferenceNodeDataProperty->ImportText_Direct(*ReferenceNodeDataString, &ReferenceNodeDatas, nullptr, EPropertyPortFlags::PPF_None);	
				for(FRigVMReferenceNodeData& ReferenceNodeData : ReferenceNodeDatas)
				{
					const TSoftObjectPtr<URigVMFunctionReferenceNode> NodePath = TSoftObjectPtr<URigVMFunctionReferenceNode>(FSoftObjectPath(ReferenceNodeData.ReferenceNodePath));
					if (ReferenceNodeData.ReferencedFunctionIdentifier.GetNodeSoftPath().IsValid())
					{
						BuildData->RegisterFunctionReference(ReferenceNodeData.ReferencedFunctionIdentifier, NodePath);
					}
					else if (ReferenceNodeData.ReferencedHeader_DEPRECATED.IsValid())
					{
						BuildData->RegisterFunctionReference(ReferenceNodeData.ReferencedHeader_DEPRECATED.LibraryPointer, NodePath);
					}
					else if (!ReferenceNodeData.ReferencedFunctionPath_DEPRECATED.IsEmpty())
					{
						BuildData->RegisterFunctionReference(ReferenceNodeData);
					}
				}
			}
		}
	}
}

void URigVMBuildData::RegisterPublicFunctionsFromAsset(const FAssetData& InAssetData)
{
	URigVMBuildData* BuildData = URigVMBuildData::Get();

	// EditorOnlyCollect prevents the cooker from treating these tag-parsed soft paths as
	// EInstigator::StartupSoftObjectPath references that bypass cook exclusion.
	FSoftObjectPathSerializationScope EditorOnlyScope(ESoftObjectPathCollectType::EditorOnlyCollect);

	// It's faster to check for a key directly then trying to get the class
	FAssetDataTagMapSharedView::FFindTagResult FoundValue = InAssetData.TagsAndValues.FindTag(PublicFunctionsPropertyName);

	if (FoundValue.IsSet())
	{
		// Do not rely on a class property to find the PublicHeaders, the RigVMBlueprintGeneratedClass actually does not contain that property
		const FArrayProperty* PublicFunctionsProperty = CastField<FArrayProperty>(FRigVMGraphFunctionHeaderArray::StaticStruct()->FindPropertyByName(TEXT("Headers")));
		if(PublicFunctionsProperty)
		{
			const FString PublicHeadersString = FoundValue.AsString();
			if(PublicHeadersString.IsEmpty())
			{
				return;
			}

			// See if it has reference node data, and register the references
			TArray<FRigVMGraphFunctionHeader> PublicHeaders;
			PublicFunctionsProperty->ImportText_Direct(*PublicHeadersString, &PublicHeaders, nullptr, EPropertyPortFlags::PPF_None);
			for(const FRigVMGraphFunctionHeader& Header : PublicHeaders)
			{
				BuildData->RegisterPublicFunction(Header.LibraryPointer, Header);
			}
		}
	}
}

const FRigVMFunctionReferenceArray* URigVMBuildData::FindFunctionReferences(const FRigVMGraphFunctionIdentifier& InFunction) const
{
	return GraphFunctionReferences.Find(InFunction);
}

void URigVMBuildData::ForEachFunctionReference(const FRigVMGraphFunctionIdentifier& InFunction,
                                               TFunction<void(URigVMFunctionReferenceNode*)> PerReferenceFunction,
                                               bool bLoadIfNecessary) const
{
	if (const FRigVMFunctionReferenceArray* ReferencesEntry = FindFunctionReferences(InFunction))
	{
		for (int32 ReferenceIndex = 0; ReferenceIndex < ReferencesEntry->Num(); ReferenceIndex++)
		{
			const TSoftObjectPtr<URigVMFunctionReferenceNode>& Reference = ReferencesEntry->operator [](ReferenceIndex);
			if (bLoadIfNecessary && !Reference.IsValid())
			{
				Reference.LoadSynchronous();
			}
			if (Reference.IsValid())
			{
				PerReferenceFunction(Reference.Get());
			}
		}
	}
}

void URigVMBuildData::ForEachFunctionReferenceSoftPtr(const FRigVMGraphFunctionIdentifier& InFunction,
                                                      TFunction<void(TSoftObjectPtr<URigVMFunctionReferenceNode>)> PerReferenceFunction) const
{
	if (const FRigVMFunctionReferenceArray* ReferencesEntry = FindFunctionReferences(InFunction))
	{
		for (int32 ReferenceIndex = 0; ReferenceIndex < ReferencesEntry->Num(); ReferenceIndex++)
		{
			const TSoftObjectPtr<URigVMFunctionReferenceNode>& Reference = ReferencesEntry->operator [](ReferenceIndex);
			PerReferenceFunction(Reference);
		}
	}
}

void URigVMBuildData::RegisterFunctionReference(const FRigVMGraphFunctionIdentifier& InFunction, URigVMFunctionReferenceNode* InReference)
{
	if(InReference == nullptr)
	{
		return;
	}

	const TSoftObjectPtr<URigVMFunctionReferenceNode> ReferenceKey(InReference);

	RegisterFunctionReference(InFunction, ReferenceKey);
}

void URigVMBuildData::RegisterFunctionReference(const FRigVMGraphFunctionIdentifier& InFunction,
                                                TSoftObjectPtr<URigVMFunctionReferenceNode> InReference)
{
	if(InReference.IsNull())
	{
		return;
	}

	if(FRigVMFunctionReferenceArray* ReferenceEntry = GraphFunctionReferences.Find(InFunction))
	{
		if(ReferenceEntry->FunctionReferences.Contains(InReference))
		{
			return;
		}

		Modify();
		ReferenceEntry->FunctionReferences.Add(InReference);
	}
	else
	{
		Modify();
		FRigVMFunctionReferenceArray NewReferenceEntry;
		NewReferenceEntry.FunctionReferences.Add(InReference);
		GraphFunctionReferences.Add(InFunction, NewReferenceEntry);
	}
	
	MarkPackageDirty();
}

void URigVMBuildData::RegisterFunctionReference(FRigVMReferenceNodeData InReferenceNodeData)
{
	if (InReferenceNodeData.ReferencedFunctionIdentifier.GetNodeSoftPath().IsValid())
	{
		const TSoftObjectPtr<URigVMFunctionReferenceNode> NodePath = TSoftObjectPtr<URigVMFunctionReferenceNode>(FSoftObjectPath(InReferenceNodeData.ReferenceNodePath));
		return RegisterFunctionReference(InReferenceNodeData.ReferencedFunctionIdentifier, NodePath);
	}

	if (!InReferenceNodeData.ReferencedFunctionIdentifier.GetNodeSoftPath().IsValid())
	{
		InReferenceNodeData.ReferencedFunctionIdentifier = InReferenceNodeData.ReferencedHeader_DEPRECATED.LibraryPointer;
	}

	if (!InReferenceNodeData.ReferencedFunctionIdentifier.GetNodeSoftPath().IsValid())
	{
		InReferenceNodeData.ReferencedFunctionIdentifier.SetLibraryNodePath(InReferenceNodeData.ReferencedFunctionPath_DEPRECATED);
	}
	
	check(InReferenceNodeData.ReferencedFunctionIdentifier.GetNodeSoftPath().IsValid());

	FString LibraryNodePath = InReferenceNodeData.ReferencedFunctionIdentifier.GetLibraryNodePath();
	TSoftObjectPtr<URigVMLibraryNode> LibraryNodePtr = TSoftObjectPtr<URigVMLibraryNode>(FSoftObjectPath(LibraryNodePath));

	// Try to find a FunctionIdentifier with the same LibraryNodePath
	bool bFound = false;
	for (TPair< FRigVMGraphFunctionIdentifier, FRigVMFunctionReferenceArray >& Pair : GraphFunctionReferences)
	{
		if (Pair.Key.GetLibraryNodePath() == LibraryNodePath)
		{
			const TSoftObjectPtr<URigVMFunctionReferenceNode> NodePath = TSoftObjectPtr<URigVMFunctionReferenceNode>(FSoftObjectPath(InReferenceNodeData.ReferenceNodePath));
			Pair.Value.FunctionReferences.Add(NodePath);
			bFound = true;
			break;
		}
	}

	// Otherwise, lets add a new identifier, even if it has no function host
	if (!bFound)
	{
		FRigVMGraphFunctionIdentifier Pointer(nullptr, LibraryNodePath);
		if (LibraryNodePtr.IsValid())
		{
			Pointer.HostObject = Cast<UObject>(LibraryNodePtr.Get()->GetFunctionHeader().GetFunctionHostObject().GetObject());
		}
		FRigVMFunctionReferenceArray RefArray;
		const TSoftObjectPtr<URigVMFunctionReferenceNode> NodePath = TSoftObjectPtr<URigVMFunctionReferenceNode>(FSoftObjectPath(InReferenceNodeData.ReferenceNodePath));
		RefArray.FunctionReferences.Add(NodePath);
		GraphFunctionReferences.Add(Pointer, RefArray);
	}
}

void URigVMBuildData::UnregisterFunctionReference(const FRigVMGraphFunctionIdentifier& InFunction,
                                                  URigVMFunctionReferenceNode* InReference)
{
	if(InReference == nullptr)
	{
		return;
	}

	const TSoftObjectPtr<URigVMFunctionReferenceNode> ReferenceKey(InReference);

	return UnregisterFunctionReference(InFunction, ReferenceKey);
}

void URigVMBuildData::UnregisterFunctionReference(const FRigVMGraphFunctionIdentifier& InFunction,
                                                  TSoftObjectPtr<URigVMFunctionReferenceNode> InReference)
{
	if(InReference.IsNull())
	{
		return;
	}

	if(FRigVMFunctionReferenceArray* ReferenceEntry = GraphFunctionReferences.Find(InFunction))
	{
		if(!ReferenceEntry->FunctionReferences.Contains(InReference))
		{
			return;
		}

		Modify();
		ReferenceEntry->FunctionReferences.Remove(InReference);
		MarkPackageDirty();
	}
}

void URigVMBuildData::ClearInvalidReferences()
{
	if (bIsRunningUnitTest)
	{
		return;
	}
	
	Modify(false);
	
	// check each function's each reference
	int32 NumRemoved = 0;
	for (TTuple<FRigVMGraphFunctionIdentifier, FRigVMFunctionReferenceArray>& FunctionReferenceInfo : GraphFunctionReferences)
	{
		FRigVMFunctionReferenceArray* ReferencesEntry = &FunctionReferenceInfo.Value;

		static FString sTransientPackagePrefix;
		if(sTransientPackagePrefix.IsEmpty())
		{
			sTransientPackagePrefix = GetTransientPackage()->GetPathName();
		}
		static const FString sTempPrefix = TEXT("/Temp/");

		NumRemoved += ReferencesEntry->FunctionReferences.RemoveAll([](TSoftObjectPtr<URigVMFunctionReferenceNode> Referencer)
		{
			// ignore keys / references within the transient package
			const FString ReferencerString = Referencer.ToString();
			return ReferencerString.StartsWith(sTransientPackagePrefix) || ReferencerString.StartsWith(sTempPrefix);
		});
	}

	if (NumRemoved > 0)
	{
		MarkPackageDirty();
	}
}

void URigVMBuildData::RegisterPublicFunction(const FRigVMGraphFunctionIdentifier& InFunction, const FRigVMGraphFunctionHeader& InHeader)
{
	PublicGraphFunctions.FindOrAdd(InFunction) = InHeader;
}

void URigVMBuildData::UnregisterPublicFunction(const FRigVMGraphFunctionIdentifier& InFunction)
{
	PublicGraphFunctions.Remove(InFunction);
}

void URigVMBuildData::UpdatePublicFunctionHeader(const FRigVMGraphFunctionIdentifier& InFunction, const FRigVMGraphFunctionHeader& InHeader)
{
	if (FRigVMGraphFunctionHeader* Value = PublicGraphFunctions.Find(InFunction))
	{
		*Value = InHeader;
	}
}

const FRigVMGraphFunctionHeader& URigVMBuildData::FindFunctionHeader(const FRigVMGraphFunctionIdentifier& InFunction)
{
	if (const FRigVMGraphFunctionHeader* Header = PublicGraphFunctions.Find(InFunction))
	{
		return *Header;
	}
	
	static const FRigVMGraphFunctionHeader EmptyHeader;
	return EmptyHeader;
}

TArray<FRigVMVariantRef> URigVMBuildData::GatherAllFunctionVariantRefs() const
{
	TArray<FRigVMVariantRef> Result;
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetTools& AssetTools = IAssetTools::Get();
	TArray<FAssetData> Assets;
	FARFilter AssetFilter;
	AssetFilter.ClassPaths.Add(URigVMEditorAssetInterface::StaticClass()->GetClassPathName());
	AssetFilter.ClassPaths.Add(URigVMRuntimeAssetInterface::StaticClass()->GetClassPathName());
	AssetFilter.bRecursiveClasses = true;
	AssetRegistryModule.Get().GetAssets(AssetFilter, Assets);

	for (const FAssetData& Asset : Assets)
	{
		if (!AssetTools.IsAssetVisible(Asset))
		{
			continue;
		}
		
		if(Asset.IsAssetLoaded())
		{
			if(const IRigVMClientHost* ClientHost = Cast<IRigVMClientHost>(Asset.GetAsset()))
			{
				if(const URigVMFunctionLibrary* FunctionLibrary = ClientHost->GetLocalFunctionLibrary())
				{
					const TArray<URigVMLibraryNode*> Functions = FunctionLibrary->GetFunctions();
					for(const URigVMLibraryNode* Function : Functions)
					{
						FRigVMVariant FunctionVariant = Function->GetFunctionVariant();
						if(!FunctionVariant.Guid.IsValid())
						{
							FunctionVariant.Guid = FRigVMVariant::GenerateGUID(Function->GetPathName());
						}
						Result.Add(FRigVMVariantRef(Function->GetPathName(), FunctionVariant));
					}
					continue;
				}
			}
		}
		TArray<FRigVMVariantRef> VariantRefs = URigVMBuildData::GatherFunctionVariantRefsForAsset(Asset);
		Result.Append(VariantRefs);
	}

	return Result;
}

TArray<FRigVMVariantRef> URigVMBuildData::GatherFunctionVariantRefsForAsset(const FAssetData& InAssetData) const
{
	TArray<FRigVMVariantRef> Result;

	const TArray<FRigVMGraphFunctionHeader> FunctionHeaders = GetFunctionHeadersForAsset(InAssetData);
	for(const FRigVMGraphFunctionHeader& FunctionHeader : FunctionHeaders)
	{
		if (FunctionHeader.LibraryPointer.IsValid())
		{
			FRigVMVariantRef& VariantRef = Result.Add_GetRef(FRigVMVariantRef());
			VariantRef.Variant = FunctionHeader.Variant;
			VariantRef.ObjectPath = FunctionHeader.LibraryPointer.GetNodeSoftPath();
			if (!VariantRef.Variant.Guid.IsValid())
			{
				VariantRef.Variant.Guid = FRigVMVariant::GenerateGUID(VariantRef.ObjectPath.ToString());
			}
		}
	}

	return Result;
}

TArray<FRigVMVariantRef> URigVMBuildData::FindFunctionVariantRefs(const FGuid& InGuid) const
{
	TArray<FRigVMVariantRef> Result = GatherAllFunctionVariantRefs();
	Result = Result.FilterByPredicate([InGuid](const FRigVMVariantRef& VariantRef) { return VariantRef.Variant.Guid == InGuid; });
	return Result;
}

FRigVMGraphFunctionIdentifier URigVMBuildData::GetFunctionIdentifierForVariant(const FRigVMVariantRef& InVariantRef) const
{
	FString SubPathString = InVariantRef.ObjectPath.GetSubPathString();
	if(!SubPathString.IsEmpty())
	{
		int32 LastSlashIndex = INDEX_NONE;
		if(SubPathString.FindLastChar(TEXT('.'), LastSlashIndex))
		{
			SubPathString = SubPathString.Mid(LastSlashIndex + 1);
		}
		
		const FRigVMGraphFunctionHeader Header = FRigVMGraphFunctionHeader::FindGraphFunctionHeader(InVariantRef.ObjectPath, *SubPathString, nullptr, nullptr);
		if(Header.IsValid())
		{
			return Header.LibraryPointer;
		}
	}
	return FRigVMGraphFunctionIdentifier();
}

FRigVMVariantRef URigVMBuildData::CreateFunctionVariant(const FRigVMGraphFunctionIdentifier& InFunctionIdentifier, FName InName)
{
	const FSoftObjectPath LibraryNodePath = FSoftObjectPath(InFunctionIdentifier.GetLibraryNodePath());
	if(const URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(LibraryNodePath.TryLoad()))
	{
		if(URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(LibraryNode->GetGraph()))
		{
			if(IRigVMEditorAssetInterface* RigVMBlueprint = FunctionLibrary->GetImplementingOuter<IRigVMEditorAssetInterface>())
			{
				if(URigVMController* Controller = RigVMBlueprint->GetOrCreateController(FunctionLibrary))
				{
					if(const URigVMLibraryNode* VariantNode = Controller->CreateFunctionVariant(InFunctionIdentifier.GetFunctionFName(), InName))
					{
						return VariantNode->GetFunctionVariantRef();
					}
				}
			}
		}
	}
	return FRigVMVariantRef();
}

TArray<FRigVMVariantRef> URigVMBuildData::GatherAllAssetVariantRefs() const
{
	TArray<FRigVMVariantRef> Result;
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	FARFilter AssetFilter;
	AssetFilter.ClassPaths.Add(URigVMEditorAssetInterface::StaticClass()->GetClassPathName());
	AssetFilter.ClassPaths.Add(URigVMRuntimeAssetInterface::StaticClass()->GetClassPathName());
	AssetFilter.ClassPaths.Add(URigVMBlueprintGeneratedClass::StaticClass()->GetClassPathName());
	AssetFilter.bRecursiveClasses = true;
	AssetRegistryModule.Get().GetAssets(AssetFilter, Assets);

	for (const FAssetData& Asset : Assets)
	{
		Result.Add(GetVariantRefForAsset(Asset));
	}

	return Result;
}

TArray<FRigVMVariantRef> URigVMBuildData::FindAssetVariantRefs(const FGuid& InGuid) const
{
	TArray<FRigVMVariantRef> Result = GatherAllAssetVariantRefs();
	Result = Result.FilterByPredicate([InGuid](const FRigVMVariantRef& VariantRef) { return VariantRef.Variant.Guid == InGuid; });
	return Result;
}

FRigVMVariantRef URigVMBuildData::CreateAssetVariant(const FAssetData& InAssetData, FName InName)
{
	FRigVMVariantRef TargetVariantRef;

#if WITH_EDITOR
	const FRigVMVariantRef SourceVariantRef = GetVariantRefForAsset(InAssetData);
	if(!SourceVariantRef.IsValid())
	{
		return SourceVariantRef;
	}
	
	UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();

	const FString SourcePackageLongName = InAssetData.GetSoftObjectPath().GetWithoutSubPath().ToString();
	FString SourcePackageDirectory, SourcePackagePath, SourcePackageName;
	FPackageName::SplitLongPackageName(SourcePackageLongName, SourcePackageDirectory, SourcePackagePath, SourcePackageName);

	int32 LastPeriodPos = INDEX_NONE;
	if(SourcePackageName.FindChar(TEXT('.'), LastPeriodPos))
	{
		SourcePackageName = SourcePackageName.Left(LastPeriodPos);
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	int32 Suffix = 1;
	const FString DestinationAssetNameBase = InName.IsNone() ? SourcePackageName : InName.ToString();
	FString DestinationAssetName = DestinationAssetNameBase;
	FString DestinationPackageLongName;
	do
	{
		DestinationPackageLongName = SourcePackageDirectory + SourcePackagePath + DestinationAssetName + TEXT(".") + DestinationAssetName;
		DestinationAssetName = DestinationAssetNameBase + TEXT("_") + FString::FromInt(++Suffix);
	}
	while (AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(DestinationPackageLongName)).IsValid());
	
	if(UObject* DuplicatedAsset = EditorAssetSubsystem->DuplicateAsset(SourcePackageLongName, DestinationPackageLongName))
	{
		if(FRigVMEditorAssetInterfacePtr RigVMBlueprint = DuplicatedAsset)
		{
			// since we duplicated the asset the variant guid will be the same too,
			// but to be sure we'll run the join code path as well.
			(void)RigVMBlueprint->JoinAssetVariant(SourceVariantRef.Variant.Guid);
			TargetVariantRef = RigVMBlueprint->GetAssetVariantRef();
		}
	}
#endif
	return TargetVariantRef;
}

FAssetData URigVMBuildData::GetAssetDataForPath(const FSoftObjectPath& InObjectPath) const
{
#if WITH_EDITOR
	UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
	return EditorAssetSubsystem->FindAssetData(InObjectPath.GetWithoutSubPath().ToString());
#else
	return FAssetData();
#endif
}

FRigVMVariantRef URigVMBuildData::GetVariantRefForAsset(const FAssetData& InAssetData) const
{
	FRigVMVariant AssetVariant;
	if(InAssetData.IsAssetLoaded())
	{
		if(const IRigVMRuntimeAssetInterface* RuntimeAsset = Cast<IRigVMRuntimeAssetInterface>(InAssetData.GetAsset()))
		{
			AssetVariant = RuntimeAsset->GetAssetVariant();
		}
		else if(const IRigVMEditorAssetInterface* EditorAsset = Cast<IRigVMEditorAssetInterface>(InAssetData.GetAsset()))
		{
			AssetVariant = EditorAsset->GetAssetVariant();
		}
	}

	if (!AssetVariant.IsValid())
	{
		static const FName AssetVariantPropertyName = TEXT("AssetVariant");
		const FProperty* AssetVariantProperty = CastField<FProperty>(InAssetData.GetClass()->FindPropertyByName(AssetVariantPropertyName));
		const FString VariantStr = InAssetData.GetTagValueRef<FString>(AssetVariantPropertyName);
		if(!VariantStr.IsEmpty())
		{
			AssetVariantProperty->ImportText_Direct(*VariantStr, &AssetVariant, nullptr, EPropertyPortFlags::PPF_None);
		}
	}

	if (!AssetVariant.Guid.IsValid())
	{
		AssetVariant.Guid = FRigVMVariant::GenerateGUID(InAssetData.PackageName.ToString());
	}
		
	return FRigVMVariantRef(InAssetData.ToSoftObjectPath(), AssetVariant);
}

FAssetData URigVMBuildData::GetAssetDataForVariant(const FRigVMVariantRef& InVariantRef) const
{
	return GetAssetDataForPath(InVariantRef.ObjectPath);
}

FRigVMVariantRef URigVMBuildData::SplitVariantFromSet(const FRigVMVariantRef& InVariantRef)
{
	FRigVMVariantRef Result = InVariantRef;
	if(Result.IsValid())
	{
		FAssetData AssetData = GetAssetDataForVariant(Result);
		if (AssetData.GetClass()->ImplementsInterface(URigVMEditorAssetInterface::StaticClass()))
		{
			FRigVMEditorAssetInterfacePtr RigVMBlueprint(AssetData.GetAsset());
			if(UObject* Subject = InVariantRef.ObjectPath.TryLoad())
			{
				if(Subject == RigVMBlueprint->GetObject())
				{
					if(RigVMBlueprint->SplitAssetVariant())
					{
						Result.Variant = RigVMBlueprint->GetAssetVariant();
					}
				}
				else if(URigVMLibraryNode* FunctionNode = Cast<URigVMLibraryNode>(Subject))
				{
					if(URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(FunctionNode->GetGraph()))
					{
						if(URigVMController* Controller = RigVMBlueprint->GetOrCreateController(FunctionLibrary))
						{
							if(Controller->SplitFunctionVariant(FunctionNode->GetFName()))
							{
								Result.Variant = FunctionNode->GetFunctionVariant();
							}
						}
					}
				}
			}
		}
	}
	return Result;
}

FRigVMVariantRef URigVMBuildData::JoinVariantSet(const FRigVMVariantRef& InVariantRef, const FGuid& InGuid)
{
	FRigVMVariantRef Result = InVariantRef;
	if(Result.IsValid() && InGuid.IsValid())
	{
		FAssetData AssetData = GetAssetDataForVariant(Result);
		if (AssetData.GetClass()->ImplementsInterface(URigVMEditorAssetInterface::StaticClass()))
		{
			FRigVMEditorAssetInterfacePtr RigVMBlueprint = AssetData.GetAsset();
			if(UObject* Subject = InVariantRef.ObjectPath.TryLoad())
			{
				if(Subject == RigVMBlueprint->GetObject())
				{
					if(RigVMBlueprint->JoinAssetVariant(InGuid))
					{
						Result.Variant = RigVMBlueprint->GetAssetVariant();
					}
				}
				else if(URigVMLibraryNode* FunctionNode = Cast<URigVMLibraryNode>(Subject))
				{
					if(URigVMFunctionLibrary* FunctionLibrary = Cast<URigVMFunctionLibrary>(FunctionNode->GetGraph()))
					{
						if(URigVMController* Controller = RigVMBlueprint->GetOrCreateController(FunctionLibrary))
						{
							if(Controller->JoinFunctionVariant(FunctionNode->GetFName(), InGuid))
							{
								Result.Variant = FunctionNode->GetFunctionVariant();
							}
						}
					}
				}
			}
		}
	}
	return Result;
}

#if WITH_EDITOR

TArray<FRigVMGraphFunctionIdentifier> URigVMBuildData::GetAllFunctionIdentifiers(bool bOnlyPublic) const
{
	static const FName PublicGraphFunctionsName = TEXT("PublicGraphFunctions");
	TArray<FRigVMGraphFunctionIdentifier> Identifiers;
	TArray<UClass*> ImplementedClasses = FindAllRigVMAssetClasses();
	
	PublicGraphFunctions.GetKeys(Identifiers);
	
	// If looking also for private functions, look at all loaded assets
	if (!bOnlyPublic)
	{
		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetTools& AssetTools = IAssetTools::Get();

		// find all assets of this class in the project
		TArray<FAssetData> AssetDatas;
		FARFilter AssetFilter;
		AssetFilter.bRecursiveClasses = true;
		for(const UClass* Class : ImplementedClasses)
		{
			AssetFilter.ClassPaths.AddUnique(Class->GetClassPathName());
		}
		AssetRegistryModule.Get().GetAssets(AssetFilter, AssetDatas);

		// loop over all found assets
		TSet<FName> PackagesProcessed;
		for(const FAssetData& AssetData : AssetDatas)
		{
			// Avoid duplication
			if (PackagesProcessed.Contains(AssetData.PackageName))
			{
				continue;
			}
			PackagesProcessed.Add(AssetData.PackageName);
			

			if(AssetData.IsAssetLoaded() && AssetTools.IsAssetVisible(AssetData))
			{
				IRigVMGraphFunctionHost* FunctionHost = Cast<IRigVMGraphFunctionHost>(AssetData.GetAsset());
				if(FunctionHost == nullptr)
				{
					if(IRigVMClientHost* ClientHost = Cast<IRigVMClientHost>(AssetData.GetAsset()))
					{
						FunctionHost = ClientHost->GetRigVMGraphFunctionHost().GetInterface();
					}
				}
				if(FunctionHost)
				{
					if(const FRigVMGraphFunctionStore* Store = FunctionHost->GetRigVMGraphFunctionStore())
					{
						// At this point we only care about private functions, the public functions of the project have already been added
						for(const FRigVMGraphFunctionData& Data : Store->PrivateFunctions)
						{
							Identifiers.Add(Data.Header.LibraryPointer);
						}
					}
				}
			}
		}
	}

	return Identifiers;
}

#endif

TArray<FRigVMGraphFunctionIdentifier> URigVMBuildData::GetUsedFunctionIdentifiers(bool bOnlyPublic) const
{
	TArray<FRigVMGraphFunctionIdentifier> Identifiers;
#if WITH_EDITOR
	Identifiers = GetAllFunctionIdentifiers().FilterByPredicate([this](const FRigVMGraphFunctionIdentifier& Identifier)
	{
		const FRigVMFunctionReferenceArray* References = FindFunctionReferences(Identifier); 
		return References != nullptr && References->Num() > 0;
	});
#else
	// outside of editor we can't do much - just get the keys
	// of the used references array
	GraphFunctionReferences.GenerateKeyArray(Identifiers);
#endif
	return Identifiers;
}

FRigVMFunctionReferenceArray URigVMBuildData::GetAllFunctionReferences() const
{
	FRigVMFunctionReferenceArray FunctionReferences;
	for (const TTuple<FRigVMGraphFunctionIdentifier, FRigVMFunctionReferenceArray>& FunctionReferenceInfo : GraphFunctionReferences)
	{
		FunctionReferences.FunctionReferences.Append(FunctionReferenceInfo.Value.FunctionReferences);
	}
	return FunctionReferences;
}

void URigVMBuildData::Reset()
{
	bInitialized = false;
	GraphFunctionReferences.Reset();
	PublicGraphFunctions.Reset();
}



