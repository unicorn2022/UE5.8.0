// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeExternal.h"

#include "MuR/Ptr.h"
#include "StructUtils/InstancedStruct.h"

#define UE_API MUTABLETOOLS_API


namespace UE::Mutable::Private
{
	class NodeExternalOperation : public NodeExternal
	{
	public:
		// Node interface
		virtual const FNodeType* GetType() const override { return GetStaticType(); }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:
		/** Forbidden. Manage with the Ptr<> template. */
		virtual ~NodeExternalOperation() override = default;

	private:
		static UE_API FNodeType StaticType;

	public:
		FInstancedStruct OperationInstancedStruct;

		TArray<Ptr<Node>> Inputs;
	};
}


#undef UE_API
