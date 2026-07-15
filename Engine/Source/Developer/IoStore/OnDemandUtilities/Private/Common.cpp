// Copyright Epic Games, Inc. All Rights Reserved.

#include "Common.h"
#include "Command.h"

#include "Misc/KeyChainUtilities.h"

namespace UE::IoStore::Tool::Common
{

////////////////////////////////////////////////////////////////////////////////
FKeyChain LoadCryptoKeys(const FContext& Context)
{
	FKeyChain Ret;

	const FString Path = FString(Context.Get<FStringView>(TEXT("-CryptoKeys")));
	if (Path.IsEmpty())
	{
		return Ret;
	}

	KeyChainUtilities::LoadKeyChainFromFile(*Path, Ret);

	return Ret;
}

} // namespace UE::IoStore::Tool::Common
