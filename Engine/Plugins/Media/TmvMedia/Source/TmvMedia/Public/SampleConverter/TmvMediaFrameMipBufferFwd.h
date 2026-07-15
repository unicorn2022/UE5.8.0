// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

class FTmvMediaFrameMipBuffer;

/**
* A shared mip buffer acquired from a pool that handles returning to the pool upon release.
*/
typedef TSharedPtr<FTmvMediaFrameMipBuffer, ESPMode::ThreadSafe> FTmvMediaFrameMipBufferHandle;
