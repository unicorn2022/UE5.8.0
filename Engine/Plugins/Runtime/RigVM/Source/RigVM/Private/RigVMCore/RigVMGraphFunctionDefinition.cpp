// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMGraphFunctionDefinition.h"
#include "RigVMCore/RigVMGraphFunctionHost.h"
#include "RigVMStringUtils.h"
#include "RigVMCore/RigVMTrait.h"

#if WITH_EDITOR
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigVMGraphFunctionDefinition)

TAutoConsoleVariable<bool> CVarRigVMCompileFunctionsToCallables(
	TEXT("RigVM.Compiler.CompileFunctionsToCallables"),
	false,
	TEXT("Turn functions into callables to save on memory footprint.")
);

TAutoConsoleVariable<bool> CVarRigVMAllowCallableFallbackToInlining(
	TEXT("RigVM.Compiler.AllowCallableFallbackToInlining"),
	true,
	TEXT("When RigVM.Compiler.CompileFunctionsToCallables is on and a function reference cannot be lowered to a callable, allow the compiler to silently fall back to function-reference inlining. Disable (set to 0) for unit tests that need to verify the callable-lowering path is actually exercised — those tests will then fail the compile instead of passing via the inline path.")
);

FRigVMPropertyDescription FRigVMFunctionCompilationPropertyDescription::ToPropertyDescription() const
{
	return FRigVMPropertyDescription(Name, CPPType, CPPTypeObject.LoadSynchronous(), DefaultValue);
}

TArray<FRigVMPropertyDescription> FRigVMFunctionCompilationPropertyDescription::ToPropertyDescription(
	const TArray<FRigVMFunctionCompilationPropertyDescription>& InDescriptions)
{
	TArray<FRigVMPropertyDescription> Result;
	Result.Reserve(InDescriptions.Num());
	for(const FRigVMFunctionCompilationPropertyDescription& Description : InDescriptions)
	{
		Result.Add(Description.ToPropertyDescription());
	}
	return Result;
}

void FRigVMFunctionCompilationData::Reset()
{
	FScopeLock Lock(&TaskMutex);
	
	ByteCode.Reset();
	FunctionNames.Reset();
	WorkPropertyDescriptions.Reset();
	WorkPropertyPathDescriptions.Reset();
	LiteralPropertyDescriptions.Reset();
	LiteralPropertyPathDescriptions.Reset();
	DebugPropertyDescriptions.Reset();
	DebugPropertyPathDescriptions.Reset();
	ExternalPropertyDescriptions.Reset();
	ExternalPropertyPathDescriptions.Reset();
	ExternalRegisterIndexToVariable.Reset();
	Operands.Reset();
	InterfaceOperands.Reset();
	Hash = 0;
	bEncounteredSurpressedErrors = false;
	bSupportsCallable.Reset();
}

FRigVMFunctionCompilationData& FRigVMFunctionCompilationData::operator=(const FRigVMFunctionCompilationData& InOther)
{
	FScopeLock Lock(&TaskMutex);
	FScopeLock OtherLock(&InOther.TaskMutex);
	
	ByteCode = InOther.ByteCode;
	FunctionNames = InOther.FunctionNames;
	WorkPropertyDescriptions = InOther.WorkPropertyDescriptions;
	WorkPropertyPathDescriptions = InOther.WorkPropertyPathDescriptions;
	LiteralPropertyDescriptions = InOther.LiteralPropertyDescriptions;
	LiteralPropertyPathDescriptions = InOther.LiteralPropertyPathDescriptions;
	DebugPropertyDescriptions = InOther.DebugPropertyDescriptions;
	DebugPropertyPathDescriptions = InOther.DebugPropertyPathDescriptions;
	ExternalPropertyDescriptions = InOther.ExternalPropertyDescriptions;
	ExternalPropertyPathDescriptions = InOther.ExternalPropertyPathDescriptions;
	ExternalRegisterIndexToVariable = InOther.ExternalRegisterIndexToVariable;
	Operands = InOther.Operands;
	InterfaceOperands = InOther.InterfaceOperands;
	Hash = InOther.Hash;
	bEncounteredSurpressedErrors = InOther.bEncounteredSurpressedErrors;
	bSupportsCallable = InOther.bSupportsCallable;

	return *this;
}

bool FRigVMFunctionCompilationData::SupportsCallable() const
{
	if (!CVarRigVMCompileFunctionsToCallables->GetBool())
	{
		return false;
	}
	
	FScopeLock Lock(&TaskMutex);

	if (bSupportsCallable.IsSet())
	{
		return bSupportsCallable.GetValue();
	}

	bSupportsCallable = true;

	auto Failed = [this]() -> bool
	{
		bSupportsCallable = false;
		return false;
	};

	// for now we disable functions for nodes that have traits on them.
	for (const FRigVMFunctionCompilationPropertyDescription& PropertyDescription : WorkPropertyDescriptions)
	{
		const UObject* CPPTypeObject = PropertyDescription.CPPTypeObject.Get();
		if (!CPPTypeObject)
		{
			continue;
		}
		const UScriptStruct* CPPTypeStruct = Cast<UScriptStruct>(CPPTypeObject);
		if (!CPPTypeStruct)
		{
			continue;
		}

		if (CPPTypeStruct->IsChildOf(FRigVMTrait::StaticStruct()))
		{
			return Failed();
		}
	}

	return bSupportsCallable.GetValue();
}

const TArray<FRigVMFunctionCompilationPropertyDescription>& FRigVMFunctionCompilationData::GetPropertyDescriptions(
	ERigVMMemoryType InMemoryType) const
{
	switch (InMemoryType)
	{
		case ERigVMMemoryType::Literal:
		{
			return LiteralPropertyDescriptions;
		}
		case ERigVMMemoryType::External:
		{
				return ExternalPropertyDescriptions;
		}
		case ERigVMMemoryType::Debug:
		{
			return DebugPropertyDescriptions;
		}
		case ERigVMMemoryType::Work:
		default:
		{
			return WorkPropertyDescriptions;
		}
	}
}

const TArray<FRigVMFunctionCompilationPropertyPath>& FRigVMFunctionCompilationData::GetPropertyPathDescriptions(ERigVMMemoryType InMemoryType) const
{
	switch (InMemoryType)
	{
		case ERigVMMemoryType::Literal:
		{
			return LiteralPropertyPathDescriptions;
		}
		case ERigVMMemoryType::External:
		{
			return ExternalPropertyPathDescriptions;
		}
		case ERigVMMemoryType::Debug:
		{
			return DebugPropertyPathDescriptions;
		}
		case ERigVMMemoryType::Work:
		default:
		{
			return WorkPropertyPathDescriptions;
		}
	}
}

FRigVMGraphFunctionArgument::FRigVMGraphFunctionArgument(const FRigVMExternalVariable& InExternalVariable)
: Name(InExternalVariable.GetName())
, DisplayName(InExternalVariable.GetName())
, CPPType(InExternalVariable.GetExtendedCPPType())
, CPPTypeObject(const_cast<UObject*>(InExternalVariable.GetCPPTypeObject()))
, bIsArray(InExternalVariable.IsArray())
, Direction(ERigVMPinDirection::Input)
, bIsConst(false)
, bIsInputVariable(true)
{
}

bool FRigVMGraphFunctionArgument::IsValid() const
{
	return !CPPType.IsNone() || IsCPPTypeObjectValid();
}

bool FRigVMGraphFunctionArgument::IsCPPTypeObjectValid() const
{
	if(!CPPTypeObject.IsValid())
	{
		// this is potentially a user defined struct or user defined enum
		// so we have to try to load it.
		(void)CPPTypeObject.LoadSynchronous();
	}
	return CPPTypeObject.IsValid();
}

bool FRigVMGraphFunctionArgument::IsExecuteContext() const
{
	if(IsCPPTypeObjectValid())
	{
		if(const UScriptStruct* ScriptStruct = Cast<UScriptStruct>(CPPTypeObject.Get()))
		{
			if(ScriptStruct->IsChildOf(FRigVMExecutePin::StaticStruct()))
			{
				return true;
			}
		}
	}
	return false;
}

void FRigVMGraphFunctionArgument::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FRigVMObjectVersion::GUID);
			
	Ar << Name;
	Ar << DisplayName;
	Ar << CPPType;
	Ar << CPPTypeObject;
	Ar << bIsArray;
	Ar << Direction;
	Ar << DefaultValue;
	Ar << bIsConst;
	Ar << PathToTooltip;

	if (Ar.IsLoading() && Ar.CustomVer(FRigVMObjectVersion::GUID) < FRigVMObjectVersion::FunctionArgumentCanRepresentInputVariable)
	{
		bIsInputVariable = false;
	}
	else
	{
		Ar << bIsInputVariable;
	}
}

FRigVMExternalVariable FRigVMGraphFunctionArgument::ToExternalVariable() const
{
	UObject* TypeObject = nullptr;
	if(IsCPPTypeObjectValid())
	{
		TypeObject = CPPTypeObject.Get();
	}
	return FRigVMExternalVariable::Make(FGuid(), Name, CPPType.ToString(), TypeObject, true, false);
}

FArchive& operator<<(FArchive& Ar, FRigVMFunctionCompilationData& Data)
{
	FScopeLock Lock(&Data.TaskMutex);
	
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
		
	Ar << Data.ByteCode;
	Ar << Data.FunctionNames;
	Ar << Data.WorkPropertyDescriptions;
	Ar << Data.WorkPropertyPathDescriptions;
	Ar << Data.LiteralPropertyDescriptions;
	Ar << Data.LiteralPropertyPathDescriptions;
	Ar << Data.DebugPropertyDescriptions;
	Ar << Data.DebugPropertyPathDescriptions;
	Ar << Data.ExternalPropertyDescriptions;
	Ar << Data.ExternalPropertyPathDescriptions;
	Ar << Data.ExternalRegisterIndexToVariable;
	Ar << Data.Operands;

	if (Ar.IsLoading() && Ar.CustomVer(FRigVMObjectVersion::GUID) < FRigVMObjectVersion::RigVMCallables)
	{
		Data.InterfaceOperands.Reset();
	}
	else
	{
		Ar << Data.InterfaceOperands;
	}

	Ar << Data.Hash;

	if(Ar.IsLoading())
	{
		Data.bEncounteredSurpressedErrors = false;
	}

	if (Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) < FUE5ReleaseStreamObjectVersion::RigVMSaveDebugMapInGraphFunctionData &&
		Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::RigVMSaveDebugMapInGraphFunctionData)
	{
		return Ar;
	}

	// Serialize OperandToDebugRegisters
	{
		if (Ar.IsLoading() && Ar.CustomVer(FRigVMObjectVersion::GUID) < FRigVMObjectVersion::DebugOperandMappingSimplified)
   		{
			uint8 NumKeys = 0;
			Ar << NumKeys;

			// this data is just loaded but then discarded.
			for (int32 KeyIndex=0; KeyIndex<NumKeys; ++KeyIndex)
			{
				FRigVMOperand Key;
				Ar << Key;
				uint8 NumValues;
				Ar << NumValues;
				TArray<FRigVMOperand> Values;
				Values.SetNumUninitialized(NumValues);
				for (int32 ValueIndex=0; ValueIndex<NumValues; ++ValueIndex)
				{
					Ar << Values[ValueIndex];
				}
			}
		}
	}

	if (Ar.IsLoading())
	{
		Data.bSupportsCallable.Reset();
	}
	
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FRigVMGraphFunctionIdentifier& Data)
{
	Ar.UsingCustomVersion(FRigVMObjectVersion::GUID);

	if(Ar.IsSaving())
	{
		if(Data.LibraryNodePath.IsEmpty() && Data.LibraryNode_DEPRECATED.IsValid())
		{
			Data.LibraryNodePath = Data.GetLibraryNodePath();
		}
	}
		
	if (Ar.IsLoading() && Ar.CustomVer(FRigVMObjectVersion::GUID) < FRigVMObjectVersion::RemoveLibraryNodeReferenceFromFunctionIdentifier)
	{
		FSoftObjectPath SoftPath;
		Ar << SoftPath;
		Data.LibraryNodePath = SoftPath.ToString();
	}
	else
	{
		Ar << Data.LibraryNodePath;
	}
		
	Ar << Data.HostObject;
	return Ar;
}

TFunction<TArray<FRigVMVariantRef>(const FGuid& InGuid)> FRigVMGraphFunctionIdentifier::GetVariantRefsByGuidFunc;

#if WITH_EDITOR
FAssetData FRigVMGraphFunctionIdentifier::GetAssetData() const
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetTools& AssetTools = IAssetTools::Get();
	FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(HostObject);
	if (!AssetData.IsValid())
	{
		AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(GetNodeSoftPath().GetWithoutSubPath());
	}
	
	if (!AssetData.IsValid() || !AssetTools.IsAssetVisible(AssetData))
	{
		return FAssetData();
	}
	
	return AssetData;
}
#endif

bool FRigVMGraphFunctionIdentifier::IsVariant() const
{
	return !GetVariants(false).IsEmpty();
}

TArray<FRigVMVariantRef> FRigVMGraphFunctionIdentifier::GetVariants(bool bIncludeSelf) const
{
	if(GetVariantRefsByGuidFunc)
	{
		const FRigVMGraphFunctionHeader& ThisHeader = FRigVMGraphFunctionHeader::FindGraphFunctionHeader(*this);
		TArray<FRigVMVariantRef> Result = GetVariantRefsByGuidFunc(ThisHeader.Variant.Guid);
		if(!bIncludeSelf)
		{
			Result.RemoveAll([this](const FRigVMVariantRef& VariantRef) -> bool
			{
				return VariantRef.ObjectPath == GetNodeSoftPath();
			});
		}
		return Result;
	}
	return TArray<FRigVMVariantRef>();
}

TArray<FRigVMGraphFunctionIdentifier> FRigVMGraphFunctionIdentifier::GetVariantIdentifiers(bool bIncludeSelf) const
{
	const TArray<FRigVMVariantRef> VariantRefs = GetVariants(bIncludeSelf);
	TArray<FRigVMGraphFunctionIdentifier> Identifiers;
	for(const FRigVMVariantRef& VariantRef : VariantRefs)
	{
		const FRigVMGraphFunctionHeader Header = FRigVMGraphFunctionHeader::FindGraphFunctionHeader(VariantRef.ObjectPath.ToString());
		if(Header.IsValid())
		{
			Identifiers.Add(Header.LibraryPointer);
		}
	}
	return Identifiers;
}

bool FRigVMGraphFunctionIdentifier::IsVariantOf(const FRigVMGraphFunctionIdentifier& InOther) const
{
	const FRigVMGraphFunctionHeader& ThisHeader = FRigVMGraphFunctionHeader::FindGraphFunctionHeader(*this);
	const FRigVMGraphFunctionHeader& OtherHeader = FRigVMGraphFunctionHeader::FindGraphFunctionHeader(InOther);
	return ThisHeader.Variant.Guid == OtherHeader.Variant.Guid;
}

TFunction<FRigVMGraphFunctionHeader(const FSoftObjectPath&, const FName&, bool*)> FRigVMGraphFunctionHeader::FindFunctionHeaderFromPathFunc;

bool FRigVMGraphFunctionHeader::IsMutable() const
{
	for(const FRigVMGraphFunctionArgument& Arg : Arguments)
	{
		if(Arg.IsCPPTypeObjectValid())
		{
			if(UScriptStruct* Struct = Cast<UScriptStruct>(Arg.CPPTypeObject.Get()))
			{
				if(Struct->IsChildOf(FRigVMExecutePin::StaticStruct()))
				{
					return true;
				}
			}
		}
	}
	return false;
}

IRigVMGraphFunctionHost* FRigVMGraphFunctionHeader::GetFunctionHost(bool bLoadIfNecessary) const
{
	return GetFunctionHostObject(bLoadIfNecessary).GetInterface();
}

TScriptInterface<IRigVMGraphFunctionHost> FRigVMGraphFunctionHeader::GetFunctionHostObject(bool bLoadIfNecessary) const
{
	UObject* HostObj = LibraryPointer.HostObject.ResolveObject();
	if (!HostObj && bLoadIfNecessary)
	{
		HostObj = LibraryPointer.HostObject.TryLoad();
	}
	return HostObj;
}

FRigVMGraphFunctionData* FRigVMGraphFunctionHeader::GetFunctionData(bool bLoadIfNecessary) const
{
	if (TScriptInterface<IRigVMGraphFunctionHost> Host = GetFunctionHostObject(bLoadIfNecessary))
	{
		return Host->GetRigVMGraphFunctionStore()->FindFunction(LibraryPointer);
	}
	return nullptr;
}

FArchive& operator<<(FArchive& Ar, FRigVMGraphFunctionHeader& Data)
{
	UE_RIGVM_ARCHIVETRACE_SCOPE(Ar, FString::Printf(TEXT("FRigVMGraphFunctionHeader(%s)"), *Data.Name.ToString()));

	Ar.UsingCustomVersion(FRigVMObjectVersion::GUID);
	
	Ar << Data.LibraryPointer;
	UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("LibraryPointer"));

	if (!Ar.IsLoading() || Ar.CustomVer(FRigVMObjectVersion::GUID) >= FRigVMObjectVersion::AddVariantToFunctionIdentifier)
	{
		Ar << Data.Variant;
		UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("Variant"));
	}
	
	Ar << Data.Name;
	UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("Name"));
	Ar << Data.NodeTitle;
	UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("NodeTitle"));
	Ar << Data.NodeColor;
	UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("NodeColor"));

	if (Ar.IsLoading())
	{
		if (Ar.CustomVer(FRigVMObjectVersion::GUID) < FRigVMObjectVersion::VMRemoveTooltipFromFunctionHeader)
		{
			Ar << Data.Tooltip_DEPRECATED;
		}
		else
		{
			Ar << Data.Description;
		}
	}
	else
	{
		Ar << Data.Description;
		UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("Description"));
	}
	
	Ar << Data.Category;
	UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("Category"));
	Ar << Data.Keywords;
	UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("Keywords"));
	Ar << Data.Arguments;
	UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("Arguments"));
	Ar << Data.Dependencies;
	UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("Dependencies"));
	Ar << Data.ExternalVariables;
	UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("ExternalVariables"));

	if (Ar.IsLoading())
	{
		if (Ar.CustomVer(FRigVMObjectVersion::GUID) >= FRigVMObjectVersion::FunctionHeaderStoresLayout)
		{
			Ar << Data.Layout;
		}
		else
		{
			Data.Layout.Reset();
		}

		Data.ExternalVariables.RemoveAll([](const FRigVMExternalVariable& ExternalVariable)
		{
			return !ExternalVariable.IsValid(true /* allow nullptr memory */);
		});
	}
	else
	{
		Ar << Data.Layout;
		UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("Layout"));
	}
	
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FRigVMGraphFunctionData& Data)
{
	UE_RIGVM_ARCHIVETRACE_SCOPE(Ar, FString::Printf(TEXT("FRigVMGraphFunctionStore(%s)"), *Data.Header.Name.ToString()));

	Ar << Data.Header;
	UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("Header"));
	Ar << Data.CompilationData;
	UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("CompilationData"));

	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::RigVMSaveSerializedGraphInGraphFunctionData)
	{
		if(Ar.IsLoading())
		{
			Data.SerializedCollapsedNode_DEPRECATED.Empty();
		}
		return Ar;
	}

	Ar << Data.SerializedCollapsedNode_DEPRECATED;
	UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("SerializedCollapsedNode"));

	if (Ar.CustomVer(FRigVMObjectVersion::GUID) < FRigVMObjectVersion::RigVMSaveSerializedGraphInGraphFunctionDataAsByteArray)
	{
		if(Ar.IsLoading())
		{
			Data.CollapseNodeArchive.Empty();
		}
		return Ar;
	}

	Ar << Data.CollapseNodeArchive;
	if (Ar.IsLoading())
	{
		Data.CollapseNodeArchive.SetVersionsFromArchive(Ar);
	}
	UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("CollapseNodeArchive"));

#if UE_BUILD_SHIPPING
	// these two members can store substantial data
	// which is not needed for a shipping game.
	Data.CollapseNodeArchive.Empty();
	Data.SerializedCollapsedNode_DEPRECATED.Empty();
#else
	if(!Data.CollapseNodeArchive.IsEmpty())
	{
		Data.SerializedCollapsedNode_DEPRECATED.Empty();
	}
#endif

	return Ar;
}

FRigVMGraphFunctionHeader FRigVMGraphFunctionHeader::FindGraphFunctionHeader(const FSoftObjectPath& InFunctionObjectPath, bool* bOutIsPublic, FString* OutErrorMessage)
{
	return FindGraphFunctionHeader(InFunctionObjectPath, NAME_None, bOutIsPublic, OutErrorMessage);
}

FRigVMGraphFunctionHeader FRigVMGraphFunctionHeader::FindGraphFunctionHeader(const FSoftObjectPath& InHostObjectPath, const FName& InFunctionName, bool* bOutIsPublic, FString* OutErrorMessage)
{
	const FName FunctionName = GetFunctionNameFromObjectPath(InHostObjectPath.ToString(), InFunctionName);
	if(FunctionName.IsNone())
	{
		return FRigVMGraphFunctionHeader();
	}
	
	if(FindFunctionHeaderFromPathFunc)
	{
		if(InHostObjectPath.ResolveObject() == nullptr)
		{
			const FRigVMGraphFunctionHeader Header = FindFunctionHeaderFromPathFunc(InHostObjectPath, FunctionName, bOutIsPublic);
			if(Header.IsValid())
			{
				return Header;
			}
		}
	}
	
	// relay to the loaded function since the hostpath is loaded
	if(const FRigVMGraphFunctionData* FunctionData = FRigVMGraphFunctionData::FindFunctionData(InHostObjectPath, FunctionName, bOutIsPublic, OutErrorMessage))
	{
		return FunctionData->Header;
	}
	return FRigVMGraphFunctionHeader(); 
}

FRigVMGraphFunctionHeader FRigVMGraphFunctionHeader::FindGraphFunctionHeader(const FRigVMGraphFunctionIdentifier& InIdentifier, bool* bOutIsPublic, FString* OutErrorMessage)
{
	if(FindFunctionHeaderFromPathFunc)
	{
		if(InIdentifier.HostObject.ResolveObject() == nullptr)
		{
			const FName FunctionName = GetFunctionNameFromObjectPath(InIdentifier.GetLibraryNodePath());
			FRigVMGraphFunctionHeader Header = FindFunctionHeaderFromPathFunc(InIdentifier.GetLibraryNodePath(), FunctionName, bOutIsPublic);
			if(Header.IsValid())
			{
				return Header;
			}
			
			// If we are in UEFN, we might want to get the header from the cooked asset instead
			Header = FindFunctionHeaderFromPathFunc(InIdentifier.HostObject, FunctionName, bOutIsPublic);
			if (Header.IsValid())
			{
				return Header;
			}
		}
	}

	if(const FRigVMGraphFunctionData* FunctionData = FRigVMGraphFunctionData::FindFunctionData(InIdentifier, bOutIsPublic, OutErrorMessage))
	{
		return FunctionData->Header;
	}
	return FRigVMGraphFunctionHeader(); 
}

FRigVMGraphFunctionHeader FRigVMGraphFunctionHeader::FindGraphFunctionHeaderFromHash(const FString& InFunctionHash, bool* bOutIsPublic, FString* OutErrorMessage)
{
	FString HostObjectPathString, FunctionName;
	if (!InFunctionHash.Split(TEXT(":"), &HostObjectPathString, &FunctionName, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
	{
		if (OutErrorMessage)
		{
			*OutErrorMessage = FString::Printf(TEXT("Failed to find the Host object for hash '%s'."), *InFunctionHash);
		}
		return FRigVMGraphFunctionHeader();
	}

	FSoftObjectPath HostObjectPath(HostObjectPathString);
	return FindGraphFunctionHeader(HostObjectPath, *FunctionName, bOutIsPublic, OutErrorMessage);
}

TArray<FRigVMExternalVariable> FRigVMGraphFunctionHeader::GetExternalVariables() const
{
	TArray<FRigVMExternalVariable> Result;
	Result.Append(ExternalVariables);

	for(const FRigVMGraphFunctionArgument& Argument : Arguments)
	{
		if (!Argument.bIsInputVariable)
		{
			continue;
		}
		FRigVMExternalVariable::MergeExternalVariable(Result, Argument.ToExternalVariable());
	}

	return Result;
}

FName FRigVMGraphFunctionHeader::GetFunctionNameFromObjectPath(const FString& InObjectPath, const FName& InOptionalFunctionName)
{
	FName FunctionName = InOptionalFunctionName;
	if(FunctionName.IsNone())
	{
		FString FunctionNameStr;
		if(!InObjectPath.Split(TEXT("."), nullptr, &FunctionNameStr, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			(void)InObjectPath.Split(TEXT("/"), nullptr, &FunctionNameStr, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		}

		if(FunctionNameStr.IsEmpty())
		{
			return NAME_None;
		}
		FunctionName = *FunctionNameStr;
	}
	return FunctionName;
}

TFunction<IRigVMGraphFunctionHost*(UObject*)> FRigVMGraphFunctionData::GetFunctionHostFromObjectFunc;

bool FRigVMGraphFunctionData::IsMutable() const
{
	return Header.IsMutable();
}

bool FRigVMGraphFunctionData::IsPublic() const
{
	IRigVMGraphFunctionHost* FunctionHost = nullptr;
	if (UObject* FunctionHostObj = Header.LibraryPointer.HostObject.TryLoad())
	{
		FunctionHost = Cast<IRigVMGraphFunctionHost>(FunctionHostObj);
	}
	else
	{
		return false;
	}

	if (!FunctionHost)
	{
		return false;
	}

	FRigVMGraphFunctionStore* FunctionStore = FunctionHost->GetRigVMGraphFunctionStore();
	if (!FunctionStore)
	{
		return false;
	}

	return FunctionStore->IsFunctionPublic(Header.LibraryPointer);
}

FRigVMGraphFunctionData* FRigVMGraphFunctionData::FindFunctionData(const FSoftObjectPath& InHostObjectPath, const FName& InFunctionName, bool* bOutIsPublic, FString* OutErrorMessage)
{
	FRigVMGraphFunctionHeader InvalidHeader;

	const FName FunctionName = FRigVMGraphFunctionHeader::GetFunctionNameFromObjectPath(InHostObjectPath.ToString(), InFunctionName);

	UObject* HostObject = InHostObjectPath.TryLoad();
	if (!HostObject)
	{
		if(OutErrorMessage)
		{
			*OutErrorMessage = FString::Printf(TEXT("Failed to load the Host object %s."), *InHostObjectPath.ToString());
		}
		return nullptr;
	}

	IRigVMGraphFunctionHost* FunctionHost = Cast<IRigVMGraphFunctionHost>(HostObject);
	if (!FunctionHost)
	{
		if(GetFunctionHostFromObjectFunc)
		{
			FunctionHost = GetFunctionHostFromObjectFunc(HostObject);
		}
	}

	if (!FunctionHost)
	{
		if(OutErrorMessage)
		{
			*OutErrorMessage = TEXT("Host object is not a IRigVMGraphFunctionHost.");
		}
		return nullptr;
	}

	FRigVMGraphFunctionStore* FunctionStore = FunctionHost->GetRigVMGraphFunctionStore();
	if (!FunctionStore)
	{
		if(OutErrorMessage)
		{
			*OutErrorMessage = TEXT("Host object does not contain a function store.");
		}
		return nullptr;
	}

	FRigVMGraphFunctionData* Data = FunctionStore->FindFunctionByName(FunctionName, bOutIsPublic);
	if (!Data)
	{
		if(OutErrorMessage)
		{
			*OutErrorMessage = FString::Printf(TEXT("Function %s not found in host %s."), *FunctionName.ToString(), *InHostObjectPath.ToString());
		}
		return nullptr;
	}

	return Data;
}

FRigVMGraphFunctionData* FRigVMGraphFunctionData::FindFunctionData(const FRigVMGraphFunctionIdentifier& InIdentifier, bool* bOutIsPublic, FString* OutErrorMessage)
{
	IRigVMGraphFunctionHost* FunctionHost = nullptr;
	if (UObject* FunctionHostObj = InIdentifier.HostObject.TryLoad())
	{
		FunctionHost = Cast<IRigVMGraphFunctionHost>(FunctionHostObj);
	}
	else
	{
		if(OutErrorMessage)
		{
			*OutErrorMessage = FString::Printf(TEXT("Failed to load the Host object %s."), *InIdentifier.HostObject.ToString());
		}
		return nullptr;
	}

	if (!FunctionHost)
	{
		if(OutErrorMessage)
		{
			*OutErrorMessage = TEXT("Host object is not a IRigVMGraphFunctionHost.");
		}
		return nullptr;
	}

	FRigVMGraphFunctionStore* FunctionStore = FunctionHost->GetRigVMGraphFunctionStore();
	if (!FunctionStore)
	{
		if(OutErrorMessage)
		{
			*OutErrorMessage = TEXT("Host object does not contain a function store.");
		}
		return nullptr;
	}

	if(FRigVMGraphFunctionData* FunctionData = FunctionStore->FindFunction(InIdentifier, bOutIsPublic))
	{
		return FunctionData;
	}

	if(OutErrorMessage)
	{
		*OutErrorMessage = FString::Printf(TEXT("Function %s not found in host %s."), *InIdentifier.GetFunctionName(), *InIdentifier.HostObject.ToString());
	}
	return nullptr;
}


FString FRigVMGraphFunctionData::GetArgumentNameFromPinHash(const FString& InPinHash)
{
	FString Left, PinPath, PinName;
	if(RigVMStringUtils::SplitNodePathAtEnd(InPinHash, Left, PinPath))
	{
		Left.Reset();
		if(RigVMStringUtils::SplitPinPathAtStart(PinPath, Left, PinName))
		{
			if(Left.Equals(EntryString, ESearchCase::CaseSensitive) ||
				Left.Equals(ReturnString, ESearchCase::CaseSensitive))
			{
				return PinName;
			}
		}
	}
	return FString();
}

FRigVMOperand FRigVMGraphFunctionData::GetOperandForArgument(const FName& InArgumentName) const
{
	const FString InArgumentNameString = InArgumentName.ToString();
	for(const TPair<FString, FRigVMOperand>& Pair : CompilationData.Operands)
	{
		const FString ArgumentName = GetArgumentNameFromPinHash(Pair.Key);
		if(!ArgumentName.IsEmpty())
		{
			if(ArgumentName.Equals(InArgumentNameString, ESearchCase::CaseSensitive))
			{
				return Pair.Value;
			}
		}
	}
	return FRigVMOperand();
}

bool FRigVMGraphFunctionData::IsAnyOperandSharedAcrossArguments() const
{
	TSet<FRigVMOperand> UsedOperands;
	UsedOperands.Reserve(Header.Arguments.Num());
	for(const FRigVMGraphFunctionArgument& Argument : Header.Arguments)
	{
		if(Argument.IsExecuteContext())
		{
			continue;
		}
		
		const FRigVMOperand Operand = GetOperandForArgument(Argument.Name);
		if(!Operand.IsValid())
		{
			continue;
		}
		
		if(UsedOperands.Contains(Operand))
		{
			return true;
		}
		UsedOperands.Add(Operand);
	}
	return false;
}

bool FRigVMGraphFunctionData::PatchSharedArgumentOperandsIfRequired()
{
	// we are doing this to avoid output arguments of a function to share
	// memory. each output argument needs its own memory for the node
	// referencing the function to rely on.
	if(!IsAnyOperandSharedAcrossArguments())
	{
		return false;
	}

	// we'll keep doing this until there is no work left since
	// we need to shift all operand indices every time we change anything.
	bool bWorkLeft = true;
	while(bWorkLeft)
	{
		bWorkLeft = false;
		
		// create a list of arguments / operands to update
		TArray<TTuple<FName, FRigVMOperand>> ArgumentOperands;
		TMap<FRigVMOperand, TArray<FName>> OperandToArguments;
		ArgumentOperands.Reserve(Header.Arguments.Num());
		for(const FRigVMGraphFunctionArgument& Argument : Header.Arguments)
		{
			if(Argument.IsExecuteContext())
			{
				continue;
			}

			const FRigVMOperand Operand = GetOperandForArgument(Argument.Name);
			if(Operand.IsValid())
			{
				ArgumentOperands.Emplace(Argument.Name, Operand);
				OperandToArguments.FindOrAdd(Operand).Add(Argument.Name);
			}
		}

		// step 1: inject the properties and operands necessary to reflect the expected layout
		FRigVMOperand SourceOperand, TargetOperand;
		FString SourcePinPath, TargetPinPath;

		int32 ArgumentIndex = -1;
		for(int32 Index = 0; Index < Header.Arguments.Num(); Index++)
		{
			const FRigVMGraphFunctionArgument& Argument = Header.Arguments[Index];
			if(Argument.IsExecuteContext())
			{
				continue;
			}
			ArgumentIndex++;
			
			SourceOperand = GetOperandForArgument(Argument.Name);
			if(!SourceOperand.IsValid())
			{
				SourceOperand = FRigVMOperand();
				continue;
			}
			
			const TArray<FName>& ArgumentsSharingOperand = OperandToArguments.FindChecked(SourceOperand);
			if(ArgumentsSharingOperand.Num() == 1 || ArgumentsSharingOperand[0].IsEqual(Argument.Name, ENameCase::CaseSensitive))
			{
				continue;
			}

			check(SourceOperand.GetMemoryType() == ERigVMMemoryType::Work);

			// clone the property
			FRigVMFunctionCompilationPropertyDescription PropertyDescription = CompilationData.WorkPropertyDescriptions[SourceOperand.GetRegisterIndex()];

			for(const TPair<FString,FRigVMOperand>& Pair : CompilationData.Operands)
			{
				if(Pair.Value == SourceOperand)
				{
					SourcePinPath = Pair.Key;
					break;
				}
			}
			check(!SourcePinPath.IsEmpty());
			FString CompleteNodePath, NodePathPrefix, NodeName, PinName;
			verify(RigVMStringUtils::SplitPinPathAtEnd(SourcePinPath, CompleteNodePath, PinName));
			verify(RigVMStringUtils::SplitNodePathAtEnd(CompleteNodePath, NodePathPrefix, NodeName));
			if(Argument.Direction == ERigVMPinDirection::Input || Argument.Direction == ERigVMPinDirection::IO)
			{
				static const FString NewNodeName = TEXT("Entry");
				CompleteNodePath = RigVMStringUtils::JoinNodePath(NodePathPrefix, NewNodeName);
			}
			else
			{
				static const FString NewNodeName = TEXT("Return");
				CompleteNodePath = RigVMStringUtils::JoinNodePath(NodePathPrefix, NewNodeName);
			}
			TargetPinPath = RigVMStringUtils::JoinPinPath(CompleteNodePath, Argument.Name.ToString());
			PropertyDescription.Name = FRigVMPropertyDescription::SanitizeName(*TargetPinPath, false);

			int32 TargetIndex = ArgumentIndex;
			if(CompilationData.WorkPropertyDescriptions.IsValidIndex(ArgumentIndex))
			{
				TargetIndex = CompilationData.WorkPropertyDescriptions.Insert(PropertyDescription, ArgumentIndex);
			}
			else
			{
				TargetIndex = CompilationData.WorkPropertyDescriptions.Add(PropertyDescription);
			}

			TargetOperand = FRigVMOperand(SourceOperand.GetMemoryType(), TargetIndex, SourceOperand.GetRegisterOffset());
			bWorkLeft = true;
			break;
		}

		if(!bWorkLeft)
		{
			return true;
		}

		auto UpdateOperand = [TargetOperand](FRigVMOperand& Operand)
		{
			if(Operand.GetMemoryType() == TargetOperand.GetMemoryType())
			{
				if(Operand.GetRegisterIndex() >= TargetOperand.GetRegisterIndex())
				{
					Operand = FRigVMOperand(Operand.GetMemoryType(), Operand.GetRegisterIndex() + 1, Operand.GetRegisterOffset());
				}
			}
		};

		// step 2: update the property paths
		for(FRigVMFunctionCompilationPropertyPath& PropertyPath : CompilationData.WorkPropertyPathDescriptions)
		{
			if(PropertyPath.PropertyIndex != INDEX_NONE)
			{
				if(PropertyPath.PropertyIndex >= TargetOperand.GetRegisterIndex())
				{
					PropertyPath.PropertyIndex++;
				}
			}
		}

		// step 3: update the operands map
		for(TPair<FString, FRigVMOperand>& Pair : CompilationData.Operands)
		{
			UpdateOperand(Pair.Value);
		}
		CompilationData.Operands.FindOrAdd(TargetPinPath) = TargetOperand;

		// step 4: update the operands in the bytecode itself
		const FRigVMInstructionArray Instructions = CompilationData.ByteCode.GetInstructions();
		for(const FRigVMInstruction& Instruction : Instructions)
		{
			FRigVMOperandArray Operands = CompilationData.ByteCode.GetOperandsForOp(Instruction);
			for(int32 Index = 0; Index < Operands.Num(); Index++)
			{
				FRigVMOperand* Operand = const_cast<FRigVMOperand*>(&Operands[Index]);
				UpdateOperand(*Operand);
			}
		}

#if WITH_EDITOR
		// step 5: update the input and output operand maps
		for (TArray<FRigVMOperand>& InputOperands : CompilationData.ByteCode.InputOperandsPerInstruction)
		{
			for (FRigVMOperand& Operand : InputOperands)
			{
				UpdateOperand(Operand);
			}
		}
		for (TArray<FRigVMOperand>& OutputOperands : CompilationData.ByteCode.OutputOperandsPerInstruction)
		{
			for (FRigVMOperand& Operand : OutputOperands)
			{
				UpdateOperand(Operand);
			}
		}
#endif

		// step 6: add copy operations at the end of the byte code
		CompilationData.ByteCode.AddCopyOp(SourceOperand, TargetOperand);
	}
	
	return true;
}

bool FRigVMGraphFunctionData::PatchExternalVariablesToArguments()
{
	if (Header.ExternalVariables.IsEmpty())
	{
		return false;
	}

	if (!IsPublic())
	{
		return false;
	}
	
	for (const FRigVMExternalVariable& ExternalVariable : Header.ExternalVariables)
	{
		if (!ExternalVariable.IsValid(true))
		{
			continue;
		}
		// convert external variable to argument
		Header.Arguments.Emplace(ExternalVariable);
		check(Header.Arguments.Last().IsValid());
	}
	Header.ExternalVariables.Reset();
	
	return true;
}

FArchive& operator<<(FArchive& Ar, FRigVMNodeLayout& Layout)
{
	Ar.UsingCustomVersion(FRigVMObjectVersion::GUID);

	Ar << Layout.Categories;
	if(Ar.IsLoading())
	{
		if (Ar.CustomVer(FRigVMObjectVersion::GUID) < FRigVMObjectVersion::FunctionHeaderLayoutStoresPinIndexInCategory)
		{
			Layout.PinIndexInCategory.Reset();
		}
		else
		{
			Ar << Layout.PinIndexInCategory;
		}
	}
	else
	{
		Ar << Layout.PinIndexInCategory;
	}
	Ar << Layout.DisplayNames;
	return Ar;
}
