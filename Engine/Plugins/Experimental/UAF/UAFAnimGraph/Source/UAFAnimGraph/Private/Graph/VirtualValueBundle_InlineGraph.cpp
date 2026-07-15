// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/VirtualValueBundle_InlineGraph.h"

namespace UE::UAF
{

const FTraitPtr* FVirtualValueBundle_InlineGraph::GetInlineGraph() const
{
	return &TraitPtr;
}

}
