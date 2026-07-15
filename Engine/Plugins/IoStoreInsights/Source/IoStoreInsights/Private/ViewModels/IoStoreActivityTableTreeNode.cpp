// Copyright Epic Games, Inc. All Rights Reserved.

#include "IoStoreActivityTableTreeNode.h"

namespace UE::IoStoreInsights
{

INSIGHTS_IMPLEMENT_RTTI(FIoStoreActivityNode)

const FText FIoStoreActivityNode::GetDisplayName() const
{
	return FText::FromString(FString::Printf(TEXT("request_%u"), RequestIndex));
}

} // UE::IoStoreInsights
