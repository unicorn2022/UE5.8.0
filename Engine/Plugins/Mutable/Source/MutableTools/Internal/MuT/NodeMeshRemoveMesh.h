// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/Types.h"
#include "MuT/Node.h"
#include "MuT/NodeMesh.h"

#define UE_API MUTABLETOOLS_API


namespace UE::Mutable::Private
{
	
	class NodeMeshRemoveMesh : public NodeMesh
	{
	public:

		Ptr<NodeMesh> Source;
    	
		Ptr<NodeMesh> RemoveMesh;

		EFaceCullStrategy FaceCullStrategy = EFaceCullStrategy::AllVerticesCulled;
    	
		// Node interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		/** Forbidden. Manage with the Ptr<> template. */
		inline ~NodeMeshRemoveMesh() = default;

	private:

		static UE_API FNodeType StaticType;

	};


}

#undef UE_API
