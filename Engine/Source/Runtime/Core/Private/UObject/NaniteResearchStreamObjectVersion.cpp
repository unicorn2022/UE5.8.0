// Copyright Epic Games, Inc. All Rights Reserved.
#include "UObject/NaniteResearchStreamObjectVersion.h"
#include "UObject/DevObjectVersion.h"
#include "Containers/Map.h"
#include "Misc/Guid.h"


TMap<FGuid, FGuid> FNaniteResearchStreamObjectVersion::GetSystemGuids()
{
	TMap<FGuid, FGuid> SystemGuids;
	const FDevSystemGuids& DevGuids = FDevSystemGuids::Get();

	SystemGuids.Add(DevGuids.NANITE_DERIVEDDATA_VER, FGuid("759C3B6C-C86B-456F-CD86-D12151DAD5FC"));
	SystemGuids.Add(DevGuids.STATICMESH_DERIVEDDATA_VER, FGuid("E5FAD907-D19B-4754-8325-9C48E405CCCC"));
	SystemGuids.Add(DevGuids.SkeletalMeshDerivedDataVersion, FGuid("9B0FD28E-09DA-4E72-BFF8-7019EEE7270B"));
	SystemGuids.Add(DevGuids.MaterialTranslationDDCVersion, FGuid("36815964-232F-430C-89F5-0B76827F63C1"));
	SystemGuids.Add(DevGuids.GROOM_DERIVED_DATA_VERSION, FGuid("AC70F226-CEFB-47FF-A67E-C3606FF53B66"));

	return SystemGuids;
}
