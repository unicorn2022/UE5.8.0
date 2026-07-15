// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RtpPacket.h"

class FRtpParser
{
public:
	static bool Parse(const TArrayView<const uint8> InData, FRtpHeader& OutHeader, TArrayView<const uint8>& OutPayload);
};
