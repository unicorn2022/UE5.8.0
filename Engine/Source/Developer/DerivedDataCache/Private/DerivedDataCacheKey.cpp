// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheKey.h"

#include "Async/SharedLock.h"
#include "Async/SharedMutex.h"
#include "Async/UniqueLock.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Containers/StringConv.h"
#include "DerivedDataCachePrivate.h"
#include "Hash/xxhash.h"
#include "IO/IoHash.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/AsciiSet.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinaryWriter.h"
#include "String/Find.h"
#include "String/Split.h"

namespace UE::DerivedData::Private
{

class FCacheBucketOwner : public FCacheBucket
{
public:
	inline explicit FCacheBucketOwner(FUtf8StringView Bucket);
	inline FCacheBucketOwner(FCacheBucketOwner&& Bucket);
	inline ~FCacheBucketOwner();

	FCacheBucketOwner(const FCacheBucketOwner&) = delete;
	FCacheBucketOwner& operator=(const FCacheBucketOwner&) = delete;

	using FCacheBucket::operator==;
	inline bool operator==(const FCacheBucketOwner& Other) const { return FCacheBucket::operator==(Other); }
	inline bool operator==(FUtf8StringView Bucket) const { return ToString() == Bucket; }
};

inline FCacheBucketOwner::FCacheBucketOwner(FUtf8StringView Bucket)
{
	checkf(FCacheBucket::IsValidName(Bucket),
		TEXT("A cache bucket name must be alphanumeric, non-empty, and contain at most %d code units. Name: '%s'"),
		FCacheBucket::MaxNameLen, *WriteToString<256>(Bucket));

	static_assert(sizeof(ANSICHAR) == sizeof(UTF8CHAR));
	const int32 BucketLen = Bucket.Len();
	const int32 PrefixSize = FMath::DivideAndRoundUp<int32>(1, sizeof(ANSICHAR));
	UTF8CHAR* Buffer = new UTF8CHAR[PrefixSize + BucketLen + 1];
	Buffer += PrefixSize;
	Name = reinterpret_cast<ANSICHAR*>(Buffer);

	reinterpret_cast<uint8*>(Buffer)[LengthOffset] = static_cast<uint8>(BucketLen);
	Bucket.CopyString(Buffer, BucketLen);
	Buffer += BucketLen;
	*Buffer = UTF8CHAR('\0');
}

inline FCacheBucketOwner::FCacheBucketOwner(FCacheBucketOwner&& Bucket)
	: FCacheBucket(Bucket)
{
	Bucket.Reset();
}

inline FCacheBucketOwner::~FCacheBucketOwner()
{
	if (Name)
	{
		const int32 PrefixSize = FMath::DivideAndRoundUp<int32>(1, sizeof(ANSICHAR));
		delete[] (Name - PrefixSize);
	}
}

struct FCacheBucketOwnerKeyFuncs : DefaultKeyFuncs<FCacheBucketOwner>
{
	static uint32 GetKeyHash(const FUtf8StringView Key)
	{
		const int32 Len = Key.Len();
		check(Len <= FCacheBucket::MaxNameLen);
		UTF8CHAR LowerKey[FCacheBucket::MaxNameLen];
		UTF8CHAR* LowerKeyIt = LowerKey;
		for (const UTF8CHAR& Char : Key)
		{
			*LowerKeyIt++ = TChar<UTF8CHAR>::ToLower(Char);
		}
		return uint32(FXxHash64::HashBuffer(LowerKey, Len).Hash);
	}

	static uint32 GetKeyHash(const FCacheBucketOwner& Key)
	{
		return GetKeyHash(Key.ToString());
	}
};

class FCacheBuckets
{
public:
	template <typename CharType>
	inline FCacheBucket FindOrAdd(TStringView<CharType> Name);

	inline void GetDisplayName(FCacheBucket Bucket, FStringBuilderBase& OutDisplayName);
	inline void SetDisplayName(FCacheBucket Bucket, FStringView DisplayName);

private:
	FSharedMutex Mutex;
	TSet<FCacheBucketOwner, FCacheBucketOwnerKeyFuncs> Buckets;
	TMap<FCacheBucket, FString> DisplayNames;
};

template <typename CharType>
inline FCacheBucket FCacheBuckets::FindOrAdd(const TStringView<CharType> Name)
{
	const auto NameCast = StringCast<UTF8CHAR, FCacheBucket::MaxNameLen + 1>(Name.GetData(), Name.Len());
	const FUtf8StringView NameView = NameCast;
	uint32 Hash = 0;

	if (NameView.Len() <= FCacheBucket::MaxNameLen)
	{
		Hash = FCacheBucketOwnerKeyFuncs::GetKeyHash(NameView);
		TSharedLock ReadLock(Mutex);
		if (const FCacheBucketOwner* Bucket = Buckets.FindByHash(Hash, NameView))
		{
			return *Bucket;
		}
	}

	FCacheBucketOwner LocalBucket(NameView);
	TUniqueLock WriteLock(Mutex);
	return Buckets.FindOrAddByHash(Hash, MoveTemp(LocalBucket));
}

void FCacheBuckets::GetDisplayName(FCacheBucket Bucket, FStringBuilderBase& OutDisplayName)
{
	if (TSharedLock ReadLock(Mutex); const FString* DisplayName = DisplayNames.Find(Bucket))
	{
		OutDisplayName << *DisplayName;
		return;
	}
	FAnsiStringView BucketName = Bucket.ToString();
	if (BucketName.StartsWith(ANSITEXTVIEW("Legacy")))
	{
		BucketName.RightChopInline(TEXTVIEW("Legacy").Len());
	}
	OutDisplayName << BucketName;
}

void FCacheBuckets::SetDisplayName(FCacheBucket Bucket, FStringView DisplayName)
{
	if (TSharedLock ReadLock(Mutex); const FString* ExistingDisplayName = DisplayNames.Find(Bucket))
	{
		if (*ExistingDisplayName == DisplayName)
		{
			return;
		}
	}

	TUniqueLock WriteLock(Mutex);
	DisplayNames.Emplace(Bucket, DisplayName);
}

static FCacheBuckets& GetCacheBuckets()
{
	static FCacheBuckets Buckets;
	return Buckets;
}

} // UE::DerivedData::Private

namespace UE::DerivedData
{

FCacheBucket::FCacheBucket(FUtf8StringView InName)
	: FCacheBucket(Private::GetCacheBuckets().FindOrAdd(InName))
{
}

FCacheBucket::FCacheBucket(FWideStringView InName)
	: FCacheBucket(Private::GetCacheBuckets().FindOrAdd(InName))
{
}

FCacheBucket::FCacheBucket(FUtf8StringView Name, FStringView DisplayName)
	: FCacheBucket(Name)
{
	if (!DisplayName.IsEmpty())
	{
		Private::GetCacheBuckets().SetDisplayName(*this, DisplayName);
	}
}

FCacheBucket::FCacheBucket(FWideStringView Name, FStringView DisplayName)
	: FCacheBucket(Name)
{
	if (!DisplayName.IsEmpty())
	{
		Private::GetCacheBuckets().SetDisplayName(*this, DisplayName);
	}
}

void FCacheBucket::ToDisplayName(FStringBuilderBase& OutDisplayName) const
{
	Private::GetCacheBuckets().GetDisplayName(*this, OutDisplayName);
}

template <typename CharType>
static inline bool TryLexKeyFromString(FCacheKey& OutKey, TStringView<CharType> String)
{
	TStringView<CharType> Bucket, Hash;
	return String::SplitFirstChar(String, '/', Bucket, Hash) &&
		TryLexFromString(OutKey.Bucket, Bucket) && TryLexFromString(OutKey.Hash, Hash);
}

bool TryLexFromString(FCacheKey& OutKey, FAnsiStringView String)
{
	return TryLexKeyFromString(OutKey, String);
}

bool TryLexFromString(FCacheKey& OutKey, FWideStringView String)
{
	return TryLexKeyFromString(OutKey, String);
}

void SerializeForLog(FCbWriter& Writer, const FCacheKey& Key)
{
	Writer.AddString(WriteToString<96>(Key));
}

FCacheKey ConvertLegacyCacheKey(const FStringView Key)
{
	FTCHARToUTF8 Utf8Key(Key);
	TUtf8StringBuilder<64> Utf8Bucket;
	Utf8Bucket << ANSITEXTVIEW("Legacy");
	if (const int32 BucketEnd = String::FindFirstChar(Utf8Key, '_'); BucketEnd != INDEX_NONE)
	{
		Utf8Bucket << FUtf8StringView(Utf8Key).Left(BucketEnd);
	}
	const FCacheBucket Bucket(Utf8Bucket);
	return {Bucket, FIoHash::HashBuffer(MakeMemoryView(Utf8Key))};
}

FCacheKey BuildLegacyCacheKey(FStringView PluginName, FStringView VersionString, FStringView PluginSpecificCacheKeySuffix)
{
	const auto AppendSanitized = [](FStringBuilderBase& Out, FStringView In)
	{
		constexpr FAsciiSet ValidChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_$";
		for (;;)
		{
			FStringView ValidPrefix = FAsciiSet::FindPrefixWith(In, ValidChars);
			Out.Append(ValidPrefix);
			In.RemovePrefix(ValidPrefix.Len());

			if (In.IsEmpty())
			{
				break;
			}

			Out.Appendf(TEXT("$%x"), uint32(In.GetData()[0]));
			In.RemovePrefix(1);
		}
	};

	TStringBuilder<1024> Out;
	Out.Reserve(PluginName.Len() + 1 + VersionString.Len() + 1 + PluginSpecificCacheKeySuffix.Len());
	AppendSanitized(Out, PluginName);
	Out.AppendChar(TEXT('_'));
	AppendSanitized(Out, VersionString);
	Out.AppendChar(TEXT('_'));
	AppendSanitized(Out, PluginSpecificCacheKeySuffix);
	return ConvertLegacyCacheKey(Out);
}

} // UE::DerivedData
