// Copyright Epic Games, Inc. All Rights Reserved.

#include "Text3DInternalTypes.h"
#include "Containers/Array.h"
#include "UObject/NameTypes.h"

namespace UE::Text3D::Material
{

TConstArrayView<FName> GetSlotNames()
{
	static const TArray<FName> SlotNames = {
		TEXT("Front"),
		TEXT("Bevel"),
		TEXT("Extrude"),
		TEXT("Back"),
	};
	return SlotNames;
}

} // UE::Text3D::Material
