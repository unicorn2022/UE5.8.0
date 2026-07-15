// Copyright Epic Games, Inc. All Rights Reserved.

#include "DNAAutoConfig.h"
#include "DNAImportSettings.h"
#include "DNAUtils.h"

FDNAConfig AutoDetectDNAConfig(const FString& Path)
{
	FDNAConfig TempDNAConfig;
	// Only the coordinate system and vertex count need to be checked
	TempDNAConfig.Layers = static_cast<int32>(EDNADataLayer::GeometryWithoutBlendShapes);
	TSharedPtr<IDNAReader> DNAReader = LoadDNAFromFile(Path, TempDNAConfig);
	if (!DNAReader.IsValid())
	{
		return {};
	}

	const bool HasGeometry = (DNAReader->GetVertexPositionCount(0) != 0);
	const bool IsLegacyDNAAsset = [DNAReader]()
	{
		const FString FNDB("FN");
		const FString FNDB_V2("FN_MH_v2");
		const FString DBName = DNAReader->GetDBName();
		return (DBName == FNDB || DBName == FNDB_V2);
	}();

	// Start from project default settings so all user-configured fields (LODs, layers, etc.) are preserved
	FDNAConfig DNAConfig = GetDefault<UDNAImportSettings>()->DefaultDNAConfig;
	if (IsLegacyDNAAsset && !HasGeometry)
	{
		// FN DNAs are loaded into the legacy UE coordinate system to match the existing SkeletalMeshes that were
		// imported from FBX. The Geometry presence check determines if the DNA is to be imported into this legacy
		// environment where geometry was already present from FBX, or if this is a fresh import, where the SkeletalMesh
		// will be generated from the DNA.
		// The interchange module that handles SkeletalMesh generation from DNA relies on the DNA data being loaded
		// into the proper UE coordinate system.
		// Only copy the fields that Legacy() explicitly sets - everything else (LODs, layers, etc.) stays from the project defaults.
		const FDNAConfig LegacyConfig = FDNAConfig::Legacy();
		DNAConfig.CoordinateSystem = LegacyConfig.CoordinateSystem;
		DNAConfig.RotationSign = LegacyConfig.RotationSign;
		DNAConfig.RotationSequence = LegacyConfig.RotationSequence;
		DNAConfig.FaceWindingOrder = LegacyConfig.FaceWindingOrder;
	}
	DNAConfig.CoordinateSystemTransformPolicy = ECoordinateSystemTransformPolicy::Transform;
	return DNAConfig;
}
