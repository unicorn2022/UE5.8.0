// Copyright Epic Games, Inc. All Rights Reserved.

#include "TaskNode.h"

#define LOCTEXT_NAMESPACE "UE::Insights::TaskGraphProfiler::FTaskNode"

namespace UE::Insights::TaskGraphProfiler
{

INSIGHTS_IMPLEMENT_RTTI(FTaskNode)

const FText FTaskNode::GetDisplayName() const
{
	return FText::FromString(FString::Printf(TEXT("task_%d"), GetRowIndex()));
}

} // namespace UE::Insights::TaskGraphProfiler

#undef LOCTEXT_NAMESPACE
