// Copyright Epic Games, Inc. All Rights Reserved.

#include "StandardEventSubscribers/SubsonicEventSubscriberBase.h"

namespace UE::Subsonic
{
	void USubsonicEventSubscriberBase::Initialize(FSubsystemCollectionBase& Collection)
	{
		Core::ISubsonicEventSubscriberInterface::Register();
	}

	void USubsonicEventSubscriberBase::Deinitialize()
	{
		Core::ISubsonicEventSubscriberInterface::Unregister();
	}
} // namespace UE::Subsonic