// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <nrr/AssetGeneration.h>
#include <nls/geometry/LodGeneration.h>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

template <class T>
class AssetGenerationWrapper
{
public:
    AssetGenerationWrapper();

    bool LoadModelBinary(const std::string &path);

    std::string Apply(const std::string &lod0HeadEyeTeethMeshVerticesJson) const;

private:
    std::unique_ptr<epic::nls::AssetGeneration<T>> m_AssetGeneration;
};


template <class T>
class LodGenerationWrapper
{
public:
    LodGenerationWrapper();

    bool LoadModelBinary(const std::string &path, const int &option = 0);

    std::string Apply(const std::string &lod0HeadEyeTeethMeshVerticesJson) const;

private:
    std::unique_ptr<epic::nls::LodGeneration<T>> m_LodGeneration;
};

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
