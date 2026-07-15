// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/ScriptDelegates.h"

#if UE_USE_DYNAMIC_DELEGATE_PAYLOADS

namespace UE::Core::Private
{
	FDelegatePayloadSerializeFunc       GDelegatePayloadSerializeFunc       = nullptr;
	FDelegatePayloadDeserializeFunc     GDelegatePayloadDeserializeFunc     = nullptr;
	FDelegatePayloadSerializeSlotFunc   GDelegatePayloadSerializeSlotFunc   = nullptr;
	FDelegatePayloadDeserializeSlotFunc GDelegatePayloadDeserializeSlotFunc = nullptr;
}

#endif
