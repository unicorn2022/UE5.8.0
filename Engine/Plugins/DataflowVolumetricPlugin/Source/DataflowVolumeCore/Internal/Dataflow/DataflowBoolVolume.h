// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "DataflowVolume.h"

#include "DataflowBoolVolume.generated.h"

namespace UE::DataflowVolume::Private
{
	class FDataflowBoolVolumeImpl;
}

/* --------------------------------------------------------------------------------------------------------------- */
/* FDataflowBoolVolume */
/* --------------------------------------------------------------------------------------------------------------- */

USTRUCT()
struct FDataflowBoolVolume : public FDataflowVolume
{
	inline static const FName TypeName = "BoolVolume";

	GENERATED_USTRUCT_BODY()

public:
	FDataflowBoolVolume() {}
	virtual ~FDataflowBoolVolume() {}

	TSharedPtr<const UE::DataflowVolume::Private::FDataflowBoolVolumeImpl> GetBoolVolumeImpl() const;
	TSharedPtr<UE::DataflowVolume::Private::FDataflowBoolVolumeImpl> GetBoolVolumeImpl();

	DATAFLOWVOLUMECORE_API FVector GetVoxelSize() const;

private:
	FDataflowBoolVolume(TSharedPtr<UE::DataflowVolume::Private::FDataflowBoolVolumeImpl> InBoolVolume);
};


