// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pipeline/Pin.h"

namespace UE::MetaHuman::Pipeline
{

FPin::FPin(const FString& InName, EPinDirection InDirection, EPinType InType)
	: Name(InName), Direction(InDirection), Type(InType)
{
}

FPin::FPin(const FString& InName, EPinDirection InDirection, EPinType InType, int32 InGroup)
	: Name(InName), Direction(InDirection), Type(InType), Group(InGroup)
{
}

FPin::FPin(const FString& InName, EPinDirection InDirection, EPinType InType, int32 InGroup, bool bInIsPassthrough, bool bInIsOptional)
	: Name(InName), Direction(InDirection), Type(InType), Group(InGroup), bIsPassthrough(bInIsPassthrough), bIsOptional(bInIsOptional)
{
}

FString FPin::ToString() const
{
	FString Message;

	Message += FString::Printf(TEXT("  Name [%s] Direction [%i] Type [%i] Group [%i] Address [%s]"), *Name, EnumToUnderlyingType(Direction), EnumToUnderlyingType(Type), Group, *Address);

	return Message;
}

}
