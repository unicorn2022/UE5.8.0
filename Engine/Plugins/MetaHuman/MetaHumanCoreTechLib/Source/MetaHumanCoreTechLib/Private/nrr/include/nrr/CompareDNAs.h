// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include <carbon/Common.h>
#include <dna/BinaryStreamReader.h>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

/* Compare DNAs (vertex positions, normals, neutral joint positions, rotations and optionally skinning weights), and return true if same to within the specified tolerances, false otherwise*/
bool CompareDNAs(dna::BinaryStreamReader* goldDataReader, dna::BinaryStreamReader* outputReader, float positionTolerance = 0.002f, float angleTolerance = 0.001f, float weightTolerance = 0.002f, bool bCompareSkinningWeights = true);

CARBON_NAMESPACE_END(TITAN_NAMESPACE)