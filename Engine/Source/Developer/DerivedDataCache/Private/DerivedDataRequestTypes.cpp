// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataRequestTypes.h"

#include "Containers/StringConv.h"
#include "Containers/StringView.h"
#include "Misc/QueuedThreadPool.h"
#include "Misc/StringBuilder.h"

namespace UE::DerivedData::Private
{

template <typename CharType>
static TStringBuilderBase<CharType>& PriorityToString(TStringBuilderBase<CharType>& Builder, const EPriority Priority)
{
	switch (Priority)
	{
	case EPriority::Lowest:   return Builder << ANSITEXTVIEW("Lowest");
	case EPriority::Low:      return Builder << ANSITEXTVIEW("Low");
	case EPriority::Normal:   return Builder << ANSITEXTVIEW("Normal");
	case EPriority::High:     return Builder << ANSITEXTVIEW("High");
	case EPriority::Highest:  return Builder << ANSITEXTVIEW("Highest");
	case EPriority::Blocking: return Builder << ANSITEXTVIEW("Blocking");
	}
	return Builder << ANSITEXTVIEW("Unknown");
}

template <typename CharType>
static bool PriorityFromString(EPriority& OutPriority, const TStringView<CharType> String)
{
	const auto ConvertedString = StringCast<UTF8CHAR, 16>(String.GetData(), String.Len());
	if (ConvertedString == UTF8TEXTVIEW("Lowest"))
	{
		OutPriority = EPriority::Lowest;
	}
	else if (ConvertedString == UTF8TEXTVIEW("Low"))
	{
		OutPriority = EPriority::Low;
	}
	else if (ConvertedString == UTF8TEXTVIEW("Normal"))
	{
		OutPriority = EPriority::Normal;
	}
	else if (ConvertedString == UTF8TEXTVIEW("High"))
	{
		OutPriority = EPriority::High;
	}
	else if (ConvertedString == UTF8TEXTVIEW("Highest"))
	{
		OutPriority = EPriority::Highest;
	}
	else if (ConvertedString == UTF8TEXTVIEW("Blocking"))
	{
		OutPriority = EPriority::Blocking;
	}
	else
	{
		return false;
	}
	return true;
}

template <typename CharType>
static TStringBuilderBase<CharType>& StatusToString(TStringBuilderBase<CharType>& Builder, const EStatus Status)
{
	switch (Status)
	{
	case EStatus::Ok:       return Builder << ANSITEXTVIEW("Ok");
	case EStatus::Error:    return Builder << ANSITEXTVIEW("Error");
	case EStatus::Canceled: return Builder << ANSITEXTVIEW("Canceled");
	}
	return Builder << ANSITEXTVIEW("Unknown");
}

template <typename CharType>
static bool StatusFromString(EStatus& OutStatus, const TStringView<CharType> String)
{
	const auto ConvertedString = StringCast<UTF8CHAR, 16>(String.GetData(), String.Len());
	if (ConvertedString == UTF8TEXTVIEW("Ok"))
	{
		OutStatus = EStatus::Ok;
	}
	else if (ConvertedString == UTF8TEXTVIEW("Error"))
	{
		OutStatus = EStatus::Error;
	}
	else if (ConvertedString == UTF8TEXTVIEW("Canceled"))
	{
		OutStatus = EStatus::Canceled;
	}
	else
	{
		return false;
	}
	return true;
}

} // UE::DerivedData::Private

namespace UE::DerivedData
{

FAnsiStringBuilderBase& operator<<(FAnsiStringBuilderBase& Builder, EPriority Priority) { return Private::PriorityToString(Builder, Priority); }
FWideStringBuilderBase& operator<<(FWideStringBuilderBase& Builder, EPriority Priority) { return Private::PriorityToString(Builder, Priority); }
FUtf8StringBuilderBase& operator<<(FUtf8StringBuilderBase& Builder, EPriority Priority) { return Private::PriorityToString(Builder, Priority); }

bool TryLexFromString(EPriority& OutPriority, FUtf8StringView String) { return Private::PriorityFromString(OutPriority, String); }
bool TryLexFromString(EPriority& OutPriority, FWideStringView String) { return Private::PriorityFromString(OutPriority, String); }

EQueuedWorkPriority ConvertToQueuedWorkPriority(EPriority Priority)
{
	switch (Priority)
	{
	case EPriority::Blocking: return EQueuedWorkPriority::Blocking;
	case EPriority::Highest:  return EQueuedWorkPriority::Highest;
	case EPriority::High:     return EQueuedWorkPriority::High;
	case EPriority::Normal:   return EQueuedWorkPriority::Normal;
	case EPriority::Low:      return EQueuedWorkPriority::Low;
	case EPriority::Lowest:   return EQueuedWorkPriority::Lowest;
	default: checkNoEntry();  return EQueuedWorkPriority::Normal;
	}
}

EPriority ConvertFromQueuedWorkPriority(EQueuedWorkPriority Priority)
{
	switch (Priority)
	{
	case EQueuedWorkPriority::Blocking: return EPriority::Blocking;
	case EQueuedWorkPriority::Highest:  return EPriority::Highest;
	case EQueuedWorkPriority::High:     return EPriority::High;
	case EQueuedWorkPriority::Normal:   return EPriority::Normal;
	case EQueuedWorkPriority::Low:      return EPriority::Low;
	case EQueuedWorkPriority::Lowest:   return EPriority::Lowest;
	default: checkNoEntry();            return EPriority::Normal;
	}
}

FAnsiStringBuilderBase& operator<<(FAnsiStringBuilderBase& Builder, EStatus Status) { return Private::StatusToString(Builder, Status); }
FWideStringBuilderBase& operator<<(FWideStringBuilderBase& Builder, EStatus Status) { return Private::StatusToString(Builder, Status); }
FUtf8StringBuilderBase& operator<<(FUtf8StringBuilderBase& Builder, EStatus Status) { return Private::StatusToString(Builder, Status); }

bool TryLexFromString(EStatus& OutStatus, FUtf8StringView String) { return Private::StatusFromString(OutStatus, String); }
bool TryLexFromString(EStatus& OutStatus, FWideStringView String) { return Private::StatusFromString(OutStatus, String); }

} // UE::DerivedData
