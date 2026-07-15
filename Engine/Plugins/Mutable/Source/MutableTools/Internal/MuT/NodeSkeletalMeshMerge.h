// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodeSkeletalMesh.h"
// #include "HAL/Platform.h"
#include "MuR/Ptr.h"
//#include "MuT/Node.h"
// #include "MuT/NodeLOD.h"


#define UE_API MUTABLETOOLS_API

namespace UE::Mutable::Private
{
	class NodeSkeletalMeshMerge : public NodeSkeletalMesh
	{
	public:
		
		/** Mesh where we want to merge into the sections of the ToAddSkeletalMesh*/
		Ptr<NodeSkeletalMesh> BaseSkeletalMesh;
		
		/** Skeletal Mesh whose section will be merged with the ones on the BaseSkeletalMesh*/
		Ptr<NodeSkeletalMesh> ToAddSkeletalMesh;
		
		// Node interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }
	
	protected:
		virtual ~NodeSkeletalMeshMerge() override {}

	private:
		static UE_API FNodeType StaticType;
	};
}

#undef UE_API
