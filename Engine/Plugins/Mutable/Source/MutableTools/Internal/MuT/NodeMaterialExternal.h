// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodeMaterial.h"

#include "MuR/Ptr.h"
#include "MuT/NodeExternal.h"

#define UE_API MUTABLETOOLS_API

namespace UE::Mutable::Private
{
	class NodeMaterialExternal : public NodeMaterial
	{
	public:
		// Node Interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:
		// Forbidden. Manage with the Ptr<> template.
		virtual ~NodeMaterialExternal() override = default;

	private:
		static UE_API FNodeType StaticType;

	public:
		Ptr<NodeExternal> Node;
	};
}

#undef UE_API
