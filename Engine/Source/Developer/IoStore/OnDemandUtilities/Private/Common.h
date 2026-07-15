// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/StringView.h"

namespace UE::IoStore::Tool { class FContext; }
struct FKeyChain;

namespace UE::IoStore::Tool::Common
{

////////////////////////////////////////////////////////////////////////////////
FKeyChain LoadCryptoKeys(const FContext& Context);

} // namespace UE::IoStore::Tool::Common
