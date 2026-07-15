// Copyright Epic Games, Inc. All Rights Reserved.

#include "UfbxConvert.h"
#include "UfbxParser.h"

#include "InterchangeSceneNode.h"


namespace UE::Interchange::Private
{

Convert::FMeshNameAndUid::FMeshNameAndUid(FUfbxParser& Parser, const ufbx_element& Mesh)
	: Label(Parser.GetMeshLabel(Mesh))
	, UniqueID(Parser.GetMeshUid(Mesh))
{
}

}
