// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlainPropsUObjectBindingsInternal.h"
#include "PlainPropsRoundtripTest.h"
#include "PlainPropsUObjectMetaBindingsInternal.h"
#include "PlainPropsUObjectModule.h"
#include "PlainPropsUObjectRuntime.h"
#include "PlainPropsUeCoreBindings.h"
#include "PlainPropsVisualize.h"
#include "PlainPropsRestoreOverridableInternal.h"
#include "PlainPropsSaveOverridableInternal.h"
#include "Algo/Compare.h"
#include "Algo/Find.h"
#include "Containers/PagedArray.h"
#include "Hash/xxhash.h"
#include "Logging/StructuredLog.h"
#include "Math/PreciseFP.h"
#include "Misc/DefinePrivateMemberPtr.h"
#include "StructUtils/PropertyBag.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Templates/SharedPointer.h"
#include "Serialization/ArchiveSerializedPropertyChain.h"
#include "Templates/UniquePtr.h"
#include "UObject/AnsiStrProperty.h"
#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/FieldPathProperty.h"
#include "UObject/Object.h"
#include "UObject/ObjectHandle.h"
#include "UObject/ObjectResource.h"
#include "UObject/PropertyOptional.h"
#include "UObject/StrProperty.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectThreadContext.h"
#include "UObject/UnrealType.h"
#include "UObject/Utf8StrProperty.h"
#include "UObject/VerseStringProperty.h"
#include "UObject/VerseValueProperty.h"
#include "UObject/OverriddenPropertySet.h"
#include "VerseVM/VVMNativeString.h"


using FUnicastScriptDelegate = TScriptDelegate<FNotThreadSafeNotCheckedDelegateMode>;
using FMulticastInvocationList = TArray<FUnicastScriptDelegate>;
using FMulticastInvocationView = TConstArrayView<FUnicastScriptDelegate>;
using FDelegateBase = TDelegateAccessHandlerBase<FNotThreadSafeDelegateMode>;

// Temp hacks. Long-term either add FProperty getters for ctor/dtor/hash function pointers 
// and delegate APIs for non-intrusive serialization or integrate PlainProps into Core/CoreUObject
UE_DEFINE_PRIVATE_MEMBER_PTR(void(void*) const, GInitPropertyValue, FProperty, InitializeValueInternal);
UE_DEFINE_PRIVATE_MEMBER_PTR(void(void*) const, GDestroyPropertyValue, FProperty, DestroyValueInternal);
UE_DEFINE_PRIVATE_MEMBER_PTR(uint32(const void*) const, GHashPropertyValue, FProperty, GetValueTypeHashInternal);
UE_DEFINE_PRIVATE_MEMBER_PTR(TArray<FName>, GFieldPathPath, FFieldPath, Path);
UE_DEFINE_PRIVATE_MEMBER_PTR(TWeakObjectPtr<UStruct>, GFieldPathOwner, FFieldPath, ResolvedOwner);
UE_DEFINE_PRIVATE_MEMBER_PTR(FWeakObjectPtr, GDelegateObject, FScriptDelegate, Object);
UE_DEFINE_PRIVATE_MEMBER_PTR(FName, GDelegateFunctionName, FScriptDelegate, FunctionName);
UE_DEFINE_PRIVATE_MEMBER_PTR(FWeakObjectPtr, GUnicastDelegateObject, FUnicastScriptDelegate, Object);
UE_DEFINE_PRIVATE_MEMBER_PTR(FName, GUnicastDelegateFunctionName, FUnicastScriptDelegate, FunctionName);
UE_DEFINE_PRIVATE_MEMBER_PTR(FMulticastInvocationList, GMulticastDelegateInvocationList, FMulticastScriptDelegate, InvocationList);
UE_DEFINE_PRIVATE_MEMBER_PTR(bool, GSparseDelegateIsBound, FSparseDelegate, bIsBound);

#if UE_DETECT_DELEGATES_RACE_CONDITIONS && 0
UE_DEFINE_PRIVATE_MEMBER_PTR(FMRSWRecursiveAccessDetector, GDelegateAccessDetector, FDelegateBase, AccessDetector);
struct FDelegateAccess : public FDelegateBase
{
	struct FReadScope : public FDelegateBase::FReadAccessScope
	{
		explicit FReadScope(const FDelegateBase& In) : FDelegateBase::FReadAccessScope(In.*GDelegateAccessDetector) {}
	};
	struct FWriteScope : public FDelegateBase::FWriteAccessScope
	{
		explicit FWriteScope(FDelegateBase& In) : FDelegateBase::FWriteAccessScope(In.*GDelegateAccessDetector) {}
	};
};
#else
struct FDelegateAccess
{
	struct FReadScope { FReadScope(const FDelegateBase& In) {} };
	using FWriteScope = FReadScope;
};
#endif // UE_DETECT_DELEGATES_RACE_CONDITIONS


DEFINE_LOG_CATEGORY(LogPlainPropsUObject);

namespace PlainProps::UE
{

////////////////////////////////////////////////////////////////////////////////////////////////

inline ELeafWidth WidthOfEnum(const UEnum* Enum)
{
	using UnderlyingType = std::underlying_type_t<UEnum::EUnderlyingType>;
	static_assert(static_cast<UnderlyingType>(UEnum::EUnderlyingType::int8)   == 0);
	static_assert(static_cast<UnderlyingType>(UEnum::EUnderlyingType::int64)  == 3);
	static_assert(static_cast<UnderlyingType>(UEnum::EUnderlyingType::uint8)  == 4);
	static_assert(static_cast<UnderlyingType>(UEnum::EUnderlyingType::uint64) == 7);

	using enum ELeafWidth;
	static constexpr ELeafWidth Widths[8] = { B8, B16, B32, B64, B8, B16, B32, B64 };
	UnderlyingType Value = static_cast<UnderlyingType>(Enum->GetUnderlyingType());
	check(0 <= Value && Value < 8);
	return Widths[Value];
}

inline bool IsSigned(const UEnum* Enum)
{
	return Enum->GetUnderlyingType() < UEnum::EUnderlyingType::uint8;
}

////////////////////////////////////////////////////////////////////////////////////////////////

struct FDefaultStruct
{
	UScriptStruct::ICppStructOps&		Ops;
	alignas(16) uint8					Instance[0];
};

static FDefaultStruct* NewDefaultStruct(UScriptStruct::ICppStructOps& Ops)
{
	check(Ops.GetAlignment() <= 16);
	uint32 Size = sizeof(FDefaultStruct) + Ops.GetSize();
	FDefaultStruct Header = {Ops};
	FDefaultStruct* Out = new (FMemory::MallocZeroed(Size)) FDefaultStruct(Header);
	FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
	ThreadContext.IsInConstructor++; // Hack: FWaterBrushActorDefaults is doing ConstructorHelpers::FObjectFinder<UCurveFloat>
	Ops.Construct(Out->Instance);
	ThreadContext.IsInConstructor--;
	check(ThreadContext.IsInConstructor >= 0);
	return Out;
}

inline void	DeleteDefaultStruct(uint8* Instance)
{
	FDefaultStruct* Struct = reinterpret_cast<FDefaultStruct*>(Instance - offsetof(FDefaultStruct, Instance));
	if (Struct->Ops.HasDestructor())
	{
		Struct->Ops.Destruct(Instance);
	}
	FMemory::Free(Struct);
}

static constexpr uint64			DefaultInstanceStaticMask = 1;
inline FDefaultInstance			MakeStaticInstance(const void* Static)			{ return { reinterpret_cast<uint64>(Static) | DefaultInstanceStaticMask }; }
inline FDefaultInstance			MakeDefaultInstance(FDefaultStruct* Default)	{ return { reinterpret_cast<uint64>(Default->Instance) }; }
inline uint8*					GetInstance(FDefaultInstance Instance)			{ return reinterpret_cast<uint8*>(Instance.Ptr & ~DefaultInstanceStaticMask); }
inline void						DeleteInstance(FDefaultInstance Instance)
{
	if (!(Instance.Ptr & DefaultInstanceStaticMask))
	{
		DeleteDefaultStruct(reinterpret_cast<uint8*>(Instance.Ptr));
	}
}

static void ReserveZeroes(/* in-out */ FMutableMemoryView& Zeroes, SIZE_T Size, uint32 Alignment)
{
	Size +=  FMath::Max<int32>(0, Alignment - 16);
	Size = Align(Size, 4096);
	if (Zeroes.GetSize() < Size)
	{
		FMemory::Free(Zeroes.GetData());
		Zeroes = FMutableMemoryView(FMemory::MallocZeroed(Size, 16), Size);
	}
}

FDefaultStructs::~FDefaultStructs()
{
	for (TPair<FBindId, FDefaultInstance> Instance : Instances)
	{
		DeleteInstance(Instance.Value);
	}
}

inline bool Flip(FBitReference Bit)
{
	Bit = !Bit;
	return Bit;
}

void FDefaultStructs::ReserveFlags(uint32 Idx)
{
	if (Idx >= static_cast<uint32>(Instanced.Num()))
	{
		Instanced.SetNum(FMath::RoundUpToPowerOfTwo64(Idx + 1), false);
#if DO_CHECK
		Bound.SetNum(Instanced.Num(), false);
#endif
	}
}

void FDefaultStructs::Bind(FBindId Id, const UScriptStruct* Struct)
{
	const EStructFlags Flags = Struct->StructFlags;
	const SIZE_T Size = Struct->GetStructureSize();
	const uint32 Alignment = Struct->GetMinAlignment();
	UScriptStruct::ICppStructOps* Ops = Struct->GetCppStructOps();

	ReserveFlags(Id.Idx);
#if DO_CHECK
	checkf(Flip(Bound[Id.Idx]), TEXT("'%s' already bound"), *GUE.Debug.Print(Id));
#endif

	if (const UUserDefinedStruct* UserStruct = Cast<UUserDefinedStruct>(Struct))
	{
		const void* DefaultInstance = UserStruct->GetDefaultInstance();
		check(DefaultInstance);
		if (FMemory::MemIsZero(DefaultInstance, Size))
		{
			ReserveZeroes(Zeroes, Size, Alignment);
		}
		else
		{
			Instanced[Id.Idx] = true;
			Instances.Emplace(Id, MakeStaticInstance(DefaultInstance));
		}
	}
	else if (!!(Flags & STRUCT_ZeroConstructor) || Ops == nullptr)
	{
		ReserveZeroes(Zeroes, Size, Alignment);
	}
	else
	{
		check(Ops->GetSize() == Size);
		FDefaultStruct* Default = NewDefaultStruct(*Ops);
		if (FMemory::MemIsZero(Default->Instance, Size))
		{
			DeleteDefaultStruct(Default->Instance);
			ReserveZeroes(Zeroes, Size, Alignment);
		}
		else
		{
			Instanced[Id.Idx] = true;
			Instances.Add(Id, MakeDefaultInstance(Default));
		}
	}
}

void FDefaultStructs::BindZeroes(FBindId Id, SIZE_T Size, uint32 Alignment)
{
	ReserveFlags(Id.Idx);
#if DO_CHECK
	checkf(Flip(Bound[Id.Idx]), TEXT("'%s' already bound"), *GUE.Debug.Print(Id));
#endif
	ReserveZeroes(Zeroes, Size, Alignment);
}

void FDefaultStructs::BindStatic(FBindId Id, const void* Struct)
{
	ReserveFlags(Id.Idx);
#if DO_CHECK
	checkf(Flip(Bound[Id.Idx]), TEXT("'%s' already bound"), *GUE.Debug.Print(Id));
#endif
	check(!Instanced[Id.Idx]);
	check(GetInstance(MakeStaticInstance(Struct)) == Struct);

	Instanced[Id.Idx] = true;
	Instances.Add(Id, MakeStaticInstance(Struct));
}

void FDefaultStructs::Drop(FBindId Id)
{
#if DO_CHECK
	checkf(!Flip(Bound[Id.Idx]), TEXT("'%s' isn't bound"), *GUE.Debug.Print(Id));
#endif
	if (Instanced[Id.Idx])
	{
		Instanced[Id.Idx] = false;
		DeleteInstance(Instances.FindAndRemoveChecked(Id));
	}
}

const void* FDefaultStructs::Get(FBindId Id)
{
#if DO_CHECK
	checkf(Bound[Id.Idx], TEXT("'%s' lack default"), *GUE.Debug.Print(Id));
#endif
	return Instanced[Id.Idx] ? GetInstance(Instances.FindChecked(Id)) : Zeroes.GetData(); 
}

////////////////////////////////////////////////////////////////////////////////////////////////

FCommonScopeIds::FCommonScopeIds(TIdIndexer<FSensitiveName>& Names)
: CoreUObject(				Names.MakeScope("/Script/CoreUObject"))
, Engine(					Names.MakeScope("/Script/Engine"))
{}

FCommonTypenameIds::FCommonTypenameIds(TIdIndexer<FSensitiveName>& Names)
: Optional(					Names.NameType("Optional"))
, Map(						Names.NameType("Map"))
, Set(						Names.NameType("Set"))
, Pair(						Names.NameType("Pair"))
, LeafArray(				Names.NameType("LeafArray"))
, TrivialArray(				Names.NameType("TrivialArray"))
, NonTrivialArray(			Names.NameType("NonTrivialArray"))
, StaticArray(				Names.NameType("StaticArray"))
, TrivialOptional(			Names.NameType("TrivialOptional"))
, IntrusiveOptional(		Names.NameType("IntrusiveOptional"))
, NonIntrusiveOptional(		Names.NameType("NonIntrusiveOptional"))
, String(					Names.NameType("String"))
, Utf8String(				Names.NameType("Utf8String"))
, AnsiString(				Names.NameType("AnsiString"))
, VerseString(				Names.NameType("VerseString"))
{}

FCommonEnumIds::FCommonEnumIds(const FCommonScopeIds& Scopes, TIdIndexer<FSensitiveName>& Names)
: PixelFormat(Names.IndexEnum({Scopes.CoreUObject, Names.MakeTypename("PixelFormat")}))
{}

FCommonPropertyStructIds::FCommonPropertyStructIds(const FCommonScopeIds& Scopes, TIdIndexer<FSensitiveName>& Names)
: Property(					Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("FProperty")}))
, EnumProperty(				Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("FEnumProperty")}))
, BoolProperty(				Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("FBoolProperty")}))
, ByteProperty(				Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("FByteProperty") }))
, StructProperty(			Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("FStructProperty")}))
, ClassProperty(			Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("FClassProperty")}))
, ObjectPropertyBase(		Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("FObjectPropertyBase")}))
, ArrayProperty(			Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("FArrayProperty")}))
, SetProperty(				Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("FSetProperty")}))
, MapProperty(				Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("FMapProperty")}))
, OptionalProperty(			Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("FOptionalProperty")}))
{}

FCommonStructIds::FCommonStructIds(const FCommonScopeIds& Scopes, TIdIndexer<FSensitiveName>& Names)
: Name(						Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("Name")}))
, Text(						Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("Text")}))
, Guid(						Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("Guid")}))
, FieldPath(				Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("FieldPath")}))
, Struct(					Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("Struct")}))
, Class(					Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("Class")}))
, Function(					Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("Function")}))
, ScriptStruct(				Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("ScriptStruct")}))
, SoftObjectPath(			Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("SoftObjectPath")}))
, ClassPtr(					Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("ClassPtr")}))
, ObjectPtr(				Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("ObjectPtr")}))
, WeakObjectPtr(			Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("WeakObjectPtr")}))
, LazyObjectPtr(			Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("LazyObjectPtr")}))
, SoftObjectPtr(			Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("SoftObjectPtr")}))
, ScriptInterface(			Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("ScriptInterface")}))
, Delegate(					Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("Delegate")}))
, MulticastDelegate(		Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("MulticastDelegate")}))
, MulticastInlineDelegate(	Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("MulticastInlineDelegate")}))
, MulticastSparseDelegate(	Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("MulticastSparseDelegate")}))
, VerseFunction(			Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("VerseFunction")}))
, DynamicallyTypedValue(	Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("DynamicallyTypedValue")}))
, ReferencePropertyValue(	Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("ReferencePropertyValue")}))
, PropertyBag(				Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("PropertyBag")}))
, InstancedStruct(			Names.IndexStruct({Scopes.CoreUObject, Names.MakeTypename("InstancedStruct")}))
{}

FCommonMemberIds::FCommonMemberIds(TIdIndexer<FSensitiveName>& Names)
: Key(						Names.NameMember("Key"))
, Value(					Names.NameMember("Value"))
, Assign(					Names.NameMember("Assign"))
, Remove(					Names.NameMember("Remove"))
, Insert(					Names.NameMember("Insert"))
, Modify(					Names.NameMember("Modify"))
, Id(						Names.NameMember("Id"))
, Object(					Names.NameMember("Object"))
, Function(					Names.NameMember("Function"))
, Invocations(				Names.NameMember("Invocations"))
, Path(						Names.NameMember("Path"))
, Owner(					Names.NameMember("Owner"))
{}

////////////////////////////////////////////////////////////////////////////////////////////////

FGlobals::FGlobals() 
: Enums(FDebugIds(Names))
, Schemas(FDebugIds(Names))
, Metadatas(FDebugIds(Names))
, Customs(FDebugIds(Names))
, Scopes(Names)
, EnumIds(Scopes, Names)
, Structs(Scopes, Names)
, Properties(Scopes, Names)
, Typenames(Names)
, Members(Names)
, Numerals(Names)
, Debug(Names)
{}

FGlobals GUE;

////////////////////////////////////////////////////////////////////////////////////////////////

static constexpr uint64 LeafMask =			CASTCLASS_FNumericProperty | CASTCLASS_FEnumProperty| CASTCLASS_FBoolProperty;
static constexpr uint64 IntSMask =			CASTCLASS_FInt8Property | CASTCLASS_FInt16Property | CASTCLASS_FIntProperty | CASTCLASS_FInt64Property;
static constexpr uint64 IntUMask =			CASTCLASS_FByteProperty | CASTCLASS_FUInt16Property | CASTCLASS_FUInt32Property | CASTCLASS_FUInt64Property;
static constexpr uint64 ContainerMask =		CASTCLASS_FArrayProperty | CASTCLASS_FSetProperty | CASTCLASS_FMapProperty  | CASTCLASS_FOptionalProperty;
static constexpr uint64 StringMask =		CASTCLASS_FStrProperty | CASTCLASS_FUtf8StrProperty | CASTCLASS_FAnsiStrProperty | CASTCLASS_FVerseStringProperty;
static constexpr uint64 CommonStructMask =	CASTCLASS_FNameProperty | CASTCLASS_FTextProperty |  CASTCLASS_FFieldPathProperty | CASTCLASS_FClassProperty |
											CASTCLASS_FObjectProperty | CASTCLASS_FWeakObjectProperty | CASTCLASS_FSoftObjectProperty | CASTCLASS_FLazyObjectProperty |
											CASTCLASS_FDelegateProperty | CASTCLASS_FMulticastInlineDelegateProperty;
static constexpr uint64 MiscMask =			CASTCLASS_FMulticastSparseDelegateProperty | CASTCLASS_FInterfaceProperty;

static FBindId FlagsToCommonBindId(uint64 MaskedCastFlags)
{
	switch (MaskedCastFlags)
	{
		case CASTCLASS_FNameProperty:								return GUE.Structs.Name;
		case CASTCLASS_FClassProperty | CASTCLASS_FObjectProperty:	return GUE.Structs.ClassPtr;
		case CASTCLASS_FObjectProperty:								return GUE.Structs.ObjectPtr;
		case CASTCLASS_FWeakObjectProperty:							return GUE.Structs.WeakObjectPtr;
		case CASTCLASS_FSoftObjectProperty:							return GUE.Structs.SoftObjectPtr;
		case CASTCLASS_FLazyObjectProperty:							return GUE.Structs.LazyObjectPtr;
		case CASTCLASS_FDelegateProperty:							return GUE.Structs.Delegate;
		case CASTCLASS_FMulticastInlineDelegateProperty:			return GUE.Structs.MulticastInlineDelegate;
		case CASTCLASS_FTextProperty:								return GUE.Structs.Text;
		case CASTCLASS_FFieldPathProperty:							return GUE.Structs.FieldPath;
		default:													break; // error
	}

	check(MaskedCastFlags); // @pre violated
	check((MaskedCastFlags & CommonStructMask) == MaskedCastFlags); // @pre violated
	checkf(FMath::CountBits64(MaskedCastFlags) == 1, TEXT("Masked CASTCLASS flags %llx match more than one common property type"), MaskedCastFlags);
	check(false); // Mismatch between this function and CommonStructMask
	return {};
}

template<uint64 Mask>
static bool HasAny(uint64 Flags)
{
	return (Mask & Flags) != 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////

static FScopeId IndexScope(const UField* Field)
{
	// This wouldn't be needed in an intrusive or cached solution
	TArray<FFlatScopeId, TInlineAllocator<64>> ReversedOuters;
	for (const UObject* Outer = Field->GetOuter(); Outer; Outer = Outer->GetOuter())
	{
		ReversedOuters.Add(GUE.Names.NameScope(Outer->GetFName()));
	}
	return GUE.Names.NestReversedScopes(ReversedOuters);
}

static FType IndexType(const UField* Field)
{
	return { IndexScope(Field), GUE.Names.MakeTypename(Field->GetFName()) };
}

static FType IndexType(const UField* Field, const char* Suffix)
{
	TUtf8StringBuilder<64> Str;
	Field->GetFName().AppendString(Str);
	Str.Append(Suffix);
	FTypenameId Typename = GUE.Names.MakeTypename(*Str);
	return { IndexScope(Field), Typename };
}

FType IndexStruct(const UStruct* Struct)
{
	return IndexType(Struct);
}

FType IndexStructMeta(const UStruct* Struct)
{
	return IndexType(Struct, "_Meta");
}

static EMemberPresence GetOccupancy(const UStruct* Struct)
{
	if (Struct->HasAnyCastFlags(CASTCLASS_UScriptStruct))
	{
		EStructFlags Flags = static_cast<const UScriptStruct*>(Struct)->StructFlags;
		return (Flags & (STRUCT_Immutable | STRUCT_Atomic)) ? EMemberPresence::RequireAll : EMemberPresence::AllowSparse; 
	}
	return EMemberPresence::AllowSparse;
}

////////////////////////////////////////////////////////////////////////////////////////////////

static FTypedRange SaveNames(TConstArrayView<FName> Names, const FSaveContext& Ctx)
{
	FBindId Id = GUE.Structs.Name;
	FStructRangeSaver Out(Ctx.Scratch, Names.Num());
	for (FName Name : Names)
	{
		Out.AddItem(SaveStruct(&Name, Id, Ctx));
	}
	return Out.Finalize(MakeStructRangeSchema(DefaultRangeMax, Id));
}

static void LoadNames(TArray<FName>& Dst, FStructRangeLoadView Src)
{
	Dst.SetNumUninitialized(static_cast<int32>(Src.Num()));
	FName* DstIt = Dst.GetData();
	for (FStructLoadView Name : Src)
	{
		LoadStruct(DstIt++, Name);
	}
}

FFieldPathBinding::FFieldPathBinding(TPropertySpecifier<2>& Spec)
: MemberIds{GUE.Members.Path, GUE.Members.Owner}
{
	Spec.Members[0] = DefaultRangeOf(GUE.Structs.Name);
	Spec.Members[1] = GUE.Structs.WeakObjectPtr;
}

void FFieldPathBinding::Save(FMemberBuilder& Dst, const FFieldPath& Src, const FFieldPath*, const FSaveContext& Ctx) const
{
	Dst.AddRange(GUE.Members.Path, SaveNames(Src.*GFieldPathPath, Ctx));
	Dst.AddStruct(GUE.Members.Owner, GUE.Structs.WeakObjectPtr, SaveStruct(&(Src.*GFieldPathOwner), GUE.Structs.WeakObjectPtr, Ctx));
}

void FFieldPathBinding::Load(FFieldPath& Dst, FStructLoadView Src, ECustomLoadMethod Method) const
{
	FMemberLoader Members(Src);
	Dst.Reset(); // ClearCachedField() more optimal
	LoadNames(Dst.*GFieldPathPath, Members.GrabRange().AsStructs());
	LoadStruct(&(Dst.*GFieldPathOwner), Members.GrabStruct());
}

bool FFieldPathBinding::Diff(const FFieldPath& A, const FFieldPath& B, const FBindContext&)
{
	return A != B;
}

////////////////////////////////////////////////////////////////////////////////////////////////

FDelegateBinding::FDelegateBinding(TPropertySpecifier<2>& Out)
: MemberIds{GUE.Members.Object, GUE.Members.Function}
{
	Out.SetMembers(GUE.Structs.WeakObjectPtr, GUE.Structs.Name);
}

void FDelegateBinding::Save(FMemberBuilder& Dst, const FScriptDelegate& Src, const FScriptDelegate* Default, const FSaveContext& Ctx) const
{
	FDelegateAccess::FReadScope Scope(Src);
	if (FName Function = Src.*GDelegateFunctionName; Function != FName())
	{
		Dst.AddStruct(GUE.Members.Object, GUE.Structs.WeakObjectPtr, SaveStruct(&(Src.*GDelegateObject), GUE.Structs.WeakObjectPtr, Ctx));
		Dst.AddStruct(GUE.Members.Function, GUE.Structs.Name, SaveStruct(&Function, GUE.Structs.Name, Ctx));
	}
}

void FDelegateBinding::Load(FScriptDelegate& Dst, FStructLoadView Src, ECustomLoadMethod Method) const
{
	FMemberLoader Members(Src);
	if (Members.HasMore())
	{
		FDelegateAccess::FWriteScope Scope(Dst);
		LoadStruct(&(Dst.*GDelegateObject), Members.GrabStruct());
		LoadStruct(&(Dst.*GDelegateFunctionName), Members.GrabStruct());
	}
	else
	{
		Dst.Clear();
	}
}

bool FDelegateBinding::Diff(const FScriptDelegate& A, const FScriptDelegate& B, const FBindContext&)
{
	return A != B;
}

////////////////////////////////////////////////////////////////////////////////////////////////

static void SaveUnicastDelegate(FMemberBuilder& Dst, const FUnicastScriptDelegate& Src, const FSaveContext& Ctx)
{
	Dst.AddStruct(GUE.Members.Object, GUE.Structs.WeakObjectPtr, SaveStruct(&(Src.*GUnicastDelegateObject), GUE.Structs.WeakObjectPtr, Ctx));
	Dst.AddStruct(GUE.Members.Function, GUE.Structs.Name, SaveStruct(&(Src.*GUnicastDelegateFunctionName), GUE.Structs.Name, Ctx));
}

static void LoadUnicastDelegate(FUnicastScriptDelegate& Dst, FStructLoadView Src)
{
	FMemberLoader Members(Src);
	LoadStruct(&(Dst.*GUnicastDelegateObject), Members.GrabStruct());
	LoadStruct(&(Dst.*GUnicastDelegateFunctionName), Members.GrabStruct());
}

static FTypedRange SaveInvocations(const FMulticastInvocationList& In, const FSaveContext& Ctx)
{
	FBindId ItemId = GUE.Structs.Delegate;
	FMemberSchema Schema = MakeStructRangeSchema(DefaultRangeMax, ItemId);
	if (int32 NumTotal = In.Num())
	{
		TBitArray<> Keep;
		Keep.Reserve(NumTotal);
		for (const FUnicastScriptDelegate& Invocation : In)
		{
			Keep.Add(!Invocation.IsCompactable());
		}
	
		if (int32 NumKept = Keep.CountSetBits())
		{
			const FStructDeclaration& ItemDecl = Ctx.GetDeclaration(ItemId);
			const FUnicastScriptDelegate* Src = In.GetData();
			FStructRangeSaver Dst(Ctx.Scratch, static_cast<uint64>(NumKept));
			FMemberBuilder Tmp;
			for (int32 Idx = 0; Idx < NumTotal; ++Idx)
			{
				if (Keep[Idx])
				{
					SaveUnicastDelegate(/* out */ Tmp, Src[Idx], Ctx);
					Dst.AddItem(Tmp.BuildAndReset(Ctx.Scratch, ItemDecl, GUE.Debug));
				}
			}
			return Dst.Finalize(Schema);
		}
	}

	return {Schema, nullptr};
}

static FStructDeclarationPtr DeclareMulticastDelegate()
{
	return Declare({GUE.Structs.MulticastDelegate, NoId, 0, EMemberPresence::RequireAll, {GUE.Members.Invocations}, {DefaultRangeOf(GUE.Structs.Delegate)}});
}

static void SaveMulticastDelegate(FMemberBuilder& Dst, const FMulticastScriptDelegate& Src, const FSaveContext& Ctx)
{
	FDelegateAccess::FReadScope Scope(Src);
	Dst.AddRange(GUE.Members.Invocations, SaveInvocations(Src.*GMulticastDelegateInvocationList, Ctx));
}

static void SaveEmptyMulticastDelegate(FMemberBuilder& Dst)
{
	Dst.AddRange(GUE.Members.Invocations, { MakeStructRangeSchema(DefaultRangeMax, GUE.Structs.Delegate), nullptr });
}

static void LoadInvocations(FMulticastInvocationList& Dst, FStructRangeLoadView Src)
{
	Dst.Reset(static_cast<int32>(Src.Num()));
	for (FStructLoadView Invocation : Src)
	{
		LoadUnicastDelegate(Dst.AddDefaulted_GetRef(), Invocation);
	}
}

static void LoadMulticastDelegate(FMulticastScriptDelegate& Dst, FMemberLoader& Src)
{
	FDelegateAccess::FWriteScope Scope(Dst);
	LoadInvocations(Dst.*GMulticastDelegateInvocationList, Src.GrabRange().AsStructs());
}

inline bool DiffInvocations(TConstArrayView<FUnicastScriptDelegate> A, TConstArrayView<FUnicastScriptDelegate> B)
{
	if (A.Num() + B.Num() == 0)
	{
		return false;
	}

	const FUnicastScriptDelegate* EndA = A.GetData() + A.Num();
	const FUnicastScriptDelegate* EndB = B.GetData() + B.Num();
	for (const FUnicastScriptDelegate* ItA = A.GetData(), *ItB = B.GetData(); true; ++ItA, ++ItB)
	{
		for (; ItA != EndA && ItA->IsCompactable(); ++ItA) {}
		for (; ItB != EndB && ItB->IsCompactable(); ++ItB) {}

		if (ItA == EndA || ItB == EndB)
		{
			return ItA != EndA || ItB != EndB;
		}
		else if (*ItA != *ItB)
		{
			return true;
		}
	}
}

static bool DiffMulticastDelegate(const FMulticastScriptDelegate& A, const FMulticastScriptDelegate& B)
{
	return DiffInvocations(A.*GMulticastDelegateInvocationList, B.*GMulticastDelegateInvocationList);
}

FMulticastInlineDelegateBinding::FMulticastInlineDelegateBinding(TPropertySpecifier<1>& Spec)
: MemberIds{GUE.Members.Invocations}
{
	Spec.Members[0] = DefaultRangeOf(GUE.Structs.Delegate);
}

void FMulticastInlineDelegateBinding::Save(FMemberBuilder& Dst, const FMulticastScriptDelegate& Src, const FMulticastScriptDelegate* Default, const FSaveContext& Ctx) const
{
	SaveMulticastDelegate(Dst, Src, Ctx);
}

void FMulticastInlineDelegateBinding::Load(FMulticastScriptDelegate& Dst, FStructLoadView Src, ECustomLoadMethod Method) const
{
	check(Method == ECustomLoadMethod::Assign);
	FMemberLoader Members(Src);
	LoadMulticastDelegate(Dst, Members);
}

bool FMulticastInlineDelegateBinding::Diff(const FMulticastScriptDelegate& A, const FMulticastScriptDelegate& B, const FBindContext&)
{
	return DiffMulticastDelegate(A, B);
}

////////////////////////////////////////////////////////////////////////////////////////////////

struct FMulticastSparseDelegateBinding final : ICustomBinding
{
	explicit FMulticastSparseDelegateBinding(const USparseDelegateFunction* SignatureFunction)
	: OwningClassName(SignatureFunction->OwningClassName)
	, DelegateName(SignatureFunction->DelegateName)
	{}

	const FName OwningClassName;
	const FName DelegateName;

	virtual void SaveCustom(FMemberBuilder& Dst, const void* Src, FBaseline Base, const FSaveContext& Ctx) override
	{
		const void* Default = Base.Get();
		if (!Default || DiffCustom(Src, Default, Ctx))
		{
			Save(Dst, *static_cast<const FSparseDelegate*>(Src), Ctx);
		}
	}

	virtual void LoadCustom(void* Dst, FStructLoadView Src, ECustomLoadMethod Method) const override
	{
		check(Method == ECustomLoadMethod::Assign);
		Load(*static_cast<FSparseDelegate*>(Dst), Src);
	}

	virtual bool DiffCustom(const void* A, const void* B, const FBindContext&) const override
	{
		return Diff(*static_cast<const FSparseDelegate*>(A), *static_cast<const FSparseDelegate*>(B));
	}

	const FMulticastScriptDelegate* GetMulticastDelegate(const FSparseDelegate& Sparse) const
	{
		if (Sparse.IsBound())
		{
			const UObject* Owner = FSparseDelegateStorage::ResolveSparseOwner(Sparse, OwningClassName, DelegateName);
			return FSparseDelegateStorage::GetMulticastDelegate(Owner, DelegateName);
		}
		return nullptr;
	}
	
	void Save(FMemberBuilder& Dst, const FSparseDelegate& Src, const FSaveContext& Ctx) const
	{
		if (const FMulticastScriptDelegate* Delegate = GetMulticastDelegate(Src))
		{
			SaveMulticastDelegate(Dst, *Delegate, Ctx);
		}
		else
		{
			SaveEmptyMulticastDelegate(Dst);
		}
	}
	
	void Load(FSparseDelegate& Dst, FStructLoadView Src) const
	{
		if (Dst.IsBound())
		{
			UObject* Owner = FSparseDelegateStorage::ResolveSparseOwner(Dst, OwningClassName, DelegateName);
			FSparseDelegateStorage::Clear(Owner, DelegateName);
			Dst.*GSparseDelegateIsBound = false;
		}

		FMemberLoader Members(Src);
		if (Members.HasMore())
		{
			UObject* Owner = FSparseDelegateStorage::ResolveSparseOwner(Dst, OwningClassName, DelegateName);
			FMulticastScriptDelegate Tmp;
			LoadMulticastDelegate(Tmp, Members);
			FSparseDelegateStorage::SetMulticastDelegate(Owner, DelegateName, MoveTemp(Tmp));
			Dst.*GSparseDelegateIsBound = true;
		}
	}

	bool Diff(const FSparseDelegate& SparseA, const FSparseDelegate& SparseB) const
	{
		const FMulticastScriptDelegate* A = GetMulticastDelegate(SparseA);
		const FMulticastScriptDelegate* B = GetMulticastDelegate(SparseB);
		if (A && B)
		{
			return DiffMulticastDelegate(*A, *B);
		}
		return !!A != !!B;
	}
};

static FBindId BindSparseDelegate(FBindId Owner, FMulticastSparseDelegateProperty* Property)
{
	// Todo: Ownership / memory leak
	ICustomBinding* Leak = new FMulticastSparseDelegateBinding(CastChecked<USparseDelegateFunction>(Property->SignatureFunction));

	FType MulticastSparseDelegate = GUE.Names.Resolve(GUE.Structs.MulticastSparseDelegate);
	FType OwnerParam = GUE.Names.Resolve(Owner);
	FType PropertyParam = { GUE.Scopes.CoreUObject, FTypenameId(GUE.Names.NameType(Property->GetFName())) };
	FType UniqueBindName = GUE.Names.MakeParametricType(MulticastSparseDelegate, {OwnerParam, PropertyParam});
	FBindId Id = GUE.Names.IndexBindId(UniqueBindName);
	
	static FStructDeclarationPtr Declaration = DeclareMulticastDelegate();
	GUE.Customs.BindStruct(Id, *Leak, FStructDeclarationPtr(Declaration), {});

	return Id;
}

////////////////////////////////////////////////////////////////////////////////////////////////

// Todo: Temporary utilities that allow us to access overridable serialization state with just a pointer
// to property data. These should be replaced by keeping track of the current override node.

static void FindPropertyWithData(UObject* Owner, const void* PropertyData,
								 const TFunctionRef<void(const FPropertyVisitorContext& /*Context*/)> PropertyMatchFunc)
{
	TSet<const FProperty*> VisitedProperties;

	Owner->GetClass()->Visit(Owner, [&VisitedProperties, PropertyData, &PropertyMatchFunc](const FPropertyVisitorContext& Context)->EPropertyVisitorControlFlow
	{
		const FPropertyVisitorPath& PropertyPath = Context.Path;
		const FPropertyVisitorData& Data = Context.Data;

		const FProperty* Property = PropertyPath.Top().Property;

		if (!Property || VisitedProperties.Contains(Property))
		{
			return EPropertyVisitorControlFlow::StepOver;
		}

		if (Data.PropertyData == PropertyData)
		{
			// We don't stop the visit here because we want to find the innermost matching property.
			PropertyMatchFunc(Context);
		}

		VisitedProperties.Emplace(Property);

		// Don't step into references to other objects to avoid cycles.
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			return EPropertyVisitorControlFlow::StepOver;
		}
		else
		{
			return EPropertyVisitorControlFlow::StepInto;
		}
	});
}

static const FOverriddenPropertyNode* GetOverriddenNode(const FOverriddenPropertySet& Overrides, const void* PropertyData)
{
	const FOverriddenPropertyNode* CurrentNode = nullptr;

	FindPropertyWithData(Overrides.GetOwner(), PropertyData, [&CurrentNode, &Overrides](const FPropertyVisitorContext& Context)
	{
		const FPropertyVisitorPath& PropertyPath = Context.Path;

		FArchiveSerializedPropertyChain PropertyChain = PropertyPath.ToSerializedPropertyChain();

		if (const FOverriddenPropertyNode* Node = Overrides.GetOverriddenPropertyNode(&PropertyChain))
		{
			CurrentNode = Node;
		}
	});

	return CurrentNode;
}

static EOverriddenPropertyOperation GetOverriddenOperation(const FOverriddenPropertySet& Overrides, const void* PropertyData)
{
	FPropertyVisitorPath FoundPath;

	FindPropertyWithData(Overrides.GetOwner(), PropertyData, [&FoundPath](const FPropertyVisitorContext& Context)
	{
		FoundPath = Context.Path;
	});

	EOverriddenPropertyOperation Operation = EOverriddenPropertyOperation::None;

	if (FoundPath.Num() > 0)
	{
		Operation = Overrides.GetOverriddenPropertyOperation(FoundPath.GetRootIterator());
	}

	return Operation;
}

static FOverriddenPropertyNode* RestoreOverriddenOperation(FOverriddenPropertySet& Overrides, EOverriddenPropertyOperation Operation, const void* PropertyData)
{
	FPropertyVisitorPath FoundPath;

	FindPropertyWithData(Overrides.GetOwner(), PropertyData, [&FoundPath](const FPropertyVisitorContext& Context)
	{
		FoundPath = Context.Path;
	});

	FOverriddenPropertyNode* OverriddenNode = nullptr;

	if (FoundPath.Num() > 0)
	{
		FArchiveSerializedPropertyChain PropertyChain = FoundPath.ToSerializedPropertyChain();
		OverriddenNode = Overrides.RestoreOverriddenPropertyOperation(Operation, &PropertyChain, /*Property*/nullptr);

	}

	return OverriddenNode;
}

////////////////////////////////////////////////////////////////////////////////////////////////

FVerseFunctionBinding::FVerseFunctionBinding(TPropertySpecifier<1>& Spec)
: MemberIds{GUE.Members.Value}
{
	Spec.Members[0] = SpecDynamicStruct;
}

void FVerseFunctionBinding::Save(FMemberBuilder& Dst, const FVerseFunction& Src, const FVerseFunction* Default, const FSaveContext& Ctx) const
{
	unimplemented();
}

void FVerseFunctionBinding::Load(FVerseFunction& Dst, FStructLoadView Src, ECustomLoadMethod Method) const
{
	unimplemented();
}

bool FVerseFunctionBinding::Diff(const FVerseFunction& A, const FVerseFunction& B, const FBindContext&)
{
	//return !(A == B);
	return false;
}
	
////////////////////////////////////////////////////////////////////////////////////////////////

FDynamicallyTypedValueBinding::FDynamicallyTypedValueBinding(TPropertySpecifier<1>& Spec)
: MemberIds{GUE.Members.Value}
{
	Spec.Members[0] = SpecDynamicStruct;
}

void FDynamicallyTypedValueBinding::Save(FMemberBuilder& Dst, const Type& Src, const Type* Default, const FSaveContext& Ctx) const
{
	unimplemented();
}

void FDynamicallyTypedValueBinding::Load(Type& Dst, FStructLoadView Src, ECustomLoadMethod Method) const
{
	unimplemented();
}

bool FDynamicallyTypedValueBinding::Diff(const ::UE::FDynamicallyTypedValue& A, const ::UE::FDynamicallyTypedValue& B, const FBindContext&)
{
	//return !UE::Verse::FRuntimeTypeDynamic::Get().AreIdentical(&A, &B);
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////

FReferencePropertyBinding::FReferencePropertyBinding(TPropertySpecifier<1>& Spec)
: MemberIds{GUE.Members.Value}
{
	Spec.Members[0] = SpecDynamicStruct;
}

void FReferencePropertyBinding::Save(FMemberBuilder& Dst, const FReferencePropertyValue& Src, const FReferencePropertyValue* Default, const FSaveContext& Ctx) const
{
	unimplemented();
}

void FReferencePropertyBinding::Load(FReferencePropertyValue& Dst, FStructLoadView Src, ECustomLoadMethod Method) const
{
	unimplemented();
}

bool FReferencePropertyBinding::Diff(const FReferencePropertyValue& A, const FReferencePropertyValue& B, const FBindContext&)
{
	unimplemented();
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////

FInstancedStructBinding::FInstancedStructBinding(TPropertySpecifier<2>& Spec)
	: MemberIds{ GUE.Names.NameMember("ScriptStruct"), GUE.Names.NameMember("ScriptMemory")}
{
	Spec.Members[0] = GUE.Structs.ObjectPtr;
	Spec.Members[1] = SpecDynamicStruct;
}

void FInstancedStructBinding::Save(FMemberBuilder& Dst, const FInstancedStruct& Src, const FInstancedStruct* Default, const FSaveContext& Ctx) const
{
	const TObjectPtr<const UScriptStruct> StructRef(Src.GetScriptStruct());
	
	// todo: remove once we have UPropertyBag support
	if (StructRef && StructRef->IsA<UPropertyBag>())
	{
		return;
	}

	Dst.AddStruct(MemberIds[0], GUE.Structs.ObjectPtr, SaveStruct(&StructRef, GUE.Structs.ObjectPtr, Ctx));

	if (!StructRef)
	{
		return;
	}

	const FBindId BindId = GUE.DynamicIds.GetStruct(StructRef);
	if (Default && Default->GetScriptStruct() == StructRef)
	{
		Dst.AddStruct(MemberIds[1], BindId, SaveStructDelta(Src.GetMemory(), FBaseline(Default->GetMemory()), BindId, Ctx));
	}
	else 
	{
		Dst.AddStruct(MemberIds[1], BindId, SaveStructDelta(Src.GetMemory(), GUE.Defaults.Get(BindId), BindId, Ctx));
	}
}

void FInstancedStructBinding::Load(FInstancedStruct& Dst, FStructLoadView Src, ECustomLoadMethod Method) const
{
	FMemberLoader It(Src);

	// todo: remove once we have UPropertyBag support
	if (!It.HasMore())
	{
		return;
	}

	TObjectPtr<UObject> StructRef;
	LoadStruct(&StructRef, It.GrabStruct());
	
	const UScriptStruct* ScriptStruct = Cast<UScriptStruct>(StructRef);
	if (!ScriptStruct)
	{
		Dst.Reset();
		return;
	}

	if (Dst.GetScriptStruct() != ScriptStruct)
	{
		Dst.InitializeAs(ScriptStruct);
	}

	LoadStruct(Dst.GetMutableMemory(), It.GrabStruct());
}

bool FInstancedStructBinding::Diff(const FInstancedStruct& A, const FInstancedStruct& B, const FBindContext& Ctx)
{
	if (A.GetScriptStruct() != B.GetScriptStruct())
	{
		return true;
	}
	else if (!A.GetScriptStruct())
	{
		return false;
	}
	// todo: remove once we have UPropertyBag support
	else if (A.GetScriptStruct()->IsA<UPropertyBag>())
	{
		return false;
	}

	const FBindId BindId = GUE.DynamicIds.GetStruct(A.GetScriptStruct());
	return DiffStructs(A.GetMemory(), B.GetMemory(), BindId, Ctx);
}

////////////////////////////////////////////////////////////////////////////////////////////////

struct FInterfaceBinding final : ICustomBinding
{
	explicit FInterfaceBinding(UClass* Class) : InterfaceClass(Class) {}

	const TObjectPtr<UClass> InterfaceClass;

	virtual void SaveCustom(FMemberBuilder& Dst, const void* Src, FBaseline Base, const FSaveContext& Ctx) override
	{
		const void* Default = Base.Get();
		if (!Default || DiffCustom(Src, Default, Ctx))
		{
			Save(Dst, *static_cast<const FScriptInterface*>(Src), Ctx);
		}
	}

	virtual void LoadCustom(void* Dst, FStructLoadView Src, ECustomLoadMethod Method) const override
	{
		check(Method == ECustomLoadMethod::Assign);
		Load(*static_cast<FScriptInterface*>(Dst), Src);
	}

	virtual bool DiffCustom(const void* A, const void* B, const FBindContext&) const override
	{
		return *static_cast<const FScriptInterface*>(A) != *static_cast<const FScriptInterface*>(B);
	}
	
	void Save(FMemberBuilder& Dst, const FScriptInterface& Src, const FSaveContext& Ctx) const
	{
		const TObjectPtr<UObject>& ObjectRef = const_cast<FScriptInterface&>(Src).GetObjectRef();
		Dst.AddStruct(GUE.Members.Object, GUE.Structs.ObjectPtr, SaveStruct(&ObjectRef, GUE.Structs.ObjectPtr, Ctx));
	}

	void Load(FScriptInterface& Dst, FStructLoadView Src) const
	{
		LoadSoleStruct(&Dst.GetObjectRef(), Src);
		UObject* Object = Dst.GetObject();
		Dst.SetInterface(Object ? Object->GetInterfaceAddress(InterfaceClass) : nullptr);
	}
};

class FInterfaceBindings
{
	const FType ScriptInterface;
	FStructDeclarationPtr Declaration;

	TMap<FType, FBindId> BoundClasses;

public:
	FInterfaceBindings()
	: ScriptInterface(GUE.Names.Resolve(GUE.Structs.ScriptInterface))
	, Declaration(Declare({GUE.Structs.ScriptInterface, NoId, 0, EMemberPresence::RequireAll, {GUE.Members.Object}, {FMemberSpec(GUE.Structs.ObjectPtr)}}))
	{}

	FBindId Bind(FInterfaceProperty* Property)
	{
		FType Class = IndexType(Property->InterfaceClass);
		if (const FBindId* BindId = BoundClasses.Find(Class))
		{
			return *BindId;
		}
		
		FType UniqueBindName = GUE.Names.MakeParametricType(ScriptInterface, {Class});
		FBindId BindId = GUE.Names.IndexBindId(UniqueBindName);
		BoundClasses.Emplace(Class, BindId);

		// Todo: Ownership / memory leak
		ICustomBinding* Leak = new FInterfaceBinding(Property->InterfaceClass);
		GUE.Customs.BindStruct(BindId, *Leak, FStructDeclarationPtr(Declaration), {});

		return BindId;
	}
};

static FBindId BindInterface(FInterfaceProperty* Property)
{
	static FInterfaceBindings Bindings;
	return Bindings.Bind(Property);
}

////////////////////////////////////////////////////////////////////////////////////////////////

inline uint32 HashRangeBindings(TConstArrayView<FRangeBinding> In)
{
	return static_cast<uint32>(FXxHash64::HashBuffer(In.GetData(), In.NumBytes()).Hash);
}

inline uint32 HashSkipOffset(FMemberBinding In)
{
	uint32 Out = HashCombineFast(GetTypeHash(In.InnermostId), GetTypeHash(In.InnermostType));
	return In.RangeBindings.IsEmpty() ? Out : HashCombineFast(Out, HashRangeBindings(In.RangeBindings));
}

inline bool EqSkipOffset(FMemberBinding A, FMemberBinding B)
{
	return A.InnermostType == B.InnermostType && A.InnermostId == B.InnermostId && Algo::Compare(A.RangeBindings, B.RangeBindings);
}

// Helper to cache various property bindings instead of a TMap KeyFunc
struct FParameterBinding : FMemberBinding
{
	friend uint32 GetTypeHash(FParameterBinding In) { return HashSkipOffset(In); };
	inline bool operator==(FParameterBinding O) const { return EqSkipOffset(*this, O); }
};

////////////////////////////////////////////////////////////////////////////////////////////////

template<const std::string_view& Suffix>
static bool EndsWithDelimitedSuffix(FName EnumName, FName ValueName)
{
	if (ValueName.GetNumber() != NAME_NO_NUMBER_INTERNAL)
	{
		return false;
	}

	// All type names and enum constants are ASCII
	ANSICHAR Buffer[NAME_SIZE];
	ValueName.GetComparisonNameEntry()->GetAnsiName(Buffer);
	FAnsiStringView Value(Buffer);
	if (Value.Len() >= Suffix.size() + 2 && Value.EndsWith(ToAnsiView(Suffix)))
	{
		// Todo: Check EnumName too, maybe based on ECppForm
		char Delimiter = Value[Value.Len() - Suffix.size() - 1];
		return Delimiter == ':' || Delimiter == '_';	
	}
	return false;
}

static bool DenyMaxValue(FName Enum)
{
	static const FName AllowsMax[] = {
		"ESlateBrushMirrorType", 
		"EFortFeedbackAddressee",
		"ECameraFocusMethod" };
	return !Algo::Find(AllowsMax, Enum);
}

static bool FitsInWidth(int64 Value, ELeafWidth Width)
{
	const uint32 Bits = 8u << (uint32)Width;
	const uint32 Shift = 64 - Bits;
	// The upper bits beyond the width are expected to be all zero (unsigned)
	// or the Value should survive a truncation + sign-extension roundtrip
	return (Value >> Bits) == 0 || Value == (static_cast<int64>(static_cast<uint64>(Value) << Shift) >> Shift);
}

static FEnumId DeclareEnum(UEnum* Enum)
{
	FType Type = IndexType(Enum);
	FEnumId Id = GUE.Names.IndexEnum(Type);
	if (GUE.Enums.Find(Id))
	{
		return Id;
	}

	EEnumMode Mode = Enum->HasAnyEnumFlags(EEnumFlags::Flags) ? EEnumMode::Flag : EEnumMode::Flat;

	// Skip _MAX and _All enumerators
	FName EnumName = Enum->GetFName();
	int32 Num = Enum->NumEnums();
	if (Num > 0 && DenyMaxValue(EnumName))
	{
		static constexpr std::string_view Max = "MAX";
		static constexpr std::string_view All = "All";
		Num -= EndsWithDelimitedSuffix<Max>(EnumName, Enum->GetNameByIndex(Num - 1));
		Num -= Num > 0 && Mode == EEnumMode::Flag && 
				EndsWithDelimitedSuffix<All>(EnumName, Enum->GetNameByIndex(Num - 1));
	}

	const ELeafWidth Width = WidthOfEnum(Enum);
	const uint64 ValueMasks[4] = { uint8(~0), uint16(~0), uint32(~0), ~uint64(0) };
	const uint64 ValueMask = ValueMasks[(uint8)Width];
	TArray<FEnumerator, TInlineAllocator<64>> Enumerators;
	for (int32 Idx = 0; Idx < Num; ++Idx)
	{
		FName ValueName = Enum->GetNameByIndex(Idx);
		int64 Value = Enum->GetValueByIndex(Idx);
		Enumerators.Emplace(GUE.Names.MakeName(ValueName), static_cast<uint64>(Value) & ValueMask);
		checkf(FitsInWidth(Value, Width), TEXT("Enumerator constant %s=%llx doesn't fit in declared width"),
			*ValueName.ToString(), Value);
	}

#if WITH_METADATA
	// IsMax() classifies more names as "max" than Enum->ContainsExistingMax()
	checkSlow(Enumerators.Num() == Num || Enum->ContainsExistingMax() || Enum->HasMetaData(TEXT("Hidden"), Num));
#endif

	GUE.Enums.Declare(Id, Type, Mode, Width, Enumerators, EEnumAliases::Strip);
	GUE.DynamicIds.AddEnum(Enum, Id);
	return Id;
}

////////////////////////////////////////////////////////////////////////////////////////////////

inline uint64 NumBytes(int32 NumItems, SIZE_T ItemSize)
{
	return static_cast<uint64>(NumItems) * ItemSize;
}

inline bool HasConstructor(const FProperty* Property)
{
	return !(Property->PropertyFlags & CPF_ZeroConstructor);
}

inline bool HasDestructor(const FProperty* Property)
{
	return !(Property->PropertyFlags & (CPF_IsPlainOldData | CPF_NoDestructor));
}

inline bool HasHash(const FProperty* Property)
{
	return !!(Property->PropertyFlags & CPF_HasGetValueTypeHash);
}

inline void ConstructValue(const FProperty* Property, void* Value)
{
	((*Property).*GInitPropertyValue)(Value);
}

inline void DestroyValue(const FProperty* Property, void* Value)
{
	((*Property).*GDestroyPropertyValue)(Value);
}

inline uint32 HashValue(const FProperty* Property, const void* Item)
{
	return ((*Property).*GHashPropertyValue)(Item);
}

inline void ConstructValues(const FProperty* Property, uint8* Values, int32 Num, SIZE_T Stride)
{
	for (uint8* It = Values, *End = Values + Num*Stride; It != End; It += Stride)
	{
		((*Property).*GInitPropertyValue)(It);
	}
}

inline void MemzeroStrided(uint8* Values, int32 Num, SIZE_T Size, SIZE_T Stride)
{
	for (uint8* It = Values, *End = Values + Num*Stride; It != End; It += Stride)
	{
		FMemory::Memzero(It, Size);
	}
}

inline void DestroyValues(const FProperty* Property, uint8* Values, int32 Num, SIZE_T Stride)
{
	for (uint8* It = Values, *End = Values + Num*Stride; It != End; It += Stride)
	{
		((*Property).*GDestroyPropertyValue)(It);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////

// Helps cache array property range bindings
struct FArrayPropertyInfo
{
	explicit FArrayPropertyInfo(FArrayProperty* Property)
	: bFreezable(!!(Property->ArrayFlags & EArrayPropertyFlags::UsesMemoryImageAllocator))
	, bDestructor(HasDestructor(Property->Inner))
	, bConstructor(HasConstructor(Property->Inner))
	, ItemAlign(Property->Inner->GetMinAlignment())
	, ItemSize(Property->Inner->GetElementSize())
	{}

	union
	{
		struct
		{
			uint32	bFreezable : 1;
			uint32	bDestructor : 1;	
			uint32	bConstructor : 1;
			uint32	ItemAlign : 29;
		};
		uint32		Int;
	};
	uint32			ItemSize;

	bool			IsTrivial() const						{ return !bDestructor && !bConstructor; }
	bool			operator==(FArrayPropertyInfo O) const	{ return Int == O.Int && ItemSize == O.ItemSize; }
	friend uint32	GetTypeHash(FArrayPropertyInfo I)		{ return HashCombineFast(I.Int, I.ItemSize); };
};
static_assert(sizeof(FArrayPropertyInfo) == 8);

// Cacheable FArrayProperty binding
template<class ScriptArray>
struct TTrivialArrayBinding : IItemRangeBinding
{
	const FArrayPropertyInfo Info;

	TTrivialArrayBinding(FArrayPropertyInfo InFo, FConcreteTypenameId BindName = GUE.Typenames.TrivialArray)
	: IItemRangeBinding(BindName)
	, Info(InFo) {}

	virtual void ReadItems(FSaveRangeContext& Ctx) const override
	{
		const ScriptArray& Array = Ctx.Request.GetRange<ScriptArray>();
		Ctx.Items.SetAll(Array.GetData(), static_cast<uint64>(Array.Num()), Info.ItemSize);
	}

	virtual void MakeItems(FLoadRangeContext& Ctx) const override
	{
		ScriptArray& Array = Ctx.Request.GetRange<ScriptArray>();

		int32 NewNum = static_cast<int32>(Ctx.Request.NumTotal());
		Array.SetNumUninitialized(NewNum, Info.ItemSize, Info.ItemAlign);
		if (NewNum)
		{
			FMemory::Memzero(Array.GetData(), NumBytes(NewNum, Info.ItemSize));
		}
		
		Ctx.Items.Set(Array.GetData(), static_cast<uint64>(NewNum), Info.ItemSize);
	}
};

// Currently can't extract constructor/destructor function pointers from FProperty, which
// requires keeping FProperty* and prevents range binding reuse, @see AllocateArrayBinding()
template<class ScriptArray>
struct TNonTrivialArrayBinding : TTrivialArrayBinding<ScriptArray>
{
	const FProperty* Inner;

	using TTrivialArrayBinding<ScriptArray>::Info;

	TNonTrivialArrayBinding(FArrayPropertyInfo InFo, const FProperty* InNer)
	: TTrivialArrayBinding<ScriptArray>(InFo, GUE.Typenames.NonTrivialArray)
	, Inner(InNer)
	{}

	virtual void MakeItems(FLoadRangeContext& Ctx) const override
	{
		ScriptArray& Array = Ctx.Request.GetRange<ScriptArray>();

		int32 NumDestroy = Info.bDestructor * Array.Num();
		DestroyValues(Inner, static_cast<uint8*>(Array.GetData()), NumDestroy, Info.ItemSize);

		uint64 NewNum = Ctx.Request.NumTotal();
		Array.SetNumUninitialized(static_cast<int32>(NewNum), Info.ItemSize, Info.ItemAlign);
		InitItems(NewNum, Array.GetData());
		
		Ctx.Items.Set(Array.GetData(), NewNum, Info.ItemSize);
	}

	inline void InitItems(uint64 Num, void* Items) const
	{
		if (Info.bConstructor)
		{
			ConstructValues(Inner, static_cast<uint8*>(Items), Num, Info.ItemSize);
		}
		else if (Num)
		{
			FMemory::Memzero(Items, Num * Info.ItemSize);
		}
	}
};

template<ELeafType Type, ELeafWidth Width>
struct TLeafArrayBinding : ILeafRangeBinding
{
	inline static constexpr SIZE_T LeafSize = SizeOf(Width);
	TLeafArrayBinding() : ILeafRangeBinding(GUE.Typenames.LeafArray) {}

	virtual void SaveLeaves(const void* Range, FLeafRangeAllocator& Out) const override
	{
		const FScriptArray& Array = *static_cast<const FScriptArray*>(Range);
		if (int32 Num = Array.Num())
		{
			void* Dst = Out.AllocateNonEmptyRange(Num, Width);
			FMemory::Memcpy(Dst, Array.GetData(), NumBytes(Num));
		}
	}

	virtual void LoadLeaves(void* Range, FLeafRangeLoadView Leaves) const override
	{
		FScriptArray& Array = *static_cast<FScriptArray*>(Range);
		Array.SetNumUninitialized(static_cast<int32>(Leaves.Num()), LeafSize, LeafSize);
		Leaves.AsBitCast<Type, Width>().Copy(Array.GetData(), NumBytes(Array.Num()));
	}

	virtual bool DiffLeaves(const void* RangeA, const void* RangeB) const override
	{
		const FScriptArray& A = *static_cast<const FScriptArray*>(RangeA);
		const FScriptArray& B = *static_cast<const FScriptArray*>(RangeB);
		return Diff(A.Num(), B.Num(), A.GetData(), B.GetData(), LeafSize);
	}

	inline constexpr uint64 NumBytes(int32 NumItems) const
	{
		return static_cast<uint64>(NumItems) * LeafSize;
	}
};

struct FOverridableArrayPropertyBinding;
inline FOverridableArrayPropertyBinding* CreateAndLeakOverridableArrayBinding(FArrayProperty* ArrayProperty, FMemberBinding InnerBinding, FBindId BindId, FDeclId DeclId);

// Reusable cache of FArrayProperty range bindings
class FArrayPropertyBindings
{
	static constexpr ESizeType SizeType = DefaultRangeMax;
	
	const TLeafArrayBinding<ELeafType::Bool, ELeafWidth::B8>	Bool;
	const TLeafArrayBinding<ELeafType::Float, ELeafWidth::B32>	Float;
	const TLeafArrayBinding<ELeafType::Float, ELeafWidth::B64>	Double;
	const TLeafArrayBinding<ELeafType::IntS, ELeafWidth::B8>	IntS8;
	const TLeafArrayBinding<ELeafType::IntS, ELeafWidth::B16>	IntS16;
	const TLeafArrayBinding<ELeafType::IntS, ELeafWidth::B32>	IntS32;
	const TLeafArrayBinding<ELeafType::IntS, ELeafWidth::B64>	IntS64;
	const TLeafArrayBinding<ELeafType::IntU, ELeafWidth::B8>	IntU8;
	const TLeafArrayBinding<ELeafType::IntU, ELeafWidth::B16>	IntU16;
	const TLeafArrayBinding<ELeafType::IntU, ELeafWidth::B32>	IntU32;
	const TLeafArrayBinding<ELeafType::IntU, ELeafWidth::B64>	IntU64;
	const FRangeBinding											Integers[2][4];

	TMap<FArrayPropertyInfo, IItemRangeBinding*>				Others;
	TMap<FParameterBinding, FBindId>							OverridableBindings;

	inline const ILeafRangeBinding& DownCast(const ILeafRangeBinding& In) { return In; }

public:
	FArrayPropertyBindings()
	: Integers{{FRangeBinding(IntU8, SizeType),	FRangeBinding(IntU16, SizeType), FRangeBinding(IntU32, SizeType), FRangeBinding(IntU64, SizeType)},
			   {FRangeBinding(IntS8, SizeType),	FRangeBinding(IntS16, SizeType), FRangeBinding(IntS32, SizeType), FRangeBinding(IntS64, SizeType)}}
	{}

	~FArrayPropertyBindings()
	{
		for (TPair<FArrayPropertyInfo, IItemRangeBinding*>& Cached : Others)
		{
			FMemory::Free(Cached.Value);
		}
	}

	FRangeBinding RangeBind(FArrayPropertyInfo Info, uint64 InnerCastFlags)
	{
		if (HasAny<LeafMask>(InnerCastFlags) && !Info.bFreezable)
		{
			uint32 SizeIdx = FMath::FloorLog2NonZero(Info.ItemSize);
			check(SizeIdx < 4);

			// Note that we throw away enum schema, only size not needed to load/save enums
			if (HasAny<IntSMask | IntUMask | CASTCLASS_FEnumProperty>(InnerCastFlags))
			{
				return Integers[HasAny<IntSMask>(InnerCastFlags)][SizeIdx];
			}
			
			check(HasAny<CASTCLASS_FFloatProperty | CASTCLASS_FDoubleProperty | CASTCLASS_FBoolProperty >(InnerCastFlags));
			const ILeafRangeBinding& Binding = HasAny<CASTCLASS_FBoolProperty>(InnerCastFlags) 
				? DownCast(Bool) : (HasAny<CASTCLASS_FFloatProperty>(InnerCastFlags) ? DownCast(Float) : Double);
			return FRangeBinding(Binding, SizeType);
		}
		else if (IItemRangeBinding** Cached = Others.Find(Info))
		{
			return FRangeBinding(**Cached, SizeType);
		}
		
		IItemRangeBinding* New = Info.bFreezable	? CreateAndCache<FFreezableScriptArray>(Info)
													: CreateAndCache<FScriptArray>(Info);
		return FRangeBinding(*New, SizeType);
	}

	FBindId BindOverridableArray(FArrayProperty* Property, FMemberBinding InnerBinding)
	{
		FProperty* Inner = Property->Inner;
		FArrayPropertyInfo Info(Property);

		check(Info.IsTrivial());

		if (const FBindId* BindId = OverridableBindings.Find(FParameterBinding(InnerBinding)))
		{
			return *BindId;
		}

		FBothType Param = InnerBinding.IndexParameterName(GUE.Names);
		FType BindType = FType{ GUE.Scopes.CoreUObject, FTypenameId(GUE.Names.MakeParametricTypeId(GUE.Typenames.TrivialArray, MakeArrayView(&Param.BindType, 1))) };
		FType DeclType = Param.IsLowered()
			? FType{ GUE.Scopes.CoreUObject, FTypenameId(GUE.Names.MakeParametricTypeId(GUE.Typenames.TrivialArray, MakeArrayView(&Param.DeclType, 1))) }
		: BindType;
		FBindId BindId = GUE.Names.IndexBindId(BindType);
		FDeclId DeclId = Param.IsLowered() ? GUE.Names.IndexDeclId(DeclType) : LowerCast(BindId);

		CreateAndLeakOverridableArrayBinding(Property, InnerBinding, BindId, DeclId);

		return OverridableBindings.Emplace(InnerBinding, BindId);
	}

	template<typename ScriptArray>
	IItemRangeBinding* CreateAndCache(FArrayPropertyInfo Info)
	{
		static_assert(std::is_trivially_destructible_v<TTrivialArrayBinding<ScriptArray>>);
		return Others.Emplace(Info, new TTrivialArrayBinding<ScriptArray>(Info));
	}
};
	
static FArrayPropertyBindings GCachedArrayBindings;

template<typename ScriptArray>
IItemRangeBinding* CreateAndLeak(FArrayPropertyInfo Info, FProperty* Inner)
{
	return new TNonTrivialArrayBinding<ScriptArray>(Info, Inner);
}

static FRangeBinding AllocateArrayBinding(FArrayProperty* Property)
{
	FProperty* Inner = Property->Inner;
	FArrayPropertyInfo Info(Property);
	if (Info.IsTrivial())
	{
		return GCachedArrayBindings.RangeBind(Info, Inner->GetCastFlags());
	}

	// Todo: Ownership / memory leak, try make non-trivial case cacheable by making FProperty ctor/dtor extractable
	IItemRangeBinding* Out = Info.bFreezable	? CreateAndLeak<FFreezableScriptArray>(Info, Inner)
												: CreateAndLeak<FScriptArray>(Info, Inner);
	return FRangeBinding(*Out, ESizeType::S32);
}

class FRangeBindingAllocator
{
public:
	TConstArrayView<FRangeBinding> Clone(FRangeBinding Head, TConstArrayView<FRangeBinding> Tail)
	{
		if (Tail.Num() == 0)
		{
			return MakeArrayView(&Ranges.Add_GetRef(Head), 1);
		}
		
		// Ensure contiguous out range by padding up with dummy tail slice
		static constexpr uint32 PageMax = decltype(Ranges)::MaxPerPage();
		const int32 OutNum = 1 + Tail.Num();
		check(OutNum <= PageMax);
		const int32 NewPages = Align(Ranges.Num() + OutNum, PageMax) / PageMax;
		if (NewPages > Ranges.NumPages() && !Ranges.IsEmpty())
		{
			int32 NumPad = Align(Ranges.Num(), PageMax) - Ranges.Num();
			Ranges.Append(Tail.Slice(0, NumPad));
			check(Ranges.Num() % PageMax == 0);
		}

		const FRangeBinding* OutData = &Ranges.Add_GetRef(Head);
		Ranges.Append(Tail);
		return MakeArrayView(OutData, OutNum);
	}

	TConstArrayView<FRangeBinding> Clone(TConstArrayView<FRangeBinding> Src)
	{
		if (Src.Num())
		{
			return Clone(Src[0], Src.RightChop(1));
		}
		return {};
	}

	void Reset()
	{
		Ranges.Reset();
	}

	void Empty()
	{
		Ranges.Empty();
	}

private:
	TPagedArray<FRangeBinding, 1024> Ranges;
};

static FRangeBindingAllocator GScratchRanges;
static FRangeBindingAllocator GRanges;

////////////////////////////////////////////////////////////////////////////////////////////////

// Helpers to avoid using leaf FProperty instances after binding
//
// Below must match FFloatProperty, FDoubleProperty, FBoolProperty, FEnumProperty, TNumericProperty
// Identical() and GetValueTypeHashInternal() implementations perfectly except not supporting nullptrs
inline uint32	LeafPropertyHash(float In)											{ return ::UE::PreciseFPHash(In); }
inline uint32	LeafPropertyHash(double In)											{ return ::UE::PreciseFPHash(In); }
inline bool		LeafPropertyIdentical(float A, float B)								{ return ::UE::PreciseFPEqual(A, B); }
inline bool		LeafPropertyIdentical(double A, double B)							{ return ::UE::PreciseFPEqual(A, B); }
template<typename T>
inline uint32	LeafPropertyHash(T In) requires (std::is_unsigned_v<T>)				{ return ::GetTypeHash(In); }
template<typename T>
inline bool		LeafPropertyIdentical(T A, T B) requires (std::is_unsigned_v<T>)	{ return A == B; }

// Type-erased just enough to call LeafPropertyHash / LeafPropertyIdentical and FLeafRangeLoadView::As/AsBitcast
enum class EPropertyKind : uint8 { Range, Struct, Bool, U8, U16, U32, U64, F32, F64 };

template<EPropertyKind Kind>
struct TEquivalentLeafType									{ using Type = void; };
template<> struct TEquivalentLeafType<EPropertyKind::Bool>	{ using Type = bool; };
template<> struct TEquivalentLeafType<EPropertyKind::U8>	{ using Type = uint8; };
template<> struct TEquivalentLeafType<EPropertyKind::U16>	{ using Type = uint16; };
template<> struct TEquivalentLeafType<EPropertyKind::U32>	{ using Type = uint32; };
template<> struct TEquivalentLeafType<EPropertyKind::U64>	{ using Type = uint64; };
template<> struct TEquivalentLeafType<EPropertyKind::F32>	{ using Type = float; };
template<> struct TEquivalentLeafType<EPropertyKind::F64>	{ using Type = double; };

template<EPropertyKind Kind>
using EquivalentLeafType = typename TEquivalentLeafType<Kind>::Type;

template<typename LeafType>
uint32 LeafHash(const void* In)
{
	return LeafPropertyHash(*static_cast<const LeafType*>(In));
}

template<typename LeafType>
bool LeafIdentical(const void* A, const void* B)
{
	return LeafPropertyIdentical(*static_cast<const LeafType*>(A), *static_cast<const LeafType*>(B));
}

template<typename LeafType>
inline auto CastAs(FLeafRangeLoadView In)
{
	if constexpr (std::is_floating_point_v<LeafType>)
	{
		return In.As<LeafType>();
	}
	else
	{
		return In.AsBitCast<LeafType>();
	}
}

inline EPropertyKind GetPropertyKind(FLeafBindType In)
{
	ELeafType Type = ToLeafType(In.Bind.Type);
	ELeafWidth Width = In.Basic.Width;

	if (Type == ELeafType::Float)
	{
		return Width == ELeafWidth::B32 ? EPropertyKind::F32 : EPropertyKind::F64;
	}
	else if (Type == ELeafType::Bool)
	{
		return EPropertyKind::Bool;
	}
	else switch (Width)
	{
		case ELeafWidth::B8:	return EPropertyKind::U8;
		case ELeafWidth::B16:	return EPropertyKind::U16;
		case ELeafWidth::B32:	return EPropertyKind::U32;
		default:				return EPropertyKind::U64;
	}								
}

static EPropertyKind GetPropertyKind(FMemberBinding In)
{
	return (In.RangeBindings.Num() > 0) ? EPropertyKind::Range
										: In.InnermostType.IsStruct()
										? EPropertyKind::Struct
										: GetPropertyKind(In.InnermostType.AsLeaf());
}

////////////////////////////////////////////////////////////////////////////////////////////////

// Inner leaf property, e.g. FEnumProperty, FNumericProperty
template<Arithmetic EquivalentType>
struct TInnerLeafProperty
{
	static constexpr EMemberKind	Kind = EMemberKind::Leaf;
	static constexpr bool			bConstruct = false;
	static constexpr bool			bDestruct = false;
	static constexpr bool			bHashable = true;
	static constexpr int32			Size = sizeof(EquivalentType);

	TInnerLeafProperty(FProperty* In) { check(Size == In->GetElementSize());}

	inline static void				InitItem(void* In)						{} // Note doesn't zero out items about to be overwritten
	inline static void				DestroyItem(void* In)					{}
	inline static EquivalentType	Cast(const void* In)					{ return *static_cast<const EquivalentType*>(In); } // Todo: Consider if BitCast is needed
	inline static uint32			Hash(const void* In)					{ return LeafPropertyHash(Cast(In)); }
	inline static bool				Identical(const void* A, const void* B) { return LeafPropertyIdentical(Cast(A), Cast(B)); }
};

// Inner range-bound property, e.g. FArrayProperty, FStringProperty, FSetProperty
struct FInnerRangeProperty
{
	static constexpr EMemberKind	Kind = EMemberKind::Range;
	static constexpr bool			bConstruct = false;
	static constexpr bool			bDestruct = true;

	FProperty*						Property;
	uint32							Size;
	bool							bHashable;

	FInnerRangeProperty(FProperty* In)
	: Property(In)
	, Size(In->GetElementSize())
	, bHashable(HasHash(In))
	{
		check(!HasConstructor(In));
		check(HasDestructor(In));
	}

	inline void						InitItem(void* In) const				{ FMemory::Memzero(In, Size); }
	inline void						DestroyItem(void* In) const				{ DestroyValue(Property, In); }
};

// Inner struct-bound property, e.g. FStructProperty, FNameProperty, FObjectProperty
struct FInnerStructProperty
{
	static constexpr EMemberKind	Kind = EMemberKind::Struct;

	FProperty*						Property;
	uint32							Size;
	bool							bConstruct;
	bool							bDestruct;
	bool							bHashable;

	FInnerStructProperty(FProperty* In)
	: Property(In)
	, Size(In->GetElementSize())
	, bConstruct(HasConstructor(In))
	, bDestruct(HasDestructor(In))
	, bHashable(HasHash(In))
	{}

	inline void						InitItem(void* Item) const
	{ 
		if (bConstruct)
		{
			ConstructValue(Property, Item);
		}
		else
		{
			FMemory::Memzero(Item, Size);
		}
	}

	inline void						DestroyItem(void* Item) const
	{
		if (bDestruct)
		{
			DestroyValue(Property, Item);
		}
	}
};

template<EPropertyKind Kind> struct TSelectInnerProperty		{ using Type = TInnerLeafProperty<EquivalentLeafType<Kind>>; };
template<> struct TSelectInnerProperty<EPropertyKind::Range>	{ using Type = FInnerRangeProperty; };
template<> struct TSelectInnerProperty<EPropertyKind::Struct>	{ using Type = FInnerStructProperty; };

template<EPropertyKind Kind>
using TInnerProperty = typename TSelectInnerProperty<Kind>::Type;

template<typename InnerPropertyType>
inline auto	MakeHashFn(const InnerPropertyType& Inner)
{
	if constexpr (InnerPropertyType::Kind == EMemberKind::Leaf)
	{
		return &InnerPropertyType::Hash;
	}
	else
	{
		return [P = Inner.Property](const void* In) { return HashValue(P, In); };
	}
}

template<typename InnerPropertyType>
inline auto	MakeIdenticalFn(const InnerPropertyType& Inner)
{
	if constexpr (InnerPropertyType::Kind == EMemberKind::Leaf)
	{
		return &InnerPropertyType::Identical;
	}
	else
	{
		return [P = Inner.Property](const void* A, const void* B) { return P->Identical(A, B); };
	}
}

template<typename InnerPropertyType>
inline void InitStridedItems(const InnerPropertyType& Inner, void* Items, uint64 Num, SIZE_T Stride)
{
	if constexpr (InnerPropertyType::Kind == EMemberKind::Leaf)
	{}
	else if (Inner.bConstruct)
	{
		ConstructValues(Inner.Property, static_cast<uint8*>(Items), Num, Stride);
	}
	else
	{
		MemzeroStrided(static_cast<uint8*>(Items), Num, Inner.Size, Stride);
	}
}

template<typename InnerPropertyType>
inline void DestroyStridedItems(const InnerPropertyType& Inner, void* Items, uint64 Num, SIZE_T Stride)
{
	if constexpr (InnerPropertyType::Kind == EMemberKind::Leaf)
	{}
	else if (Inner.bDestruct)
	{
		DestroyValues(Inner.Property, static_cast<uint8*>(Items), Num, Stride);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////

template<typename EquivalentType>
struct TLeafRangeSerializer
{
	using RangeSaver = TLeafRangeSaver<EquivalentType>;
	static constexpr SIZE_T	Size = sizeof(EquivalentType);

	FMemberType				InnerType;
	FOptionalInnerId		EnumId;

	explicit TLeafRangeSerializer(FMemberBinding In)
	: InnerType(ToLeafType(In.InnermostType.AsLeaf()))
	, EnumId(In.InnermostId)
	{
		check(In.RangeBindings.IsEmpty());
		check(InnerType.AsLeaf().Width == WidthOf(Size));
	}

	inline static EquivalentType Cast(const void* In)
	{
		return *static_cast<const EquivalentType*>(In);
	}

	inline FMemberSchema MakeMemberSchema() const
	{
		return { FMemberType(DefaultRangeMax), InnerType, 1, EnumId, nullptr };
	}

	inline FMemberSpec SpecMember() const
	{
		return DefaultRangeOf(FMemberSpec(InnerType, FOptionalInnerId(EnumId)));
	}

	inline static EquivalentType SaveItem(const void* In, const FSaveContext&)
	{
		return Cast(In);
	}

	inline static void LoadItem(void* Dst, FByteReader& SrcBytes, FBitCacheReader& SrcBits, FOptionalSchemaId, const FLoadBatch&) requires (std::is_same_v<EquivalentType, bool>)
	{
		*static_cast<bool*>(Dst) = SrcBits.GrabNext(SrcBytes);
	}

	inline static void LoadItem(void* Dst, FByteReader& SrcBytes, FBitCacheReader&, FOptionalSchemaId, const FLoadBatch&)
	{
		*static_cast<EquivalentType*>(Dst) = Cast(SrcBytes.GrabBytes(Size));
	}
};

struct FStructRangeSerializer
{
	using RangeSaver = FStructRangeSaver;
	
	FMemberType				InnerType;
	FBindId					SaveId;

	explicit FStructRangeSerializer(FMemberBinding Item)
	: InnerType(Item.InnermostType.AsStruct())
	, SaveId(Item.InnermostId.Get().AsStruct())
	{
		check(Item.RangeBindings.IsEmpty());
	}

	inline FMemberSchema MakeMemberSchema() const
	{
		return { FMemberType(DefaultRangeMax), InnerType, 1, FInnerId(SaveId), nullptr };
	}

	inline FMemberSpec SpecMember() const
	{
		// Lowercast is safe since FProperty doesnt support type-erasure
		return DefaultRangeOf(LowerCast(SaveId));
	}

	inline FBuiltStruct* SaveItem(const void* In, const FSaveContext& Ctx) const
	{
		return SaveStruct(In, SaveId, Ctx);
	}

	void LoadItem(void* Dst, FByteReader& SrcBytes, FBitCacheReader&, FOptionalSchemaId LoadId, const FLoadBatch& Batch) const
	{
		LoadStruct(Dst, FByteReader(SrcBytes.GrabSkippableSlice()), static_cast<FStructSchemaId>(LoadId.Get()), Batch);
	}
};

struct FNestedRangeSerializer
{
	using RangeSaver = FNestedRangeSaver;

	FOptionalInnerId								InnermostSaveId;
	uint16											NumInners;
	TArray<FMemberType, TInlineAllocator<8>>		InnerTypes;
	TArray<FMemberBindType, TInlineAllocator<8>>	InnerBindTypes;
	TArray<FRangeBinding, TInlineAllocator<2>>		InnerBindings;

	explicit FNestedRangeSerializer(FMemberBinding Item)
	: InnermostSaveId(Item.InnermostId)
	, NumInners(IntCastChecked<uint16>(1 + Item.RangeBindings.Num()))
	, InnerBindings(Item.RangeBindings)
	{
		check(NumInners >= 2);
		for (FRangeBinding Inner : Item.RangeBindings)
		{
			InnerTypes.Emplace(Inner.GetSizeType());
			InnerBindTypes.Emplace(Inner.GetSizeType());
		}
		InnerTypes.Emplace(Item.InnermostType.IsStruct() ? FMemberType(Item.InnermostType.AsStruct()) 
														 : FMemberType(ToLeafType(Item.InnermostType.AsLeaf())));
		InnerBindTypes.Emplace(Item.InnermostType);
	}

	inline FMemberSchema MakeMemberSchema() const
	{
		return { FMemberType(DefaultRangeMax), InnerTypes[0], NumInners, InnermostSaveId, InnerTypes.GetData() };
	}

	inline FMemberSpec SpecMember() const
	{
		// InnermostSaveId is safe since FProperty doesnt support type-erasure
		return DefaultRangeOf(FMemberSpec(InnerTypes, InnermostSaveId));
	}

	FBuiltRange* SaveItem(const void* In, const FSaveContext& Ctx) const
	{
		FRangeMemberBinding Member = { InnerBindTypes.GetData() + 1, InnerBindings.GetData(), NumInners - 1, InnermostSaveId, 0 };
		return SaveRange(In, Member, Ctx);
	}

	inline void LoadItem(void* Dst, FByteReader& SrcBytes, FBitCacheReader& SrcBits, FOptionalSchemaId InnermostLoadId, const FLoadBatch& Batch) const
	{
		FRangeLoadSchema Schema = { InnerTypes[1], InnermostLoadId, InnerTypes.GetData() + 2, Batch};
		LoadRange(Dst, SrcBytes, SrcBits, DefaultRangeMax, Schema, InnerBindings);
	}
};

template<EPropertyKind Kind> struct TSelectRangeSerializer		{ using Type = TLeafRangeSerializer<EquivalentLeafType<Kind>>; };
template<> struct TSelectRangeSerializer<EPropertyKind::Range>	{ using Type = FNestedRangeSerializer; };
template<> struct TSelectRangeSerializer<EPropertyKind::Struct>	{ using Type = FStructRangeSerializer; };

template<EPropertyKind Kind>
using TPropertyRangeSerializer = typename TSelectRangeSerializer<Kind>::Type;

////////////////////////////////////////////////////////////////////////////////////////////////

#if !UE_USE_COMPACT_SET_AS_DEFAULT
inline FScriptSparseArray& AsSparseArray(FScriptSet& In) { return reinterpret_cast<FScriptSparseArray&>(In); }
inline FScriptSparseArray& AsSparseArray(FScriptMap& In) { return reinterpret_cast<FScriptSparseArray&>(In); }

// @pre Elems.Num() > 0
inline FExistingItemSlice GetContiguousSlice(int32 Idx, const FScriptSparseArray& Elems, const uint8* Data, SIZE_T Stride)
{
	checkSlow(!Elems.IsEmpty());
	int32 Num = 1;
	for (;!Elems.IsValidIndex(Idx); ++Idx) { checkSlow(Idx < Elems.GetMaxIndex()); }
	for (; Elems.IsValidIndex(Idx + Num); ++Num) {}
	return { Data + NumBytes(Idx, Stride), static_cast<uint64>(Num) };
}

// Save flat TSet/TMap
inline void ReadSparseItems(FExistingItems& Dst, const FScriptSparseArray& Src, const FScriptSparseArrayLayout& Layout)
{
	const uint8* Data = static_cast<const uint8*>(Src.GetData(0, Layout));

	if (Src.IsEmpty())
	{
		Dst.SetAll(nullptr, 0, Layout.Size);
	}
	else if (FExistingItemSlice LastRead = Dst.Slice)
	{
		// Continue partial response
		int64 PriorBytesRead = static_cast<const uint8*>(LastRead.Data) - Data;
		check(PriorBytesRead % Layout.Size == 0);
		int32 LastIdx = PriorBytesRead / Layout.Size;
		int32 NextIdx = LastIdx + LastRead.Num + /* skip one known invalid */ 1;
		check(NextIdx < Src.GetMaxIndex());
		Dst.Slice = GetContiguousSlice(NextIdx, Src, Data, Layout.Size);
	}
	else if (Src.IsCompact())
	{
		Dst.SetAll(Data, static_cast<uint64>(Src.Num()), Layout.Size);
	}
	else
	{
		// Start partial response
		Dst.NumTotal = Src.Num();
		Dst.Stride = Layout.Size;
		Dst.Slice = GetContiguousSlice(0, Src, Data, Layout.Size);
	}
}
#endif

template<typename ScriptSet>
inline bool IsCompact(const ScriptSet& Set)
{
	return Set.NumUnchecked() == Set.GetMaxIndex();
}

// There's no TScriptSparseArray::SetNumUninitialized() (yet), 
// reserve using Empty() and add items one by one instead
template<class ScriptType, class LayoutType>
uint8* SetNumUninitialized(ScriptType& Dst, const LayoutType& Layout, uint64 Num)
{
	check(Dst.IsEmpty());
	Dst.Empty(static_cast<int32>(Num), Layout);
	for (uint64 Idx = 0; Idx < Num; ++Idx)
	{
		Dst.AddUninitialized(Layout);
	}
	check(IsCompact(Dst));

	return static_cast<uint8*>(Dst.GetData(0, Layout));
}

////////////////////////////////////////////////////////////////////////////////////////////////

// Helps save TSet/TMap deltas
template<class ScriptType, class LayoutType>
struct TSubSetIterator
{
	const LayoutType			Layout;
	const ScriptType&			Set;
	const TBitArray<>&			Subset;
	const int32					Max;
	int32						Idx;

	TSubSetIterator(const LayoutType& InLayout, const ScriptType& InSet, const TBitArray<>& InSubset)
	: Layout(InLayout)
	, Set(InSet)
	, Subset(InSubset)
	, Max(InSet.GetMaxIndex())
	, Idx(Max > 0 ? Subset.Find(true) : INDEX_NONE)
	{}

	explicit operator bool() const	{ return Idx != INDEX_NONE; }
	const void* operator*() const	{ return Set.GetData(Idx, Layout); }
	void operator++()				{ Idx = ++Idx < Max ? Subset.FindFrom(true, Idx) : INDEX_NONE; }
	uint32 CountNum() const			{ return Subset.CountSetBits(); }
};

// Helps save TSet/TMap deltas
template<class ScriptType, class LayoutType, class SerializerType>
FTypedRange SaveAll(const ScriptType& Set, const LayoutType& Layout, const SerializerType& Serializer, const FSaveContext& Ctx)
{
	if (Set.IsEmpty())
	{
		return { Serializer.MakeMemberSchema(), nullptr };
	}

	typename SerializerType::RangeSaver Range(Ctx.Scratch, static_cast<uint64>(Set.Num()));
	for (int32 Idx = 0, Max = Set.GetMaxIndex(); Idx < Max; ++Idx)
	{
		if (Set.IsValidIndex(Idx))
		{
			Range.AddItem(Serializer.SaveItem(Set.GetData(Idx, Layout), Ctx));
		}
	}
	return Range.Finalize(Serializer.MakeMemberSchema());
}

// Helps save TSet/TMap deltas
template<class SubSetIteratorType, class SerializerType>
FTypedRange SaveSome(SubSetIteratorType& It, const SerializerType& Serializer, const FSaveContext& Ctx)
{
	typename SerializerType::RangeSaver Range(Ctx.Scratch, It.CountNum());
	for (; It; ++It)
	{
		Range.AddItem(Serializer.SaveItem(*It, Ctx));
	}
	return Range.Finalize(Serializer.MakeMemberSchema());
}

template<typename BindingType, typename ScriptType>
void SaveSetDelta(const BindingType& Binding, FMemberBuilder& Dst, const ScriptType& Src, const ScriptType* Default, const FSaveContext& Ctx)
{
	if (!Default)
	{
		Dst.AddRange(GUE.Members.Assign, SaveAll(Src, Binding.Layout, Binding.GetItemRange(), Ctx));
	}
	else if (Default->IsEmpty())
	{
		if (!Src.IsEmpty())
		{
			Dst.AddRange(GUE.Members.Insert, SaveAll(Src, Binding.Layout, Binding.GetItemRange(), Ctx));
		}
	}
	else if (Src.IsEmpty())
	{
		Dst.AddRange(GUE.Members.Remove, SaveAll(*Default, Binding.Layout, Binding.GetKeyRange(), Ctx));
	}
	else // Neither are empty
	{
		TBitArray<> RemoveIds(false, Default->GetMaxIndex());
		for (int32 Idx = 0, Max = Default->GetMaxIndex(); Idx < Max; ++Idx)
		{
			RemoveIds[Idx] = Default->IsValidIndex(Idx) && !Binding.HasKey(Src, Default->GetData(Idx, Binding.Layout));
		}
		if (typename BindingType::SubSetIterator Removed{Binding.Layout, *Default, RemoveIds})
		{
			Dst.AddRange(GUE.Members.Remove, SaveSome(Removed, Binding.GetKeyRange(), Ctx));
		}
			
		TBitArray<> InsertIds(false, Src.GetMaxIndex());
		for (int32 Idx = 0, Max = Src.GetMaxIndex(); Idx < Max; ++Idx)
		{
			InsertIds[Idx] = Src.IsValidIndex(Idx) && !Binding.HasItem(*Default, Src.GetData(Idx, Binding.Layout));
		}
		if (typename BindingType::SubSetIterator Inserted{Binding.Layout, Src, InsertIds})
		{
			Dst.AddRange(GUE.Members.Insert, SaveSome(Inserted, Binding.GetItemRange(), Ctx));
		}
	}
}

template<typename BindingType, typename ScriptType>
void SaveSetOverrides(const BindingType& Binding, FMemberBuilder& Dst, const ScriptType& Src, const ScriptType* Default, const FSaveContext& Ctx,
					  const FOverriddenPropertySet* Overrides)
{
	if (Overrides && Default)
	{
		TBitArray<> RemoveIds(false, Default->GetMaxIndex());
		TBitArray<> InsertIds(false, Src.GetMaxIndex());
		TBitArray<> ModifyIds(false, Src.GetMaxIndex());
		bool bIsFullyReplaced = false;

		Binding.SaveOverrides(Dst, Src, *Default, *Overrides, RemoveIds, InsertIds, ModifyIds, bIsFullyReplaced);

		if (bIsFullyReplaced)
		{
			Dst.AddRange(GUE.Members.Assign, SaveAll(Src, Binding.Layout, Binding.GetItemRange(), Ctx));
		}
		else
		{
			if (typename BindingType::SubSetIterator Removed{ Binding.Layout, *Default, RemoveIds })
			{
				Dst.AddRange(GUE.Members.Remove, SaveSome(Removed, Binding.GetKeyRange(), Ctx));
			}
			if (typename BindingType::SubSetIterator Inserted{ Binding.Layout, Src, InsertIds })
			{
				Dst.AddRange(GUE.Members.Insert, SaveSome(Inserted, Binding.GetItemRange(), Ctx));
			}
			if (typename BindingType::SubSetIterator Modified{ Binding.Layout, Src, ModifyIds })
			{
				Dst.AddRange(GUE.Members.Modify, SaveSome(Modified, Binding.GetItemRange(), Ctx));
			}
		}
	}
	else
	{
		SaveSetDelta(Binding, Dst, Src, Default, Ctx);
	}
}

template<typename BindingType, typename ScriptType>
void InsertSetItems(const BindingType& Binding, ScriptType& Dst, FRangeLoadView Items)
{
	// Insert
	if (Dst.IsEmpty())
	{
		Binding.AssignEmpty(Dst, Items);
	}
	else
	{
		Binding.InsertNonEmpty(Dst, Items);
	}
}

template<typename BindingType, typename ScriptType>
void LoadSetDelta(const BindingType& Binding, ScriptType& Dst, FStructLoadView Src, FOverriddenPropertySet* Overrides = nullptr)
{
	FMemberLoader Members(Src);
	FOptionalMemberId Name = Members.PeekName();
	FRangeLoadView Range = Members.GrabRange();
	if (Name == GUE.Members.Insert)
	{
		InsertSetItems(Binding, Dst, Range);

		Binding.RestoreOverrides(Dst, Range, Overrides, EOverriddenPropertyOperation::Add);
	}
	else if (Name == GUE.Members.Assign)
	{
		Binding.DestroyAll(Dst);
		Dst.Empty(Dst.Max(), Binding.Layout);
		Binding.AssignEmpty(Dst, Range);

		Binding.RestoreOverrides(Dst, Range, Overrides, EOverriddenPropertyOperation::Replace);
	}
	else if (Name == GUE.Members.Remove)
	{
		Binding.RestoreOverrides(Dst, Range, Overrides, EOverriddenPropertyOperation::Remove);

		Binding.Remove(Dst, Range);
		if (Members.HasMore())
		{
			checkSlow(Members.PeekNameUnchecked() == GUE.Members.Insert);
			InsertSetItems(Binding, Dst, Members.GrabRange());
		}
	}
	else
	{
		checkSlow(Name == GUE.Members.Modify);

		Binding.RestoreOverrides(Dst, Range, Overrides, EOverriddenPropertyOperation::Modified);
	}

	checkSlow(!Members.HasMore());
}

template<typename BindingType, typename ScriptType>
inline bool DiffSet(const BindingType& Binding, const ScriptType& A, const ScriptType& B)
{
	if (A.NumUnchecked() != B.NumUnchecked())
	{
		return true;
	}

	if (A.NumUnchecked() > 0)
	{
		for (int32 IdxA = 0, MaxA = A.GetMaxIndex(); IdxA < MaxA; ++IdxA)
		{
			if (A.IsValidIndex(IdxA) && !Binding.HasItem(B, A.GetData(IdxA, Binding.Layout)))
			{
				return true;
			}
		}
	}

	return false;
}

// FDiffContext version of DiffSet that tries to generate extra debug info for nested differences inside TMaps
template<typename BindingType, typename ScriptType>
bool DiffMap(const BindingType& Binding, const ScriptType& A, const ScriptType& B, FDiffContext& Ctx)
{
	auto VerifyDebugDiff = [&Binding, &A, &B](bool bDebugDiff) -> bool
	{
#if DO_CHECK
		bool bActualDiff = DiffSet<BindingType, ScriptType>(Binding, A, B);
		check(bDebugDiff == bActualDiff);
		return bActualDiff;
#else
		return bDebugDiff;
#endif
	};

	if (A.NumUnchecked() != B.NumUnchecked())
	{
		return VerifyDebugDiff(true);
	}

	if (A.NumUnchecked() > 0)
	{
		for (int32 IdxA = 0, MaxA = A.GetMaxIndex(); IdxA < MaxA; ++IdxA)
		{
			if (!A.IsValidIndex(IdxA))
			{
				continue;
			}
			const void* KeyA = A.GetData(IdxA, Binding.Layout);
			int32 IdxB = Binding.FindKey(B, KeyA);
			if (IdxB == INDEX_NONE)
			{
				return VerifyDebugDiff(true);
			}

			const void* PairA = KeyA;
			const void* PairB = B.GetData(IdxB, Binding.Layout);
			FBindId ValueId = Binding.PairRange.SaveId;
			// Diff the whole Pair to try to generate extra nested diffs for the different values
			if (DiffStructs(PairA, PairB, ValueId, Ctx))
			{
				return VerifyDebugDiff(true);
			}

			// Fallback to the DiffSet version.
			// This is required since custom bindings may diff a more restricted subset of members than Identical.
			if (!Binding.HasItem(B, PairA))
			{
				return VerifyDebugDiff(true);
			}
		}
	}

	return VerifyDebugDiff(false);
}

////////////////////////////////////////////////////////////////////////////////////////////////

template<EPropertyKind ElemKind>
struct TSetPropertyBinding : IItemRangeBinding, ICustomBinding
{
	using SetType = FScriptSet;
	using LeafType = EquivalentLeafType<ElemKind>;
	using SubSetIterator = TSubSetIterator<FScriptSet, FScriptSetLayout>;
	static constexpr bool bLeaves = !std::is_void_v<LeafType>;

	const FScriptSetLayout							Layout;
	const TInnerProperty<ElemKind>					Inner;
	const TPropertyRangeSerializer<ElemKind>		Range;

	const TPropertyRangeSerializer<ElemKind>& GetKeyRange() const { return Range; }
	const TPropertyRangeSerializer<ElemKind>& GetItemRange() const { return Range; }

	TSetPropertyBinding(FSetProperty* In, FMemberBinding Elem)
	: IItemRangeBinding(GUE.Typenames.Set)
	, Layout(In->SetLayout)
	, Inner(In->ElementProp)
	, Range(Elem)
	{
		check(Layout.Size == GetStride());
		check(Inner.bHashable);
	}

	inline FStructDeclarationPtr DeclareCustom(FDeclId Id) const
	{
		FMemberId Members[] = { GUE.Members.Assign, GUE.Members.Remove, GUE.Members.Insert };
		FMemberSpec RangeSpec = Range.SpecMember();
		return Declare({Id, NoId, 0, EMemberPresence::AllowSparse, Members, {RangeSpec, RangeSpec, RangeSpec}});
	}

#if UE_USE_COMPACT_SET_AS_DEFAULT
	inline SIZE_T GetStride() const requires (bLeaves)	{ return sizeof(LeafType); }
#else
	inline SIZE_T GetStride() const requires (bLeaves)	{ return sizeof(TSparseSetElement<LeafType>); }
#endif
	inline SIZE_T GetStride() const						{ return Layout.Size; }

	inline int32 FindIndex(const FScriptSet& Set, const void* Elem) const
	{
		return Set.FindIndex(Elem, Layout, MakeHashFn(Inner), MakeIdenticalFn(Inner));
	}

#if UE_USE_COMPACT_SET_AS_DEFAULT
	inline void RemoveElem(FScriptSet& Set, const void* Elem) const
	{
		if (int32 Idx = FindIndex(Set, Elem); Idx != INDEX_NONE)
		{
			Set.RemoveAt(Idx, Layout, MakeHashFn(Inner), [this](void* Data)
				{
					Inner.DestroyItem(Data);
				});
		}
	}
#else
	inline void DestroyElem(FScriptSet& Set, int32 Idx) const
	{
		Inner.DestroyItem(Set.GetData(Idx, Layout));
	}

	inline void RemoveElem(FScriptSet& Set, const void* Elem) const
	{
		if (int32 Idx = FindIndex(Set, Elem); Idx != INDEX_NONE)
		{
			DestroyElem(Set, Idx);
			Set.RemoveAt(Idx, Layout);
		}
	}
#endif
	
	inline bool HasItem(const FScriptSet& Set, const void* Elem) const
	{
		return FindIndex(Set, Elem) != INDEX_NONE;
	}

	inline bool HasKey(const FScriptSet& Set, const void* Elem) const
	{
		return HasItem(Set, Elem);
	}
	
	inline void DestroyAll(FScriptSet& Set) const requires (bLeaves) {}
	inline void DestroyAll(FScriptSet& Set) const
	{
		uint8* It = static_cast<uint8*>(Set.GetData(0, Layout));
		SIZE_T Stride = GetStride();
		if (IsCompact(Set))
		{
			DestroyStridedItems(Inner, It, Set.NumUnchecked(), Stride);
		}
		else
		{
			for (int32 Idx = 0, Max = Set.GetMaxIndex(); Idx < Max; ++Idx)
			{
				if (Set.IsValidIndex(Idx))
				{
					DestroyValue(Inner.Property, It);
				}
				It += Stride;
			}
		}
	}

	inline void RestoreOverrides(const FScriptSet& Set, FRangeLoadView Items, FOverriddenPropertySet* Overrides, EOverriddenPropertyOperation Operation) const
	{
	}
	
	virtual void ReadItems(FSaveRangeContext& Ctx) const override
	{
#if UE_USE_COMPACT_SET_AS_DEFAULT
		const FScriptSet& Set = Ctx.Request.GetRange<FScriptSet>();
		Ctx.Items.SetAll(Set.GetData(0, Layout), Set.Num(), Layout.Size);
#else
		ReadSparseItems(/* out */ Ctx.Items, Ctx.Request.GetRange<FScriptSparseArray>(), Layout.SparseArrayLayout);
#endif
	}

	virtual void MakeItems(FLoadRangeContext& Ctx) const override
	{
		FScriptSet& Set = Ctx.Request.GetRange<FScriptSet>();
		int32 NewNum = static_cast<int32>(Ctx.Request.NumTotal());

		if (Ctx.Request.IsFirstCall())
		{
			DestroyAll(Set);
			Set.Empty(NewNum, Layout);
			if (NewNum)
			{
				uint8* Items = SetNumUninitialized(Set, Layout, NewNum);
				InitStridedItems(Inner, Items, NewNum, GetStride());
				Ctx.Items.Set(Items, Ctx.Request.NumTotal(), GetStride());
				Ctx.Items.RequestFinalCall();
			}
		}
		else
		{
			check(Ctx.Request.IsFinalCall());
			Set.Rehash(Layout, MakeHashFn(Inner));
		}
	}

	virtual void SaveCustom(FMemberBuilder& Dst, const void* Src, FBaseline Base, const FSaveContext& Ctx) override
	{
		SaveSetDelta(*this, Dst, *static_cast<const FScriptSet*>(Src), static_cast<const FScriptSet*>(Base.Get()), Ctx);
	}

	virtual void LoadCustom(void* Dst, FStructLoadView Src, ECustomLoadMethod Method) const override
	{
		check(Method == ECustomLoadMethod::Assign);
		LoadSetDelta(*this, *static_cast<FScriptSet*>(Dst), Src);
	}

	virtual bool DiffCustom(const void* A, const void* B, const FBindContext&) const override
	{
		return DiffSet(*this, *static_cast<const FScriptSet*>(A), *static_cast<const FScriptSet*>(B));
	}

	// Load into empty set
	inline void AssignEmpty(FScriptSet& Dst, FRangeLoadView Src) const
	{
		SIZE_T Stride = GetStride();
		uint8* It = SetNumUninitialized(Dst, Layout, Src.Num());
		InitStridedItems(Inner, It, Src.Num(), Stride);

		if constexpr (bLeaves)
		{
			for (LeafType Item : CastAs<LeafType>(Src.AsLeaves()))
			{
				*reinterpret_cast<LeafType*>(It) = Item;
				It += Stride;
			}
		}
		else if constexpr (ElemKind == EPropertyKind::Range)
		{
			TConstArrayView<FRangeBinding> InnerBindings = Range.InnerBindings;
			for (FRangeLoadView Item : Src.AsRanges())
			{
				LoadRange(It, Item, InnerBindings);
				It += Stride;
			}
		}
		else
		{
			for (FStructLoadView Item : Src.AsStructs())
			{
				LoadStruct(It, Item);
				It += Stride;
			}
		}

		Dst.Rehash(Layout, MakeHashFn(Inner));
	}

	// Load leaves into non-empty set
	inline void InsertNonEmpty(FScriptSet& Dst, FRangeLoadView Src) const requires(bLeaves)
	{	
		auto HashFn = MakeHashFn(Inner);

		for (LeafType Item : CastAs<LeafType>(Src.AsLeaves()))
		{
			if (!HasItem(Dst, &Item))
			{
				 void* Elem = Dst.GetData(Dst.AddUninitialized(Layout), Layout);
				 *static_cast<LeafType*>(Elem) = Item;
				 Dst.CommitLastUninitialized(Layout, HashFn);
			}
		}

		Dst.CommitAllUninitialized(Layout, HashFn);
	}

	inline void* AddItem(FScriptSet& Dst, int32& OutIdx) const
	{
		OutIdx = Dst.AddUninitialized(Layout);
		void* Out = Dst.GetData(OutIdx, Layout);
		Inner.InitItem(Out);
		return Out;
	}

	// Load structs or ranges into non-empty set
	inline void InsertNonEmpty(FScriptSet& Dst, FRangeLoadView Src) const
	{
		auto HashFn = MakeHashFn(Inner);

		// Written to avoid FProperty::CopyCompleteValue_InContainer dependency
		// Items are loaded directly into sparse array and then removed if a duplicate existed
		const int32 OldNum = Dst.NumUnchecked();
		int32 TmpIdx;
		void* Tmp = AddItem(Dst, /* out */ TmpIdx);
		if constexpr (ElemKind == EPropertyKind::Range)
		{
			TConstArrayView<FRangeBinding> InnerBindings = Range.InnerBindings;
			for (FRangeLoadView Item : Src.AsRanges())
			{
				LoadRange(Tmp, Item, InnerBindings);
				if (!HasItem(Dst, Tmp))
				{
					Dst.CommitLastUninitialized(Layout, HashFn);
					Tmp = AddItem(Dst, /* out */ TmpIdx);
				}
			}
		}
		else
		{
			for (FStructLoadView Item : Src.AsStructs())
			{
				LoadStruct(Tmp, Item);
				if (!HasItem(Dst, Tmp))
				{
					Dst.CommitLastUninitialized(Layout, HashFn);
					Tmp = AddItem(Dst, /* out */ TmpIdx);
				}
			}
		}

		Inner.DestroyItem(Tmp);
		Dst.RemoveAtUninitialized(Layout, TmpIdx);

		if (Dst.NumUnchecked() != OldNum)
		{
			Dst.CommitAllUninitialized(Layout, HashFn);
		}
	}

	inline void Remove(FScriptSet& Dst, FRangeLoadView Src) const requires(bLeaves)
	{
		for (LeafType Item : CastAs<LeafType>(Src.AsLeaves()))
		{
			RemoveElem(Dst, &Item);
		}
	}

	inline void Remove(FScriptSet& Dst, FRangeLoadView Src) const
	{
		TArray<uint8, TInlineAllocator<64>> Buffer;
		Buffer.SetNumUninitialized(Inner.Size);
		Inner.InitItem(Buffer.GetData());
		void* Tmp = Buffer.GetData();

		if constexpr (ElemKind == EPropertyKind::Range)
		{
			TConstArrayView<FRangeBinding> Inners = Range.InnerBindings;
			for (FRangeLoadView Item : Src.AsRanges())
			{
				LoadRange(Tmp, Item, Inners);
				RemoveElem(Dst, Tmp);
			}
		}
		else
		{
			for (FStructLoadView Item : Src.AsStructs())
			{
				LoadStruct(Tmp, Item);
				RemoveElem(Dst, Tmp);
			}
		}

		Inner.DestroyItem(Tmp);
	}

	inline void DestroyRemoved(FScriptSet& Dst, int32 Idx) const
	{
		checkSlow(Idx < Dst.GetMaxIndex());
		checkSlow(!Dst.IsValidIndex(Idx));
		void* Elem = Dst.GetData(Idx, Layout);
		Inner.DestroyItem(Elem);
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////

class FSetBindings
{
	TMap<FParameterBinding, FBindId> Bindings;

	template<EPropertyKind ItemKind>
	void BindNew(FSetProperty* Property, FMemberBinding Elem, FBothStructId Both) 
	{
		// Todo: Ownership / memory leak
		TSetPropertyBinding<ItemKind>* Leak = new TSetPropertyBinding<ItemKind>(Property, Elem);
		GUE.Customs.BindStruct(Both.BindId, *Leak, Leak->DeclareCustom(Both.DeclId), {});
	}

	void DeltaBindNew(FSetProperty* Property, FMemberBinding Elem, FBothStructId Both)
	{
		switch (GetPropertyKind(Elem))
		{
		case EPropertyKind::Range:		BindNew<EPropertyKind::Range >(Property, Elem, Both); break;
		case EPropertyKind::Struct:		BindNew<EPropertyKind::Struct>(Property, Elem, Both); break;
		case EPropertyKind::Bool:		BindNew<EPropertyKind::Bool	 >(Property, Elem, Both); break;
		case EPropertyKind::U8:			BindNew<EPropertyKind::U8	 >(Property, Elem, Both); break;
		case EPropertyKind::U16:		BindNew<EPropertyKind::U16	 >(Property, Elem, Both); break;
		case EPropertyKind::U32:		BindNew<EPropertyKind::U32	 >(Property, Elem, Both); break;
		case EPropertyKind::U64:		BindNew<EPropertyKind::U64	 >(Property, Elem, Both); break;
		case EPropertyKind::F32:		BindNew<EPropertyKind::F32	 >(Property, Elem, Both); break;
		case EPropertyKind::F64:		BindNew<EPropertyKind::F64	 >(Property, Elem, Both); break;
		default:						check(false); break;
		}
	}
public:

	FBindId Bind(FSetProperty* Property, FMemberBinding Elem)
	{
		check(Elem.Offset == 0);
		if (const FBindId* BindId = Bindings.Find(FParameterBinding(Elem)))
		{
			return *BindId;
		}

		// Index custom delta binding struct name
		FBothType Param = Elem.IndexParameterName(GUE.Names);
		FType BindType = FType{ GUE.Scopes.CoreUObject, FTypenameId(GUE.Names.MakeParametricTypeId(GUE.Typenames.Set, MakeArrayView(&Param.BindType, 1))) };
		FType DeclType = Param.IsLowered()
						 ? FType{ GUE.Scopes.CoreUObject, FTypenameId(GUE.Names.MakeParametricTypeId(GUE.Typenames.Set, MakeArrayView(&Param.DeclType, 1))) }
						 : BindType;
		FBindId BindId = GUE.Names.IndexBindId(BindType);
		FDeclId DeclId = Param.IsLowered() ? GUE.Names.IndexDeclId(DeclType) : LowerCast(BindId);

		DeltaBindNew(Property, Elem, {BindId, DeclId});
		if (Elem.RangeBindings.Num())
		{
			Elem.RangeBindings = GRanges.Clone(Elem.RangeBindings);
		}
		return Bindings.Emplace(Elem, BindId);
	}
};
static FSetBindings GSets;

////////////////////////////////////////////////////////////////////////////////////////////////

// Flat TMap binding
template<class ScriptMap, EPropertyKind KeyKind, EPropertyKind ValueKind>
struct TMapPropertyItemBinding : IItemRangeBinding
{
	const FScriptMapLayout							Layout;
	const TInnerProperty<KeyKind>					InnerKey;
	const TInnerProperty<ValueKind>					InnerValue;
	
	TMapPropertyItemBinding(FMapProperty* In)
	: IItemRangeBinding(GUE.Typenames.Map)
	, Layout(In->MapLayout)
	, InnerKey(In->KeyProp)
	, InnerValue(In->ValueProp)
	{
		check(InnerKey.Size <= static_cast<uint64>(Layout.ValueOffset));
		check(InnerValue.Size <= static_cast<uint64>(Layout.SetLayout.Size - Layout.ValueOffset));
	}

	inline SIZE_T GetStride() const	{ return Layout.SetLayout.Size; }

	inline uint8* InitMap(ScriptMap& Map, int32 Num) const
	{
		uint8* It = SetNumUninitialized(Map, Layout, Num);
		InitStridedItems(InnerKey, It, Num, GetStride());
		InitStridedItems(InnerValue, It + Layout.ValueOffset, Num, GetStride());
		return It;
	}
	
	inline void Rehash(ScriptMap& Map) const
	{
		Map.Rehash(Layout, MakeHashFn(InnerKey));
	}

	inline void DestroyAll(ScriptMap& Map) const
	{
		if (InnerKey.bDestruct || InnerValue.bDestruct)
		{
			const SIZE_T Stride = GetStride();
			const int32 ValueOffset = Layout.ValueOffset;
			const int32 Num = Map.NumUnchecked();
			uint8* It = static_cast<uint8*>(Map.GetData(0, Layout));
			if (IsCompact(Map))
			{
				DestroyStridedItems(InnerKey, It, Num, Stride);
				DestroyStridedItems(InnerValue, It + ValueOffset, Num, Stride);
			}
			else
			{
				for (int32 Idx = 0, Max = Map.GetMaxIndex(); Idx < Max; ++Idx)
				{
					if (Map.IsValidIndex(Idx))
					{
						InnerKey.DestroyItem(It);
						InnerValue.DestroyItem(It + ValueOffset);
					}
					It += Stride;
				}
			}
		}
	}

	virtual void ReadItems(FSaveRangeContext& Ctx) const override
	{
#if UE_USE_COMPACT_SET_AS_DEFAULT
		const FScriptMap& Set = Ctx.Request.GetRange<FScriptMap>();
		Ctx.Items.SetAll(Set.GetData(0, Layout), Set.Num(), Layout.SetLayout.Size);
#else
		ReadSparseItems(/* out */ Ctx.Items, Ctx.Request.GetRange<FScriptSparseArray>(), Layout.SetLayout.SparseArrayLayout);
#endif
	}

	virtual void MakeItems(FLoadRangeContext& Ctx) const override
	{
		ScriptMap& Map = Ctx.Request.GetRange<ScriptMap>();
		int32 NewNum = static_cast<int32>(Ctx.Request.NumTotal());

		if (Ctx.Request.IsFirstCall())
		{
			DestroyAll(Map);
			Map.Empty(NewNum, Layout);
			if (NewNum)
			{
				void* Items = InitMap(Map, NewNum);
				Ctx.Items.Set(Items, Ctx.Request.NumTotal(), GetStride());
				Ctx.Items.RequestFinalCall();
			}
		}
		else
		{
			check(Ctx.Request.IsFinalCall());
			Rehash(Map);
		}
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////

struct FMapMemberBindings
{
	FMemberBinding Key;
	FMemberBinding Value;
	FMemberBinding Pair;
};

template <EPropertyKind KeyKind, EPropertyKind ValueKind>
struct TMapPropertyCustomBinding : TMapPropertyItemBinding<FScriptMap, KeyKind, ValueKind>, ICustomBinding
{
	using Super = TMapPropertyItemBinding<FScriptMap, KeyKind, ValueKind>;
	using Super::Layout;
	using Super::InnerKey;
	using Super::InnerValue;
	using Super::GetStride;
	using Super::InitMap;
	using Super::Rehash;
	using SubSetIterator = TSubSetIterator<FScriptMap, FScriptMapLayout>;
	
	const TPropertyRangeSerializer<KeyKind>			KeyRange;
	const TPropertyRangeSerializer<ValueKind>		ValueRange;
	const FStructRangeSerializer					PairRange;

	const TPropertyRangeSerializer<KeyKind>&		GetKeyRange() const { return KeyRange; }
	const FStructRangeSerializer&					GetItemRange() const { return PairRange; }

	TMapPropertyCustomBinding(FMapProperty* Map, FMapMemberBindings Members)
	: Super(Map)
	, KeyRange(Members.Key)
	, ValueRange(Members.Value)
	, PairRange(Members.Pair)
	{}

	FStructDeclarationPtr Declare(FDeclId Id) const
	{
		FMemberId Members[] = { GUE.Members.Assign, GUE.Members.Remove, GUE.Members.Insert };
		FMemberSpec KeysSpec = KeyRange.SpecMember();
		FMemberSpec PairsSpec = PairRange.SpecMember();
		return PlainProps::Declare({Id, NoId, 0, EMemberPresence::AllowSparse, Members, {PairsSpec, KeysSpec, PairsSpec}});
	}

	inline const void* GetValue(const void* Pair) const
	{
		return static_cast<const uint8*>(Pair) + Layout.ValueOffset;
	} 

	inline int32 FindKey(const FScriptMap& Map, const void* Key) const
	{
		return Map.FindPairIndex(Key, Layout, MakeHashFn(InnerKey), MakeIdenticalFn(InnerKey));
	}

	inline bool HasKey(const FScriptMap& Map, const void* Key) const
	{
		return FindKey(Map, Key) != INDEX_NONE;
	}

	inline bool HasItem(const FScriptMap& Map, const void* Pair) const
	{
		const void* Key = Pair;
		if (int32 Idx = FindKey(Map, Key); Idx != INDEX_NONE)
		{
			const void* FoundPair = Map.GetData(Idx, Layout);
			return MakeIdenticalFn(InnerValue)(GetValue(Pair), GetValue(FoundPair));
		}
		return false;
	}

	inline void RestoreOverrides(const FScriptMap& Map, FRangeLoadView Src, FOverriddenPropertySet* Overrides, EOverriddenPropertyOperation Operation) const
	{
	}

	inline uint8* AddPair(FScriptMap& Dst, int32& OutIdx) const
	{
		OutIdx = Dst.AddUninitialized(Layout);
		uint8* Out = static_cast<uint8*>(Dst.GetData(OutIdx, Layout));
		InnerKey.InitItem(Out);
		InnerValue.InitItem(Out + Layout.ValueOffset);
		return Out;
	}

#if UE_USE_COMPACT_SET_AS_DEFAULT
	inline void RemoveKey(FScriptMap& Map, const void* Key) const
	{
		if (int32 Idx = FindKey(Map, Key); Idx != INDEX_NONE)
		{
			Map.RemoveAt(Idx, Layout, MakeHashFn(InnerKey), [this](void* Pair)
				{
					InnerKey.DestroyItem(Pair);
					InnerValue.DestroyItem(static_cast<uint8*>(Pair) + Layout.ValueOffset);
				});
		}
	}
#else
	inline void DestroyPair(FScriptMap& Map, int32 Idx) const
	{
		if (InnerKey.bDestruct || InnerValue.bDestruct)
		{
			void* Pair = Map.GetData(Idx, Layout);
			InnerKey.DestroyItem(Pair);
			InnerValue.DestroyItem(static_cast<uint8*>(Pair) + Layout.ValueOffset);
		}
	}

	inline void RemoveKey(FScriptMap& Map, const void* Key) const
	{
		if (int32 Idx = FindKey(Map, Key); Idx != INDEX_NONE)
		{
			DestroyPair(Map, Idx);
			Map.RemoveAt(Idx, Layout);
		}
	}
#endif

	virtual void SaveCustom(FMemberBuilder& Dst, const void* Src, FBaseline Base, const FSaveContext& Ctx) override
	{
		SaveSetDelta(*this, Dst, *static_cast<const FScriptMap*>(Src), static_cast<const FScriptMap*>(Base.Get()), Ctx);
	}

	virtual void LoadCustom(void* Dst, FStructLoadView Src, ECustomLoadMethod Method) const override
	{
		check(Method == ECustomLoadMethod::Assign);
		LoadSetDelta(*this, *static_cast<FScriptMap*>(Dst), Src);
	}

	// Load into empty set
	inline void AssignEmpty(FScriptMap& Dst, FRangeLoadView Src) const
	{
		const int32 ValueOffset = Layout.ValueOffset;
		const SIZE_T Stride = GetStride();

		uint8* It = InitMap(Dst, Src.Num());
		
		if (!Src.IsEmpty())
		{
			FOptionalSchemaId InnerLoadIds[2];
			FStructRangeLoadView Structs = Src.AsStructs();
			Structs.GetSchema().GetInnerLoadIds(/* out */ MakeArrayView(InnerLoadIds));
			for (FStructLoadView Struct : Structs)
			{
				// Equivalent to LoadStruct(It, Struct);
				FBitCacheReader Bits;
				KeyRange.LoadItem(It,					/* in-out */ Struct.Values, /* in-out */ Bits, InnerLoadIds[0], Struct.Schema.Batch);
				ValueRange.LoadItem(It + ValueOffset,	/* in-out */ Struct.Values, /* in-out */ Bits, InnerLoadIds[1], Struct.Schema.Batch);
				It += Stride;
			}
		}

		Rehash(Dst);
	}

	// Load structs or ranges into non-empty map
	inline void InsertNonEmpty(FScriptMap& Dst, FRangeLoadView Src) const
	{		
		// Create temporary key
		TArray<uint8, TInlineAllocator<32>> TmpData;
		TmpData.SetNumUninitialized(InnerKey.Size);
		void* TmpKey = TmpData.GetData();
		InnerKey.InitItem(TmpKey);

		FOptionalSchemaId InnerLoadIds[2];
		FStructRangeLoadView Structs = Src.AsStructs();
		Structs.GetSchema().GetInnerLoadIds(/* out */ MakeArrayView(InnerLoadIds));
		for (FStructLoadView Struct : Structs)
		{
			// Equivalent to LoadStruct(It, Struct);
			FBitCacheReader Bits;
			KeyRange.LoadItem(/* out*/ TmpKey,	/* in-out */ Struct.Values, /* in-out */ Bits, InnerLoadIds[0], Struct.Schema.Batch);

			void* Value = Dst.FindOrAdd(TmpKey, Layout, MakeHashFn(InnerKey), MakeIdenticalFn(InnerKey), [this, TmpKey](void* K, void* V)
			{
				FMemory::Memcpy(K, TmpKey, InnerKey.Size);	// Move construct K (assuming trivial relocatability)
				InnerKey.InitItem(TmpKey);					// Reinit moved from tmp key
				InnerValue.InitItem(V);						// Default construct V
			});

			ValueRange.LoadItem(/* out */ Value, /* in-out */ Struct.Values, /* in-out */ Bits, InnerLoadIds[1], Struct.Schema.Batch);
		}

		InnerKey.DestroyItem(TmpKey);
	}

	inline void Remove(FScriptMap& Dst, FRangeLoadView Src) const
	{
		using LeafType = EquivalentLeafType<KeyKind>;
		for (LeafType Item : CastAs<LeafType>(Src.AsLeaves()))
		{
			RemoveKey(Dst, &Item);
		}
	}

	inline void Remove(FScriptMap& Dst, FRangeLoadView Src) const requires (KeyKind == EPropertyKind::Range)
	{
		TArray<uint8, TInlineAllocator<64>> Buffer;
		Buffer.SetNumUninitialized(InnerKey.Size);
		void* Tmp = Buffer.GetData();

		InnerKey.InitItem(Tmp);
		for (FRangeLoadView Item : Src.AsRanges())
		{
			LoadRange(Tmp, Item, KeyRange.InnerBindings);
			RemoveKey(Dst, Tmp);
		}
		InnerKey.DestroyItem(Tmp);
	}

	inline void Remove(FScriptMap& Dst, FRangeLoadView Src) const requires (KeyKind == EPropertyKind::Struct)
	{
		TArray<uint8, TInlineAllocator<64>> Buffer;
		Buffer.SetNumUninitialized(InnerKey.Size);
		void* Tmp = Buffer.GetData();

		InnerKey.InitItem(Tmp);
		for (FStructLoadView Item : Src.AsStructs())
		{
			LoadStruct(Tmp, Item);
			RemoveKey(Dst, Tmp);
		}
		InnerKey.DestroyItem(Tmp);
	}

	virtual bool DiffCustom(const void* A, const void* B, const FBindContext&) const override
	{
		return DiffSet(*this, *static_cast<const FScriptMap*>(A), *static_cast<const FScriptMap*>(B));
	}
	
	virtual bool DiffCustom(const void* A, const void* B, FDiffContext& Ctx) const override
	{
		return DiffMap(*this, *static_cast<const FScriptMap*>(A), *static_cast<const FScriptMap*>(B), Ctx);
	}
};

template <EPropertyKind KeyKind, EPropertyKind ValueKind>
struct TOverridableMapBinding : TMapPropertyCustomBinding<KeyKind, ValueKind>
{
	using Super = TMapPropertyCustomBinding<KeyKind, ValueKind>;
	using Super::Layout;
	using Super::InnerKey;
	using Super::InnerValue;
	using Super::KeyRange;
	using Super::PairRange;

	FMapProperty*	MapProperty;

	TOverridableMapBinding(FMapProperty* Map, FMapMemberBindings Members)
		: Super(Map, Members)
		, MapProperty(Map)
	{
	}

	FStructDeclarationPtr Declare(FDeclId Id) const
	{
		FMemberId Members[] = { GUE.Members.Assign, GUE.Members.Remove, GUE.Members.Insert, GUE.Members.Modify };
		FMemberSpec KeysSpec = KeyRange.SpecMember();
		FMemberSpec PairsSpec = PairRange.SpecMember();
		return PlainProps::Declare({ Id, NoId, 0, EMemberPresence::AllowSparse, Members, {PairsSpec, KeysSpec, PairsSpec, PairsSpec} });
	}

	inline void SaveOverrides(FMemberBuilder& Dst, const FScriptMap& Map, const FScriptMap& Default,
							  const FOverriddenPropertySet& Overrides,
							  TBitArray<>& RemoveIds, TBitArray<>& InsertIds, TBitArray<>& ModifyIds, bool& bIsFullyReplaced) const
	{
		bIsFullyReplaced = false;

		if (const FOverriddenPropertyNode* MapNode = GetOverriddenNode(Overrides, &Map))
		{
			if (MapNode->GetOperation() == EOverriddenPropertyOperation::Replace)
			{
				bIsFullyReplaced = true;
			}
			else
			{
				FScriptMapHelper MapHelper(MapProperty, &Map);
				FScriptMapHelper DefaultMapHelper(MapProperty, &Default);

				for (const FOverriddenPropertyNode& SubNode : MapNode->GetSubPropertyNodes())
				{
					switch (SubNode.GetOperation())
					{
					case EOverriddenPropertyOperation::Remove:
					{
						const int32 InternalIndex = SubNode.GetNodeID().ToMapInternalIndex(DefaultMapHelper);
						if (InternalIndex != INDEX_NONE)
						{
							check(InternalIndex < Default.GetMaxIndex());
							RemoveIds[InternalIndex] = 1;
						}
						break;
					}
					case EOverriddenPropertyOperation::Add:
					{
						const int32 InternalIndex = SubNode.GetNodeID().ToMapInternalIndex(MapHelper);
						if (InternalIndex != INDEX_NONE)
						{
							check(InternalIndex < Map.GetMaxIndex());
							InsertIds[InternalIndex] = 1;
						}
						break;
					}
					case EOverriddenPropertyOperation::Modified:
					{
						const int32 InternalIndex = SubNode.GetNodeID().ToMapInternalIndex(MapHelper);
						if (InternalIndex != INDEX_NONE)
						{
							check(InternalIndex < Map.GetMaxIndex());
							ModifyIds[InternalIndex] = 1;
						}
						break;
					}
					case EOverriddenPropertyOperation::Externalized:
					{
						// Todo: Handle external objects.
						break;
					}
					default:
						checkf(false, TEXT("Unsupported map operation"));
						break;
					}
				}
			}
		}
	}

	inline void RestoreOverrides(const FScriptMap& Map, FRangeLoadView Src,
								 FOverriddenPropertySet* Overrides, EOverriddenPropertyOperation Operation) const
	{
		if (Overrides)
		{
			if (Operation == EOverriddenPropertyOperation::Replace)
			{
				RestoreOverriddenOperation(*Overrides, EOverriddenPropertyOperation::Replace, &Map);
			}
			else if (Operation == EOverriddenPropertyOperation::Add ||
					 Operation == EOverriddenPropertyOperation::Remove ||
					 Operation == EOverriddenPropertyOperation::Modified)
			{
				TArray<uint8, TInlineAllocator<64>> Buffer;
				Buffer.SetNumUninitialized(Layout.SetLayout.Size);
				void* Tmp = Buffer.GetData();

				InnerKey.InitItem(Tmp);
				InnerValue.InitItem(static_cast<uint8*>(Tmp) + Layout.ValueOffset);

				if (!Src.IsEmpty())
				{
					FOptionalSchemaId InnerLoadIds[2];
					FStructRangeLoadView Structs = Src.AsStructs();
					Structs.GetSchema().GetInnerLoadIds(/* out */ MakeArrayView(InnerLoadIds));
					for (FStructLoadView Struct : Structs)
					{
						LoadStruct(Tmp, Struct);

						// Need to fetch the ArrayNode every loop as the previous iteration might have reallocated the node.
						if (FOverriddenPropertyNode* MapNode = RestoreOverriddenOperation(*Overrides, EOverriddenPropertyOperation::Modified, &Map))
						{
							FOverriddenPropertyNodeID KeyID = FOverriddenPropertyNodeID::FromMapKey(MapProperty->GetKeyProperty(), Tmp);
							Overrides->RestoreSubPropertyOperation(Operation, *MapNode, KeyID);
						}
					}
				}

				InnerKey.DestroyItem(Tmp);
				InnerValue.DestroyItem(static_cast<uint8*>(Tmp) + Layout.ValueOffset);
			}
			else
			{
				checkf(false, TEXT("Unsupported override operation for maps"));
			}
		}
	}

	virtual void SaveCustom(FMemberBuilder& Dst, const void* Src, FBaseline Base, const FSaveContext& Ctx) override
	{
		SaveSetOverrides(*this, Dst, *static_cast<const FScriptMap*>(Src), static_cast<const FScriptMap*>(Base.Get()), Ctx, GetSaveOverrides());
	}

	virtual void LoadCustom(void* Dst, FStructLoadView Src, ECustomLoadMethod Method) const override
	{
		check(Method == ECustomLoadMethod::Assign);
		LoadSetDelta(*this, *static_cast<FScriptMap*>(Dst), Src, GetRestoreOverrides());
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////

class FMapBindings
{
	TMap<FBindId, FBindId>			NormalBindings;
	TMap<FBindId, FRangeBinding>	FrozenBindings;

	template<EPropertyKind KeyKind, EPropertyKind ValueKind>
	static IItemRangeBinding* New3(FMapProperty* Property, FMapMemberBindings Members, FBindId* OutCustomId)
	{
		if (OutCustomId == nullptr) // Freezable maps aren't delta serialized
		{
			return new TMapPropertyItemBinding<FFreezableScriptMap, KeyKind, ValueKind>(Property);
		}

		const bool bIsOverridable = Property->HasAnyPropertyFlags(CPF_ExperimentalOverridableLogic);

		// Index custom delta binding struct name
		FBothStructId BothPair;
		TArray<FInnerStruct, TInlineAllocator<3>> Inners;
		if (Members.Key.RangeBindings.Num() + Members.Value.RangeBindings.Num() > 0)
		{
			FBothType BothKey = Members.Key.IndexParameterName(GUE.Names);
			FBothType BothValue = Members.Value.IndexParameterName(GUE.Names);
			checkf(BothKey.IsLowered() || BothValue.IsLowered(), TEXT("Key or Value is range-bound and should be type-erased / lowered"));
			FType BindParams[2] = {BothKey.BindType, BothValue.BindType};
			FType DeclParams[2] = {BothKey.DeclType, BothValue.DeclType};
			FType BindType = { GUE.Scopes.CoreUObject, FTypenameId(GUE.Names.MakeParametricTypeId(GUE.Typenames.Map, BindParams)) };
			FType DeclType = { GUE.Scopes.CoreUObject, FTypenameId(GUE.Names.MakeParametricTypeId(GUE.Typenames.Map, DeclParams)) };
			BothPair = { GUE.Names.IndexBindId(BindType), GUE.Names.IndexDeclId(DeclType) };

			FBindId PairId = Members.Pair.InnermostId.Get().AsStructBindId();
			if (BothKey.IsLowered())
			{
				FBindId KeyId = GUE.Names.IndexBindId(BothKey.BindType);
				Inners.Emplace(GUE.Members.Assign, PairId);
				Inners.Emplace(GUE.Members.Remove, KeyId);
				Inners.Emplace(GUE.Members.Insert, PairId);
			}
			else
			{
				Inners.Emplace(GUE.Members.Assign, PairId);
				Inners.Emplace(GUE.Members.Insert, PairId);
			}

			if (bIsOverridable)
			{
				Inners.Emplace(GUE.Members.Modify, PairId);
			}
		}
		else
		{
			checkf(!Members.Key.IndexParameterName(GUE.Names).IsLowered(), TEXT("Only range-bound keys or values should be type-erased / lowered"));
			checkf(!Members.Value.IndexParameterName(GUE.Names).IsLowered(), TEXT("Only range-bound keys or values should be type-erased / lowered"));

			FParametricTypeId PairTypename = GUE.Names.Resolve(Members.Pair.InnermostId.Get().AsStruct()).Name.AsParametric();
			TConstArrayView<FType> PairParams = GUE.Names.Resolve(PairTypename).GetParameters();
			FType MapParams[2] = { PairParams[0], PairParams[1] };
			FType Type = { GUE.Scopes.CoreUObject, FTypenameId(GUE.Names.MakeParametricTypeId(GUE.Typenames.Map, MapParams)) };
			FDualStructId Dual{GUE.Names.IndexStruct(Type)};
			BothPair = { Dual, Dual };
		}

		// Todo: Ownership / memory leak
		TMapPropertyCustomBinding<KeyKind, ValueKind>* Out = nullptr;

		if (bIsOverridable)
		{
			auto Binding = new TOverridableMapBinding<KeyKind, ValueKind>(Property, Members);
			GUE.Customs.BindStruct(BothPair.BindId, *Binding, Binding->Declare(BothPair.DeclId), Inners);
			Out = Binding;
		}
		else
		{
			Out = new TMapPropertyCustomBinding<KeyKind, ValueKind>(Property, Members);
			GUE.Customs.BindStruct(BothPair.BindId, *Out, Out->Declare(BothPair.DeclId), Inners);
		}
		
		*OutCustomId = BothPair.BindId;
		return Out;
	}

	template<EPropertyKind KeyKind>
	inline IItemRangeBinding* New2(FMapProperty* Property, FMapMemberBindings Members, FBindId* OutCustomId)
	{
		switch (GetPropertyKind(Members.Value))
		{
		case EPropertyKind::Range:		return New3<KeyKind, EPropertyKind::Range>(Property, Members, OutCustomId);
		case EPropertyKind::Struct:		return New3<KeyKind, EPropertyKind::Struct>(Property, Members, OutCustomId);
		case EPropertyKind::Bool:		return New3<KeyKind, EPropertyKind::Bool>(Property, Members, OutCustomId);
		case EPropertyKind::U8:			return New3<KeyKind, EPropertyKind::U8>(Property, Members, OutCustomId);
		case EPropertyKind::U16:		return New3<KeyKind, EPropertyKind::U16>(Property, Members, OutCustomId);
		case EPropertyKind::U32:		return New3<KeyKind, EPropertyKind::U32>(Property, Members, OutCustomId);
		case EPropertyKind::U64:		return New3<KeyKind, EPropertyKind::U64>(Property, Members, OutCustomId);
		case EPropertyKind::F32:		return New3<KeyKind, EPropertyKind::F32>(Property, Members, OutCustomId);
		case EPropertyKind::F64:		return New3<KeyKind, EPropertyKind::F64>(Property, Members, OutCustomId);
		default:						check(false); return nullptr;
		}
	}

	inline IItemRangeBinding* New(FMapProperty* Property, FMapMemberBindings Members, FBindId* OutCustomId)
	{
		switch (GetPropertyKind(Members.Key))
		{
		case EPropertyKind::Range:		return New2<EPropertyKind::Range>(Property, Members, OutCustomId);
		case EPropertyKind::Struct:		return New2<EPropertyKind::Struct>(Property, Members, OutCustomId);
		case EPropertyKind::Bool:		return New2<EPropertyKind::Bool>(Property, Members, OutCustomId);
		case EPropertyKind::U8:			return New2<EPropertyKind::U8>(Property, Members, OutCustomId);
		case EPropertyKind::U16:		return New2<EPropertyKind::U16>(Property, Members, OutCustomId);
		case EPropertyKind::U32:		return New2<EPropertyKind::U32>(Property, Members, OutCustomId);
		case EPropertyKind::U64:		return New2<EPropertyKind::U64>(Property, Members, OutCustomId);
		case EPropertyKind::F32:		return New2<EPropertyKind::F32>(Property, Members, OutCustomId);
		case EPropertyKind::F64:		return New2<EPropertyKind::F64>(Property, Members, OutCustomId);
		default:						check(false); return nullptr;
		}
	}

public:
	FBindId BindNormal(FMapProperty* Property, FBindId PairId, FMapMemberBindings Members)
	{
		if (const FBindId* CustomId = NormalBindings.Find(PairId))
		{
			return *CustomId;
		}

		FBindId CustomId;
		New(Property, Members, /* out */ &CustomId);
		return NormalBindings.Emplace(PairId, CustomId);
	}

	FRangeBinding BindFreezable(FMapProperty* Property, FBindId PairId, FMapMemberBindings Members)
	{
		if (const FRangeBinding* RangeBinding = FrozenBindings.Find(PairId))
		{
			return *RangeBinding;
		}

		IItemRangeBinding* Leak = New(Property, Members, nullptr);
		return FrozenBindings.Emplace(PairId, FRangeBinding(*Leak, DefaultRangeMax));
	}
};
static FMapBindings GMaps;

////////////////////////////////////////////////////////////////////////////////////////////////

// Utilities to allow the overridable array binding to use the underlying array as a set.
// This is so we can use the templated set utilities (Save, Load, Diff, etc).

struct FOverridableScriptArrayLayout
{
	int32 NumBytesPerElement;
	uint32 AlignmentOfElement;
};

class FOverridableScriptArray : public FScriptArray
{
public:

	[[nodiscard]] int32 GetMaxIndex() const
	{
		return Num();
	}
	[[nodiscard]] void* GetData(int32 Index, const FOverridableScriptArrayLayout& Layout)
	{
		void* Elems = FScriptArray::GetData();
		return static_cast<uint8*>(Elems) + (Index * Layout.NumBytesPerElement);
	}
	[[nodiscard]] const void* GetData(int32 Index, const FOverridableScriptArrayLayout& Layout) const
	{
		const void* Elems = FScriptArray::GetData();
		return static_cast<const uint8*>(Elems) + (Index * Layout.NumBytesPerElement);
	}
	void Empty(int32 Slack, const FOverridableScriptArrayLayout& Layout)
	{
		FScriptArray::Empty(Slack, Layout.NumBytesPerElement, Layout.AlignmentOfElement);
	}
	int32 AddUninitialized(const FOverridableScriptArrayLayout& Layout)
	{
		return Add(1, Layout.NumBytesPerElement, Layout.AlignmentOfElement);
	}
	void RemoveAtUninitialized(const FOverridableScriptArrayLayout& Layout, int32 Index)
	{
		FScriptArray::Remove(Index, /*Count*/1, Layout.NumBytesPerElement, Layout.AlignmentOfElement);
	}
};

struct FOverridableArrayPropertyBinding : IItemRangeBinding, ICustomBinding
{
	using SubSetIterator = TSubSetIterator<FOverridableScriptArray, FOverridableScriptArrayLayout>;

	const FOverridableScriptArrayLayout						Layout;
	const TInnerProperty<EPropertyKind::Struct>				Inner;
	const TPropertyRangeSerializer<EPropertyKind::Struct>	Range;

	const TPropertyRangeSerializer<EPropertyKind::Struct>& GetKeyRange() const { return Range; }
	const TPropertyRangeSerializer<EPropertyKind::Struct>& GetItemRange() const { return Range; }

	FOverridableArrayPropertyBinding(FArrayProperty* In, FMemberBinding Elem)
		: IItemRangeBinding(GUE.Typenames.TrivialArray)
		, Layout(In->Inner->GetElementSize(), In->Inner->GetMinAlignment())
		, Inner(In->Inner)
		, Range(Elem)
	{
		check(Inner.bHashable);
		checkf(CastField<FObjectProperty>(Inner.Property) && Inner.Property->HasAnyPropertyFlags(CPF_PersistentInstance), 
			TEXT("Only instanced sub-object arrays can be overridable"));
	}

	inline FStructDeclarationPtr DeclareCustom(FDeclId Id) const
	{
		FMemberId Members[] = { GUE.Members.Assign, GUE.Members.Remove, GUE.Members.Insert, GUE.Members.Modify };
		FMemberSpec RangeSpec = Range.SpecMember();
		return Declare({ Id, NoId, 0, EMemberPresence::AllowSparse, Members, {RangeSpec, RangeSpec, RangeSpec, RangeSpec} });
	}

	inline SIZE_T GetStride() const { return Layout.NumBytesPerElement; }

	inline int32 FindIndex(const FOverridableScriptArray& Array, const void* Elem) const
	{
		int32 Num = Array.Num();
		int32 Result = INDEX_NONE;

		if (const UObject** Obj = (const UObject**)Elem)
		{
			for (int32 Idx = 0; Idx < Num; ++Idx)
			{
				UObject** TestObj = (UObject**)Array.GetData(Idx, Layout);

				if (TestObj && *TestObj == *Obj)
				{
					Result = Idx;
					break;
				}
			}
		}

		return Result;
	}

	inline int32 FindIndexFromNode(const FOverriddenPropertyNodeID& Node, const FOverridableScriptArray& Array) const
	{
		int32 Num = Array.Num();

		for (int32 Idx = 0; Idx < Num; ++Idx)
		{
			UObject** Item = (UObject**)Array.GetData(Idx, Layout);

			if (Node == FOverriddenPropertyNodeID(*Item))
			{
				return Idx;
			}
		}

		return INDEX_NONE;
	}

	inline void SaveOverrides(FMemberBuilder& Dst, const FOverridableScriptArray& Array, const FOverridableScriptArray& Default,
								const FOverriddenPropertySet& Overrides, 
								TBitArray<>& RemoveIds, TBitArray<>& InsertIds, TBitArray<>& ModifyIds, bool& bIsFullyReplaced) const
	{
		bIsFullyReplaced = false;

		if (const FOverriddenPropertyNode* ArrayNode = GetOverriddenNode(Overrides, &Array))
		{
			if (ArrayNode->GetOperation() == EOverriddenPropertyOperation::Replace)
			{
				bIsFullyReplaced = true;
			}
			else
			{
				for (const FOverriddenPropertyNode& SubNode : ArrayNode->GetSubPropertyNodes())
				{
					EOverriddenPropertyOperation Op = SubNode.GetOperation();

					switch (Op)
					{
					case EOverriddenPropertyOperation::Remove:
					{
						const int32 DefaultIdx = FindIndexFromNode(SubNode.GetNodeID(), Default);
						if (DefaultIdx != INDEX_NONE)
						{
							RemoveIds[DefaultIdx] = 1;
						}
						else
						{
							UE_LOGFMT(LogPlainPropsUObject, VeryVerbose, "Unable to save deleted item '{Item}'.", *SubNode.GetNodeID().ToDebugString());
						}
						break;
					}
					case EOverriddenPropertyOperation::Add:
					{
						const int32 Idx = FindIndexFromNode(SubNode.GetNodeID(), Array);
						if (Idx != INDEX_NONE)
						{
							InsertIds[Idx] = 1;
						}
						else
						{
							UE_LOGFMT(LogPlainPropsUObject, VeryVerbose, "Unable to save added item '{Item}'.", *SubNode.GetNodeID().ToDebugString());
						}
						break;
					}
					case EOverriddenPropertyOperation::Modified:
					{
						const int32 Idx = FindIndexFromNode(SubNode.GetNodeID(), Array);
						if (Idx != INDEX_NONE)
						{
							ModifyIds[Idx] = 1;
						}
						else
						{
							UE_LOGFMT(LogPlainPropsUObject, VeryVerbose, "Unable to save modified item '{Item}'.", *SubNode.GetNodeID().ToDebugString());
						}
						break;
					}
					case EOverriddenPropertyOperation::Externalized:
					{
						// Todo: Handle external objects.
						break;
					}
					default:
						checkf(false, TEXT("Unsupported operation type"));
						break;
					}
				}
			}
		}
	}

	inline void RestoreOverrides(const FOverridableScriptArray& Array, FRangeLoadView Items,
								 FOverriddenPropertySet* Overrides, EOverriddenPropertyOperation Operation) const
	{
		if (Overrides)
		{
			if (Operation == EOverriddenPropertyOperation::Replace)
			{
				RestoreOverriddenOperation(*Overrides, EOverriddenPropertyOperation::Replace, &Array);
			}
			else if (Operation == EOverriddenPropertyOperation::Add || 
					 Operation == EOverriddenPropertyOperation::Remove ||
					 Operation == EOverriddenPropertyOperation::Modified)
			{
				// We need to add a temporary item to the array which we will remove
				// after iterating over the items.
				FOverridableScriptArray& MutArray = const_cast<FOverridableScriptArray&>(Array);

				int32 TmpIdx;
				void* Tmp = AddItem(MutArray, /* out */ TmpIdx);
				for (FStructLoadView Item : Items.AsStructs())
				{
					LoadStruct(Tmp, Item);
					int32 Idx = FindIndex(Array, Tmp);

					if (Idx != INDEX_NONE && Idx != TmpIdx)
					{
						UObject** SubObject = (UObject**)Array.GetData(Idx, Layout);

						if (*SubObject)
						{
							// Need to fetch the ArrayNode every loop as the previous iteration might have reallocated the node.
							if (FOverriddenPropertyNode* ArrayNode = RestoreOverriddenOperation(*Overrides, EOverriddenPropertyOperation::Modified, &Array))
							{
								// Rebuild the overridden info
								Overrides->RestoreSubObjectOperation(Operation, *ArrayNode, *SubObject);
							}
						}
					}
				}

				Inner.DestroyItem(Tmp);
				MutArray.RemoveAtUninitialized(Layout, TmpIdx);
			}
			else
			{
				checkf(false, TEXT("Unsupported override operation for arrays"));
			}
		}
	}

	inline void DestroyElem(FOverridableScriptArray& Array, int32 Idx) const
	{
		if (Idx < Array.Num())
		{
			Inner.DestroyItem(Array.GetData(Idx, Layout));
		}
	}

	inline void RemoveElem(FOverridableScriptArray& Array, const void* Elem) const
	{
		if (int32 Idx = FindIndex(Array, Elem); Idx != INDEX_NONE)
		{
			DestroyElem(Array, Idx);
			Array.Remove(Idx, 1, 8, 16);
		}
	}

	inline bool HasItem(const FOverridableScriptArray& Array, const void* Elem) const
	{
		return FindIndex(Array, Elem) != INDEX_NONE;
	}

	inline bool HasKey(const FOverridableScriptArray& Array, const void* Elem) const
	{
		return HasItem(Array, Elem);
	}

	inline void DestroyAll(FOverridableScriptArray& Array) const
	{
		for (int32 Idx = 0; Idx < Array.Num(); ++Idx)
		{
			void* Item = Array.GetData(Idx, Layout);
			DestroyValue(Inner.Property, Item);
		}
	}

	virtual void ReadItems(FSaveRangeContext& Ctx) const override
	{
		const FScriptArray& Array = Ctx.Request.GetRange<FScriptArray>();
		Ctx.Items.SetAll(Array.GetData(), static_cast<uint64>(Array.Num()), GetStride());
	}

	virtual void MakeItems(FLoadRangeContext& Ctx) const override
	{
		FScriptArray& Array = Ctx.Request.GetRange<FScriptArray>();

		SIZE_T Stride = GetStride();

		int32 NewNum = static_cast<int32>(Ctx.Request.NumTotal());
		Array.SetNumUninitialized(NewNum, Stride, Layout.AlignmentOfElement);
		if (NewNum)
		{
			FMemory::Memzero(Array.GetData(), NumBytes(NewNum, Stride));
		}

		Ctx.Items.Set(Array.GetData(), static_cast<uint64>(NewNum), Stride);
	}

	virtual void SaveCustom(FMemberBuilder& Dst, const void* Src, FBaseline Base, const FSaveContext& Ctx) override
	{
		SaveSetOverrides(*this, Dst, *static_cast<const FOverridableScriptArray*>(Src), static_cast<const FOverridableScriptArray*>(Base.Get()),
						 Ctx, GetSaveOverrides());
	}

	virtual void LoadCustom(void* Dst, FStructLoadView Src, ECustomLoadMethod Method) const override
	{
		check(Method == ECustomLoadMethod::Assign);
		LoadSetDelta(*this, *static_cast<FOverridableScriptArray*>(Dst), Src, GetRestoreOverrides());
	}

	virtual bool DiffCustom(const void* A, const void* B, const FBindContext&) const override
	{
		return DiffSet(*this, *static_cast<const FOverridableScriptArray*>(A), *static_cast<const FOverridableScriptArray*>(B));
	}

	// Load into empty array
	inline void AssignEmpty(FOverridableScriptArray& Dst, FRangeLoadView Src) const
	{
		SIZE_T Stride = GetStride();
		uint8* It = SetNumUninitialized(Dst, Layout, Src.Num());
		InitStridedItems(Inner, It, Src.Num(), Stride);

		for (FStructLoadView Item : Src.AsStructs())
		{
			LoadStruct(It, Item);
			It += Stride;
		}
	}

	inline void* AddItem(FOverridableScriptArray& Dst, int32& OutIdx) const
	{
		OutIdx = Dst.AddUninitialized(Layout);
		void* Out = Dst.GetData(OutIdx, Layout);
		Inner.InitItem(Out);
		return Out;
	}

	// Load structs into non-empty array
	inline void InsertNonEmpty(FOverridableScriptArray& Dst, FRangeLoadView Src) const
	{
		// Written to avoid FProperty::CopyCompleteValue_InContainer dependency
		// Items are loaded directly into a temporary entry at the end of the array 
		// and then removed if a duplicate existed.

		int32 TmpIdx;
		void* Tmp = AddItem(Dst, /* out */ TmpIdx);
		for (FStructLoadView Item : Src.AsStructs())
		{
			LoadStruct(Tmp, Item);
			int32 FoundIdx = FindIndex(Dst, Tmp);

			if (FoundIdx == TmpIdx)
			{
				Tmp = AddItem(Dst, /* out */ TmpIdx);
			}
		}

		Inner.DestroyItem(Tmp);
		Dst.RemoveAtUninitialized(Layout, TmpIdx);

		check(Dst.NumUnchecked() >= Src.Num());
	}

	inline void Remove(FOverridableScriptArray& Dst, FRangeLoadView Src) const
	{
		TArray<uint8, TInlineAllocator<64>> Buffer;
		Buffer.SetNumUninitialized(Inner.Size);
		Inner.InitItem(Buffer.GetData());
		void* Tmp = Buffer.GetData();

		for (FStructLoadView Item : Src.AsStructs())
		{
			LoadStruct(Tmp, Item);
			RemoveElem(Dst, Tmp);
		}

		Inner.DestroyItem(Tmp);
	}

	inline void DestroyRemoved(FOverridableScriptArray& Dst, int32 Idx) const
	{
		checkSlow(Idx < Dst.GetMaxIndex());
		checkSlow(!Dst.IsValidIndex(Idx));
		void* Elem = Dst.GetData(Idx, Layout);
		Inner.DestroyItem(Elem);
	}
};

// Utility that allows us to forward declare FOverridableArrayPropertyBinding.
inline FOverridableArrayPropertyBinding* CreateAndLeakOverridableArrayBinding(FArrayProperty* Property, FMemberBinding InnerBinding, 
																			  FBindId BindId, FDeclId DeclId)
{
	FOverridableArrayPropertyBinding* New = new FOverridableArrayPropertyBinding(Property, InnerBinding);
	GUE.Customs.BindStruct(BindId, *New, New->DeclareCustom(DeclId), {});
	return New;
}

////////////////////////////////////////////////////////////////////////////////////////////////

// @pre Binding.InnermostId isn't lowered
static FMemberSpec ToSpec(FMemberBinding Binding)
{
	FMemberSpec Out = Binding.InnermostType.IsLeaf() 
		? FMemberSpec(ToLeafType(Binding.InnermostType.AsLeaf()), ToOptionalEnum(Binding.InnermostId))
		: FMemberSpec(Binding.InnermostId.Get().AsStructDeclId());

	for (FRangeBinding RangeBinding : ReverseIterate(Binding.RangeBindings))
	{
		Out.RangeWrap(RangeBinding.GetSizeType());
	}

	return Out;
}

class FPairBindings
{
	struct FPair
	{
		FMemberBinding KV[2];
		friend uint32 GetTypeHash(FPair In) { return HashCombineFast(HashSkipOffset(In.KV[0]), HashSkipOffset(In.KV[1])); };
		inline bool operator==(const FPair& O) const { return EqSkipOffset(KV[0], O.KV[0]) && EqSkipOffset(KV[1], O.KV[1]); }
	};

	TMap<FPair, FBindId> Bindings;

	inline FBindId BindImpl(FPair Pair)
	{
		check(Pair.KV[0].Offset == 0 && Pair.KV[1].Offset > 0);
		if (const FBindId* BindId = Bindings.Find(Pair))
		{
			return *BindId;
		}

		// Index names, can be optimized by checking if KeyParam / BindParam IsLowered()
		// or better checking if either one of them is a range
		FBothType KeyParam = Pair.KV[0].IndexParameterName(GUE.Names);
		FBothType ValueParam = Pair.KV[1].IndexParameterName(GUE.Names);
		FType BindParams[2] = { KeyParam.BindType, ValueParam.BindType };
		FType DeclParams[2] = { KeyParam.DeclType, ValueParam.DeclType };
		FType BindType = { GUE.Scopes.CoreUObject, FTypenameId(GUE.Names.MakeParametricTypeId(GUE.Typenames.Pair, BindParams)) };
		FType DeclType = { GUE.Scopes.CoreUObject, FTypenameId(GUE.Names.MakeParametricTypeId(GUE.Typenames.Pair, DeclParams)) };
		FBindId BindId = GUE.Names.IndexBindId(BindType);
		FDeclId DeclId = GUE.Names.IndexDeclId(DeclType);

		FMemberId Members[2] = { GUE.Members.Key, GUE.Members.Value };
		FMemberSpec Specs[2] = { ToSpec(Pair.KV[0]), ToSpec(Pair.KV[1]) };

		// Todo: Ownership / memory leak
		GUE.Schemas.BindStruct(BindId, Pair.KV, {DeclId, NoId, 0, EMemberPresence::RequireAll, Members, {Specs}});

		Bindings.Emplace(Pair, BindId);

		return BindId;
	}
public:

	inline FMemberBinding Bind(FMemberBinding Key, FMemberBinding Value)
	{
		FMemberBinding Out(0);
		Out.InnermostId = FInnerId(BindImpl(FPair{Key, Value}));
		Out.InnermostType = DefaultStructBindType;
		return Out;
	}
};
static FPairBindings GPairs;

//////////////////////////////////////////////////////////////////////////////////////////////

inline void ReadBoolOptionalItem(FSaveRangeContext& Ctx, uint32 ItemSize)
{
	checkf((&Ctx.Request.GetRange<uint8>())[ItemSize] <= uint8(true),
		TEXT("Non-intrusive TOptional::bIsSet should be true or false, but byte at offset %d was %d"), ItemSize, (&Ctx.Request.GetRange<uint8>())[ItemSize]);
	bool bSet = (&Ctx.Request.GetRange<bool>())[ItemSize];
	Ctx.Items.SetAll(bSet ? Ctx.Request.Range : nullptr, uint64(bSet), ItemSize);
}

inline void MakeBoolOptionalItem(FLoadRangeContext& Ctx, uint32 ItemSize)
{
	check((&Ctx.Request.GetRange<uint8>())[ItemSize] <= 1);
	bool& bSet = (&Ctx.Request.GetRange<bool>())[ItemSize];
	bSet = Ctx.Request.NumTotal() > 0;
	Ctx.Items.Set(&Ctx.Request.GetRange<uint8>(), uint64(bSet), ItemSize);
}

static constexpr std::string_view TrivialOptionalName = "TrivialOptional";
template<uint32 ItemSize>
struct TTrivialOptionalBinding : IItemRangeBinding
{
	TTrivialOptionalBinding() : IItemRangeBinding(GUE.Names.IndexRangeBindName(ToAnsiView(Concat<TrivialOptionalName, HexString<ItemSize>>))) {}

	virtual void ReadItems(FSaveRangeContext& Ctx) const override { ReadBoolOptionalItem(Ctx, ItemSize); }
	virtual void MakeItems(FLoadRangeContext& Ctx) const override { MakeBoolOptionalItem(Ctx, ItemSize); }
};

struct FTrivialOptionalBinding : IItemRangeBinding
{
	const uint32 ItemSize;
	explicit FTrivialOptionalBinding(uint32 Size) : IItemRangeBinding(GUE.Typenames.TrivialOptional), ItemSize(Size) {}

	virtual void ReadItems(FSaveRangeContext& Ctx) const override { ReadBoolOptionalItem(Ctx, ItemSize); }
	virtual void MakeItems(FLoadRangeContext& Ctx) const override { MakeBoolOptionalItem(Ctx, ItemSize); }
};

struct FOptionalBindingBase : IItemRangeBinding
{
	const FProperty*	Inner;
	const uint32		ItemSize;
	const bool			bConstructor;
	const bool			bDestructor;
	
	explicit FOptionalBindingBase(const FProperty* In, FConcreteTypenameId BindName)
	: IItemRangeBinding(BindName)
	, Inner(In)
	, ItemSize(In->GetElementSize())
	, bConstructor(HasConstructor(In))
	, bDestructor(HasDestructor(In))
	{}

	void InitItem(void* Value) const
	{
		if (bConstructor)
		{
			ConstructValue(Inner, Value);
		}
		else
		{
			FMemory::Memzero(Value, ItemSize);
		}
	}
};

struct FIntrusiveOptionalBinding : FOptionalBindingBase
{
	explicit FIntrusiveOptionalBinding(const FProperty* In)
	: FOptionalBindingBase(In, GUE.Typenames.IntrusiveOptional)
	{}
	
	virtual void ReadItems(FSaveRangeContext& Ctx) const override
	{
		bool bSet = Inner->IsIntrusiveOptionalValueSet(Ctx.Request.Range);
		Ctx.Items.SetAll(bSet ? Ctx.Request.Range : nullptr, uint64(bSet), ItemSize);
	}

	virtual void MakeItems(FLoadRangeContext& Ctx) const override
	{
		uint8* Value = &Ctx.Request.GetRange<uint8>();
		Inner->ClearIntrusiveOptionalValue(Value);

		if (Ctx.Request.NumTotal() > 0)
		{
			InitItem(Value);
			Ctx.Items.Set(Value, 1, ItemSize);
		}
		else
		{
			Ctx.Items.Set(nullptr, 0, ItemSize);
		}
	}
};

struct FNonIntrusiveOptionalBinding : FOptionalBindingBase
{
	explicit FNonIntrusiveOptionalBinding(const FProperty* In)
	: FOptionalBindingBase(In, GUE.Typenames.NonIntrusiveOptional)
	{}

	virtual void ReadItems(FSaveRangeContext& Ctx) const override
	{
		ReadBoolOptionalItem(Ctx, ItemSize);
	}

	virtual void MakeItems(FLoadRangeContext& Ctx) const override
	{
		uint8* Value = &Ctx.Request.GetRange<uint8>();
		bool& bSet = reinterpret_cast<bool&>(Value[ItemSize]);
		if (bDestructor && bSet)
		{
			DestroyValue(Inner, Value);
		}

		bSet = Ctx.Request.NumTotal() > 0;
		Ctx.Items.Set(bSet ? Value : nullptr , 1, ItemSize);
		if (bSet)
		{
			InitItem(Value);	
		}
	}
};

class FOptionalBindings
{
	TTrivialOptionalBinding<1> Trivial1;
	TTrivialOptionalBinding<2> Trivial2;
	TTrivialOptionalBinding<4> Trivial4;
	TTrivialOptionalBinding<8> Trivial8;
	TTrivialOptionalBinding<12> Trivial12;
	TTrivialOptionalBinding<16> Trivial16;
	TTrivialOptionalBinding<24> Trivial24;
	TTrivialOptionalBinding<32> Trivial32;

	TMap<FParameterBinding, FRangeBinding>	NormalBindings;
	TMap<FParameterBinding, FRangeBinding>	IntrusiveBindings;

	const IItemRangeBinding* BindNew(FProperty* Inner)
	{
		if (HasConstructor(Inner) || HasDestructor(Inner))
		{
			return new FNonIntrusiveOptionalBinding(Inner);
		}
		else switch (Inner->GetElementSize())
		{
			case 1:		return &Trivial1;
			case 2:		return &Trivial2;
			case 4:		return &Trivial4;
			case 8:		return &Trivial8;
			case 12:	return &Trivial12;
			case 16:	return &Trivial16;
			case 24:	return &Trivial24;
			case 32:	return &Trivial32;
			default:	return new FTrivialOptionalBinding(Inner->GetElementSize());
		}
	}

public:
	FRangeBinding Bind(FProperty* Inner, FMemberBinding Key)
	{
		check(Key.Offset == 0);
		
		bool bIntrusive = Inner->HasIntrusiveUnsetOptionalState();
		TMap<FParameterBinding, FRangeBinding>& Bindings = bIntrusive ? IntrusiveBindings : NormalBindings;
		if (const FRangeBinding* Binding = Bindings.Find(FParameterBinding(Key)))
		{
			return *Binding;
		}

		// Todo: Ownership / memory leak
		const IItemRangeBinding* Out = bIntrusive ? new FIntrusiveOptionalBinding(Inner) : BindNew(Inner);
		if (Key.RangeBindings.Num())
		{
			Key.RangeBindings = GRanges.Clone(Key.RangeBindings);
		}
		return Bindings.Emplace(Key, FRangeBinding(*Out, ESizeType::Uni));
	}
};

static FOptionalBindings GOptionals;

//////////////////////////////////////////////////////////////////////////////////////////////

// Temporary unsafe helper for Verse::FNativeString that uses a FUtf8String wrapped in a TSharedPtr
struct FVerseStringBinding : TStringBinding<FUtf8String>
{
	using TStringBinding<FUtf8String>::TStringBinding;
	
	virtual void SaveLeaves(const void* Range, FLeafRangeAllocator& Out) const override
	{
		const void** Ptr = reinterpret_cast<const void**>(const_cast<void*>(Range));
		TStringBinding<FUtf8String>::SaveLeaves(*Ptr, Out);
	}

	virtual void LoadLeaves(void* Range, FLeafRangeLoadView Items) const override
	{
		FUtf8String Str;
		TStringBinding<FUtf8String>::LoadLeaves(&Str, Items);
		*static_cast<Verse::FNativeString*>(Range) = MoveTemp(Str);
	}

	virtual bool DiffLeaves(const void* RangeA, const void* RangeB) const override
	{
		const void** A = reinterpret_cast<const void**>(const_cast<void*>(RangeA));
		const void** B = reinterpret_cast<const void**>(const_cast<void*>(RangeB));
		return TStringBinding<FUtf8String>::DiffLeaves(*A, *B);
	}
};

struct FStringBindings
{
	TStringBinding<FString>			TCharInstance{GUE.Typenames.String};
	TStringBinding<FUtf8String>		Utf8Instance{GUE.Typenames.Utf8String};
	TStringBinding<FAnsiString>		AnsiInstance{GUE.Typenames.AnsiString};
	FVerseStringBinding				VerseInstance{GUE.Typenames.VerseString};
	FRangeBinding					TChar{TCharInstance, DefaultRangeMax};
	FRangeBinding					Utf8{Utf8Instance, DefaultRangeMax};
	FRangeBinding					Ansi{AnsiInstance, DefaultRangeMax};
	FRangeBinding					Verse{VerseInstance, DefaultRangeMax};

	inline const FRangeBinding&	SelectBinding(uint64 CastFlags) const
	{
		switch (CastFlags & StringMask)
		{
			case CASTCLASS_FStrProperty:			return TChar;
			case CASTCLASS_FUtf8StrProperty:		return Utf8;
			case CASTCLASS_FAnsiStrProperty:		return Ansi;
			case CASTCLASS_FVerseStringProperty:	return Verse;
			default:								break;
		}
		check(FMath::CountBits64(CastFlags & StringMask) == 1);
		check(false);
		return TChar;
	}

	FMemberBinding Bind(FProperty* Property, uint64 CastFlags) const
	{
		const FRangeBinding& Binding = SelectBinding(CastFlags);

		FMemberBinding Out(Property->GetOffset_ForInternal());
		Out.InnermostType = FMemberBindType(ReflectLeaf<char8_t>);
		Out.RangeBindings = MakeArrayView(&Binding, 1);
		return Out;
	}
};
static const FStringBindings GStrings; // static init dependency after GUE

////////////////////////////////////////////////////////////////////////////////////////////////

struct FStaticArrayBinding : IItemRangeBinding
{
	uint32 Num;
	uint32 Stride;

	FStaticArrayBinding(uint32 InNum, uint32 InStride)
	: IItemRangeBinding(GUE.Typenames.StaticArray)
	, Num(InNum)
	, Stride(InStride)
	{}

	virtual void				ReadItems(FSaveRangeContext& Ctx) const override	{ Ctx.Items.SetAll(Ctx.Request.Range, Num, Stride); }
	virtual void				MakeItems(FLoadRangeContext& Ctx) const override	{ Ctx.Items.Set(&Ctx.Request.GetRange<uint8>(), Num, Stride); }
};

////////////////////////////////////////////////////////////////////////////////////////////////

// Serialize property-less UObject and UScriptStructs as their super class.
//
// E.g. FVector_NetQuantize10 is a pure runtime abstraction serialized as FVector.
//		FAttenuationSubmixSendSettings is just a FSoundSubmixSendInfoBase but has a different 
//		default constructor that matters in sparse delta serialization.
//		UObjects are never instantiated during serialization so can be safely simplified.
//
// These heuristics might need more tuning
const UStruct* SkipEmptyBases(const UStruct* In)
{
	const UStruct* FirstOwner = In->PropertyLink ? In->PropertyLink->GetOwnerChecked<UStruct>() : In;
	if (In != FirstOwner)
	{
		if (const UScriptStruct* Struct = Cast<const UScriptStruct>(In))
		{
			if (!(Struct->StructFlags & STRUCT_ZeroConstructor))
			{
				return In;
			}
		}

		return FirstOwner;
	}
	return In;
}

////////////////////////////////////////////////////////////////////////////////////////////////

class FPropertyBinder
{
public:
	FPropertyBinder(FBindId Id, EMemberPresence InOccupancy)
	: Owner(Id)
	, Occupancy(InOccupancy)
	{}

	void											BindSuper(FDeclId SuperId);
	void											BindMember(FProperty* Property);
	void											Interlace(TConstArrayView<FAuxMember> Members);
	TConstArrayView<FMemberBinding>					GetMembers() const { return Members; }
	TConstArrayView<EPropertyFlags>					GetMembersFlags() const { return MemberFlags; }

	FStructDeclarationPtr							Declare() const;	

	// Only true for noexport UScriptStruct with STRUCT_Immutable | STRUCT_Atomic flags
	bool											IsDense() { return Occupancy == EMemberPresence::RequireAll; }

private:
	const FBindId									Owner;
	FOptionalDeclId									Super;
	TOptional<FScopeId>								OwnerScope;
	const EMemberPresence							Occupancy;
	TArray<FMemberId, TInlineAllocator<64>>			Names;
	TArray<FMemberBinding, TInlineAllocator<64>>	Members;
	TArray<FMemberSpec, TInlineAllocator<64>>		Specs;
	TArray<EPropertyFlags, TInlineAllocator<64>>	MemberFlags;

	// BPVM only?
	const FName										VerseFunctionProperty{"VerseFunctionProperty"};
	const FName										VerseDynamicProperty{"VerseDynamicProperty"};
	const FName										ReferenceProperty{"ReferenceProperty"}; // Verse reference + FProperty*

	static FMemberBinding Todo(FProperty* Property)
	{
		return FMemberBinding(Property->GetOffset_ForInternal());
	}

	FScopeId GetOwnerScope()
	{
		if (!OwnerScope)
		{
			FType OwnerType = GUE.Names.Resolve(Owner);
			OwnerScope = GUE.Names.NestFlatScope(OwnerType.Scope, {OwnerType.Name.AsConcrete().Id});
		}

		return OwnerScope.GetValue();
	}

	inline FMemberBinding BindAsRange(FProperty* Property, FRangeBinding RangeBinding, FMemberBinding Inner)
	{
		if (Inner.InnermostType.IsLeaf() && Inner.InnermostType.AsLeaf().Bind.Type == ELeafBindType::BitfieldBool)
		{
			UE_LOGFMT(LogPlainPropsUObject, Warning,
				"Property '{Property}' in '{Owner}' is a '{Container}' of bitfield bools, which make no sense. Binding as range of bools.", 
				Property->GetFName(), *Property->GetOwnerStruct()->GetPathName(), GUE.Debug.Print(RangeBinding.GetBindName()));
			Inner.InnermostType = FMemberBindType(ReflectArithmetic<bool>);
		}

		FMemberBinding Out(Property->GetOffset_ForInternal());
		Out.InnermostType = Inner.InnermostType;
		Out.InnermostId = Inner.InnermostId;
		Out.RangeBindings = GScratchRanges.Clone(RangeBinding, Inner.RangeBindings);
		return Out;
	}

	inline FMemberBinding BindAsStruct(FProperty* Property, FBindId Id)
	{
		FMemberBinding Out(Property->GetOffset_ForInternal());
		Out.InnermostId = FInnerId(Id);
		Out.InnermostType = DefaultStructBindType;
		return Out;
	}

	inline FMemberBinding BindAsStruct(FProperty* Property, UStruct* Struct)
	{
		FType Type = IndexType(SkipEmptyBases(Struct));
		return BindAsStruct(Property, GUE.Names.IndexBindId(Type));
	}

	inline static FBitfieldBoolBindType MakeBitfieldBool(uint8 BitIdx)
	{
		return {EMemberKind::Leaf, ELeafBindType::BitfieldBool, BitIdx};
	}
	
	inline FMemberBinding BindBool(FBoolProperty* Property)
	{
		check(Property->GetByteOffset() == 0);
		FMemberBinding Out(Property->GetOffset_ForInternal());
		uint8 BitIdx = static_cast<uint8>(FMath::FloorLog2NonZero(Property->GetFieldMask()));
		FLeafBindType Type = Property->IsNativeBool()	? FLeafBindType(ELeafBindType::Bool, ELeafWidth::B8)
														: FLeafBindType(MakeBitfieldBool(BitIdx));
		Out.InnermostType = FMemberBindType(Type);
		return Out;
	}

	inline FMemberBinding BindEnum(FEnumProperty* Property)
	{
		FMemberBinding Out(Property->GetOffset_ForInternal());
		FEnumId Id = GUE.Names.IndexEnum(IndexType(Property->GetEnum()));
		Out.InnermostId = FInnerId(Id);
		FUnpackedLeafType Leaf = {ELeafType::Enum, WidthOfEnum(Property->GetEnum())};
		Out.InnermostType = FMemberBindType(Leaf);
		return Out;
	}

	inline FLeafBindType BindByte(FByteProperty* Property, FOptionalInnerId& OutEnumId)
	{
		if (const UEnum* Enum = Property->GetIntPropertyEnum())
		{
			OutEnumId = FInnerId(GUE.Names.IndexEnum(IndexType(Enum)));
			return FLeafBindType(ELeafBindType::Enum, ELeafWidth::B8);
		}
		return FLeafBindType(ELeafBindType::IntU, ELeafWidth::B8);
	}

	inline FMemberBinding BindNumeric(FNumericProperty* Property, uint64 Flags)
	{
		FMemberBinding Out(Property->GetOffset_ForInternal());
		bool bFloat = HasAny<CASTCLASS_FFloatProperty | CASTCLASS_FDoubleProperty>(Flags);
		bool bIntS = HasAny<CASTCLASS_FInt8Property | CASTCLASS_FInt16Property | CASTCLASS_FIntProperty | CASTCLASS_FInt64Property>(Flags);
		if (HasAny<CASTCLASS_FByteProperty>(Flags))
		{
			FLeafBindType Leaf = BindByte(static_cast<FByteProperty*>(Property), /* out enum */ Out.InnermostId);
			Out.InnermostType = FMemberBindType(Leaf);
		}
		else
		{
			ELeafType Type = bFloat ? ELeafType::Float : (bIntS ? ELeafType::IntS : ELeafType::IntU);
			FUnpackedLeafType Leaf = {Type, WidthOf(Property->GetElementSize())};
			Out.InnermostType = FMemberBindType(Leaf);	
		}
		
		return Out;
	}
	
	inline FMemberBinding BindArray(FArrayProperty* Property)
	{
		FMemberBinding Inner = BindSingleProperty(Property->Inner);

		const bool bIsOverridable = Property->HasAnyPropertyFlags(CPF_ExperimentalOverridableLogic);

		if (bIsOverridable)
		{
			return BindAsStruct(Property, GCachedArrayBindings.BindOverridableArray(Property, Inner));
		}
		else
		{
			return BindAsRange(Property, AllocateArrayBinding(Property), Inner);
		}
	}
	inline FMemberBinding BindMap(FMapProperty* Property)
	{
		FMemberBinding Key = BindSingleProperty(Property->KeyProp);
		FMemberBinding Value = BindSingleProperty(Property->ValueProp);
		FMemberBinding Pair = GPairs.Bind(Key, Value);
		FBindId PairId = Pair.InnermostId.Get().AsStructBindId();

		bool bFreezable = EnumHasAnyFlags(Property->MapFlags, EMapPropertyFlags::UsesMemoryImageAllocator);
		return bFreezable	? BindAsRange(Property, GMaps.BindFreezable(Property, PairId, {Key, Value, Pair}), Pair)
							: BindAsStruct(Property, GMaps.BindNormal(Property, PairId, {Key, Value, Pair}));
	}
	inline FMemberBinding BindSet(FSetProperty* Property)
	{
		FMemberBinding Elem = BindSingleProperty(Property->ElementProp);
		return BindAsStruct(Property, GSets.Bind(Property, Elem));
	}
	inline FMemberBinding BindOptional(FOptionalProperty* Property)
	{
		FMemberBinding Inner = BindSingleProperty(Property->GetValueProperty());
		return BindAsRange(Property, GOptionals.Bind(Property->GetValueProperty(), Inner), Inner);
	}

	#if WITH_VERSE_VM
	inline FMemberBinding BindVValue(FVValueProperty* Property) { return Todo(Property); }
	inline FMemberBinding BindVRestValue(FVRestValueProperty* Property) { return Todo(Property); }
	#endif

	FMemberBinding BindSingleProperty(FProperty* Property)
	{
		FName PropertyTypename = Property->GetClass()->GetFName();
		uint64 Flags = Property->GetCastFlags();
		if (HasAny<LeafMask>(Flags))
		{
			if (HasAny<CASTCLASS_FNumericProperty>(Flags))
			{
				return BindNumeric(static_cast<FNumericProperty*>(Property), Flags);
			}
			return HasAny<CASTCLASS_FEnumProperty>(Flags)
				? BindEnum(static_cast<FEnumProperty*>(Property))
				: BindBool(static_cast<FBoolProperty*>(Property));
		}
		else if (HasAny<CommonStructMask>(Flags))
		{
			return BindAsStruct(Property, FlagsToCommonBindId(Flags & CommonStructMask));
		}
		else if (HasAny<CASTCLASS_FStructProperty>(Flags))
		{
			return BindAsStruct(Property, static_cast<FStructProperty*>(Property)->Struct);
		}
		else if (HasAny<ContainerMask>(Flags))
		{
			if (HasAny<CASTCLASS_FArrayProperty>(Flags))
			{
				return BindArray(static_cast<FArrayProperty*>(Property));
			}
			if (HasAny<CASTCLASS_FMapProperty>(Flags))
			{
				return BindMap(static_cast<FMapProperty*>(Property));
			}
			return HasAny<CASTCLASS_FSetProperty>(Flags)
				? BindSet(static_cast<FSetProperty*>(Property))
				: BindOptional(static_cast<FOptionalProperty*>(Property));
		}
		else if (HasAny<StringMask>(Flags))
		{
			return GStrings.Bind(Property, Flags);
		}
		else if (HasAny<MiscMask>(Flags))
		{
			FBindId BindId = HasAny<CASTCLASS_FInterfaceProperty>(Flags)
				? BindInterface(static_cast<FInterfaceProperty*>(Property))
				: BindSparseDelegate(Owner, static_cast<FMulticastSparseDelegateProperty*>(Property));
			return BindAsStruct(Property, BindId);
		}
#if WITH_VERSE_VM
		else if (HasAny<CASTCLASS_FVValueProperty | CASTCLASS_FVRestValueProperty>(Flags))
		{
			return HasAny<CASTCLASS_FVValueProperty>(Flags)
				? BindVValue(static_cast<FVValueProperty*>(Property))
				: BindVRestValue(static_cast<FVRestValueProperty*>(Property)); 
		}
#else // Verse BPVM
		else
		{
			if (PropertyTypename == VerseFunctionProperty) // FVerseFunctionProperty
			{
				return BindAsStruct(Property, GUE.Structs.VerseFunction);
			}
			else if (PropertyTypename == VerseDynamicProperty) // FVerseDynamicProperty
			{
				return BindAsStruct(Property, GUE.Structs.DynamicallyTypedValue);
			}
			else if (PropertyTypename == ReferenceProperty) // FReferenceProperty
			{
				return BindAsStruct(Property, GUE.Structs.ReferencePropertyValue);
			}
		}
#endif

		checkf(false, TEXT("Unrecognized class cast flags %llx in %s %s"), Flags, *PropertyTypename.ToString(), *Property->GetNameCPP());
		return FMemberBinding(Property->GetOffset_ForInternal());
	}

	FType MakeStaticArrayTypename(FName PropertyName)
	{
		return { GetOwnerScope(), GUE.Names.MakeTypename(PropertyName) };
	}

	FMemberBinding BindProperty(FProperty* Property)
	{
		FMemberBinding Out = BindSingleProperty(Property);
		if (Property->GetSize() == Property->GetElementSize())
		{
			return Out;
		}

		// Bind static array
		uint32 TotalSize = static_cast<uint32>(Property->GetSize());
		uint32 ElementSize = static_cast<uint32>(Property->GetElementSize());
		uint32 ArrayDim = TotalSize / ElementSize;
		check(ArrayDim * ElementSize == TotalSize);
		if (Occupancy == EMemberPresence::RequireAll || ArrayDim > FStructDeclaration::MaxMembers)
		{
			// Create range binding that isn't delta-serializable
			//
			// Could generate nested numeral structs instead. Unsure if automatic
			// per-element delta serialization for massive arrays is desirable.
			//
			// To delta-serialize massive arrays, custom-bind the owning struct
			// and implement delta serialization manually

			// Todo: Ownership / memory leak
			const FStaticArrayBinding& ItemBinding = *new FStaticArrayBinding(ArrayDim, ElementSize);
			ESizeType SizeType = ArrayDim < 256 ? ESizeType::U8 : ((ArrayDim < 65536) ? ESizeType::U16 : ESizeType::U32);
			Out.RangeBindings =	GScratchRanges.Clone(FRangeBinding(ItemBinding, SizeType), Out.RangeBindings);
		}
		else
		{
			// Create struct binding to allow delta serialization
			FType StaticArrayType = MakeStaticArrayTypename(Property->GetFName());
			FDualStructId StaticArrayId{GUE.Names.IndexStruct(StaticArrayType)};

			TArray<FMemberBinding, TInlineAllocator<64>> ElemBindings;			
			ElemBindings.Init(Out, ArrayDim);
			uint64 Offset = 0;
			for (FMemberBinding& Element : ElemBindings)
			{
				Element.Offset = Offset;
				Offset += ElementSize;
			}
			
			TConstArrayView<FMemberId> Numerals = GUE.Numerals.MakeRange(IntCastChecked<uint16>(ArrayDim));
			TArray<FMemberSpec, TInlineAllocator<64>> ElemSpecs;
			ElemSpecs.Init(ToSpec(Out), ArrayDim);
			FStructSpec Spec = {StaticArrayId, NoId, 0, EMemberPresence::AllowSparse, Numerals, { ElemSpecs }};

			// Todo: Ownership
			GUE.Schemas.BindStruct(StaticArrayId, ElemBindings, Spec);
			
			Out.InnermostType = DefaultStructBindType;
			Out.InnermostId = FInnerId(StaticArrayId);
			Out.RangeBindings = {};
		}
	
		return Out;	
	}
};

void FPropertyBinder::BindSuper(FDeclId SuperId)
{
	check(!IsDense());

	Super = SuperId;

	FMemberBinding Member;
	Member.InnermostType = SuperStructBindType;
	Member.InnermostId = FInnerId(SuperId);
	Members.Emplace(Member);
}

void FPropertyBinder::BindMember(FProperty* Property)
{
	Members.Emplace(BindProperty(Property));
	Specs.Emplace(ToSpec(Members.Last()));
	Names.Emplace(GUE.Names.NameMember(Property->GetFName()));
	MemberFlags.Emplace(Property->PropertyFlags);
}

FStructDeclarationPtr FPropertyBinder::Declare() const
{
	return PlainProps::Declare({LowerCast(Owner), Super, 0, Occupancy, Names, {Specs}});
}

void FMetadataBindings::BindMetadata(FBindId Id, TConstArrayView<EPropertyFlags> Members)
{
	if (Id.Idx >= static_cast<uint32>(Metadatas.Num()))
	{
		Metadatas.SetNum(Id.Idx + 1);
	}
	checkf(!Metadatas[Id.Idx].IsValid(), TEXT("'%s' metadata already bound"), *Debug.Print(Id));
	Metadatas[Id.Idx] = FMemberMetadata { .Flags = TArray<EPropertyFlags>(Members) };
}

void FMetadataBindings::DropMetadata(FBindId Id)
{
	checkf(Id.Idx < (uint32)Metadatas.Num(), TEXT("'%s' is unbound"), *Debug.Print(Id));
	Metadatas[Id.Idx].Reset();
}

TConstArrayView<EPropertyFlags> FMetadataBindings::GetMemberFlags(FBindId Id) const
{
	checkf(Id.Idx < (uint32)Metadatas.Num(), TEXT("'%s' is unbound"), *Debug.Print(Id));
	return Metadatas[Id.Idx].Flags;
}

////////////////////////////////////////////////////////////////////////////////////////////////

// Meta version with similar structure as FPropertyBinder::BindSingleProperty
FDualStructId IndexPropertyMeta(const FField* Property)
{
	FName MemberTypename = Property->GetFName();
	uint64 Flags = Property->GetCastFlags();
	if (HasAny<LeafMask>(Flags))
	{
		if (HasAny<CASTCLASS_FNumericProperty>(Flags))
		{
			if (HasAny<CASTCLASS_FByteProperty>(Flags))
			{
				return GUE.Properties.ByteProperty;
			}

			return GUE.Properties.Property;
		}
		return HasAny<CASTCLASS_FEnumProperty>(Flags)
			? GUE.Properties.EnumProperty
			: GUE.Properties.BoolProperty;
	}
	else if (HasAny<CASTCLASS_FStructProperty>(Flags))
	{
		return GUE.Properties.StructProperty;
	}
	else if (HasAny<CASTCLASS_FClassProperty>(Flags))
	{
		return GUE.Properties.ClassProperty;
	}
	else if (HasAny<CASTCLASS_FObjectPropertyBase>(Flags))
	{
		return GUE.Properties.ObjectPropertyBase;
	}
	else if (HasAny<ContainerMask>(Flags))
	{
		if (HasAny<CASTCLASS_FArrayProperty>(Flags))
		{
			return GUE.Properties.ArrayProperty;
		}
		if (HasAny<CASTCLASS_FMapProperty>(Flags))
		{
			return GUE.Properties.MapProperty;
		}
		return HasAny<CASTCLASS_FSetProperty>(Flags)
			? GUE.Properties.SetProperty
			: GUE.Properties.OptionalProperty;
	}
	// Todo: check for complete coverage after implementing all missing FProperty bindings.
	// checkf(false, TEXT("Unrecognized class cast flags %llx in %s %s"),
	// Flags, *MemberTypename.ToString(), *Field->GetName());
	return GUE.Properties.Property;
}

////////////////////////////////////////////////////////////////////////////////////////////////

static EPropertyFlags GDontBind;

static bool ShouldBind(const FProperty* Property)
{
	if (Property->PropertyFlags & GDontBind)
	{
		return false;
	}

	if (HasAny<CASTCLASS_FStructProperty>(Property->GetCastFlags()))
	{
		return ShouldBind(static_cast<const FStructProperty*>(Property)->Struct);
	}

	return true;
}

bool ShouldBind(const UStruct* Struct)
{
	for (FProperty* Property = Struct->PropertyLink; Property; Property = Property->PropertyLinkNext)
	{
		if (ShouldBind(Property))
		{
			return true;
		}
	}
	if (Struct->HasAnyCastFlags(CASTCLASS_UScriptStruct))
	{
		return !!(static_cast<const UScriptStruct*>(Struct)->StructFlags & STRUCT_SerializeNative);
	}
#if PP_OVERRIDE_OBJECT_LOADING
	// Just as we need to include custom serialize structs without properties for BindMembers and GetSuperToBind,
	// we also need to include all custom bound structs.
	// temp: for now it is enough to check for the global common structs, but re-indexing every time is wasteful.
	// todo: optimize and simplify and check for structs where the custom binding may not exist yet,
	// probably needs a type trait...
	FBindId Id = GUE.Names.IndexBindId(IndexType(Struct));
	if (GUE.Customs.FindStruct(Id))
	{
		return true;
	}
#endif
	return false;
}

inline void BindMembers(FPropertyBinder& Out, const UStruct* Struct)
{
	for (FProperty* It = Struct->PropertyLink; It && It->GetOwner<UStruct>() == Struct; It = It->PropertyLinkNext)
	{
		if (ShouldBind(It))
		{
			Out.BindMember(It);
		}
	}
}

static void BindSuperMembers(FPropertyBinder& Out, const UStruct* Struct)
{
	if (const UStruct* Super = Struct->GetInheritanceSuper())
	{
		BindSuperMembers(Out, Super);
		if (ShouldBind(Super))
		{
			BindMembers(Out, Super);
		}
	}
}

static const UStruct* GetSuperToBind(const UStruct* Struct)
{
	const UStruct* Super = Struct->GetInheritanceSuper();
	while (Super)
	{
		if (ShouldBind(Super))
		{
			return SkipEmptyBases(Super);
		}
		Super = Super->GetInheritanceSuper();
	}
	return nullptr;
}

void BindStruct(FBindId Id, const UStruct* Struct)
{
	if (GUE.Customs.FindStruct(Id))
	{
		return;
	}

	if (GUE.Schemas.FindStruct(Id))
	{
		return;
	}

	FPropertyBinder Binder(Id, GetOccupancy(Struct));
	if (const UStruct* Super = GetSuperToBind(Struct))
	{
		if (Binder.IsDense())
		{
			// Flatten inheritance chain for dense structs
			for (; Super; Super = GetSuperToBind(Super))
			{
				BindMembers(/* out */ Binder, Super);	
			}
		}
		else
		{
			FDeclId SuperId = GUE.Names.IndexDeclId(IndexType(Super));
			Binder.BindSuper(SuperId);
		}
	}

	BindMembers(/* out */ Binder, Struct);

	GUE.Schemas.BindStruct(Id, Binder.GetMembers(), Binder.Declare());
	GUE.Metadatas.BindMetadata(Id, Binder.GetMembersFlags());
	GUE.DynamicIds.AddStruct(Struct, Id);
	
	// Don't bind CDO defaults, object defaults are passed in from top and objects aren't owned by containers 
	if (Struct->HasAnyCastFlags(CASTCLASS_UScriptStruct))
	{
		GUE.Defaults.Bind(Id, static_cast<const UScriptStruct*>(Struct));
	}
}

static EPropertyFlags GetPropertiesToSkip(EBindMode Mode)
{
	switch (Mode)
	{
	case EBindMode::Source:		return CPF_Transient | CPF_Deprecated | CPF_SkipSerialization;
	case EBindMode::Runtime:	return CPF_Transient | CPF_Deprecated | CPF_SkipSerialization | CPF_EditorOnly;
	default:					return CPF_None;
	}
}

static TArray<FBindId> GetStructsToSkip(EBatchType BatchType)
{
	switch (BatchType)
	{
	case EBatchType::Plain:		return { GUE.Structs.VerseFunction };
	case EBatchType::Linker:	return { GUE.Structs.VerseFunction, GUE.Structs.SoftObjectPath, GUE.Structs.PropertyBag };
	default:					return {};
	}
}

static void InitPropertiesMeta()
{
	GUE.Defaults.BindZeroes(GUE.Properties.Property, sizeof(FProperty), alignof(FProperty));
	GUE.Defaults.BindZeroes(GUE.Properties.EnumProperty, sizeof(FEnumProperty), alignof(FEnumProperty));
	GUE.Defaults.BindZeroes(GUE.Properties.BoolProperty, sizeof(FBoolProperty), alignof(FBoolProperty));
	GUE.Defaults.BindZeroes(GUE.Properties.ByteProperty, sizeof(FByteProperty), alignof(FByteProperty));
	GUE.Defaults.BindZeroes(GUE.Properties.StructProperty, sizeof(FStructProperty), alignof(FStructProperty));
	GUE.Defaults.BindZeroes(GUE.Properties.ClassProperty, sizeof(FClassProperty), alignof(FClassProperty));
	GUE.Defaults.BindZeroes(GUE.Properties.ObjectPropertyBase, sizeof(FObjectPropertyBase), alignof(FObjectPropertyBase));
	GUE.Defaults.BindZeroes(GUE.Properties.ArrayProperty, sizeof(FArrayProperty), alignof(FArrayProperty));
	GUE.Defaults.BindZeroes(GUE.Properties.SetProperty, sizeof(FSetProperty), alignof(FSetProperty));
	GUE.Defaults.BindZeroes(GUE.Properties.MapProperty, sizeof(FMapProperty), alignof(FMapProperty));
	GUE.Defaults.BindZeroes(GUE.Properties.OptionalProperty, sizeof(FOptionalProperty), alignof(FOptionalProperty));
}

static void InitBatchedProperties(EBatchType BatchType)
{
	GUE.Defaults.BindZeroes(GUE.Structs.FieldPath, sizeof(FFieldPath), alignof(FFieldPath));
	GUE.Defaults.BindZeroes(GUE.Structs.Name, sizeof(FName), alignof(FName));
	GUE.Defaults.BindStatic(GUE.Structs.Text, &FText::GetEmpty());
	GUE.Defaults.BindZeroes(GUE.Structs.ClassPtr, sizeof(TSubclassOf<UClass>), alignof(TSubclassOf<UClass>));
	GUE.Defaults.BindZeroes(GUE.Structs.ObjectPtr, sizeof(FObjectPtr), alignof(FObjectPtr));
	GUE.Defaults.BindZeroes(GUE.Structs.WeakObjectPtr, sizeof(FWeakObjectPtr), alignof(FWeakObjectPtr));
	GUE.Defaults.BindZeroes(GUE.Structs.SoftObjectPtr, sizeof(FSoftObjectPtr), alignof(FSoftObjectPtr));
	GUE.Defaults.BindZeroes(GUE.Structs.LazyObjectPtr, sizeof(FLazyObjectPtr), alignof(FLazyObjectPtr));
	if (BatchType == EBatchType::Linker)
	{
		GUE.Defaults.BindZeroes(GUE.Structs.SoftObjectPath, sizeof(FSoftObjectPath), alignof(FSoftObjectPath));
	}
}

void SchemaBindAllTypes(EBindMode Mode, EBatchType BatchType)
{
	InitPropertiesMeta();
	InitBatchedProperties(BatchType);

	GDontBind = GetPropertiesToSkip(Mode);
	
	// Declare all UEnums
	for (TObjectIterator<UEnum> It; It; ++It)
	{
		DeclareEnum(*It);
	}

	// Bind all UScriptStruct/UClass/UFunction
	TArray<FBindId> SkipStructs = GetStructsToSkip(BatchType);
	for (TObjectIterator<UStruct> It; It; ++It)
	{
		const UStruct* Struct = *It;
		FBindId Id = GUE.Names.IndexBindId(IndexType(Struct));
		if (Algo::Find(SkipStructs, Id))
		{
			continue;
		}
		
		// Additional 'IsA' exclusion check (e.g., to exclude UPropertyBag types).
		bool bSkipStruct = false;
		for (const UStruct* Base : UStruct::FConstSuperStructIterator(Struct->GetClass()))
		{
			FBindId BaseId = GUE.Names.IndexBindId(IndexType(Base));
			if (Algo::Find(SkipStructs, BaseId))
			{
				bSkipStruct = true;
				break;
			}
		}

		if (bSkipStruct)
		{
			continue;
		}

		BindStruct(Id, Struct);
#if PP_OVERRIDE_OBJECT_LOADING
		BindStructMeta(GUE.Names.IndexBindId(IndexStructMeta(Struct)), Struct);
#endif

		GScratchRanges.Reset();
	}
	GScratchRanges.Empty();
}

////////////////////////////////////////////////////////////////////////////////////////////////

struct FPlainPropsBindingScope
{
	inline static const FTransform DefaultTransform;

	FPlainPropsBindingScope()
		: Transform()
		, Guid()
		, Color()
		, LinearColor()
		, FieldPath(GUE.Structs.FieldPath)
		, Delegate(GUE.Structs.Delegate)
		, InstancedStruct(GUE.Structs.InstancedStruct)
		, InlineMulticast({ GUE.Structs.MulticastInlineDelegate, GUE.Structs.MulticastDelegate })
		, VerseFunction(GUE.Structs.VerseFunction)
		, DynamicallyTypedValue(GUE.Structs.DynamicallyTypedValue)
		, ReferencePropertyValue(GUE.Structs.ReferencePropertyValue)
#if PP_OVERRIDE_OBJECT_LOADING
		, ClassFlags()
		, FunctionFlags()
		, StructFlags()
		, PropertyFlags()
		, Property(GUE.Properties.Property)
		, EnumProperty(GUE.Properties.EnumProperty)
		, BoolProperty(GUE.Properties.BoolProperty)
		, ByteProperty(GUE.Properties.ByteProperty)
		, StructProperty(GUE.Properties.StructProperty)
		, ClassProperty(GUE.Properties.ClassProperty)
		, ArrayProperty(GUE.Properties.ArrayProperty)
		, SetProperty(GUE.Properties.SetProperty)
		, MapProperty(GUE.Properties.MapProperty)
		, OptionalProperty(GUE.Properties.OptionalProperty)
		, ObjectPropertyBase(GUE.Properties.ObjectPropertyBase)
		, Struct(GUE.Structs.Struct)
		, Class(GUE.Structs.Class)
		, Function(GUE.Structs.Function)
		, ScriptStruct(GUE.Structs.ScriptStruct)
		, ImplementedInterface()
#endif
	{
		GUE.Defaults.BindStatic(Transform.BindId, &DefaultTransform);
		GUE.Defaults.BindZeroes<FGuid>(Guid.BindId);
		GUE.Defaults.BindZeroes<FColor>(Color.BindId);
		GUE.Defaults.BindZeroes<FLinearColor>(LinearColor.BindId);
		GUE.Defaults.BindZeroes<FInstancedStruct>(InstancedStruct.BindId);
	}

	~FPlainPropsBindingScope()
	{
		GUE.Defaults.Drop(Transform.BindId);
		GUE.Defaults.Drop(Guid.BindId);
		GUE.Defaults.Drop(Color.BindId);
		GUE.Defaults.Drop(LinearColor.BindId);
		GUE.Defaults.Drop(InstancedStruct.BindId);
	}

	TScopedStructBinding<FTransform, FDefaultRuntime> Transform;
	TScopedStructBinding<FGuid, FDefaultRuntime> Guid;
	TScopedStructBinding<FColor, FDefaultRuntime> Color;
	TScopedStructBinding<FLinearColor, FDefaultRuntime> LinearColor;
	TScopedStructBinding<FFieldPath, FDefaultRuntime> FieldPath;
	TScopedStructBinding<FScriptDelegate, FDefaultRuntime> Delegate;
	TScopedStructBinding<FInstancedStruct, FDefaultRuntime> InstancedStruct;
	// MulticastDelegate declaration is shared with MulticastSparseDelegate
	TScopedStructBinding<FMulticastScriptDelegate, FDefaultRuntime> InlineMulticast;
	// Verse
	TScopedStructBinding<FVerseFunction, FDefaultRuntime> VerseFunction;
	TScopedStructBinding<::UE::FDynamicallyTypedValue, FDefaultRuntime> DynamicallyTypedValue;
	TScopedStructBinding<FReferencePropertyValue, FDefaultRuntime> ReferencePropertyValue;

#if PP_OVERRIDE_OBJECT_LOADING
	TScopedEnumDeclaration<EClassFlags, EEnumMode::Flag, FDefaultRuntime> ClassFlags;
	TScopedEnumDeclaration<EFunctionFlags, EEnumMode::Flag, FDefaultRuntime> FunctionFlags;
	TScopedEnumDeclaration<EStructFlags, EEnumMode::Flag, FDefaultRuntime> StructFlags;
	TScopedEnumDeclaration<EPropertyFlags, EEnumMode::Flag, FDefaultRuntime> PropertyFlags;
	TScopedStructBinding<FProperty, FDefaultRuntime> Property;
	TScopedStructBinding<FEnumProperty, FDefaultRuntime> EnumProperty;
	TScopedStructBinding<FBoolProperty, FDefaultRuntime> BoolProperty;
	TScopedStructBinding<FByteProperty, FDefaultRuntime> ByteProperty;
	TScopedStructBinding<FStructProperty, FDefaultRuntime> StructProperty;
	TScopedStructBinding<FClassProperty, FDefaultRuntime> ClassProperty;
	TScopedStructBinding<FArrayProperty, FDefaultRuntime> ArrayProperty;
	TScopedStructBinding<FSetProperty, FDefaultRuntime> SetProperty;
	TScopedStructBinding<FMapProperty, FDefaultRuntime> MapProperty;
	TScopedStructBinding<FOptionalProperty, FDefaultRuntime> OptionalProperty;
	TScopedStructBinding<FObjectPropertyBase, FDefaultRuntime> ObjectPropertyBase;
	TScopedStructBinding<UStruct, FDefaultRuntime> Struct;
	TScopedStructBinding<UClass, FDefaultRuntime> Class;
	TScopedStructBinding<UFunction, FDefaultRuntime> Function;
	TScopedStructBinding<UScriptStruct, FDefaultRuntime> ScriptStruct;
	TScopedStructBinding<FImplementedInterface, FDefaultRuntime> ImplementedInterface;
#endif
};

} // namespace PlainProps::UE

////////////////////////////////////////////////////////////////////////////////////////////////

static TUniquePtr<PlainProps::UE::FPlainPropsBindingScope> BindingScopePtr = nullptr;
static TUniquePtr<PlainProps::DbgVis::FIdScope> DebugIdScopePtr = nullptr;

void FPlainPropsUObjectModule::StartupModule()
{
	using namespace PlainProps;
	using namespace PlainProps::UE;
	DebugIdScopePtr = MakeUnique<DbgVis::FIdScope>(GUE.Names, "SensName");
	BindingScopePtr = MakeUnique<FPlainPropsBindingScope>();
}

void FPlainPropsUObjectModule::ShutdownModule()
{
	BindingScopePtr.Reset();
	DebugIdScopePtr.Reset();
}
