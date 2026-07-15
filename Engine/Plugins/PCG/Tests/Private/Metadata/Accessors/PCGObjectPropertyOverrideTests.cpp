// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// PCG Object Property Override Tests
//
// Verifies FPCGObjectOverrides<T> end-to-end against every metadata type
// FPCGMetadataAttributeDesc::CreateFromProperty supports
//
// Three override modes covered:
//   1. SingleValue       - param data has 1 entry, target is a scalar property
//   2. MultiValue        - param data has N entries, target is a TArray<T> property
//                          (uses bAllowedToUseAllSourceDataForContainers)
//   3. ArrayAttribute    - param data has 1 entry whose attribute is itself an
//                          array, target is a TArray<T> property
// =============================================================================

#include "PCGObjectPropertyOverrideTests.h"

#include "CoreMinimal.h"
#include "TestHarness.h"

#include "PCGTestsCommon.h"

#include "PCGContext.h"
#include "PCGParamData.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Metadata/PCGObjectPropertyOverride.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"

namespace PCGObjectPropertyOverrideTestsHelpers
{
	static const FName AttrName = TEXT("OverrideSource");

	// Builds a UPCGParamData with one typed attribute and N entries set from Values.
	template <typename T>
	UPCGParamData* MakeParamData(FPCGContext* Context, TConstArrayView<T> Values)
	{
		UPCGParamData* Data = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
		REQUIRE(Data);
		REQUIRE(Data->Metadata);

		FPCGMetadataAttributeBase* Attribute = Data->Metadata->CreateAttribute<T>(AttrName, T{}, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);
		REQUIRE(Attribute);
		
		TArray<PCGMetadataEntryKey> EntryKeys;
		Algo::Transform(Values, EntryKeys, [Data](const T&) { return Data->Metadata->AddEntry(); });
		Attribute->SetValues(EntryKeys, Values);

		return Data;
	}

	// Builds a UPCGParamData with a single entry whose attribute is itself an array of T.
	template <typename T>
	UPCGParamData* MakeArrayAttributeParamData(FPCGContext* Context, TConstArrayView<T> ArrayValue)
	{
		UPCGParamData* Data = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
		REQUIRE(Data);
		REQUIRE(Data->Metadata);

		FPCGMetadataAttributeBase* Attribute = Data->Metadata->CreateAttribute<TArray<T>>(AttrName, TArray<T>{}, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);
		REQUIRE(Attribute);

		Data->Metadata->AddEntry();

		TUniquePtr<IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateAccessor(Attribute, Data->Metadata);
		TUniquePtr<IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateKeys(Data, FPCGAttributePropertySelector::CreateAttributeSelector(AttrName));
		REQUIRE(Accessor);
		REQUIRE(Keys);
		REQUIRE_EQUAL(Keys->GetNum(), 1);

		REQUIRE(Accessor->Set<TConstArrayView<T>>(ArrayValue, *Keys));

		return Data;
	}

	// Initializes FPCGObjectOverrides<T> against TargetStruct and applies one row.
	template <typename T>
	bool RunOverride_Impl(
		FPCGContext* Context,
		T& TargetStruct,
		FStringView PropertyName,
		const UPCGData* SourceData,
		bool bAllowedToUseAllSourceDataForContainers,
		int32 InputKeyIndex,
		bool& OutInitValid)
	{
		TArray<FPCGObjectPropertyOverrideDescription> Descriptions;
		FPCGObjectPropertyOverrideDescription& Description = Descriptions.Emplace_GetRef(); 
		Description.InputSource = FPCGAttributePropertySelector::CreateAttributeSelector<FPCGAttributePropertyInputSelector>(AttrName);
		Description.PropertyTarget = FString(PropertyName);

		FPCGObjectOverrides<T> Overrides(&TargetStruct);

		typename FPCGObjectOverrides<T>::FInitializeParams InitParams = {
			.OverrideDescriptions = Descriptions,
			.TemplateObject = &TargetStruct,
			.SourceData = SourceData,
			.OptionalContext = Context,
			.bInPropagateEditChangeEvent = false,
			.bAllowedToUseAllSourceDataForContainers = bAllowedToUseAllSourceDataForContainers
		};

		Overrides.Initialize(InitParams);
		OutInitValid = Overrides.IsValid();
		if (!OutInitValid)
		{
			return false;
		}

		return Overrides.Apply(InputKeyIndex);
	}

	template <typename T>
	bool RunOverride(FPCGContext* Context, T& TargetStruct, FStringView PropertyName, const UPCGData* SourceData, bool bAllowedToUseAllSourceDataForContainers, int32 InputKeyIndex = 0, bool bShouldSucceed = true)
	{
		bool bInitValid = false;
		const bool bApplyOk = RunOverride_Impl(Context, TargetStruct, PropertyName, SourceData, bAllowedToUseAllSourceDataForContainers, InputKeyIndex, bInitValid);
		REQUIRE_EQUAL(bInitValid, bShouldSucceed);
		return bApplyOk;
	}
}

// -----------------------------------------------------------------------------
// Mode 1: Single value -> single property
// -----------------------------------------------------------------------------
TEST_CASE_METHOD(PCGTests::FPCGBaseTestWithBasicContext, "PCG::Metadata::Override::SingleValue", "[PCG][Override]")
{
	using namespace PCGObjectPropertyOverrideTestsHelpers;

	FPCGObjectPropertyOverrideTestsStruct Target{};

	SECTION("Double")
	{
		const double Expected = 4.25;
		UPCGParamData* Source = MakeParamData<double>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("DoubleValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.DoubleValue == Catch::Approx(Expected));
	}

	SECTION("Float")
	{
		const float Expected = 4.25f;
		UPCGParamData* Source = MakeParamData<float>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("FloatValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.FloatValue == Catch::Approx(Expected));
	}

	SECTION("Int32")
	{
		const int32 Expected = 7;
		UPCGParamData* Source = MakeParamData<int32>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("Int32Value"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.Int32Value == Expected);
	}

	SECTION("Int64")
	{
		const int64 Expected = int64(1) << 40;
		UPCGParamData* Source = MakeParamData<int64>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("Int64Value"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.Int64Value == Expected);
	}

	SECTION("Bool")
	{
		const bool Expected = true;
		UPCGParamData* Source = MakeParamData<bool>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("BoolValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.BoolValue == Expected);
	}
	
	SECTION("Bitflag1")
	{
		const bool Expected = true;
		UPCGParamData* Source = MakeParamData<bool>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("bBitfieldFlag1"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.bBitfieldFlag1 == Expected);
	}
	
	SECTION("Bitflag2")
	{
		const bool Expected = true;
		UPCGParamData* Source = MakeParamData<bool>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("bBitfieldFlag2"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.bBitfieldFlag2 == Expected);
	}

	SECTION("Vector2D")
	{
		const FVector2D Expected(1.5, -2.5);
		UPCGParamData* Source = MakeParamData<FVector2D>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("Vector2DValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.Vector2DValue.Equals(Expected));
	}

	SECTION("Vector")
	{
		const FVector Expected(1.0, 2.0, 3.0);
		UPCGParamData* Source = MakeParamData<FVector>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("VectorValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.VectorValue.Equals(Expected));
	}

	SECTION("Vector4")
	{
		const FVector4 Expected(1.0, 2.0, 3.0, 4.0);
		UPCGParamData* Source = MakeParamData<FVector4>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("Vector4Value"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.Vector4Value == Expected);
	}

	SECTION("Quat")
	{
		const FQuat Expected = FQuat::MakeFromEuler(FVector(15.0, 30.0, 45.0));
		UPCGParamData* Source = MakeParamData<FQuat>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("QuatValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.QuatValue.Equals(Expected));
	}

	SECTION("Rotator")
	{
		const FRotator Expected(15.0, 30.0, 45.0);
		UPCGParamData* Source = MakeParamData<FRotator>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("RotatorValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.RotatorValue.Equals(Expected));
	}

	SECTION("Transform")
	{
		const FTransform Expected(FQuat::Identity, FVector(1.0, 2.0, 3.0), FVector(2.0, 2.0, 2.0));
		UPCGParamData* Source = MakeParamData<FTransform>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("TransformValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.TransformValue.Equals(Expected));
	}

	SECTION("String")
	{
		const FString Expected = TEXT("Hello PCG");
		UPCGParamData* Source = MakeParamData<FString>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("StringValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.StringValue == Expected);
	}

	SECTION("Name")
	{
		const FName Expected = TEXT("PCGName");
		UPCGParamData* Source = MakeParamData<FName>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("NameValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.NameValue == Expected);
	}

	SECTION("Object")
	{
		UPCGObjectPropertyOverrideTestsObject* Instance = FPCGContext::NewObject_AnyThread<UPCGObjectPropertyOverrideTestsObject>(GetContext());
		REQUIRE(Instance);
		const FSoftObjectPath ExpectedPath(Instance);
		UPCGParamData* Source = MakeParamData<FSoftObjectPath>(GetContext(), MakeArrayView({ExpectedPath}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("ObjectValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.ObjectValue == Instance);
	}

	SECTION("SoftObject")
	{
		UPCGObjectPropertyOverrideTestsObject* Instance = FPCGContext::NewObject_AnyThread<UPCGObjectPropertyOverrideTestsObject>(GetContext());
		REQUIRE(Instance);
		const FSoftObjectPath ExpectedPath(Instance);
		UPCGParamData* Source = MakeParamData<FSoftObjectPath>(GetContext(), MakeArrayView({ExpectedPath}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("SoftObjectValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.SoftObjectValue.ToSoftObjectPath() == ExpectedPath);
	}

	SECTION("Class")
	{
		UClass* const ExpectedClass = UPCGObjectPropertyOverrideTestsObject::StaticClass();
		const FSoftClassPath ExpectedPath(ExpectedClass);
		UPCGParamData* Source = MakeParamData<FSoftClassPath>(GetContext(), MakeArrayView({ExpectedPath}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("ClassValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.ClassValue.Get() == ExpectedClass);
	}

	SECTION("SoftClass")
	{
		UClass* const ExpectedClass = UPCGObjectPropertyOverrideTestsObject::StaticClass();
		const FSoftClassPath ExpectedPath(ExpectedClass);
		UPCGParamData* Source = MakeParamData<FSoftClassPath>(GetContext(), MakeArrayView({ExpectedPath}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("SoftClassValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.SoftClassValue.ToSoftObjectPath() == ExpectedPath);
	}

	SECTION("SoftObjectPath")
	{
		UPCGObjectPropertyOverrideTestsObject* Instance = FPCGContext::NewObject_AnyThread<UPCGObjectPropertyOverrideTestsObject>(GetContext());
		REQUIRE(Instance);
		const FSoftObjectPath Expected(Instance);
		UPCGParamData* Source = MakeParamData<FSoftObjectPath>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("SoftObjectPathValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.SoftObjectPathValue == Expected);
	}

	SECTION("SoftClassPath")
	{
		const FSoftClassPath Expected(UPCGObjectPropertyOverrideTestsObject::StaticClass());
		UPCGParamData* Source = MakeParamData<FSoftClassPath>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("SoftClassPathValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.SoftClassPathValue == Expected);
	}

	SECTION("Enum")
	{
		const EPCGObjectPropertyOverrideTestsEnum Expected = EPCGObjectPropertyOverrideTestsEnum::C;
		UPCGParamData* Source = MakeParamData<EPCGObjectPropertyOverrideTestsEnum>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("EnumValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.EnumValue == Expected);
	}
	
	SECTION("Enum - AsInt")
	{
		const EPCGObjectPropertyOverrideTestsEnum Expected = EPCGObjectPropertyOverrideTestsEnum::C;
		UPCGParamData* Source = MakeParamData<int32>(GetContext(), MakeArrayView({static_cast<int32>(Expected)}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("EnumValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.EnumValue == Expected);
	}
	
	SECTION("Struct")
	{
		const FPCGObjectPropertyOverrideTestsSubStruct Expected{.Inner = 5.0};
		UPCGParamData* Source = MakeParamData<FPCGObjectPropertyOverrideTestsSubStruct>(GetContext(), MakeArrayView({Expected}));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("StructValue"), Source, /*bAllowedToUseAllSourceDataForContainers=*/false));
		CHECK(Target.StructValue == Expected);
	}
}

// -----------------------------------------------------------------------------
// Mode 2: Multiple param-data entries -> array property
// -----------------------------------------------------------------------------
TEST_CASE_METHOD(PCGTests::FPCGBaseTestWithBasicContext, "PCG::Metadata::Override::MultiValue", "[PCG][Override]")
{
	using namespace PCGObjectPropertyOverrideTestsHelpers;

	FPCGObjectPropertyOverrideTestsStruct Target{};

	SECTION("Double")
	{
		const TArray<double> Expected = {1.0, 2.0, 3.0, 4.0};
		UPCGParamData* Source = MakeParamData<double>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("DoubleArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.DoubleArray.Num() == Expected.Num());
		CHECK_THAT(Target.DoubleArray, Catch::Matchers::RangeEquals(Expected, [](double A, double B) { return FMath::IsNearlyEqual(A, B); }));
	}

	SECTION("Float")
	{
		const TArray<float> Expected = {1.0f, 2.0f, 3.0f, 4.0f};
		UPCGParamData* Source = MakeParamData<float>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("FloatArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.FloatArray.Num() == Expected.Num());
		CHECK_THAT(Target.FloatArray, Catch::Matchers::RangeEquals(Expected, [](float A, float B) { return FMath::IsNearlyEqual(A, B); }));
	}

	SECTION("Int32")
	{
		const TArray<int32> Expected = {1, 2, 3, 4};
		UPCGParamData* Source = MakeParamData<int32>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("Int32Array"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK(Target.Int32Array == Expected);
	}

	SECTION("Int64")
	{
		const TArray<int64> Expected = {int64(1) << 40, int64(2) << 40, int64(3) << 40};
		UPCGParamData* Source = MakeParamData<int64>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("Int64Array"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK(Target.Int64Array == Expected);
	}

	SECTION("Bool")
	{
		const TArray<bool> Expected = {true, false, true, true};
		UPCGParamData* Source = MakeParamData<bool>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("BoolArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.BoolArray.Num() == Expected.Num());
		for (int32 i = 0; i < Expected.Num(); ++i)
		{
			CHECK(Target.BoolArray[i] == Expected[i]);
		}
	}

	SECTION("Vector2D")
	{
		const TArray<FVector2D> Expected = { FVector2D(1.0, 2.0), FVector2D(3.0, 4.0) };
		UPCGParamData* Source = MakeParamData<FVector2D>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("Vector2DArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.Vector2DArray.Num() == Expected.Num());
		CHECK_THAT(Target.Vector2DArray, Catch::Matchers::RangeEquals(Expected, [](const FVector2D& A, const FVector2D& B) { return A.Equals(B); }));
	}

	SECTION("Vector")
	{
		const TArray<FVector> Expected = { FVector(1.0, 2.0, 3.0), FVector(4.0, 5.0, 6.0), FVector(7.0, 8.0, 9.0) };
		UPCGParamData* Source = MakeParamData<FVector>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("VectorArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.VectorArray.Num() == Expected.Num());
		CHECK_THAT(Target.VectorArray, Catch::Matchers::RangeEquals(Expected, [](const FVector& A, const FVector& B) { return A.Equals(B); }));
	}

	SECTION("Vector4")
	{
		const TArray<FVector4> Expected = { FVector4(1, 2, 3, 4), FVector4(5, 6, 7, 8) };
		UPCGParamData* Source = MakeParamData<FVector4>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("Vector4Array"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.Vector4Array.Num() == Expected.Num());
		CHECK(Target.Vector4Array == Expected);
	}

	SECTION("Quat")
	{
		const TArray<FQuat> Expected = {
			FQuat::MakeFromEuler(FVector(10.0, 20.0, 30.0)),
			FQuat::MakeFromEuler(FVector(40.0, 50.0, 60.0))
		};
		UPCGParamData* Source = MakeParamData<FQuat>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("QuatArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.QuatArray.Num() == Expected.Num());
		CHECK_THAT(Target.QuatArray, Catch::Matchers::RangeEquals(Expected, [](const FQuat& A, const FQuat& B) { return A.Equals(B); }));
	}

	SECTION("Rotator")
	{
		const TArray<FRotator> Expected = { FRotator(10, 20, 30), FRotator(40, 50, 60) };
		UPCGParamData* Source = MakeParamData<FRotator>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("RotatorArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.RotatorArray.Num() == Expected.Num());
		CHECK_THAT(Target.RotatorArray, Catch::Matchers::RangeEquals(Expected, [](const FRotator& A, const FRotator& B) { return A.Equals(B); }));
	}

	SECTION("Transform")
	{
		const TArray<FTransform> Expected = {
			FTransform(FQuat::Identity, FVector(1, 2, 3), FVector::OneVector),
			FTransform(FQuat::Identity, FVector(4, 5, 6), FVector(2, 2, 2))
		};
		UPCGParamData* Source = MakeParamData<FTransform>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("TransformArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.TransformArray.Num() == Expected.Num());
		CHECK_THAT(Target.TransformArray, Catch::Matchers::RangeEquals(Expected, [](const FTransform& A, const FTransform& B) { return A.Equals(B); }));
	}

	SECTION("String")
	{
		const TArray<FString> Expected = { TEXT("alpha"), TEXT("beta"), TEXT("gamma") };
		UPCGParamData* Source = MakeParamData<FString>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("StringArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK(Target.StringArray == Expected);
	}

	SECTION("Name")
	{
		const TArray<FName> Expected = { TEXT("alpha"), TEXT("beta"), TEXT("gamma") };
		UPCGParamData* Source = MakeParamData<FName>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("NameArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK(Target.NameArray == Expected);
	}

	SECTION("Object")
	{
		UPCGObjectPropertyOverrideTestsObject* InstanceA = FPCGContext::NewObject_AnyThread<UPCGObjectPropertyOverrideTestsObject>(GetContext());
		UPCGObjectPropertyOverrideTestsObject* InstanceB = FPCGContext::NewObject_AnyThread<UPCGObjectPropertyOverrideTestsObject>(GetContext());
		REQUIRE(InstanceA);
		REQUIRE(InstanceB);
		const TArray<FSoftObjectPath> ExpectedPaths = { FSoftObjectPath(InstanceA), FSoftObjectPath(InstanceB) };
		UPCGParamData* Source = MakeParamData<FSoftObjectPath>(GetContext(), ExpectedPaths);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("ObjectArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.ObjectArray.Num() == 2);
		CHECK(Target.ObjectArray[0] == InstanceA);
		CHECK(Target.ObjectArray[1] == InstanceB);
	}

	SECTION("SoftObject")
	{
		UPCGObjectPropertyOverrideTestsObject* InstanceA = FPCGContext::NewObject_AnyThread<UPCGObjectPropertyOverrideTestsObject>(GetContext());
		UPCGObjectPropertyOverrideTestsObject* InstanceB = FPCGContext::NewObject_AnyThread<UPCGObjectPropertyOverrideTestsObject>(GetContext());
		REQUIRE(InstanceA);
		REQUIRE(InstanceB);
		const TArray<FSoftObjectPath> ExpectedPaths = { FSoftObjectPath(InstanceA), FSoftObjectPath(InstanceB) };
		UPCGParamData* Source = MakeParamData<FSoftObjectPath>(GetContext(), ExpectedPaths);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("SoftObjectArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.SoftObjectArray.Num() == 2);
		CHECK(Target.SoftObjectArray[0].ToSoftObjectPath() == ExpectedPaths[0]);
		CHECK(Target.SoftObjectArray[1].ToSoftObjectPath() == ExpectedPaths[1]);
	}

	SECTION("SoftObjectPath")
	{
		UPCGObjectPropertyOverrideTestsObject* InstanceA = FPCGContext::NewObject_AnyThread<UPCGObjectPropertyOverrideTestsObject>(GetContext());
		UPCGObjectPropertyOverrideTestsObject* InstanceB = FPCGContext::NewObject_AnyThread<UPCGObjectPropertyOverrideTestsObject>(GetContext());
		REQUIRE(InstanceA);
		REQUIRE(InstanceB);
		const TArray<FSoftObjectPath> Expected = { FSoftObjectPath(InstanceA), FSoftObjectPath(InstanceB) };
		UPCGParamData* Source = MakeParamData<FSoftObjectPath>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("SoftObjectPathArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK(Target.SoftObjectPathArray == Expected);
	}

	SECTION("SoftClassPath")
	{
		const TArray<FSoftClassPath> Expected = {
			FSoftClassPath(UPCGObjectPropertyOverrideTestsObject::StaticClass()),
			FSoftClassPath(UObject::StaticClass())
		};
		UPCGParamData* Source = MakeParamData<FSoftClassPath>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("SoftClassPathArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK(Target.SoftClassPathArray == Expected);
	}

	SECTION("Enum")
	{
		const TArray<EPCGObjectPropertyOverrideTestsEnum> Expected = {
			EPCGObjectPropertyOverrideTestsEnum::B,
			EPCGObjectPropertyOverrideTestsEnum::D
		};
		UPCGParamData* Source = MakeParamData<EPCGObjectPropertyOverrideTestsEnum>(GetContext(), MakeArrayView(Expected));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("EnumArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK(Target.EnumArray == Expected);
	}
	
	SECTION("Enum - AsInt")
	{
		const TArray<int32> Expected = {
			static_cast<int32>(EPCGObjectPropertyOverrideTestsEnum::B),
			static_cast<int32>(EPCGObjectPropertyOverrideTestsEnum::D)
		};
		UPCGParamData* Source = MakeParamData<int32>(GetContext(), MakeArrayView(Expected));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("EnumArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK_THAT(Target.EnumArray, Catch::Matchers::RangeEquals(Expected, [](const EPCGObjectPropertyOverrideTestsEnum& A, const int32& B) { return static_cast<int32>(A) == B; }));
	}
	
	SECTION("Struct")
	{
		const TArray<FPCGObjectPropertyOverrideTestsSubStruct> Expected =
		{
			{.Inner = 5.0},
			{.Inner = 6.0}
		};
		UPCGParamData* Source = MakeParamData<FPCGObjectPropertyOverrideTestsSubStruct>(GetContext(), MakeArrayView(Expected));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("StructArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK(Target.StructArray == Expected);
	}
}

// -----------------------------------------------------------------------------
// Mode 3: Single param-data entry holding an array attribute -> array property
// -----------------------------------------------------------------------------
TEST_CASE_METHOD(PCGTests::FPCGBaseTestWithBasicContext, "PCG::Metadata::Override::ArrayAttribute", "[PCG][Override]")
{
	using namespace PCGObjectPropertyOverrideTestsHelpers;

	FPCGObjectPropertyOverrideTestsStruct Target{};

	SECTION("Double")
	{
		const TArray<double> Expected = {1.0, 2.0, 3.0, 4.0};
		UPCGParamData* Source = MakeArrayAttributeParamData<double>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("DoubleArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.DoubleArray.Num() == Expected.Num());
		CHECK_THAT(Target.DoubleArray, Catch::Matchers::RangeEquals(Expected, [](double A, double B) { return FMath::IsNearlyEqual(A, B); }));
	}

	SECTION("Float")
	{
		const TArray<float> Expected = {1.0f, 2.0f, 3.0f};
		UPCGParamData* Source = MakeArrayAttributeParamData<float>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("FloatArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.FloatArray.Num() == Expected.Num());
		CHECK_THAT(Target.FloatArray, Catch::Matchers::RangeEquals(Expected, [](float A, float B) { return FMath::IsNearlyEqual(A, B); }));
	}

	SECTION("Int32")
	{
		const TArray<int32> Expected = {10, 20, 30};
		UPCGParamData* Source = MakeArrayAttributeParamData<int32>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("Int32Array"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK(Target.Int32Array == Expected);
	}

	SECTION("Int64")
	{
		const TArray<int64> Expected = {int64(1) << 40, int64(2) << 40, int64(3) << 40};
		UPCGParamData* Source = MakeArrayAttributeParamData<int64>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("Int64Array"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK(Target.Int64Array == Expected);
	}

	SECTION("Bool")
	{
		const TArray<bool> Expected = {false, true, true, false};
		UPCGParamData* Source = MakeArrayAttributeParamData<bool>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("BoolArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.BoolArray.Num() == Expected.Num());
		for (int32 i = 0; i < Expected.Num(); ++i)
		{
			CHECK(Target.BoolArray[i] == Expected[i]);
		}
	}

	SECTION("Vector")
	{
		const TArray<FVector> Expected = { FVector(1.0, 2.0, 3.0), FVector(4.0, 5.0, 6.0) };
		UPCGParamData* Source = MakeArrayAttributeParamData<FVector>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("VectorArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.VectorArray.Num() == Expected.Num());
		CHECK_THAT(Target.VectorArray, Catch::Matchers::RangeEquals(Expected, [](const FVector& A, const FVector& B) { return A.Equals(B); }));
	}

	SECTION("Vector2D")
	{
		const TArray<FVector2D> Expected = { FVector2D(1.0, 2.0), FVector2D(3.0, 4.0) };
		UPCGParamData* Source = MakeArrayAttributeParamData<FVector2D>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("Vector2DArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.Vector2DArray.Num() == Expected.Num());
		CHECK_THAT(Target.Vector2DArray, Catch::Matchers::RangeEquals(Expected, [](const FVector2D& A, const FVector2D& B) { return A.Equals(B); }));
	}

	SECTION("Vector4")
	{
		const TArray<FVector4> Expected = { FVector4(1, 2, 3, 4), FVector4(5, 6, 7, 8) };
		UPCGParamData* Source = MakeArrayAttributeParamData<FVector4>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("Vector4Array"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.Vector4Array.Num() == Expected.Num());
		CHECK(Target.Vector4Array == Expected);
	}

	SECTION("Quat")
	{
		const TArray<FQuat> Expected = {
			FQuat::MakeFromEuler(FVector(10.0, 20.0, 30.0)),
			FQuat::MakeFromEuler(FVector(40.0, 50.0, 60.0))
		};
		UPCGParamData* Source = MakeArrayAttributeParamData<FQuat>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("QuatArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.QuatArray.Num() == Expected.Num());
		CHECK_THAT(Target.QuatArray, Catch::Matchers::RangeEquals(Expected, [](const FQuat& A, const FQuat& B) { return A.Equals(B); }));
	}

	SECTION("Rotator")
	{
		const TArray<FRotator> Expected = { FRotator(10, 20, 30), FRotator(40, 50, 60) };
		UPCGParamData* Source = MakeArrayAttributeParamData<FRotator>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("RotatorArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.RotatorArray.Num() == Expected.Num());
		CHECK_THAT(Target.RotatorArray, Catch::Matchers::RangeEquals(Expected, [](const FRotator& A, const FRotator& B) { return A.Equals(B); }));
	}

	SECTION("Transform")
	{
		const TArray<FTransform> Expected = {
			FTransform(FQuat::Identity, FVector(1, 2, 3), FVector::OneVector),
			FTransform(FQuat::Identity, FVector(4, 5, 6), FVector(2, 2, 2))
		};
		UPCGParamData* Source = MakeArrayAttributeParamData<FTransform>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("TransformArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.TransformArray.Num() == Expected.Num());
		CHECK_THAT(Target.TransformArray, Catch::Matchers::RangeEquals(Expected, [](const FTransform& A, const FTransform& B) { return A.Equals(B); }));
	}

	SECTION("String")
	{
		const TArray<FString> Expected = { TEXT("a"), TEXT("b"), TEXT("c") };
		UPCGParamData* Source = MakeArrayAttributeParamData<FString>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("StringArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK(Target.StringArray == Expected);
	}

	SECTION("Name")
	{
		const TArray<FName> Expected = { TEXT("a"), TEXT("b"), TEXT("c") };
		UPCGParamData* Source = MakeArrayAttributeParamData<FName>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("NameArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK(Target.NameArray == Expected);
	}

	SECTION("Object")
	{
		UPCGObjectPropertyOverrideTestsObject* InstanceA = FPCGContext::NewObject_AnyThread<UPCGObjectPropertyOverrideTestsObject>(GetContext());
		UPCGObjectPropertyOverrideTestsObject* InstanceB = FPCGContext::NewObject_AnyThread<UPCGObjectPropertyOverrideTestsObject>(GetContext());
		REQUIRE(InstanceA);
		REQUIRE(InstanceB);
		const TArray<FSoftObjectPath> ExpectedPaths = { FSoftObjectPath(InstanceA), FSoftObjectPath(InstanceB) };
		UPCGParamData* Source = MakeArrayAttributeParamData<FSoftObjectPath>(GetContext(), ExpectedPaths);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("ObjectArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.ObjectArray.Num() == 2);
		CHECK(Target.ObjectArray[0] == InstanceA);
		CHECK(Target.ObjectArray[1] == InstanceB);
	}

	SECTION("SoftObject")
	{
		UPCGObjectPropertyOverrideTestsObject* InstanceA = FPCGContext::NewObject_AnyThread<UPCGObjectPropertyOverrideTestsObject>(GetContext());
		UPCGObjectPropertyOverrideTestsObject* InstanceB = FPCGContext::NewObject_AnyThread<UPCGObjectPropertyOverrideTestsObject>(GetContext());
		REQUIRE(InstanceA);
		REQUIRE(InstanceB);
		const TArray<FSoftObjectPath> ExpectedPaths = { FSoftObjectPath(InstanceA), FSoftObjectPath(InstanceB) };
		UPCGParamData* Source = MakeArrayAttributeParamData<FSoftObjectPath>(GetContext(), ExpectedPaths);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("SoftObjectArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		REQUIRE(Target.SoftObjectArray.Num() == 2);
		CHECK(Target.SoftObjectArray[0].ToSoftObjectPath() == ExpectedPaths[0]);
		CHECK(Target.SoftObjectArray[1].ToSoftObjectPath() == ExpectedPaths[1]);
	}

	SECTION("SoftObjectPath")
	{
		UPCGObjectPropertyOverrideTestsObject* InstanceA = FPCGContext::NewObject_AnyThread<UPCGObjectPropertyOverrideTestsObject>(GetContext());
		UPCGObjectPropertyOverrideTestsObject* InstanceB = FPCGContext::NewObject_AnyThread<UPCGObjectPropertyOverrideTestsObject>(GetContext());
		REQUIRE(InstanceA);
		REQUIRE(InstanceB);
		const TArray<FSoftObjectPath> Expected = { FSoftObjectPath(InstanceA), FSoftObjectPath(InstanceB) };
		UPCGParamData* Source = MakeArrayAttributeParamData<FSoftObjectPath>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("SoftObjectPathArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK(Target.SoftObjectPathArray == Expected);
	}

	SECTION("SoftClassPath")
	{
		const TArray<FSoftClassPath> Expected = {
			FSoftClassPath(UPCGObjectPropertyOverrideTestsObject::StaticClass()),
			FSoftClassPath(UObject::StaticClass())
		};
		UPCGParamData* Source = MakeArrayAttributeParamData<FSoftClassPath>(GetContext(), Expected);
		REQUIRE(RunOverride(GetContext(), Target, TEXT("SoftClassPathArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK(Target.SoftClassPathArray == Expected);
	}
	
	SECTION("Enum")
	{
		const TArray<EPCGObjectPropertyOverrideTestsEnum> Expected = {
			EPCGObjectPropertyOverrideTestsEnum::B,
			EPCGObjectPropertyOverrideTestsEnum::D
		};
		UPCGParamData* Source = MakeArrayAttributeParamData<EPCGObjectPropertyOverrideTestsEnum>(GetContext(), MakeArrayView(Expected));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("EnumArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK(Target.EnumArray == Expected);
	}
	
	SECTION("Enum - AsInt - Not supported")
	{
		const TArray<int32> Expected = {
			static_cast<int32>(EPCGObjectPropertyOverrideTestsEnum::B),
			static_cast<int32>(EPCGObjectPropertyOverrideTestsEnum::D)
		};

		UPCGParamData* Source = MakeArrayAttributeParamData<int32>(GetContext(), MakeArrayView(Expected));
		PCGTests::FPCGTestsLogOutputDevice LogCapture(/*bSuppressErrors=*/true);
		REQUIRE_FALSE(RunOverride(GetContext(), Target, TEXT("EnumArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true, /*InputKeyIndex=*/0, /*bShouldSucceed=*/false));
		REQUIRE(LogCapture.NbMessageReceived.Contains(ELogVerbosity::Error));
		CHECK(Target.EnumArray.IsEmpty());
	}
	
	SECTION("Struct")
	{
		const TArray<FPCGObjectPropertyOverrideTestsSubStruct> Expected =
		{
			{.Inner = 5.0},
			{.Inner = 6.0}
		};
		UPCGParamData* Source = MakeArrayAttributeParamData<FPCGObjectPropertyOverrideTestsSubStruct>(GetContext(), MakeArrayView(Expected));
		REQUIRE(RunOverride(GetContext(), Target, TEXT("StructArray"), Source, /*bAllowedToUseAllSourceDataForContainers=*/true));
		CHECK(Target.StructArray == Expected);
	}
}
