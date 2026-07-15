// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Defs.h"
#include <carbon/Math.h>
#include <map>
#include <memory>
#include <stdint.h>
#include <string>
#include <vector>


namespace dna
{

class Reader;

} // namespace dna

namespace TITAN_API_NAMESPACE
{


class TITAN_API EvaluateRigAPI
{
public:
    EvaluateRigAPI();
    ~EvaluateRigAPI();
    EvaluateRigAPI(EvaluateRigAPI&&) = delete;
    EvaluateRigAPI(const EvaluateRigAPI&) = delete;
    EvaluateRigAPI& operator=(EvaluateRigAPI&&) = delete;
    EvaluateRigAPI& operator=(const EvaluateRigAPI&) = delete;


	/**
	 * Load the DNA file.
	 * @param[in] InDNAStream  DNA input stream.
	 * @returns True if the DNA file could be loaded.
	 */
	bool LoadDNA(dna::Reader* InDNAStream);


	/**
	 * Return whether the rig DNA has been set for the class
	 * @returns true if the rig DNA has been set, false otherwise
	 */
	bool IsRigDNASet() const;

	/**
	 * Evaluate the supplied map of raw controls, and return the evaluated mesh vertices for the specified mesh indices. Rig must have been set using LoadDNA.
	 * @param[in] InControls: a map of raw control name to control value. Only valid controls will be set, and any controls not set will be set to 0. Control names should use the control names
	 * used in the DNA rather than those used in the animation sequence ie CTRL_expressions. rather than CTRL_expressions_
	 * @param[in] InMeshIndices: an array of mesh indices for which we want to evaluate the vertices. Mesh Indices must be in the range 0-GetNumMeshes()-1
	 * @param[in] InLod: the lod to evaluate the mesh vertices for
	 * @param[out] OutMeshVertices: an array of array of vector. Each array contains the evaluated vertices for the specified mesh index.
	 * @returns true if evaluated successfully, false otherwise
	 */
	bool EvaluateRawControls(const std::map<std::string, float>& InControls, const std::vector<int>& InMeshIndices, int InLod, std::vector<Eigen::Matrix<float, 3, -1>>& OutMeshVertices) const;

	/**
	 * Get the number of meshes for the rig DNA. Rig must have been set using LoadDNA.
	 * @param[out] OutNumMeshes: contains the number of meshes for the rig DNA.
	 * @returns true if evaluated successfully, false otherwise
	 */
	bool GetNumMeshes(int& OutNumMeshes) const;

	/**
	 * Get the mesh index for the specified mesh name for the rig DNA. Rig must have been set using LoadDNA.
	 * @param[in] InMeshName: the mesh name. Must be a valid mesh name for the rig.
	 * @param[out] OutMeshIndex: contains the corresponding mesh index for the mesh name.
	 * @returns true if evaluated successfully, false otherwise
	 */
	bool GetMeshIndex(const std::string& InMeshName, int& OutMeshIndex) const;


	/**
	 * Get the number of lods for the rig DNA. Rig must have been set using LoadDNA.
	 * @param[out] OutNumLODs: contains the number of lods for the rig DNA.
	 * @returns true if evaluated successfully, false otherwise
	 */
	bool GetNumLODs(int& OutNumLODs) const;

	/**
	 * Get the raw control names for the rig DNA. Rig must have been set using LoadDNA.
	 * @param[out] OutRawControlNames: contains the control names
	 * @returns true if evaluated successfully, false otherwise
	 */
	bool GetRawControlNames(std::vector<std::string>& OutRawControlNames)const;

	/**
	 * Get the mesh names for the rig DNA. Rig must have been set using LoadDNA.
	 * @param[out] OutMeshNames: contains the mesh names
	 * @returns true if evaluated successfully, false otherwise
	 */
	bool GetMeshNames(std::vector<std::string>& OutMeshNames)const;

private:
    struct Private;
    Private* m{};
};

} // namespace TITAN_API_NAMESPACE
