// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundConverterNodeRegistrationMacro.h"

#include "MetasoundAutoConverterNode.h"
#include "MetasoundFrontendNodeClassRegistry.h"

namespace Metasound
{
	bool RegisterAutoConverterNode(const AutoConverterNodePrivate::FConvertDataTypeInfo& InConvertInfo)
	{
		using namespace AutoConverterNodePrivate;
		using namespace Frontend;

		FConverterNodeRegistryKey RegistryKey = { InConvertInfo.FromDataTypeName, InConvertInfo.ToDataTypeName };

		FConverterNodeInfo ConverterNodeInfo =
		{
			GetInputName(InConvertInfo),
			GetOutputName(InConvertInfo),
			CreateAutoConverterNodeClassRegistryKey(InConvertInfo.FromDataTypeName, InConvertInfo.ToDataTypeName)
		};

		return INodeClassRegistry::GetChecked().RegisterConversionNode(RegistryKey, ConverterNodeInfo);
	}
}
