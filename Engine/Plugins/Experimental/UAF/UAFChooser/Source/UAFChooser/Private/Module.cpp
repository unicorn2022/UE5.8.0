// Copyright Epic Games, Inc. All Rights Reserved.

#include <UAF/AnimNodeCore/UAFAnimNodeFactory.h>
#include <UAF/AnimNodeCore/UAFGraphFactoryAssetAnimNodeFactory.h>

#include "CoreMinimal.h"
#include "Module/AnimNextModule.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "RigVMCore/RigVMRegistry.h"
#include "Chooser.h"
#include "Factory/AnimGraphFactory.h"
#include "Factory/AnimNextFactoryParams.h"
#include "UAFAnimChooser.h"
#include "ChooserPlayerTraitData.h"
#include "UAFGraphFactoryAsset_Chooser.h"
#include "AnimNode/UAFChooserPlayerNode.h"
#include "Traits/BlendSmoother.h"
#include "Traits/BlendStackTrait.h"
#include "UAF/UAFAssetFactory.h"

namespace UE::UAF::Chooser
{

class FModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		static TPair<UClass*, FRigVMRegistry::ERegisterObjectOperation> const AllowedObjectTypes[] =
		{
			{ UChooserTable::StaticClass(), FRigVMRegistry::ERegisterObjectOperation::Class },
			{ UUAFAnimChooserTable::StaticClass(), FRigVMRegistry::ERegisterObjectOperation::Class },
		};

		FRigVMRegistry::Get().RegisterObjectTypes(AllowedObjectTypes);

		{
			// Register Chooser Player factory
			FAnimNextFactoryParams Params;
			Params.AddTraitStruct<FAnimNextBlendStackCoreTraitSharedData>(ETraitVariableMapping::None, 0);
			Params.AddTraitStruct<FAnimNextBlendSmootherCoreTraitSharedData>(ETraitVariableMapping::None, 0);
			Params.AddTraitStruct<FChooserPlayerData>(ETraitVariableMapping::All, 0);

			FAnimGraphFactory::RegisterAsset<FUAFGraphFactoryAsset_Chooser>(
				MoveTemp(Params),
				[](const FUAFGraphFactoryAsset_Chooser& AssetData, FAnimNextFactoryParams& InOutParams)
				{
					ensure(InOutParams.AccessTraitStruct<FChooserPlayerData>(0, [&AssetData, &InOutParams](FChooserPlayerData& InStruct)
					{
						InStruct.SetChooser(AssetData.ChooserTable);
						InStruct.SetEvaluationFrequency(AssetData.EvaluationFrequency);
					}));
				});
		}

		{
			// Register with asset factory
			ChooserTableClassPath = FAssetDataFactory::RegisterAsset<FUAFGraphFactoryAsset_Chooser, UUAFAnimChooserTable>(
				[](const UUAFAnimChooserTable* ChooserTable)
			{
				return FUAFGraphFactoryAsset_Chooser(ChooserTable);
			});
		}

		FUAFAnimNodeFactory::RegisterAsset(UUAFAnimChooserTable::StaticClass(), [](UObject* Object, FUAFAnimGraphUpdateContext& Context)
			{
				UUAFAnimChooserTable* Chooser = CastChecked<UUAFAnimChooserTable>(Object);
				return FUAFChooserPlayerNode::CreateInstance(Context, Chooser);
			});

		ChooserStructPath = FUAFGraphFactoryAssetAnimNodeFactory::RegisterStruct(FUAFGraphFactoryAsset_Chooser::StaticStruct(), [](TConstStructView<FUAFGraphFactoryAsset> Struct, FUAFAnimGraphUpdateContext& Context)
			{
				FUAFGraphFactoryAsset_Chooser ChooserData = Struct.Get<FUAFGraphFactoryAsset_Chooser>();
				return FUAFChooserPlayerNode::CreateInstance(Context, ChooserData.ChooserTable, ChooserData.EvaluationFrequency);
			});
	}

	virtual void ShutdownModule() override
	{
		FAssetDataFactory::UnregisterAsset(ChooserTableClassPath);
		
		FUAFAnimNodeFactory::UnregisterAsset(ChooserTableClassPath);
		FUAFGraphFactoryAssetAnimNodeFactory::UnregisterStruct(ChooserStructPath);
	}
	
	FTopLevelAssetPath ChooserTableClassPath;
	FTopLevelAssetPath ChooserStructPath;
};

}

IMPLEMENT_MODULE(UE::UAF::Chooser::FModule, UAFChooser)
