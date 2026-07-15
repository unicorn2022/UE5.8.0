// Copyright Epic Games, Inc. All Rights Reserved.

// ====================================================================================================================================================
// PCG Extract Member From Struct Element Tests
//
// Tests for UPCGExtractMemberFromStructSettings (PCGExtractMemberFromStruct.cpp), the node that walks a property chain into a struct-typed attribute
// and emits the leaf member across all entries. The output is a duplicate of the input data with the new attribute added.
//
// The element runs the extraction with bCreateNewData=false (writes into a duplicate of the input data).
//
// Test summary:
//
// Normal path — each FPCGPoint member type:
//  - FromStruct::FPCGPoint::Density:    float member, 2 entries
//  - FromStruct::FPCGPoint::Steepness:  float member at non-default offset
//  - FromStruct::FPCGPoint::Seed:       int32 member
//  - FromStruct::FPCGPoint::BoundsMin:  FVector member
//
// Entry-count axis:
//  - FromStruct::SingleEntry:           1 entry round-trips correctly
//  - FromStruct::ManyEntries:           10 entries round-trip in order
//
// Error paths:
//  - FromStruct::EmptyExtraNames:       no extra names → error
//  - FromStruct::NonStructAttribute:    extra name on a float attribute → error
//  - FromStruct::InvalidMemberName:     bogus member name → no output
//
// Output shape:
//  - FromStruct::OutputDuplicatesInput: original struct attribute survives on the output; new attribute appears alongside it
//  - FromStruct::PointDataInput:        UPCGBasePointData duplicates correctly
//
// Property-chain validation (array placement in chain):
//  - Chain::StructInMiddle::Allowed:    Outer.NestedStruct.Value — non-array struct in the middle of the chain extracts cleanly (control case)
//  - Chain::ArrayInMiddle::Rejected:    Outer.ArrayOfStructs.Value — FArrayProperty in the middle is rejected with an error log, no output
//  - Chain::LeafArray::AllowedAtEnd:    Outer.LeafIntArray (chain length 1) passes validation; SKIPped pending framework support for cross-FProperty
//                                        array sources (currently SIGSEGVs from FScriptArrayHelper Inner mismatch)
//
// bExtractAll mode:
//  - ExtractAll::FPCGPointRoot::Members:               bExtractAll on FPCGPoint emits one attribute per BlueprintReadWrite member
//  - ExtractAll::FPCGPointRoot::SourceKept:            bDeleteSourceAttribute=false keeps the source struct attribute alongside the extracted ones
//  - ExtractAll::NestedStruct::Allowed:                bExtractAll into a nested BPVisible struct emits the leaf's BPVisible member; non-BP filtered
//  - ExtractAll::NonStructLeaf::Rejected:              bExtractAll on a path that resolves to a non-struct leaf logs ExtractAllOnNonStruct, no output
//  - ExtractAll::FiltersByBlueprintVisible:            plain UPROPERTY() members are excluded; only CPF_BlueprintVisible ones are emitted
//  - ExtractAll::NoBlueprintVisibleMembers::NoOutput:  a struct with zero BlueprintVisible members triggers NoPropertiesFound
// ====================================================================================================================================================

#include "CoreMinimal.h"
#include "TestHarness.h"

#include "PCGCommon.h"
#include "PCGContext.h"
#include "PCGData.h"
#include "PCGParamData.h"
#include "PCGPoint.h"
#include "PCGTestsCommon.h"
#include "Data/PCGBasePointData.h"
#include "Elements/Metadata/PCGExtractMemberFromStruct.h"
#include "PCGExtractMemberFromStructElementTestTypes.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataAttributeTraits.h"

class FPCGExtractMemberFromStructTest : public PCGTests::FPCGSingleElementBaseTest<UPCGExtractMemberFromStructSettings>
{
public:
	inline static const FName SourceAttrName = TEXT("Source");
	inline static const FName PointAttrName = TEXT("MyPoint");
	inline static const FName OutputAttrName = TEXT("OutResult");

	// ------------------------------------------------------------------
	// Input factories
	// ------------------------------------------------------------------

	// Build a UPCGParamData with one attribute of type T, filled with one entry per
	// value. Works for both basic types (int32, double, FVector, FString, …) and
	// non-basic structs (FPCGPoint, …) — CreateAttribute<T> picks the right overload
	// and the typed return upcasts cleanly to FPCGMetadataAttributeBase*.
	template <typename T>
	UPCGParamData* MakeParam(const TArray<T>& PerEntry, FName InAttrName = SourceAttrName)
	{
		UPCGParamData* ParamData = NewObject<UPCGParamData>();
		check(ParamData && ParamData->Metadata);

		FPCGMetadataAttributeBase* Attr = ParamData->Metadata->CreateAttribute<T>(
			InAttrName, T{}, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);
		check(Attr);

		for (const T& Value : PerEntry)
		{
			const PCGMetadataEntryKey Key = ParamData->Metadata->AddEntry();
			Attr->SetValue<T>(Key, Value);
		}

		return ParamData;
	}

	// Build a UPCGBasePointData with one attribute of type T, one entry per point.
	// Same FPCGMetadataAttributeBase* + templated SetValue<T> trick as MakeParam.
	template <typename T>
	UPCGBasePointData* MakePointData(const TArray<T>& PerPoint, FName InAttrName = SourceAttrName)
	{
		UPCGBasePointData* PointData = CreatePointData();
		PointData->SetNumPoints(PerPoint.Num());
		PointData->AllocateProperties(EPCGPointNativeProperties::MetadataEntry);

		FPCGMetadataAttributeBase* Attr = PointData->MutableMetadata()->CreateAttribute<T>(
			InAttrName, T{}, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);
		check(Attr);

		TPCGValueRange<PCGMetadataEntryKey> EntryRange = PointData->GetMetadataEntryValueRange();
		for (int32 i = 0; i < PerPoint.Num(); ++i)
		{
			PointData->MutableMetadata()->InitializeOnSet(EntryRange[i]);
			Attr->SetValue<T>(EntryRange[i], PerPoint[i]);
		}
		return PointData;
	}

	// ------------------------------------------------------------------
	// Configuration / execution
	// ------------------------------------------------------------------

	void ConfigureFromStruct(FName InAttrName, FName InMemberName, FName InOutputName = OutputAttrName)
	{
		TypedSettings->InputSource = FPCGAttributePropertySelector::CreateAttributeSelector<FPCGAttributePropertyInputSelector>(
			InAttrName,
			/*DomainName=*/NAME_None,
			/*InExtraNames=*/{InMemberName.ToString()});
		TypedSettings->OutputAttributeName.SetAttributeName(InOutputName);
	}

	void ConfigureFromStructNoMember(FName InAttrName, FName InOutputName = OutputAttrName)
	{
		TypedSettings->InputSource = FPCGAttributePropertySelector::CreateAttributeSelector<FPCGAttributePropertyInputSelector>(InAttrName);
		TypedSettings->OutputAttributeName.SetAttributeName(InOutputName);
	}

	void AddInput(UPCGData* Data)
	{
		FPCGTaggedData& Tagged = InputData.TaggedData.Emplace_GetRef();
		Tagged.Data = Data;
		Tagged.Pin = PCGPinConstants::DefaultInputLabel;
	}

	// ------------------------------------------------------------------
	// Output assertions
	// ------------------------------------------------------------------

	const UPCGData* RequireSingleOutput()
	{
		REQUIRE(Context->OutputData.TaggedData.Num() == 1);
		const UPCGData* OutData = Context->OutputData.TaggedData[0].Data;
		REQUIRE(OutData);
		return OutData;
	}

	void ExpectNoOutput()
	{
		CHECK(Context->OutputData.TaggedData.Num() == 0);
	}

	// Read a basic-type single-attribute value at the given metadata entry key.
	template <typename T>
	T ReadAttr(const UPCGData* OutData, FName AttrName, PCGMetadataEntryKey EntryKey)
	{
		const UPCGMetadata* OutMetadata = OutData->ConstMetadata();
		REQUIRE(OutMetadata);
		const FPCGMetadataAttributeBase* Attr = OutMetadata->GetConstAttribute(AttrName);
		REQUIRE(Attr);
		return Attr->GetValueFromItemKey<T>(EntryKey);
	}
};

// =============================================================================
// Normal path: each FPCGPoint member type
// =============================================================================

namespace PCGExtractMemberFromStructTests
{
	// Build a pair of FPCGPoints that differ on every member used by the tests below.
	inline TArray<FPCGPoint> TwoDistinctPoints()
	{
		FPCGPoint A;
		A.Density = 0.25f;
		A.Steepness = 0.1f;
		A.Seed = 111;
		A.BoundsMin = FVector(-1.0, -2.0, -3.0);

		FPCGPoint B;
		B.Density = 0.75f;
		B.Steepness = 0.9f;
		B.Seed = 222;
		B.BoundsMin = FVector(-4.0, -5.0, -6.0);

		return {A, B};
	}
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::FPCGPoint::Density", "[PCG][ExtractMemberFromStruct]")
{
	const TArray<FPCGPoint> Points = PCGExtractMemberFromStructTests::TwoDistinctPoints();

	ConfigureFromStruct(PointAttrName, TEXT("Density"));
	AddInput(MakeParam<FPCGPoint>(Points, PointAttrName));

	ExecuteElement();

	CHECK(NumErrors == 0);
	const UPCGData* OutData = RequireSingleOutput();
	REQUIRE(OutData->ConstMetadata());
	CHECK(OutData->ConstMetadata()->GetLocalItemCount() == 2);

	CHECK(ReadAttr<double>(OutData, OutputAttrName, PCGFirstEntryKey) == Points[0].Density);
	CHECK(ReadAttr<double>(OutData, OutputAttrName, PCGFirstEntryKey + 1) == Points[1].Density);
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::FPCGPoint::Steepness", "[PCG][ExtractMemberFromStruct]")
{
	const TArray<FPCGPoint> Points = PCGExtractMemberFromStructTests::TwoDistinctPoints();

	ConfigureFromStruct(PointAttrName, TEXT("Steepness"));
	AddInput(MakeParam<FPCGPoint>(Points, PointAttrName));

	ExecuteElement();

	CHECK(NumErrors == 0);
	const UPCGData* OutData = RequireSingleOutput();
	CHECK(ReadAttr<double>(OutData, OutputAttrName, PCGFirstEntryKey) == Points[0].Steepness);
	CHECK(ReadAttr<double>(OutData, OutputAttrName, PCGFirstEntryKey + 1) == Points[1].Steepness);
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::FPCGPoint::Seed", "[PCG][ExtractMemberFromStruct]")
{
	const TArray<FPCGPoint> Points = PCGExtractMemberFromStructTests::TwoDistinctPoints();

	ConfigureFromStruct(PointAttrName, TEXT("Seed"));
	AddInput(MakeParam<FPCGPoint>(Points, PointAttrName));

	ExecuteElement();

	CHECK(NumErrors == 0);
	const UPCGData* OutData = RequireSingleOutput();
	CHECK(ReadAttr<int32>(OutData, OutputAttrName, PCGFirstEntryKey) == Points[0].Seed);
	CHECK(ReadAttr<int32>(OutData, OutputAttrName, PCGFirstEntryKey + 1) == Points[1].Seed);
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::FPCGPoint::BoundsMin", "[PCG][ExtractMemberFromStruct]")
{
	const TArray<FPCGPoint> Points = PCGExtractMemberFromStructTests::TwoDistinctPoints();

	ConfigureFromStruct(PointAttrName, TEXT("BoundsMin"));
	AddInput(MakeParam<FPCGPoint>(Points, PointAttrName));

	ExecuteElement();

	CHECK(NumErrors == 0);
	const UPCGData* OutData = RequireSingleOutput();
	CHECK(ReadAttr<FVector>(OutData, OutputAttrName, PCGFirstEntryKey) == Points[0].BoundsMin);
	CHECK(ReadAttr<FVector>(OutData, OutputAttrName, PCGFirstEntryKey + 1) == Points[1].BoundsMin);
}

// =============================================================================
// Entry-count axis
// =============================================================================

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::SingleEntry", "[PCG][ExtractMemberFromStruct]")
{
	FPCGPoint Single;
	Single.Density = 0.42f;

	ConfigureFromStruct(PointAttrName, TEXT("Density"));
	AddInput(MakeParam<FPCGPoint>({Single}, PointAttrName));

	ExecuteElement();

	CHECK(NumErrors == 0);
	const UPCGData* OutData = RequireSingleOutput();
	CHECK(OutData->ConstMetadata()->GetLocalItemCount() == 1);
	CHECK(ReadAttr<double>(OutData, OutputAttrName, PCGFirstEntryKey) == Single.Density);
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::ManyEntries", "[PCG][ExtractMemberFromStruct]")
{
	const int32 Count = 10;
	TArray<FPCGPoint> Points;
	Points.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		FPCGPoint P;
		P.Density = 0.05f * static_cast<float>(i + 1);
		Points.Add(P);
	}

	ConfigureFromStruct(PointAttrName, TEXT("Density"));
	AddInput(MakeParam<FPCGPoint>(Points, PointAttrName));

	ExecuteElement();

	CHECK(NumErrors == 0);
	const UPCGData* OutData = RequireSingleOutput();
	REQUIRE(OutData->ConstMetadata()->GetLocalItemCount() == Count);

	for (int32 i = 0; i < Count; ++i)
	{
		CHECK(ReadAttr<double>(OutData, OutputAttrName, PCGFirstEntryKey + i) == Points[i].Density);
	}
}

// =============================================================================
// Error paths
// =============================================================================

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::EmptyExtraNames", "[PCG][ExtractMemberFromStruct][Errors]")
{
	ConfigureFromStructNoMember(PointAttrName);
	AddInput(MakeParam<FPCGPoint>(PCGExtractMemberFromStructTests::TwoDistinctPoints(), PointAttrName));

	FSuppressErrorsScope Suppress(*this);
	ExecuteElement();

	CHECK(NumErrors >= 1);
	ExpectNoOutput();
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::NonStructAttribute", "[PCG][ExtractMemberFromStruct][Errors]")
{
	// A float attribute with an "extra name" that doesn't apply — the validation in
	// the source path checks IsSingleValue && Struct && ValueTypeObject and bails out.
	ConfigureFromStruct(SourceAttrName, TEXT("AnyMember"));
	AddInput(MakeParam<double>({1.0, 2.0}));

	FSuppressErrorsScope Suppress(*this);
	ExecuteElement();

	CHECK(NumErrors >= 1);
	ExpectNoOutput();
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::InvalidMemberName", "[PCG][ExtractMemberFromStruct][Errors]")
{
	ConfigureFromStruct(PointAttrName, TEXT("ThisFieldDoesNotExistOnFPCGPoint"));
	AddInput(MakeParam<FPCGPoint>(PCGExtractMemberFromStructTests::TwoDistinctPoints(), PointAttrName));

	FSuppressErrorsScope Suppress(*this);
	ExecuteElement();

	// GetPropertyChain failure is silent — but no output should be produced.
	ExpectNoOutput();
}

// =============================================================================
// Output shape
// =============================================================================

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::OutputDuplicatesInputIfRequested", "[PCG][ExtractMemberFromStruct]")
{
	// The element runs the extraction with bCreateNewData=false, meaning the
	// output is a duplicate of the input data with the new attribute added — the
	// original struct attribute must still be present.
	const TArray<FPCGPoint> Points = PCGExtractMemberFromStructTests::TwoDistinctPoints();

	ConfigureFromStruct(PointAttrName, TEXT("Density"));
	AddInput(MakeParam<FPCGPoint>(Points, PointAttrName));
	
	TypedSettings->bDeleteSourceAttribute = false;

	ExecuteElement();

	CHECK(NumErrors == 0);
	const UPCGData* OutData = RequireSingleOutput();
	REQUIRE(OutData->ConstMetadata());

	const UPCGMetadata* OutMeta = OutData->ConstMetadata();
	CHECK(OutMeta->GetConstAttribute(PointAttrName) != nullptr);     // Original struct attribute survives.
	CHECK(OutMeta->GetConstTypedAttribute<double>(OutputAttrName) != nullptr); // New attribute exists.
	CHECK(OutMeta->GetLocalItemCount() == Points.Num());
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::PointDataInput", "[PCG][ExtractMemberFromStruct]")
{
	const TArray<FPCGPoint> Points = PCGExtractMemberFromStructTests::TwoDistinctPoints();

	ConfigureFromStruct(PointAttrName, TEXT("Density"));
	AddInput(MakePointData<FPCGPoint>(Points, PointAttrName));

	ExecuteElement();

	CHECK(NumErrors == 0);
	const UPCGData* OutData = RequireSingleOutput();

	// PointData input → duplicated PointData output.
	const UPCGBasePointData* OutPoints = Cast<const UPCGBasePointData>(OutData);
	REQUIRE(OutPoints);
	CHECK(OutPoints->GetNumPoints() == Points.Num());

	// Read the new attribute via the per-point metadata entry keys.
	const UPCGMetadata* OutMeta = OutPoints->ConstMetadata();
	REQUIRE(OutMeta);
	const FPCGMetadataAttribute<double>* DensityAttr = OutMeta->GetConstTypedAttribute<double>(OutputAttrName);
	REQUIRE(DensityAttr);

	const TConstPCGValueRange<PCGMetadataEntryKey> EntryRange = OutPoints->GetConstMetadataEntryValueRange();
	REQUIRE(EntryRange.Num() == Points.Num());
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		CHECK(DensityAttr->GetValueFromItemKey(EntryRange[i]) == Points[i].Density);
	}
}

// =============================================================================
// Property-chain validation: array placement in the chain
//
// The element validates the property chain that resolves the requested member.
// The rule (PCGExtractMemberFromStruct.cpp): every property in the chain *except
// the last* must be non-array. Equivalently:
//  - Leaf array (chain length 1, just an array property)             → allowed
//  - Non-array struct in the middle, leaf is anything                 → allowed
//  - Array property in the middle of a 2+ element chain               → rejected
// These tests pin that contract so the leaf-array allowance doesn't regress.
// =============================================================================

namespace PCGExtractMemberFromStructTests
{
	// UPCGParamData with one entry holding a single FPCGExtractAttrTestOuter
	// populated for chain-validation tests.
	inline UPCGParamData* MakeOuterParam(FName AttrName, const FPCGExtractAttrTestOuter& Value)
	{
		UPCGParamData* ParamData = NewObject<UPCGParamData>();
		check(ParamData && ParamData->Metadata);

		FPCGMetadataAttributeBase* Attr = ParamData->Metadata->CreateAttribute<FPCGExtractAttrTestOuter>(
			AttrName, FPCGExtractAttrTestOuter{}, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);
		check(Attr);

		const PCGMetadataEntryKey Key = ParamData->Metadata->AddEntry();
		Attr->SetValue<FPCGExtractAttrTestOuter>(Key, Value);
		return ParamData;
	}

	inline FPCGExtractAttrTestOuter MakePopulatedOuter()
	{
		FPCGExtractAttrTestOuter Outer;
		Outer.LeafIntArray = {10, 20, 30};
		Outer.ArrayOfStructs = {FPCGExtractAttrTestLeaf{1}, FPCGExtractAttrTestLeaf{2}};
		Outer.NestedStruct.Value = 42;
		return Outer;
	}
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::Chain::LeafArray::AllowedAtEnd", "[PCG][ExtractMemberFromStruct][Chain]")
{
	// Path = "Outer.LeafIntArray" (chain length 1: a single TArray<int32> property).
	// The "all-but-last must be non-array" validation sees an empty view and passes,
	// so the leaf-array policy is honored at the contract level.

	static const FName OuterAttrName = TEXT("Outer");

	ConfigureFromStruct(OuterAttrName, TEXT("LeafIntArray"));
	AddInput(PCGExtractMemberFromStructTests::MakeOuterParam(OuterAttrName, PCGExtractMemberFromStructTests::MakePopulatedOuter()));

	ExecuteElement();

	CHECK(NumErrors == 0);
	const UPCGData* OutData = RequireSingleOutput();
	REQUIRE(OutData->ConstMetadata());

	const FPCGMetadataAttributeBase* OutAttr = OutData->ConstMetadata()->GetConstAttribute(OutputAttrName);
	REQUIRE(OutAttr);
	CHECK(OutAttr->IsOfType<TArray<int32>>());
	TConstArrayView<int32> Value = ReadAttr<TConstArrayView<int32>>(OutData, OutputAttrName, PCGFirstEntryKey);
	CHECK_THAT(Value, Catch::Matchers::RangeEquals({10, 20, 30}, [](const int32 LHS, const int32 RHS) { return LHS == RHS; }));
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::Chain::ArrayInMiddle::Rejected", "[PCG][ExtractMemberFromStruct][Chain][Errors]")
{
	// Path = "Outer.ArrayOfStructs.Value" (chain length 2: FArrayProperty followed
	// by int32). The check inspects all-but-last and finds an FArrayProperty in
	// the middle → logs LOCTEXT("ArrayPropertyFound", ...) and bails out with no
	// output.
	static const FName OuterAttrName = TEXT("Outer");

	TypedSettings->InputSource = FPCGAttributePropertySelector::CreateAttributeSelector<FPCGAttributePropertyInputSelector>(
		OuterAttrName,
		/*DomainName=*/NAME_None,
		/*InExtraNames=*/{TEXT("ArrayOfStructs"), TEXT("Value")});
	TypedSettings->OutputAttributeName.SetAttributeName(OutputAttrName);

	AddInput(PCGExtractMemberFromStructTests::MakeOuterParam(OuterAttrName, PCGExtractMemberFromStructTests::MakePopulatedOuter()));

	FSuppressErrorsScope Suppress(*this);
	ExecuteElement();

	CHECK(NumErrors >= 1);
	ExpectNoOutput();
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::Chain::StructInMiddle::Allowed", "[PCG][ExtractMemberFromStruct][Chain]")
{
	// Control case for the array-in-middle rejection: a non-array struct in the
	// middle (NestedStruct, an FStructProperty) must NOT trip the validation.
	// Path = "Outer.NestedStruct.Value" (chain length 2: FStructProperty then int32).
	static const FName OuterAttrName = TEXT("Outer");

	TypedSettings->InputSource = FPCGAttributePropertySelector::CreateAttributeSelector<FPCGAttributePropertyInputSelector>(
		OuterAttrName,
		/*DomainName=*/NAME_None,
		/*InExtraNames=*/{TEXT("NestedStruct"), TEXT("Value")});
	TypedSettings->OutputAttributeName.SetAttributeName(OutputAttrName);

	const FPCGExtractAttrTestOuter Outer = PCGExtractMemberFromStructTests::MakePopulatedOuter();
	AddInput(PCGExtractMemberFromStructTests::MakeOuterParam(OuterAttrName, Outer));

	ExecuteElement();

	CHECK(NumErrors == 0);
	const UPCGData* OutData = RequireSingleOutput();
	CHECK(ReadAttr<int32>(OutData, OutputAttrName, PCGFirstEntryKey) == Outer.NestedStruct.Value);
}

// =============================================================================
// bExtractAll mode
//
// When bExtractAll is true, the element walks the input struct (or the struct
// reached by the property chain) and emits one output attribute per
// CPF_BlueprintVisible top-level member, naming each output attribute after
// the source property. The user-provided OutputAttributeName is ignored.
//
// Rules being pinned here:
//  - Root normal path:          bExtractAll on a struct attribute emits each
//                               BlueprintReadWrite member as a separate attribute
//                               on the duplicated output.
//  - Source deletion:           the source struct attribute is deleted by default
//                               (bDeleteSourceAttribute=true), and survives when
//                               the flag is false.
//  - Nested-struct chain:       bExtractAll with extra names that resolve to a
//                               nested struct emits that nested struct's members.
//  - Non-struct leaf rejected:  bExtractAll with extra names that resolve to a
//                               non-struct logs ExtractAllOnNonStruct and emits
//                               no output.
//  - BlueprintVisible filter:   non-BlueprintReadWrite UPROPERTY()s are excluded
//                               from the extracted set.
//  - Empty extracted set:       a struct with no BlueprintReadWrite members
//                               produces no output and logs NoPropertiesFound.
// =============================================================================

namespace PCGExtractMemberFromStructTests
{
	// Single-entry param holding one FPCGExtractAttrTestBPOuter. Mirror of
	// MakeOuterParam but for the BlueprintReadWrite-tagged variant.
	inline UPCGParamData* MakeBPOuterParam(FName AttrName, const FPCGExtractAttrTestBPOuter& Value)
	{
		UPCGParamData* ParamData = NewObject<UPCGParamData>();
		check(ParamData && ParamData->Metadata);

		FPCGMetadataAttributeBase* Attr = ParamData->Metadata->CreateAttribute<FPCGExtractAttrTestBPOuter>(
			AttrName, FPCGExtractAttrTestBPOuter{}, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);
		check(Attr);

		const PCGMetadataEntryKey Key = ParamData->Metadata->AddEntry();
		Attr->SetValue<FPCGExtractAttrTestBPOuter>(Key, Value);
		return ParamData;
	}

	inline FPCGExtractAttrTestBPOuter MakePopulatedBPOuter()
	{
		FPCGExtractAttrTestBPOuter Outer;
		Outer.BPInt = 7;
		Outer.BPNestedStruct.BPValue = 42;
		Outer.BPNestedStruct.NonBPValue = 99;
		Outer.NonBPInt = 13;
		return Outer;
	}
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::ExtractAll::FPCGPointRoot::Members", "[PCG][ExtractMemberFromStruct][ExtractAll]")
{
	// bExtractAll on the FPCGPoint struct attribute should produce one attribute
	// per BlueprintReadWrite member.
	const TArray<FPCGPoint> Points = PCGExtractMemberFromStructTests::TwoDistinctPoints();

	ConfigureFromStructNoMember(PointAttrName);
	TypedSettings->bExtractAll = true;
	AddInput(MakeParam<FPCGPoint>(Points, PointAttrName));

	ExecuteElement();

	CHECK(NumErrors == 0);
	const UPCGData* OutData = RequireSingleOutput();
	REQUIRE(OutData->ConstMetadata());
	CHECK(OutData->ConstMetadata()->GetLocalItemCount() == 2);

	// Output attributes are named after the source property, not OutputAttrName.
	CHECK(ReadAttr<double>(OutData, TEXT("Density"), PCGFirstEntryKey)     == Points[0].Density);
	CHECK(ReadAttr<double>(OutData, TEXT("Density"), PCGFirstEntryKey + 1) == Points[1].Density);
	CHECK(ReadAttr<int32>(OutData, TEXT("Seed"), PCGFirstEntryKey)         == Points[0].Seed);
	CHECK(ReadAttr<int32>(OutData, TEXT("Seed"), PCGFirstEntryKey + 1)     == Points[1].Seed);
	CHECK(ReadAttr<FVector>(OutData, TEXT("BoundsMin"), PCGFirstEntryKey)     == Points[0].BoundsMin);
	CHECK(ReadAttr<FVector>(OutData, TEXT("BoundsMin"), PCGFirstEntryKey + 1) == Points[1].BoundsMin);

	// Source struct attribute deleted by default (bDeleteSourceAttribute=true).
	CHECK(OutData->ConstMetadata()->GetConstAttribute(PointAttrName) == nullptr);
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::ExtractAll::FPCGPointRoot::SourceKept", "[PCG][ExtractMemberFromStruct][ExtractAll]")
{
	// Same as ::Members but with bDeleteSourceAttribute=false — the source
	// struct attribute must survive on the duplicated output alongside the
	// newly extracted attributes. This pins the bDeleteSourceAttribute toggle
	// for the bExtractAll path specifically.
	const TArray<FPCGPoint> Points = PCGExtractMemberFromStructTests::TwoDistinctPoints();

	ConfigureFromStructNoMember(PointAttrName);
	TypedSettings->bExtractAll = true;
	TypedSettings->bDeleteSourceAttribute = false;
	AddInput(MakeParam<FPCGPoint>(Points, PointAttrName));

	ExecuteElement();

	CHECK(NumErrors == 0);
	const UPCGData* OutData = RequireSingleOutput();
	REQUIRE(OutData->ConstMetadata());

	CHECK(OutData->ConstMetadata()->GetConstAttribute(PointAttrName) != nullptr); // Source survives.
	CHECK(ReadAttr<double>(OutData, TEXT("Density"), PCGFirstEntryKey)     == Points[0].Density);
	CHECK(ReadAttr<int32>(OutData, TEXT("Seed"), PCGFirstEntryKey + 1)     == Points[1].Seed);
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::ExtractAll::NestedStruct::Allowed", "[PCG][ExtractMemberFromStruct][ExtractAll]")
{
	// bExtractAll with extra names pointing at a nested BlueprintReadWrite
	// struct should emit that struct's BPVisible members. Path =
	// "Outer.BPNestedStruct" (chain length 1: FStructProperty(BPLeaf)).
	static const FName BPOuterAttrName = TEXT("BPOuter");

	TypedSettings->InputSource = FPCGAttributePropertySelector::CreateAttributeSelector<FPCGAttributePropertyInputSelector>(
		BPOuterAttrName,
		/*DomainName=*/NAME_None,
		/*InExtraNames=*/{TEXT("BPNestedStruct")});
	TypedSettings->bExtractAll = true;

	const FPCGExtractAttrTestBPOuter Outer = PCGExtractMemberFromStructTests::MakePopulatedBPOuter();
	AddInput(PCGExtractMemberFromStructTests::MakeBPOuterParam(BPOuterAttrName, Outer));

	ExecuteElement();

	CHECK(NumErrors == 0);
	const UPCGData* OutData = RequireSingleOutput();
	REQUIRE(OutData->ConstMetadata());

	// BPLeaf::BPValue is BlueprintReadWrite → extracted.
	CHECK(ReadAttr<int32>(OutData, TEXT("BPValue"), PCGFirstEntryKey) == Outer.BPNestedStruct.BPValue);
	// BPLeaf::NonBPValue is plain UPROPERTY() → filtered out.
	CHECK(OutData->ConstMetadata()->GetConstAttribute(TEXT("NonBPValue")) == nullptr);
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::ExtractAll::NonStructLeaf::Ignored", "[PCG][ExtractMemberFromStruct][ExtractAll][Errors]")
{
	// bExtractAll + extra names that resolve to a non-struct leaf will ignore
	// the extract all and extract normally. Path = "MyPoint.Density"
	// resolves to a float, not a struct.
	const TArray<FPCGPoint> Points = PCGExtractMemberFromStructTests::TwoDistinctPoints();
	
	ConfigureFromStruct(PointAttrName, TEXT("Density"));
	TypedSettings->bExtractAll = true;
	AddInput(MakeParam<FPCGPoint>(Points, PointAttrName));
	
	ExecuteElement();

	CHECK(NumErrors == 0);
	const UPCGData* OutData = RequireSingleOutput();
	REQUIRE(OutData->ConstMetadata());
	
	CHECK(ReadAttr<double>(OutData, OutputAttrName, PCGFirstEntryKey) == Points[0].Density);
	CHECK(ReadAttr<double>(OutData, OutputAttrName, PCGFirstEntryKey + 1) == Points[1].Density);
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::ExtractAll::FiltersByBlueprintVisible", "[PCG][ExtractMemberFromStruct][ExtractAll]")
{
	// bExtractAll on a struct with mixed BlueprintReadWrite / plain UPROPERTY
	// members must emit only the CPF_BlueprintVisible ones. Pins the
	// ShouldKeepPropertyFunc filter so it can't silently regress.
	static const FName BPOuterAttrName = TEXT("BPOuter");

	TypedSettings->InputSource = FPCGAttributePropertySelector::CreateAttributeSelector<FPCGAttributePropertyInputSelector>(BPOuterAttrName);
	TypedSettings->bExtractAll = true;

	const FPCGExtractAttrTestBPOuter Outer = PCGExtractMemberFromStructTests::MakePopulatedBPOuter();
	AddInput(PCGExtractMemberFromStructTests::MakeBPOuterParam(BPOuterAttrName, Outer));

	ExecuteElement();

	CHECK(NumErrors == 0);
	const UPCGData* OutData = RequireSingleOutput();
	REQUIRE(OutData->ConstMetadata());

	// BPInt is BlueprintReadWrite → present.
	CHECK(ReadAttr<int32>(OutData, TEXT("BPInt"), PCGFirstEntryKey) == Outer.BPInt);
	// NonBPInt is plain UPROPERTY() → filtered out.
	CHECK(OutData->ConstMetadata()->GetConstAttribute(TEXT("NonBPInt")) == nullptr);
}

TEST_CASE_METHOD(FPCGExtractMemberFromStructTest, "PCG::ExtractMemberFromStruct::ExtractAll::NoBlueprintVisibleMembers::NoOutput", "[PCG][ExtractMemberFromStruct][ExtractAll][Errors]")
{
	// FPCGExtractAttrTestOuter has only plain UPROPERTY() members — every one
	// is filtered out by the CPF_BlueprintVisible gate. The bExtractAll branch
	// then finds an empty PropertyAccessorsAndSelectors and bails with the
	// NoPropertiesFound error.
	static const FName OuterAttrName = TEXT("Outer");

	TypedSettings->InputSource = FPCGAttributePropertySelector::CreateAttributeSelector<FPCGAttributePropertyInputSelector>(OuterAttrName);
	TypedSettings->bExtractAll = true;

	AddInput(PCGExtractMemberFromStructTests::MakeOuterParam(OuterAttrName, PCGExtractMemberFromStructTests::MakePopulatedOuter()));

	FSuppressErrorsScope Suppress(*this);
	ExecuteElement();

	CHECK(NumErrors >= 1);
	ExpectNoOutput();
}
