// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaRundownDefines.generated.h"

UENUM(BlueprintType, DisplayName = "Motion Design Rundown Page Play Type")
enum class EAvaRundownPagePlayType : uint8
{
	PlayFromStart,
	PreviewFromStart,
	PreviewFromFrame
};

/** Indicates the source that initiated play pages */
UENUM()
enum class EAvaRundownPagePlaySource : uint8
{
	/** Undefined source */
	Undefined,
	/** The rundown editor initiated the request */
	Editor,
	/** The rundown server initiated the request*/
	Server,
	/** The rundown component initiated the request */
	Component,

	Default = Undefined,
};

namespace UE::AvaMedia::Rundown
{
	enum { InvalidPageId = -1};
}