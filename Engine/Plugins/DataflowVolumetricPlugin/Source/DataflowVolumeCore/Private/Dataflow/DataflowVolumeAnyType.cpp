// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowVolumeAnyType.h"
#include "Dataflow/DataflowAnyTypeRegistry.h"

namespace UE::Dataflow
{
	void RegisterVolumeAnyTypes()
	{
		UE_DATAFLOW_REGISTER_ANYTYPE(FDataflowVolumeTypes);
	}
}