// Copyright Epic Games, Inc. All Rights Reserved.

#include <nrr/AssetGenerationWrapper.h>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

template <class T>
AssetGenerationWrapper<T>::AssetGenerationWrapper()
{
    m_AssetGeneration = std::make_unique<epic::nls::AssetGeneration<T>>();
}

template <class T>
bool AssetGenerationWrapper<T>::LoadModelBinary(const std::string &path)
{
    auto inputString = TITAN_NAMESPACE::ReadFile(path);
    auto modelDescription = TITAN_NAMESPACE::ReadJson(inputString);

    const auto &inputDict = modelDescription.Map();
    const auto &modelsMap = inputDict.at("Models").Map();
    const std::string &binaryPath = modelsMap.at("AssetGenerationBinary").String();
    std::string binaryPathAbsolute;

    if (binaryPath[0] == '.' || binaryPath[0] == '/' || binaryPath[0] == '\\')
    {
        std::filesystem::path p(path);
        std::filesystem::path folderPath = p.parent_path();

        binaryPathAbsolute = folderPath.string() + binaryPath;
    }
    else
    {
        binaryPathAbsolute = binaryPath;
    }

    return m_AssetGeneration->LoadModelBinary(binaryPathAbsolute);
}

template <class T>
std::string AssetGenerationWrapper<T>::Apply(const std::string &lod0HeadEyeTeethMeshVerticesJsonString) const
{
    // repack input to Eigen::Matrix
    auto lod0HeadEyeTeethMeshVerticesJson = TITAN_NAMESPACE::ReadJson(lod0HeadEyeTeethMeshVerticesJsonString);
    std::map<std::string, Eigen::Matrix<T, 3, -1>> lod0HeadEyeTeethMeshVertices;

    for (const auto &element : lod0HeadEyeTeethMeshVerticesJson.Map())
    {
        const auto &name = element.first;
        const auto &dataVector = element.second.Array();

        lod0HeadEyeTeethMeshVertices[name] = Eigen::Matrix<T, 3, -1>::Zero(3, dataVector.size());

        int col = 0;
        for (int pointIdx = 0; pointIdx < (int)dataVector.size(); ++pointIdx)
        {
            int row = 0;
            for (auto const &pointCoord : dataVector[pointIdx].Array())
            {
                lod0HeadEyeTeethMeshVertices[name].col(col)(row) = pointCoord.Value<T>();

                row++;
            }

            col++;
        }
    }


    // calculate the assets
    std::map<std::string, Eigen::Matrix<T, 3, -1>> lod0AssetVertices;

    m_AssetGeneration->Apply(lod0HeadEyeTeethMeshVertices, lod0AssetVertices);

    // repack output to json
    std::map<std::string, std::vector<std::vector<T>>> output;

    for (const auto &element : lod0AssetVertices)
    {
        const auto &name = element.first;
        const auto &data = element.second;

        output[name] = {};

        for (int col = 0; col < data.cols(); ++col)
        {
            std::vector<T> point;

            for (int row = 0; row < 3; ++row)
            {
                point.push_back(data.col(col)(row));
            }

            output[name].push_back(point);
        }
    }

    JsonElement outputJson(output);

    std::ostringstream ss;
    WriteJson(ss, outputJson);

    return ss.str();
}


template <class T>
LodGenerationWrapper<T>::LodGenerationWrapper()
{
    m_LodGeneration = std::make_unique<epic::nls::LodGeneration<T>>();
}

template <class T>
bool LodGenerationWrapper<T>::LoadModelBinary(const std::string &path, const int &option)
{
    auto inputString = TITAN_NAMESPACE::ReadFile(path);
    auto modelDescription = TITAN_NAMESPACE::ReadJson(inputString);

    const auto &inputDict = modelDescription.Map();
    const auto &modelsMap = inputDict.at("Models").Map();
    const auto &lodGenerationModels = modelsMap.at("LODGenerationBinaries").Array();
    if (option < 0 || option > static_cast<int>(lodGenerationModels.size() - 1))
    {
        LOG_ERROR("Invalid option specified, {} models available.", lodGenerationModels.size());
        return false;
    }
    const std::string &binaryPath = lodGenerationModels.at(option).String();
    std::string binaryPathAbsolute;

    if (binaryPath[0] == '.' || binaryPath[0] == '/' || binaryPath[0] == '\\')
    {
        std::filesystem::path p(path);
        std::filesystem::path folderPath = p.parent_path();

        binaryPathAbsolute = folderPath.string() + binaryPath;
    }
    else
    {
        binaryPathAbsolute = binaryPath;
    }

    return m_LodGeneration->LoadModelBinary(binaryPathAbsolute);
}

template <class T>
std::string LodGenerationWrapper<T>::Apply(const std::string &lod0MeshVerticesJsonString) const
{
    // repack input to Eigen::Matrix
    auto lod0MeshVerticesJson = TITAN_NAMESPACE::ReadJson(lod0MeshVerticesJsonString);
    std::map<std::string, Eigen::Matrix<T, 3, -1>> lod0MeshVertices;

    for (const auto &element : lod0MeshVerticesJson.Map())
    {
        const auto &name = element.first;
        const auto &dataVector = element.second.Array();

        lod0MeshVertices[name] = Eigen::Matrix<T, 3, -1>::Zero(3, dataVector.size());

        int col = 0;
        for (int pointIdx = 0; pointIdx < (int)dataVector.size(); ++pointIdx)
        {
            int row = 0;
            for (auto const &pointCoord : dataVector[pointIdx].Array())
            {
                lod0MeshVertices[name].col(col)(row) = pointCoord.Value<T>();

                row++;
            }

            col++;
        }
    }


    // calculate the assets
    std::map<std::string, Eigen::Matrix<T, 3, -1>> higherLodTgtVertices;

    m_LodGeneration->Apply(lod0MeshVertices, higherLodTgtVertices);

    // repack output to json
    std::map<std::string, std::vector<std::vector<T>>> output;

    for (const auto &element : higherLodTgtVertices)
    {
        const auto &name = element.first;
        const auto &data = element.second;

        output[name] = {};

        for (int col = 0; col < data.cols(); ++col)
        {
            std::vector<T> point;

            for (int row = 0; row < 3; ++row)
            {
                point.push_back(data.col(col)(row));
            }

            output[name].push_back(point);
        }
    }

    JsonElement outputJson(output);

    std::ostringstream ss;
    WriteJson(ss, outputJson);

    return ss.str();
}

template class AssetGenerationWrapper<float>;
template class AssetGenerationWrapper<double>;

template class LodGenerationWrapper<float>;
template class LodGenerationWrapper<double>;

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
