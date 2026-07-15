// Copyright Epic Games, Inc. All Rights Reserved.

#include <rigcalibration/RigCalibrationDatabaseDescription.h>
#include <carbon/io/JsonIO.h>
#include <carbon/io/Utils.h>

#include <filesystem>

#include "resourceloader/MetaHumanFileResourceLoader.h"

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

const std::string InsertSuffix(const std::string& filename, const std::string& suffix)
{
    size_t dotPosition = filename.find_last_of('.');
    if (dotPosition == std::string::npos) {
        return filename + suffix;
    }
    return filename.substr(0, dotPosition) + suffix + filename.substr(dotPosition);
};

const std::string& RigCalibrationDatabaseDescription::GetExpressionModelPath(const int it) const
{
    return m_loadedModelPaths[it];
}

const std::string& RigCalibrationDatabaseDescription::GetExpressionBlendshapeModelPath(const int it) const
{
    return m_loadedBlendshapeModelPaths[it];
}

bool RigCalibrationDatabaseDescription::Load(const std::string& inputFile, bool jointsAndBlends)
{
	const std::string dataDescriptionDirectory = std::filesystem::path(inputFile).parent_path().string();

	JsonElement json = FMetaHumanFileResourceLoader::GetJsonElementForFile(inputFile);
	
    if (json.Contains("identity_model_name"))
    {
        m_loadedIdentityModelName = json["identity_model_name"].String();
    }
    else
    {
        LOG_ERROR("Pca model database description does not contain identity_model_name.");
        return false;
    }

    if (json.Contains("version_identifier"))
    {
        m_modelVersionIdentifier = json["version_identifier"].String();
    }
    else
    {
        LOG_ERROR("Pca model database description does not contain version_identifier. Please switch to the newer database.");
        return false;
    }

    if (json.Contains("blendshape_model_suffix"))
    {
        m_blendshapeModelSuffix = json["blendshape_model_suffix"].String();
    }

    if (json.Contains("skinning_model_name"))
    {
        m_skinningModelName = json["skinning_model_name"].String();
        if (json.Contains("skinning_model_path"))
        {
            m_skinningModelPath = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, json["skinning_model_path"].String());
        }
        else
        {
            LOG_INFO("Pca model database description does not contain skinning_model_name.");
        }
    }
    else
    {
        LOG_INFO("Pca model database description does not contain skinning_model_name.");
    }

    if (json.Contains("expression_models"))
    {
        const JsonElement &jExpressions = json["expression_models"];
        auto neutralExpressionIt = jExpressions.Map().find(m_loadedIdentityModelName);
        if (neutralExpressionIt != jExpressions.Map().end())
        {
        	const std::string NeutralExpression = neutralExpressionIt->second.String();
        	FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, NeutralExpression);
            m_loadedModelNames.push_back(neutralExpressionIt->first);
            m_loadedModelPaths.push_back(FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, NeutralExpression));
            m_loadedBlendshapeModelPaths.push_back({});
        }
        else
        {
            LOG_ERROR("Pca model database description does not contain model for \"{}\".", m_loadedIdentityModelName);
            return false;
        }
        for (auto &&[expressionName, expressionPath] : jExpressions.Map())
        {
            if (expressionName == m_loadedIdentityModelName) continue;
            const auto path = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, expressionPath.String());
            m_loadedModelNames.push_back(expressionName);
            m_loadedModelPaths.push_back(path);
            if (expressionName == m_loadedIdentityModelName)
            {
                m_loadedBlendshapeModelPaths.push_back({});
            }
            else
            {
                m_loadedBlendshapeModelPaths.push_back(InsertSuffix(path, m_blendshapeModelSuffix));
            }
        }
    }
    else
    {
        LOG_ERROR("Pca model database description does not contain expression_models.");
        return false;
    }

    const auto neutralNameIt = std::find(m_loadedModelNames.begin(), m_loadedModelNames.end(), m_loadedIdentityModelName);
    if (neutralNameIt == m_loadedModelNames.end())
    {
        LOG_ERROR("expression_models do not contain {}", m_loadedIdentityModelName);
        return false;
    }

    if (json.Contains("stabilization"))
    {
        m_stabModelPath = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, json["stabilization"].String());
    }

    if (json.Contains("genecode"))
    {
        m_geneCodeMatrixPath = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, json["genecode"].String());
    }
    else
    {
        LOG_ERROR("Pca model database description does not contain genecode.");
        return false;
    }

    if (json.Contains("archetype"))
    {
        if (json["archetype"].IsObject())
        {
            if (json["archetype"].Contains("joints_only") && json["archetype"].Contains("joints_and_blends"))
            {
                if (jointsAndBlends)
                {
                    if (json["archetype"]["joints_and_blends"].IsObject())
                    {
                        const std::string jointsWithRBF = json["archetype"]["joints_and_blends"]["with_rbf"].String();
                    	m_archetypeDnaPath[0] = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, jointsWithRBF);
                    	const std::string jointsNoRBF = json["archetype"]["joints_and_blends"]["without_rbf"].String();
                    	m_archetypeDnaPath[1] = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, jointsNoRBF);
                    }
                    else
                    {
                    	const std::string jointsAndBlendsjson = json["archetype"]["joints_and_blends"].String();
                    	m_archetypeDnaPath[0] = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, jointsAndBlendsjson);
                    	m_archetypeDnaPath[1] = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, jointsAndBlendsjson);
                    }
                }
                else
                {
                    if (json["archetype"]["joints_only"].IsObject())
                    {
                    	const std::string jointsOnlyRBF = json["archetype"]["joints_only"]["with_rbf"].String();
                    	const std::string jointsOnlyNoRBF = json["archetype"]["joints_only"]["without_rbf"].String();

                    	m_archetypeDnaPath[0] = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, jointsOnlyRBF);
                    	m_archetypeDnaPath[1] = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, jointsOnlyNoRBF);
                    }
                    else
                    {
                    	const std::string jointsOnly = json["archetype"]["joints_only"].String();
                    	m_archetypeDnaPath[0] = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, jointsOnly);
                    	m_archetypeDnaPath[1] = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, jointsOnly);
                    }
                }
            }
        }
        else
        {
        	const std::string archetypeJson = json["archetype"].String();
        	m_archetypeDnaPath[0] = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, archetypeJson);
        	m_archetypeDnaPath[1] = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, archetypeJson);
        }
    }
    else
    {
        LOG_ERROR("Pca model database description does not contain archetype.");
        // return false;
    }

    if (json.Contains("mesh_ids"))
    {
        m_modelMeshIds = json["mesh_ids"].Get<std::vector<int>>();
    }
    if (json.Contains("skinning_mesh_ids"))
    {
        m_skinningMeshIds = json["skinning_mesh_ids"].Get<std::vector<int>>();
    }

    if (json.Contains("rdf"))
    {
        if (json["rdf"].IsObject())
        {
            if (json["rdf"].Contains("joints_only") && json["rdf"].Contains("joints_and_blends"))
            {
                if (jointsAndBlends)
                {
                    if (json["rdf"]["joints_and_blends"].IsObject())
                    {
                        m_rigDefinitionPath[0] =  FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, json["rdf"]["joints_and_blends"]["with_rbf"].String());
                        m_rigDefinitionPath[1] =  FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, json["rdf"]["joints_and_blends"]["without_rbf"].String());
                    }
                    else
                    {
                        m_rigDefinitionPath[0] =  FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, json["rdf"]["joints_and_blends"].String());
                        m_rigDefinitionPath[1] =  FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, json["rdf"]["joints_and_blends"].String());
                    }
                }
                else
                {
                    if (json["rdf"]["joints_only"].IsObject())
                    {
                        m_rigDefinitionPath[0] =  FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, json["rdf"]["joints_only"]["with_rbf"].String());
                        m_rigDefinitionPath[1] =  FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, json["rdf"]["joints_only"]["without_rbf"].String());
                    }
                    else
                    {
                        m_rigDefinitionPath[0] =  FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, json["rdf"]["joints_only"].String());
                        m_rigDefinitionPath[1] =  FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, json["rdf"]["joints_only"].String());
                    }
                }
            }
        }
        else
        {
            m_rigDefinitionPath[0] = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, json["rdf"].String());
            m_rigDefinitionPath[1] = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, json["rdf"].String());
        }
    }
    else
    {
        LOG_ERROR("Pca model database description does not contain rdf.");
        // return false;
    }

    if (json.Contains("calibration_configuration"))
    {
        m_calibrationConfigurationFile = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, json["calibration_configuration"].String());
    }
    else
    {
        LOG_INFO("Pca model database description does not contain calibration_configuration.");
    }

    if (json.Contains("neutral_fitting_configuration"))
    {
        m_neutralFittingConfigurationFile = FMetaHumanFileResourceLoader::GetResolvedPathForFile(dataDescriptionDirectory, json["neutral_fitting_configuration"].String());
    }
    else
    {
        LOG_INFO("Pca model database description does not contain neutral_fitting_configuration.");
    }   

    return true;
}

const std::string& RigCalibrationDatabaseDescription::GetRigDefinitionFilePath(bool withoutRbf) const
{
    return withoutRbf ? m_rigDefinitionPath[1] : m_rigDefinitionPath[0];
}

const std::string& RigCalibrationDatabaseDescription::GetArchetypeDnaFilePath(bool withoutRbf) const
{
    return withoutRbf ? m_archetypeDnaPath[1] : m_archetypeDnaPath[0];
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
