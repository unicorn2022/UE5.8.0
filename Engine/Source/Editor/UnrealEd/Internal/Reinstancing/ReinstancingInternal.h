// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/** Flags to copy during reinstantiation of UObjects */
#define RF_Reinst					((EObjectFlags)(RF_Public | RF_ArchetypeObject | RF_Transactional | \
	RF_Transient | RF_TextExportTransient | RF_InheritableComponentTemplate | RF_Standalone | RF_HasDynamicImports)) 
