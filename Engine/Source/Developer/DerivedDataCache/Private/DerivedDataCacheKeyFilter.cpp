// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheKeyFilter.h"

#include "Algo/AnyOf.h"
#include "Algo/BinarySearch.h"
#include "Algo/Find.h"
#include "DerivedDataCacheKey.h"
#include "Hash/xxhash.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/StringBuilder.h"
#include "String/Find.h"
#include "String/LexFromString.h"
#include "String/ParseTokens.h"

namespace UE::DerivedData
{

struct Private::FCacheKeyFilterState
{
	struct FTypeRate
	{
		FCacheBucket Type;
		FCacheBucket LegacyType;
		uint32 Rate = 0;
	};

	TArray<FTypeRate> Types;
	TArray<FCacheKey> Keys;
	uint32 DefaultRate = 0;
	uint32 Salt = 0;

	inline uint32 ApplySalt(const uint32 Hash) const
	{
		return Hash * Salt;
	}

	static inline uint32 ConvertMatchRate(const double Rate)
	{
		return uint32(0.01 * Rate * MAX_uint32);
	}

	static bool TryParseTypeRate(const FStringView TypeConfig, FTypeRate& OutTypeRate);
};

bool Private::FCacheKeyFilterState::TryParseTypeRate(const FStringView TypeConfig, FTypeRate& OutTypeRate)
{
	FStringView TypeView;
	FStringView RateView = TEXTVIEW("100");

	String::ParseTokens(TypeConfig, TEXT('@'), [&TypeView, &RateView](const FStringView TypeOrRate)
	{
		(TypeView.IsEmpty() ? TypeView : RateView) = TypeOrRate;
	}, String::EParseTokensOptions::Trim);

	double Rate;
	LexFromString(Rate, RateView);
	if (!FCacheBucket::IsValidName(TypeView) || Rate < 0.0 || Rate > 100.0)
	{
		return false;
	}

	OutTypeRate.Type = FCacheBucket(WriteToAnsiString<64>(TypeView));
	OutTypeRate.LegacyType = FCacheBucket(WriteToAnsiString<64>(ANSITEXTVIEW("Legacy"), TypeView));
	OutTypeRate.Rate = ConvertMatchRate(Rate);
	return true;
}

FCacheKeyFilter FCacheKeyFilter::Parse(
	const TCHAR* const Config,
	const TCHAR* const BucketPrefix,
	const TCHAR* const KeyPrefix,
	const float DefaultRate)
{
	using namespace UE::DerivedData::Private;

	TArray<FCacheKeyFilterState::FTypeRate> Types;
	if (BucketPrefix)
	{
		FString TypeConfigArray;
		for (FStringView ConfigView(Config), PrefixView(BucketPrefix); FParse::Value(ConfigView.GetData(), BucketPrefix, TypeConfigArray, /*bShouldStopOnSeparator*/ false);)
		{
			const int32 PrefixIndex = String::FindFirst(ConfigView, PrefixView, ESearchCase::IgnoreCase);
			ConfigView.RightChopInline(PrefixIndex + PrefixView.Len() + TypeConfigArray.Len());
			String::ParseTokensMultiple(TypeConfigArray, {TEXT('+'), TEXT(',')}, [&Types](const FStringView TypeConfig)
			{
				FCacheKeyFilterState::FTypeRate TypeRate;
				if (FCacheKeyFilterState::TryParseTypeRate(TypeConfig, TypeRate))
				{
					Types.Emplace(TypeRate);
				}
			}, String::EParseTokensOptions::SkipEmpty | String::EParseTokensOptions::Trim);
		}
	}

	TArray<FCacheKey> Keys;
	if (KeyPrefix)
	{
		FString KeyArray;
		for (FStringView ConfigView(Config), PrefixView(KeyPrefix); FParse::Value(ConfigView.GetData(), KeyPrefix, KeyArray, /*bShouldStopOnSeparator*/ false);)
		{
			const int32 PrefixIndex = String::FindFirst(ConfigView, PrefixView, ESearchCase::IgnoreCase);
			ConfigView.RightChopInline(PrefixIndex + PrefixView.Len() + KeyArray.Len());
			String::ParseTokensMultiple(KeyArray, {TEXT('+'), TEXT(',')}, [&Keys](const FStringView KeyConfig)
			{
				FCacheKey Key;
				if (TryLexFromString(Key, KeyConfig))
				{
					Keys.Emplace(Key);
				}
			}, String::EParseTokensOptions::SkipEmpty | String::EParseTokensOptions::Trim);
		}
		Keys.Sort();
	}

	if (Types.IsEmpty() && Keys.IsEmpty() && DefaultRate == 0.0f)
	{
		return {};
	}

	FCacheKeyFilter Filter;
	Filter.State = MakePimpl<FCacheKeyFilterState, EPimplPtrMode::DeepCopy>();
	Filter.State->Types = MoveTemp(Types);
	Filter.State->Keys = MoveTemp(Keys);
	Filter.State->DefaultRate = FCacheKeyFilterState::ConvertMatchRate(FMath::Clamp(DefaultRate, 0.0f, 100.0f));
	Filter.SetSalt(0);
	return Filter;
}

void FCacheKeyFilter::SetSalt(uint32 Salt)
{
	// Generate a random salt in the range [1, MAX_int32].
	// Zero is invalid because the salt is multiplied with the key hash.
	// Values above MAX_int32 are avoided because FParse::Value cannot parse values in that range.
	Salt &= MAX_int32;
	while (Salt == 0)
	{
		// A new guid is the most reliable random value that the engine can generate.
		// Other options are not consistently seeded in a way that guarantees a random value when this executes.
		const FGuid Guid = FGuid::NewGuid();
		Salt = uint32(FXxHash64::HashBuffer(&Guid, sizeof(FGuid)).Hash & MAX_int32);
	}
	if (State)
	{
		State->Salt = Salt;
	}
}

uint32 FCacheKeyFilter::GetSalt() const
{
	return State ? State->Salt : 0;
}

bool FCacheKeyFilter::RequiresSalt() const
{
	using namespace UE::DerivedData::Private;
	using FTypeRate = FCacheKeyFilterState::FTypeRate;
	const auto HasFractionalRate = [](uint32 Rate) { return Rate > 0 && Rate < MAX_uint32; };
	return State && (HasFractionalRate(State->DefaultRate) || Algo::AnyOf(State->Types, Projection(&FTypeRate::Rate, HasFractionalRate)));
}

bool FCacheKeyFilter::IsMatch(const FCacheKey& Key) const
{
	using namespace UE::DerivedData::Private;

	if (!State)
	{
		return false;
	}

	if (!State->Keys.IsEmpty() && Algo::BinarySearch(State->Keys, Key) != INDEX_NONE)
	{
		return true;
	}

	uint32 TargetRate = State->DefaultRate;

	if (!State->Types.IsEmpty())
	{
		using FTypeRate = FCacheKeyFilterState::FTypeRate;
		const auto MatchType = [Bucket = Key.Bucket](const FTypeRate& TypeRate)
		{
			return TypeRate.Type == Bucket || TypeRate.LegacyType == Bucket;
		};
		if (const FTypeRate* TypeRate = Algo::FindByPredicate(State->Types, MatchType))
		{
			TargetRate = TypeRate->Rate;
		}
	}

	if (TargetRate == 0)
	{
		return false;
	}
	if (TargetRate == MAX_uint32)
	{
		return true;
	}
	return State->ApplySalt(GetTypeHash(Key.Hash)) < TargetRate;
}

} // UE::DerivedData
