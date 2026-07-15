// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/LODInfo.h"

#include "Templates/TypeHash.h"
#include "MuR/Types.h"


namespace UE::Mutable::Private
{
	uint32 GetTypeHash(const FLODInfo& Key)
	{
		uint32 Result = 0;
			
		Result = HashCombineFast(Result, GetTypeHash(Key.ScreenSize));
		Result = HashCombineFast(Result, GetTypeHash(Key.LODHysteresis));
		Result = HashCombineFast(Result, GetTypeHash(Key.bSupportUniformlyDistributedSampling));
		Result = HashCombineFast(Result, GetTypeHash(Key.bAllowCPUAccess));
		 		
		return Result;
	}
}
