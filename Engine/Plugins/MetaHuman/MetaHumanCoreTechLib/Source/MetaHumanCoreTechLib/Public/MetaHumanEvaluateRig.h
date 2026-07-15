// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DNA.h"
#include "DNAAsset.h"

#define UE_API METAHUMANCORETECHLIB_API

namespace UE
{
    namespace Wrappers
    {

        /**
        * Brief@ FMetaHumanEvaluateRig is a wrapper class around core tech lib which allows evaluation of rig raw controls, and returns the vertices for user-specified mesh(es)
        */
        class FMetaHumanEvaluateRig
        {
        public:

			/**
			 * Default constructor
			 */
            UE_API FMetaHumanEvaluateRig();

			/**
			 * Set the DNA of the rig to be evaluated
			 * @param[in] InDNA: the UDNA asset containing the rig to be evaluated
			 * @returns true if the DNA is set successfully, false otherwise
			 */
			UE_API bool SetRigDNA(UDNA* InDNA);

			/**
			 * Set the DNA of the rig to be evaluated (deprecated - use UDNA instead)
			 * @param[in] InDNAAsset: the legacy DNAAsset containing the rig to be evaluated
			 * @returns true if the DNAAsset is set successfully, false otherwise
			 * @deprecated Use SetRigDNA(UDNA*) instead
			 */
			UE_DEPRECATED(5.8, "Use SetRigDNA(UDNA*) instead")
			UE_API bool SetRigDNA(UDNAAsset* InDNAAsset);

			/**
			 * Return whether the rig DNA has been set for the class
			 * @returns true if the rig DNA has been set, false otherwise
			 */
			UE_API bool IsRigDNASet() const;

			/**
			 * Evaluate the supplied map of raw controls, and return the evaluated mesh vertices for the specified mesh indices. Rig must have been set using SetRigDNA.
			 * @param[in] InControls: a map of raw control name to control value. Additional controls will be ignored; missing controls will be set to 0. Control names should 
			 * use the control names used in MH Animation sequences ie CTRL_expressions_ rather than CTRL_expressions.
			 * @param[in] InMeshIndices: an array of mesh indices for which we want to evaluate the vertices. Mesh Indices must be in the range 0-GetNumMeshes()-1
			 * @param[in] InLod: the lod to evaluate the vertices for, which must be >=0 and < GetNumLods()
			 * @param[out] OutMeshVertices: an array of array of vector. Each array contains the evaluated vertices for the specified mesh index.
			 * @returns true if evaluated successfully, false otherwise
			 */
			UE_API bool EvaluateRawControls(const TMap<FString, float>& InControls, const TArray<int> & InMeshIndices, int32 InLod, TArray<TArray<FVector>>& OutMeshVertices) const;

			/**
			 * Get the number of meshes for the rig DNA. Rig must have been set using SetRigDNA.
			 * @param[out] OutNumMeshes: contains the number of meshes for the rig DNA.
			 * @returns true if evaluated successfully, false otherwise
			 */
			UE_API bool GetNumMeshes(int32& OutNumMeshes) const;

			/**
			 * Get the number of lods for the rig DNA. Rig must have been set using SetRigDNA.
			 * @param[out] OutNumLods: contains the number of lods for the rig DNA.
			 * @returns true if evaluated successfully, false otherwise
			 */
			UE_API bool GetNumLODs(int32& OutNumLods) const;

			/**
			 * Get the mesh index for the specified mesh name for the rig DNA. Rig must have been set using SetRigDNA.
			 * @param[in] InMeshName: the mesh name. Must be a valid mesh name for the rig.
			 * @param[out] OutMeshIndex: contains the corresponding mesh index for the mesh name.
			 * @returns true if evaluated successfully, false otherwise
			 */
			UE_API bool GetMeshIndex(const FString& InMeshName, int32& OutMeshIndex) const;

			/**
			 * Get the raw control names for the rig DNA. Rig must have been set using SetRigDNA.
			 * @param[out] OutRawControlNames: contains the control names
			 * @returns true if evaluated successfully, false otherwise
			 */
			UE_API bool GetRawControlNames(TArray<FString>& OutRawControlNames)const;

			/**
			 * Get the mesh names for the rig DNA. Rig must have been set using SetRigDNA.
			 * @param[out] OutMeshNames: contains the mesh names
			 * @returns true if evaluated successfully, false otherwise
			 */
			UE_API bool GetMeshNames(TArray<FString>& OutMeshNames)const;

 
        private:
            struct Private;
            TPimplPtr<Private> Impl;
        };
    }
}

#undef UE_API
