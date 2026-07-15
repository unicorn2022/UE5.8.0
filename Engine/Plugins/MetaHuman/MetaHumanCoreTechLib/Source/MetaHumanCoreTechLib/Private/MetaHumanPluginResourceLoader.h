// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "resourceloader/IMetaHumanFileResourceLoader.h"

struct JsonElement;

class FMetaHumanPluginResourceLoader : public coretechlib::epic::nls::IMetaHumanFileResourceLoader
{
public:
	// IMetaHumanFileResourceLoader

	virtual ~FMetaHumanPluginResourceLoader() override = default;
	
	virtual bool Exists(const char* InPathUtf8) const override;
	virtual bool ReadAllText(const char* InPathUtf8, char*& OutData, std::size_t& OutSize) const override;
	virtual bool ReadAllBytes(const char* InPathUtf8, void*& OutData, std::size_t& OutSize) const override;
	virtual void Free(const void* InPtr) const override;
	virtual bool ResolveDataLoadingPath(const char* InBasePathUtf8, const std::size_t& InBaseSize,
		const char* InAppendPathUtf8, std::size_t& InAppendSize, char*& OutPathUtf8, std::size_t& OutSize) const override;

private:
	FString GetResolvedMetaHumanPluginContentPath(const FString& InPath) const;
};
