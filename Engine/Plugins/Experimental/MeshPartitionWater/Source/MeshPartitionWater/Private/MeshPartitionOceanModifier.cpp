// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshPartitionOceanModifier.h"

namespace UE::MeshPartition
{

TSharedPtr<const MeshPartition::IModifierBackgroundOp> UOceanModifier::CreateBackgroundOp(const MeshPartition::EBuildType InBuildType) const
{
	return nullptr;
}

} // namespace UE::MeshPartition
