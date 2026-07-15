// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataConfig.h"

#include "CoreGlobals.h"
#include "DerivedDataPrivate.h"
#include "Logging/StructuredLog.h"
#include "Misc/AsciiSet.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/StringBuilder.h"

namespace UE::DerivedData::Config
{

constexpr static TCHAR ObjectStartChar = '(';
constexpr static TCHAR ObjectEndChar = ')';
constexpr static TCHAR FieldDelimiterChar = ',';
constexpr static TCHAR ValueDelimiterChar = '=';
constexpr static TCHAR QuoteChar = '"';
constexpr static FAsciiSet ObjectStartSet{{ObjectStartChar, TEXT('\0')}};
constexpr static FAsciiSet ObjectEndSet{{ObjectEndChar, TEXT('\0')}};
constexpr static FAsciiSet FieldDelimiterSet{{FieldDelimiterChar, TEXT('\0')}};
constexpr static FAsciiSet QuoteSet{{QuoteChar, TEXT('\0')}};
constexpr static FAsciiSet EscapeSet = "\\";
constexpr static FAsciiSet WhiteSpaceSet = " \t";
constexpr static FAsciiSet FieldNameSet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_";

constexpr static FStringView FieldDelimiter = TEXTVIEW(", ");

FConfigIterator::FConfigIterator(FStringView Config)
{
	Config = FAsciiSet::TrimPrefixWith(Config, WhiteSpaceSet);
	if (Config.StartsWith(ObjectStartChar))
	{
		Tail = Config.RightChop(1);
		++*this;
	}
	else
	{
		bAtEnd = true;
		bError = true;
	}
}

FConfigIterator& FConfigIterator::operator++()
{
	if (bLastField)
	{
		bAtEnd = true;
	}

	if (bAtEnd)
	{
		return *this;
	}

	// Try to parse the optional field name.
	FStringView Config = Tail;
	Config = FAsciiSet::TrimPrefixWith(Config, WhiteSpaceSet);
	Name = FAsciiSet::FindPrefixWith(Config, FieldNameSet);
	if (!Name.IsEmpty())
	{
		FStringView Rest = Config.RightChop(Name.Len());
		Rest = FAsciiSet::TrimPrefixWith(Rest, WhiteSpaceSet);
		if (Rest.StartsWith(ValueDelimiterChar))
		{
			Config = Rest.RightChop(1);
			Config = FAsciiSet::TrimPrefixWith(Config, WhiteSpaceSet);
		}
		else
		{
			Name.Reset();
		}
	}

	// Try to parse the field value by skipping objects and strings.
	FStringView NextTail = Config;
	for (int32 ObjectCount = 0;;)
	{
		if (ObjectCount)
		{
			NextTail = FAsciiSet::TrimPrefixWithout(NextTail, ObjectStartSet | ObjectEndSet | QuoteSet);
		}
		else
		{
			NextTail = FAsciiSet::TrimPrefixWithout(NextTail, FieldDelimiterSet | ObjectStartSet | ObjectEndSet | QuoteSet);
		}

		if (NextTail.IsEmpty())
		{
			bAtEnd = true;
			bError = true;
			return *this;
		}

		const TCHAR Control = *NextTail.GetData();
		bLastField = (Control == ObjectEndChar);
		NextTail.RightChopInline(1);

		if (bLastField || Control == FieldDelimiterChar)
		{
			if (ObjectCount == 0)
			{
				break;
			}
			--ObjectCount;
		}
		else if (Control == ObjectStartChar)
		{
			++ObjectCount;
		}
		else if (Control == QuoteChar)
		{
			for (bool bFoundEnd = false; !bFoundEnd;)
			{
				NextTail = FAsciiSet::TrimPrefixWithout(NextTail, QuoteSet | EscapeSet);
				bFoundEnd = NextTail.IsEmpty() || NextTail.StartsWith(QuoteChar);
				NextTail.RightChopInline(bFoundEnd ? 1 : 2);
			}
		}
	}

	Tail = NextTail;
	Value = Config.LeftChop(NextTail.Len() + 1);
	Value = FAsciiSet::TrimSuffixWith(Value, WhiteSpaceSet);
	bAtEnd = Name.IsEmpty() && Value.IsEmpty();
	return *this;
}

FStringView ParseQuotedString(FStringView String, FStringBuilderBase& OutScratch)
{
	const bool bStartsWithQuote = String.StartsWith(QuoteChar);
	const bool bEndsWithQuote = String.EndsWith(QuoteChar);
	if (!bStartsWithQuote && !bEndsWithQuote)
	{
		// No Quotes
		return String;
	}
	String.RightChopInline(bStartsWithQuote ? 1 : 0);
	String.LeftChopInline(bEndsWithQuote ? 1 : 0);

	FStringView Prefix = FAsciiSet::FindPrefixWithout(String, EscapeSet);
	if (Prefix.Len() == String.Len())
	{
		// No Escapes
		return String;
	}

	OutScratch.Reset();
	do
	{
		OutScratch.Append(Prefix);
		String.RightChopInline(Prefix.Len() + 1);
		if (String.Len() > 0)
		{
			switch (TCHAR C = String.GetData()[0])
			{
			case '\'': OutScratch.AppendChar('\''); break;
			case '\"': OutScratch.AppendChar('\"'); break;
			case '\?': OutScratch.AppendChar('\?'); break;
			case '\\': OutScratch.AppendChar('\\'); break;
			case 'a': OutScratch.AppendChar('\a'); break;
			case 'b': OutScratch.AppendChar('\b'); break;
			case 'f': OutScratch.AppendChar('\f'); break;
			case 'n': OutScratch.AppendChar('\n'); break;
			case 'r': OutScratch.AppendChar('\r'); break;
			case 't': OutScratch.AppendChar('\t'); break;
			case 'v': OutScratch.AppendChar('\v'); break;
			default: OutScratch.AppendChar(C); break;
			}
			String.RightChopInline(1);
		}
		Prefix = FAsciiSet::FindPrefixWithout(String, EscapeSet);
	}
	while (!String.IsEmpty());
	return OutScratch;
}

bool FindFirstFieldValue(FStringView Config, FStringView Name, FStringView& OutValue)
{
	FConfigIterator It(Config);
	for (; It; ++It)
	{
		if (Name == It.GetName())
		{
			OutValue = It.GetValue();
			return true;
		}
	}
	return false;
}

bool VisitFieldValues(FStringView Config, FStringView Name, TFunctionWithContext<void (FStringView)> OnValue)
{
	FConfigIterator It(Config);
	for (; It; ++It)
	{
		if (Name == It.GetName())
		{
			OnValue(It.GetValue());
		}
	}
	return !It.HasError();
}

bool TrySplitNameAndConfig(FStringView Value, FStringView& OutName, FStringView& OutConfig)
{
	Value = FAsciiSet::TrimPrefixWith(Value, WhiteSpaceSet);
	Value = FAsciiSet::TrimSuffixWith(Value, WhiteSpaceSet);
	const FStringView Name = FAsciiSet::FindPrefixWith(Value, FieldNameSet);

	Value.RightChopInline(Name.Len());
	Value = FAsciiSet::TrimPrefixWith(Value, WhiteSpaceSet);

	const bool bHasValue = Value.StartsWith(ObjectStartChar) && Value.EndsWith(ObjectEndChar);
	const bool bHasName = !Name.IsEmpty() && (bHasValue || Value.IsEmpty());
	if (bHasName || bHasValue)
	{
		OutName = Name;
		OutConfig = Value;
		return true;
	}
	return false;
}

void AppendConfig(FStringBuilderBase& OutConfig, FStringView Name, FStringView Value)
{
	if (!Name.IsEmpty())
	{
		OutConfig.Append(Name);
		OutConfig.AppendChar(ValueDelimiterChar);
	}

	if (FStringView ValueName, Config; TrySplitNameAndConfig(Value, ValueName, Config))
	{
		OutConfig.Append(ValueName);
		if (!Config.IsEmpty())
		{
			FConfigIterator It(Config);
			const int32 ResetSize = OutConfig.Len();
			OutConfig.AppendChar(ObjectStartChar);
			for (bool bDelimit = false; It; ++It, bDelimit = true)
			{
				if (bDelimit)
				{
					OutConfig.Append(FieldDelimiter);
				}
				AppendConfig(OutConfig, It.GetName(), It.GetValue());
			}
			OutConfig.AppendChar(ObjectEndChar);
			if (It.HasError())
			{
				OutConfig.RemoveSuffix(OutConfig.Len() - ResetSize);
				OutConfig.Append(Config);
			}
		}
	}
	else
	{
		OutConfig.Append(Value);
	}
}

class FConfigBuilder
{
public:
	inline explicit FConfigBuilder(FStringBuilderBase& OutConfig)
		: Out(OutConfig)
	{
		Out.AppendChar(ObjectStartChar);
	}

	inline ~FConfigBuilder()
	{
		if (Out.ToView().EndsWith(FieldDelimiter, ESearchCase::CaseSensitive))
		{
			Out.RemoveSuffix(FieldDelimiter.Len());
		}

		Out.AppendChar(ObjectEndChar);

		if (bError)
		{
			Out.RemoveSuffix(Out.Len() - StartIndex);
		}
	}

	inline void Append(FStringView Name, FStringView Value)
	{
		AppendConfig(Out, Name, Value);
		Out.Append(FieldDelimiter);
	}

	inline void SetError() { bError = true; }
	inline bool HasError() const { return bError; }

private:
	FStringBuilderBase& Out;
	const int32 StartIndex = Out.Len();
	bool bError = false;
};

} // UE::DerivedData::Config

namespace UE::DerivedData
{

static bool TryConvertLegacyCacheStoreConfig(FStringView StoreName, FStringView GraphName, Config::FConfigBuilder& Builder);

static bool TryConvertLegacyCacheGraphConfig(FStringView GraphName, FStringView StoreName, FStringView StoreConfig, Config::FConfigBuilder& Builder)
{
	if (FStringView StoreType; Config::FindFirstFieldValue(StoreConfig, TEXTVIEW("Type"), StoreType))
	{
		bool bIsHierarchy = (StoreType == TEXTVIEW("Hierarchical"));
		if (bIsHierarchy || (StoreType == TEXTVIEW("AsyncPut") || StoreType == TEXTVIEW("KeyLength") || StoreType == TEXTVIEW("Verify")))
		{
			UE_CLOGFMT(!bIsHierarchy, LogDerivedDataCache, Warning,
				"The cache graph '{Graph}' contains cache store '{Store}' of deprecated type '{StoreType}'.",
				GraphName, StoreName, StoreType);
			int32 InnerCount = 0;
			Config::FConfigIterator It(StoreConfig);
			for (; It && (bIsHierarchy || InnerCount == 0); ++It)
			{
				if (It.GetName() == TEXTVIEW("Inner"))
				{
					++InnerCount;
					if (!TryConvertLegacyCacheStoreConfig(It.GetValue(), GraphName, Builder))
					{
						return false;
					}
				}
			}
			if (!bIsHierarchy && InnerCount == 0)
			{
				UE_LOGFMT(LogDerivedDataCache, Warning,
					"Failed to parse inner cache store in '{Store}' from cache graph '{Graph}'.", StoreName, GraphName);
				return false;
			}
			return !It.HasError();
		}
		Builder.Append({}, StoreName);
		return true;
	}
	UE_LOGFMT(LogDerivedDataCache, Warning,
		"Failed to parse cache store '{Store}' from cache graph '{Graph}' in config section [{Graph}] of the '{Config}' config. "
		"Migrate the configuration to the [DerivedDataCacheGraphs] section of the '{Config}' config.",
		("Graph", GraphName), ("Store", StoreName), ("Config", GEngineIni));
	return false;
}

bool TryFindCacheGraphConfig(FStringView GraphNameOrConfig, FStringBuilderBase& OutConfig)
{
	FStringView GraphName;
	FStringView GraphConfig;
	if (!GraphNameOrConfig.IsEmpty() && !Config::TrySplitNameAndConfig(GraphNameOrConfig, GraphName, GraphConfig))
	{
		return false;
	}

	if (!GraphName.IsEmpty() && !GraphConfig.IsEmpty())
	{
		return false;
	}

	// UE_DEPRECATED(5.8, "Remove the LegacyConfig block after the standard deprecation period.")
	if (FString LegacyConfig; !GraphName.IsEmpty() && GConfig->GetString(*WriteToString<64>(GraphName), TEXT("Root"), LegacyConfig, GEngineIni))
	{
		UE_LOGFMT(LogDerivedDataCache, Warning,
			"Configuring cache graph '{Graph}' in config section [{Graph}] of the '{Config}' config is deprecated. "
			"Migrate the configuration to the [DerivedDataCacheGraphs] section of the '{Config}' config.",
			("Graph", GraphNameOrConfig), ("Config", GEngineIni));
		Config::FConfigBuilder Builder(OutConfig);
		return TryConvertLegacyCacheGraphConfig(GraphName, TEXTVIEW("Root"), LegacyConfig, Builder);
	}

	FString GraphConfigStorage;
	if (!GraphName.IsEmpty() && GConfig->GetString(TEXT("DerivedDataCacheGraphs"), *WriteToString<64>(GraphName), GraphConfigStorage, GEngineIni))
	{
		GraphConfig = GraphConfigStorage;
	}

	if (GraphConfig.IsEmpty())
	{
		return false;
	}

	Config::FConfigBuilder Builder(OutConfig);
	Config::FConfigIterator It(GraphConfig);
	for (; It; ++It)
	{
		if (It.GetName() == TEXTVIEW("Deprecated"))
		{
			TStringBuilder<128> MessageScratch;
			UE_LOGFMT(LogDerivedDataCache, Warning, "The cache graph '{Graph}' is deprecated. {Message}",
				GraphName, Config::ParseQuotedString(It.GetValue(), MessageScratch));
		}
		else if (FStringView StoreName = It.GetName(); !StoreName.IsEmpty())
		{
			Builder.Append({}, StoreName);
		}
		else if (FStringView StoreConfig; Config::TrySplitNameAndConfig(It.GetValue(), StoreName, StoreConfig))
		{
			Builder.Append({}, StoreName);
		}
		else
		{
			Builder.SetError();
		}
	}
	if (It.HasError())
	{
		Builder.SetError();
	}
	return !Builder.HasError();
}

class FCacheStoreConfigBuilder
{
public:
	explicit FCacheStoreConfigBuilder(FStringBuilderBase& OutConfig, bool bLogLegacyConfig = true);

	bool TryFind(FStringView StoreName, FStringView GraphNameOrConfig);

private:
	bool TryQuery(FStringView StoreName, FStringView GraphNameOrConfig = {});
	bool TryBuild(FStringView StoreName, FStringView StoreConfig);

	Config::FConfigBuilder Builder;
	bool bLogLegacyConfig;
};

FCacheStoreConfigBuilder::FCacheStoreConfigBuilder(FStringBuilderBase& OutConfig, bool bInLogLegacyConfig)
	: Builder(OutConfig)
	, bLogLegacyConfig(bInLogLegacyConfig)
{
}

bool FCacheStoreConfigBuilder::TryFind(FStringView StoreName, FStringView GraphNameOrConfig)
{
	const bool bOk = TryQuery(StoreName, GraphNameOrConfig);
	if (!bOk)
	{
		Builder.SetError();
	}
	return bOk;
}

bool FCacheStoreConfigBuilder::TryQuery(FStringView StoreName, FStringView GraphNameOrConfig)
{
	FStringView GraphName;
	FStringView GraphConfig;
	if (!GraphNameOrConfig.IsEmpty() && !Config::TrySplitNameAndConfig(GraphNameOrConfig, GraphName, GraphConfig))
	{
		return false;
	}

	if (!GraphName.IsEmpty() && !GraphConfig.IsEmpty())
	{
		return false;
	}

	// UE_DEPRECATED(5.8, "Remove the LegacyConfig block after the standard deprecation period.")
	FString LegacyConfig;
	if (!GraphName.IsEmpty() && GConfig->GetString(*WriteToString<64>(GraphName), *WriteToString<64>(StoreName), LegacyConfig, GEngineIni))
	{
		UE_CLOGFMT(bLogLegacyConfig, LogDerivedDataCache, Warning,
			"Configuring cache store '{Store}' in config section [{Graph}] of the '{Config}' config is deprecated. "
			"Migrate the configuration to the [DerivedDataCacheStores] section of the '{Config}' config.",
			("Graph", GraphNameOrConfig), ("Store", StoreName), ("Config", GEngineIni));
		return TryBuild(StoreName, LegacyConfig);
	}

	bool bOk = true;
	bool bHasConfig = false;

	FString GraphConfigStorage;
	if (!GraphName.IsEmpty() && GConfig->GetString(TEXT("DerivedDataCacheGraphs"), *WriteToString<64>(GraphName), GraphConfigStorage, GEngineIni))
	{
		GraphConfig = GraphConfigStorage;
	}

	if (!GraphConfig.IsEmpty())
	{
		Config::FConfigIterator It(GraphConfig);
		for (; It; ++It)
		{
			FStringView GraphStoreName, GraphStoreConfig;
			if (Config::TrySplitNameAndConfig(It.GetValue(), GraphStoreName, GraphStoreConfig) &&
				(It.GetName() == StoreName || (It.GetName().IsEmpty() && GraphStoreName == StoreName)))
			{
				if (!GraphStoreConfig.IsEmpty())
				{
					bHasConfig = true;
					bOk &= TryBuild(StoreName, GraphStoreConfig);
				}
				StoreName = GraphStoreName;
				if (StoreName.IsEmpty())
				{
					return bOk && bHasConfig;
				}
				break;
			}
		}
	}

	FString StoreConfig;
	if (GConfig->GetString(TEXT("DerivedDataCacheStores"), *WriteToString<64>(StoreName), StoreConfig, GEngineIni))
	{
		bHasConfig = true;
		bOk &= TryBuild(StoreName, StoreConfig);
	}

	return bOk && bHasConfig;
}

bool FCacheStoreConfigBuilder::TryBuild(FStringView StoreName, FStringView StoreConfig)
{
	bool bOk = true;
	FStringView BaseName;

	Config::FConfigIterator It(StoreConfig);
	for (; It; ++It)
	{
		if (It.GetName() == TEXTVIEW("Base"))
		{
			bOk &= BaseName.IsEmpty(); // Only one Base is allowed.
			BaseName = It.GetValue();
		}
		else if (It.GetName() == TEXTVIEW("Deprecated"))
		{
			TStringBuilder<128> MessageScratch;
			UE_LOGFMT(LogDerivedDataCache, Warning, "The cache store '{Store}' is deprecated. {Message}",
				StoreName, Config::ParseQuotedString(It.GetValue(), MessageScratch));
		}
		else
		{
			Builder.Append(It.GetName(), It.GetValue());
		}
	}
	bOk &= !It.HasError();

	// Process the base after appending the current config to ensure overrides come first.
	if (bOk && !BaseName.IsEmpty())
	{
		bOk &= TryQuery(BaseName);
	}

	return bOk;
}

bool TryFindCacheStoreConfig(FStringView StoreName, FStringView GraphNameOrConfig, FStringBuilderBase& OutConfig)
{
	FCacheStoreConfigBuilder Builder(OutConfig);
	return Builder.TryFind(StoreName, GraphNameOrConfig);
}

static bool TryConvertLegacyCacheStoreConfig(FStringView StoreName, FStringView GraphName, Config::FConfigBuilder& Builder)
{
	TStringBuilder<256> StoreConfig;
	bool bOk = FCacheStoreConfigBuilder(StoreConfig, /*bLogLegacyConfig*/ false).TryFind(StoreName, GraphName);
	return bOk && TryConvertLegacyCacheGraphConfig(GraphName, StoreName, StoreConfig, Builder);
}

} // UE::DerivedData
