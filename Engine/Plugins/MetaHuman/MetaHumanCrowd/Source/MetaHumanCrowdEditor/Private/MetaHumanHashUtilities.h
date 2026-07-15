// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoHash.h"
#include "Misc/NotNull.h"

class UObject;
class UScriptStruct;

namespace UE::MetaHuman::HashUtilities
{
	/**
	 * Hashes state of a UObject via its Serialize path.
	 *
	 * @param Object		The object to hash.
	 * @param bLogDetails	If true, log the object name, serialized byte count, and hash.
	 *						Additional diagnostics are controlled by the MetaHuman.Crowd.HashDiagnostics cvar.
	 */
	FIoHash HashUObject(TNotNull<const UObject*> Object, bool bLogDetails = false);

	/**
	 * Hashes state of a USTRUCT instance via UStruct::SerializeItem.
	 *
	 * @param Struct		The UScriptStruct describing the struct type.
	 * @param StructData	Pointer to the struct instance data.
	 * @param bLogDetails	If true, log the struct name, serialized byte count, and hash.
	 *						Additional diagnostics are controlled by the MetaHuman.Crowd.HashDiagnostics cvar.
	 */
	FIoHash HashUStruct(TNotNull<const UScriptStruct*> Struct, TNotNull<const void*> StructData, bool bLogDetails = false);
}
