// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

UE_DEPRECATED_HEADER(5.8, "Use WorldPartition/HLOD/HLODSourceCellPlaceholderActor.h instead of WorldPartition/HLOD/CustomHLODPlaceholderActor.h")
#include "WorldPartition/HLOD/HLODSourceCellPlaceholderActor.h"

#if WITH_EDITOR
UE_DEPRECATED(5.8, "AWorldPartitionCustomHLODPlaceholder has been renamed to AWorldPartitionHLODSourceCellPlaceholder")
typedef AWorldPartitionHLODSourceCellPlaceholder AWorldPartitionCustomHLODPlaceholder;
#endif
