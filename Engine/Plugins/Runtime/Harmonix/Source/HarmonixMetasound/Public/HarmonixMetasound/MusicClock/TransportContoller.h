// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreTypes.h"

namespace Harmonix
{
	enum class ETransportRequest : uint8
	{
		None,
		Play,
		Pause,
		Continue,
		Stop,
		Seek
	};
}
