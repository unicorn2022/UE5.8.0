// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeColor.h"
#include "MuT/NodeScalar.h"
#include "MuT/NodeImage.h"

#define UE_API MUTABLETOOLS_API


namespace UE::Mutable::Private
{

	/** Obtain a color by sampling an image at specific homogeneous coordinates.
	*/
	class NodeColorSampleImage : public NodeColor
	{
	public:

		Ptr<NodeImage> Image;
		Ptr<NodeScalar> X;
		Ptr<NodeScalar> Y;

	public:

		// Node interface
		virtual const FNodeType* GetType() const { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		/** Forbidden. Manage with the Ptr<> template. */
		~NodeColorSampleImage() = default;

	private:

		static UE_API FNodeType StaticType;

	};


}

#undef UE_API
