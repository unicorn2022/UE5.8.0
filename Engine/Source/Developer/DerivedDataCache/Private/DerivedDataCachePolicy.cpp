// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCachePolicy.h"

#include "Algo/Accumulate.h"
#include "Algo/AnyOf.h"
#include "Algo/BinarySearch.h"
#include "Containers/StringConv.h"
#include "Containers/StringView.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataValue.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"
#include "String/ParseTokens.h"
#include "Templates/Function.h"

namespace UE::DerivedData::Private { class FCacheRecordPolicyShared; }

namespace UE::DerivedData::Private
{

static constexpr ANSICHAR CachePolicyDelimiter = ',';

struct FCachePolicyToText
{
	ECachePolicy Policy;
	FAnsiStringView Text;
};

static const FCachePolicyToText CachePolicyToText[]
{
	// Flags with multiple bits are ordered by bit count to minimize token count in the text format.
	{ ECachePolicy::Default,       ANSITEXTVIEW("Default") },
	{ ECachePolicy::Remote,        ANSITEXTVIEW("Remote") },
	{ ECachePolicy::Local,         ANSITEXTVIEW("Local") },
	{ ECachePolicy::Store,         ANSITEXTVIEW("Store") },
	{ ECachePolicy::Query,         ANSITEXTVIEW("Query") },
	// Flags with only one bit can be in any order. Match the order in ECachePolicy.
	{ ECachePolicy::QueryLocal,    ANSITEXTVIEW("QueryLocal") },
	{ ECachePolicy::QueryRemote,   ANSITEXTVIEW("QueryRemote") },
	{ ECachePolicy::StoreLocal,    ANSITEXTVIEW("StoreLocal") },
	{ ECachePolicy::StoreRemote,   ANSITEXTVIEW("StoreRemote") },
	{ ECachePolicy::SkipMeta,      ANSITEXTVIEW("SkipMeta") },
	{ ECachePolicy::SkipData,      ANSITEXTVIEW("SkipData") },
	{ ECachePolicy::PartialRecord, ANSITEXTVIEW("PartialRecord") },
	{ ECachePolicy::KeepAlive,     ANSITEXTVIEW("KeepAlive") },
	{ ECachePolicy::NonDeterministic, ANSITEXTVIEW("NonDeterministic") },
	// None must be last because it matches every policy.
	{ ECachePolicy::None,          ANSITEXTVIEW("None") },
};

static constexpr ECachePolicy CachePolicyKnownFlags
	= ECachePolicy::Default
	| ECachePolicy::SkipMeta
	| ECachePolicy::SkipData
	| ECachePolicy::PartialRecord
	| ECachePolicy::KeepAlive
	| ECachePolicy::NonDeterministic;

template <typename CharType>
static TStringBuilderBase<CharType>& CachePolicyToString(TStringBuilderBase<CharType>& Builder, ECachePolicy Policy)
{
	// Mask out unknown flags. None will be written if no flags are known.
	Policy &= CachePolicyKnownFlags;
	for (const FCachePolicyToText& Pair : CachePolicyToText)
	{
		if (EnumHasAllFlags(Policy, Pair.Policy))
		{
			EnumRemoveFlags(Policy, Pair.Policy);
			Builder << Pair.Text << CachePolicyDelimiter;
			if (Policy == ECachePolicy::None)
			{
				break;
			}
		}
	}
	Builder.RemoveSuffix(1);
	return Builder;
}

template <typename CharType>
static bool CachePolicyFromString(ECachePolicy& OutPolicy, const TStringView<CharType> String)
{
	OutPolicy = ECachePolicy::None;

	if (String.IsEmpty())
	{
		return false;
	}

	String::ParseTokens(StringCast<UTF8CHAR, 128>(String.GetData(), String.Len()), UTF8CHAR(CachePolicyDelimiter),
		[&OutPolicy, Index = int32(0)](FUtf8StringView Token) mutable
		{
			const int32 EndIndex = Index;
			for (; Index < UE_ARRAY_COUNT(CachePolicyToText); ++Index)
			{
				if (CachePolicyToText[Index].Text == Token)
				{
					OutPolicy |= CachePolicyToText[Index].Policy;
					++Index;
					return;
				}
			}
			for (Index = 0; Index < EndIndex; ++Index)
			{
				if (CachePolicyToText[Index].Text == Token)
				{
					OutPolicy |= CachePolicyToText[Index].Policy;
					++Index;
					return;
				}
			}
		});
	return true;
}

template <typename CharType>
static ECachePolicy ParseCachePolicy(const TStringView<CharType> Text)
{
	checkf(!Text.IsEmpty(), TEXT("ParseCachePolicy requires a non-empty string."));
	ECachePolicy Policy = ECachePolicy::None;
	CachePolicyFromString(Policy, Text);
	return Policy;
}

} // UE::DerivedData::Private

namespace UE::DerivedData
{

ECachePolicy CombineCachePolicy(const ECachePolicy A, const ECachePolicy B)
{
	constexpr ECachePolicy NegativeFlags = ECachePolicy::SkipData | ECachePolicy::SkipMeta | ECachePolicy::NonDeterministic;
	return ((A | B) & ~NegativeFlags) | ((A & B) & NegativeFlags);
}

FAnsiStringBuilderBase& operator<<(FAnsiStringBuilderBase& Builder, ECachePolicy Policy) { return Private::CachePolicyToString(Builder, Policy); }
FWideStringBuilderBase& operator<<(FWideStringBuilderBase& Builder, ECachePolicy Policy) { return Private::CachePolicyToString(Builder, Policy); }
FUtf8StringBuilderBase& operator<<(FUtf8StringBuilderBase& Builder, ECachePolicy Policy) { return Private::CachePolicyToString(Builder, Policy); }

bool TryLexFromString(ECachePolicy& OutPolicy, const FUtf8StringView String) { return Private::CachePolicyFromString(OutPolicy, String); }
bool TryLexFromString(ECachePolicy& OutPolicy, const FWideStringView String) { return Private::CachePolicyFromString(OutPolicy, String); }

ECachePolicy ParseCachePolicy(FAnsiStringView Text) { return Private::ParseCachePolicy(Text); }
ECachePolicy ParseCachePolicy(FWideStringView Text) { return Private::ParseCachePolicy(Text); }
ECachePolicy ParseCachePolicy(FUtf8StringView Text) { return Private::ParseCachePolicy(Text); }

class Private::FCacheRecordPolicyShared final : public Private::ICacheRecordPolicyShared
{
public:
	inline void AddRef() const final
	{
		ReferenceCount.fetch_add(1, std::memory_order_relaxed);
	}

	inline void Release() const final
	{
		if (ReferenceCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			delete this;
		}
	}

	inline void AddValuePolicy(const FCacheValuePolicy& Value) final
	{
		checkf(Value.Id.IsValid(), TEXT("Failed to add value policy because the ID is null."));
		const int32 Index = Algo::LowerBoundBy(Values, Value.Id, &FCacheValuePolicy::Id);
		checkf(!(Values.IsValidIndex(Index) && Values[Index].Id == Value.Id),
			TEXT("Failed to add value policy with ID %s because it has an existing value policy with that ID. "
			     "New: %s. Existing: %s."),
			*WriteToString<32>(Value.Id), *WriteToString<128>(Value.Policy), *WriteToString<128>(Values[Index].Policy));
		Values.Insert(Value, Index);
	}

	inline TConstArrayView<FCacheValuePolicy> GetValuePolicies() const final
	{
		return Values;
	}

private:
	TArray<FCacheValuePolicy, TInlineAllocator<14>> Values;
	mutable std::atomic<uint32> ReferenceCount{0};
};

ECachePolicy FCacheRecordPolicy::GetValuePolicy(const FValueId& Id) const
{
	if (Shared)
	{
		const TConstArrayView<FCacheValuePolicy> Values = Shared->GetValuePolicies();
		if (const int32 Index = Algo::BinarySearchBy(Values, Id, &FCacheValuePolicy::Id); Index != INDEX_NONE)
		{
			return Values[Index].Policy;
		}
	}
	return DefaultValuePolicy;
}

FCacheRecordPolicy FCacheRecordPolicy::Transform(const TFunctionRef<ECachePolicy (ECachePolicy)> Op) const
{
	if (IsUniform())
	{
		return Op(RecordPolicy);
	}

	FCacheRecordPolicyBuilder Builder(Op(GetBasePolicy()));
	for (const FCacheValuePolicy& Value : GetValuePolicies())
	{
		Builder.AddValuePolicy({Value.Id, Op(Value.Policy) & FCacheValuePolicy::PolicyMask});
	}
	return Builder.Build();
}

void FCacheRecordPolicyBuilder::AddValuePolicy(const FCacheValuePolicy& Value)
{
	checkf(!EnumHasAnyFlags(Value.Policy, ~Value.PolicyMask),
		TEXT("Value policy contains flags that only make sense on the record policy. Policy: %s"),
		*WriteToString<128>(Value.Policy));
	if (Value.Policy == (BasePolicy & Value.PolicyMask))
	{
		return;
	}
	if (!Shared)
	{
		Shared = new Private::FCacheRecordPolicyShared;
	}
	Shared->AddValuePolicy(Value);
}

FCacheRecordPolicy FCacheRecordPolicyBuilder::Build()
{
	FCacheRecordPolicy Policy(BasePolicy);
	if (Shared)
	{
		const TConstArrayView<FCacheValuePolicy> Values = Shared->GetValuePolicies();
		Policy.RecordPolicy = Algo::TransformAccumulate(Values, &FCacheValuePolicy::Policy, BasePolicy, CombineCachePolicy);
		Policy.Shared = MoveTemp(Shared);
	}
	return Policy;
}

FValue ApplySkipPolicy(const FValue& Value, ECachePolicy Policy)
{
	return EnumHasAnyFlags(Policy, ECachePolicy::SkipData) ? Value.RemoveData() : Value;
}

FValueWithId ApplySkipPolicy(const FValueWithId& Value, ECachePolicy Policy)
{
	return EnumHasAnyFlags(Policy, ECachePolicy::SkipData) ? Value.RemoveData() : Value;
}

FCacheRecord ApplySkipPolicy(const FCacheRecord& Record, const FCacheRecordPolicy& Policy)
{
	const ECachePolicy RecordPolicy = Policy.GetRecordPolicy();
	const bool bMightSkipMeta = EnumHasAnyFlags(RecordPolicy, ECachePolicy::SkipMeta);
	const bool bMightSkipData = EnumHasAnyFlags(RecordPolicy, ECachePolicy::SkipData) || !Policy.IsUniform();

	const FCbObject& Meta = Record.GetMeta();
	const TConstArrayView<FValueWithId> Values = Record.GetValues();

	const bool bSkippedMeta = bMightSkipMeta && Meta;
	if (bSkippedMeta || (bMightSkipData && Algo::AnyOf(Values, &FValueWithId::HasData)))
	{
		FCbObject NewMeta;
		if (!bMightSkipMeta && Meta)
		{
			NewMeta = Meta;
		}

		bool bSkippedData = false;
		TArray<FValueWithId> NewValues;
		NewValues.Reserve(Values.Num());
		for (const FValueWithId& Value : Values)
		{
			const bool bSkipData = EnumHasAnyFlags(Policy.GetValuePolicy(Value.GetId()), ECachePolicy::SkipData);
			NewValues.Add(bSkipData ? Value.RemoveData() : Value);
			bSkippedData |= bSkipData && Value.HasData();
		}

		if ((bSkippedMeta | bSkippedData))
		{
			return FCacheRecord::CreateByMove(Record.GetKey(), MoveTemp(NewMeta), MoveTemp(NewValues));
		}
	}

	return Record;
}

} // UE::DerivedData
