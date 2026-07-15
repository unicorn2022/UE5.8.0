// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BoneIndices.h"

namespace UE::UAF
{
	// This represents an attribute index within an attribute set
	struct FAttributeSetIndex final : public FBoneIndexBase
	{
	public:
		// Constructs an invalid index
		FAttributeSetIndex() = default;

		// Constructs an index with the specified value
		explicit FAttributeSetIndex(int32 InAttributeIndex) { BoneIndex = InAttributeIndex; }

		UE_BONE_INDEX_OPERATORS(FAttributeSetIndex)
	};
}
