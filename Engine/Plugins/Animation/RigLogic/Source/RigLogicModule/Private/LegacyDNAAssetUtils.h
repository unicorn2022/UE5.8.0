// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "riglogic/RigLogic.h"

bool IsLegacyDNAAsset(const dna::Reader* DNAReader);
dna::ScopedPtr<dna::BinaryStreamReader> MigrateLegacyDNAAsset(dna::BoundedIOStream* Stream, const dna::Configuration& DNAConfig);
