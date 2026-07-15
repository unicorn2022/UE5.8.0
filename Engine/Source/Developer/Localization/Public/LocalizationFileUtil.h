// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "HAL/Platform.h"
#include "Templates/FunctionFwd.h"
#include "Templates/SharedPointerFwd.h"

#define UE_API LOCALIZATION_API

class FLocalizationSCC;
class FString;
class ILocFileNotifies;

namespace UE::Localization::FileUtil
{
	
/**
 * Generic file hash, that hashes the binary representation of the file.
 */
UE_API uint64 BinaryHash(const FString& Filename);

/**
 * Generic file hash, that hashes the textual representation of the file (ignoring line endings and encoding).
 */
UE_API uint64 TextHash(const FString& Filename);

/**
 * Specialized file hash for PO files, that skips the transient information (like timestamps) from the PO file header.
 */
UE_API uint64 PortableObjectHash(const FString& Filename);

/**
 * Utility to write a file to disk, but only if it has changed.
 * 
 * This function works by using WriteFunc to generate a .tmp file and then uses HashFunc to compare the contents of the .tmp file against the contents of Filename (if it exists).
 * If the hash is different, then the file is atomically renamed to Filename, which ensures that we don't get a partial file write if the application crashes.
 * 
 * @return True if the file was modified or up-to-date, or false if the file failed to write.
 */
UE_API bool WriteFileIfModified(const FString& Filename, TFunctionRef<bool(const FString&)> WriteFunc, TFunctionRef<uint64(const FString&)> HashFunc);
UE_API bool WriteFileIfModified(const FString& Filename, TFunctionRef<bool(const FString&)> WriteFunc, TFunctionRef<uint64(const FString&)> HashFunc, const TSharedPtr<FLocalizationSCC>& SourceControlInfo);
UE_API bool WriteFileIfModified(const FString& Filename, TFunctionRef<bool(const FString&)> WriteFunc, TFunctionRef<uint64(const FString&)> HashFunc, const TSharedPtr<ILocFileNotifies>& LocFileNotifies);
	
}

#undef UE_API
