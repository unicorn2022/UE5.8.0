// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dataflow/DataflowTaskGraph.h"

namespace UE::Dataflow
{
	struct FSystem
	{
	public:
		static Dataflow::FSystem& Get();

		/* TaskGraph to schedule Dtaaflow specific tasks */
		Dataflow::FTaskGraph TaskGraph;
	};

}