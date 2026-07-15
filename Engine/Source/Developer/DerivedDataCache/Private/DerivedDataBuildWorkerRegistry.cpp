// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildWorkerRegistry.h"

#include "Algo/Find.h"
#include "Async/SharedLock.h"
#include "Async/SharedMutex.h"
#include "Async/UniqueLock.h"
#include "Containers/SharedString.h"
#include "Containers/StringView.h"
#include "DerivedDataBuildWorker.h"
#include "Features/IModularFeatures.h"
#include "Misc/Guid.h"
#include "Templates/Function.h"

namespace UE::DerivedData::Private
{

class FBuildWorkerRegistry final : public IBuildWorkerRegistry
{
public:
	FBuildWorkerRegistry();
	~FBuildWorkerRegistry();

	void RegisterWorker(FBuildWorker&& Worker, TNotNull<IBuildWorkerFactory*> Factory) final;

	FBuildWorker* FindWorker(
		const FUtf8SharedString& Function,
		const FGuid& FunctionVersion,
		const FGuid& BuildSystemVersion,
		IBuildWorkerExecutor*& OutWorkerExecutor) const final;

	FBuildWorkerBuilder CreateWorker() const final { return {}; }

	TOptional<FBuildWorker> LoadWorker(FStringView PackagePath) const final { return FBuildWorker::Load(PackagePath); }

private:
	void OnModularFeatureRegistered(const FName& Type, IModularFeature* ModularFeature);
	void OnModularFeatureUnregistered(const FName& Type, IModularFeature* ModularFeature);

	void AddWorkers(IBuildWorkerFactory* Factory);
	void RemoveWorkers(IBuildWorkerFactory* Factory);

private:
	IBuildWorkerExecutor* Executor = nullptr;
	TMap<IBuildWorkerFactory*, TArray<TUniquePtr<FBuildWorker>>> Workers;
	TMultiMap<TTuple<FUtf8SharedString, FGuid>, FBuildWorker*> Functions;
	mutable FSharedMutex Mutex;
};

FBuildWorkerRegistry::FBuildWorkerRegistry()
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	if (ModularFeatures.IsModularFeatureAvailable(IBuildWorkerExecutor::FeatureName))
	{
		Executor = &ModularFeatures.GetModularFeature<IBuildWorkerExecutor>(IBuildWorkerExecutor::FeatureName);
	}
	for (IBuildWorkerFactory* Worker : ModularFeatures.GetModularFeatureImplementations<IBuildWorkerFactory>(IBuildWorkerFactory::FeatureName))
	{
		AddWorkers(Worker);
	}
	ModularFeatures.OnModularFeatureRegistered().AddRaw(this, &FBuildWorkerRegistry::OnModularFeatureRegistered);
	ModularFeatures.OnModularFeatureUnregistered().AddRaw(this, &FBuildWorkerRegistry::OnModularFeatureUnregistered);
}

FBuildWorkerRegistry::~FBuildWorkerRegistry()
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	ModularFeatures.OnModularFeatureUnregistered().RemoveAll(this);
	ModularFeatures.OnModularFeatureRegistered().RemoveAll(this);
}

void FBuildWorkerRegistry::OnModularFeatureRegistered(const FName& Type, IModularFeature* ModularFeature)
{
	if (!Executor && Type == IBuildWorkerExecutor::FeatureName)
	{
		TUniqueLock Lock(Mutex);
		Executor = static_cast<IBuildWorkerExecutor*>(ModularFeature);
	}
	else if (Type == IBuildWorkerFactory::FeatureName)
	{
		AddWorkers(static_cast<IBuildWorkerFactory*>(ModularFeature));
	}
}

void FBuildWorkerRegistry::OnModularFeatureUnregistered(const FName& Type, IModularFeature* ModularFeature)
{
	if (Executor == ModularFeature && Type == IBuildWorkerExecutor::FeatureName)
	{
		IModularFeature* NextExecutor = nullptr;
		IModularFeatures& ModularFeatures = IModularFeatures::Get();
		if (ModularFeatures.IsModularFeatureAvailable(IBuildWorkerExecutor::FeatureName))
		{
			NextExecutor = &ModularFeatures.GetModularFeature<IBuildWorkerExecutor>(IBuildWorkerExecutor::FeatureName);
		}
		TUniqueLock Lock(Mutex);
		Executor = static_cast<IBuildWorkerExecutor*>(NextExecutor);
	}
	else if (Type == IBuildWorkerFactory::FeatureName)
	{
		RemoveWorkers(static_cast<IBuildWorkerFactory*>(ModularFeature));
	}
}

void FBuildWorkerRegistry::AddWorkers(IBuildWorkerFactory* Factory)
{
	Factory->CreateWorkers(*this);
}

void FBuildWorkerRegistry::RemoveWorkers(IBuildWorkerFactory* Factory)
{
	Factory->AbortCreateWorkers();

	TUniqueLock Lock(Mutex);
	if (TArray<TUniquePtr<FBuildWorker>>* WorkerPtrs = Workers.Find(Factory))
	{
		for (TUniquePtr<FBuildWorker>& Worker : *WorkerPtrs)
		{
			Worker->IterateFunctions([this, Worker = Worker.Get()](FUtf8StringView Name, const FGuid& Version)
			{
				Functions.Remove(MakeTuple(FUtf8SharedString(Name), Version), Worker);
			});
		}
		Workers.Remove(Factory);
	}
}

void FBuildWorkerRegistry::RegisterWorker(FBuildWorker&& InWorker, TNotNull<IBuildWorkerFactory*> Factory)
{
	TUniquePtr<FBuildWorker> Worker = MakeUnique<FBuildWorker>(MoveTemp(InWorker));
	TUniqueLock Lock(Mutex);
	Worker->IterateFunctions([this, Worker = Worker.Get()](FUtf8StringView Name, const FGuid& Version)
	{
		Functions.Emplace(MakeTuple(FUtf8SharedString(Name), Version), Worker);
	});
	Workers.FindOrAdd(Factory).Emplace(MoveTemp(Worker));
}

FBuildWorker* FBuildWorkerRegistry::FindWorker(
	const FUtf8SharedString& Function,
	const FGuid& FunctionVersion,
	const FGuid& BuildSystemVersion,
	IBuildWorkerExecutor*& OutWorkerExecutor) const
{
	TSharedLock Lock(Mutex);
	if (Executor)
	{
		TConstArrayView<FUtf8StringView> ExecutorHostPlatforms = Executor->GetHostPlatforms();
		TArray<FBuildWorker*, TInlineAllocator<8>> FunctionWorkers;
		Functions.MultiFind(MakeTuple(Function, FunctionVersion), FunctionWorkers);
		for (FBuildWorker* Worker : FunctionWorkers)
		{
			if (Worker->GetBuildSystemVersion() == BuildSystemVersion &&
				Algo::Find(ExecutorHostPlatforms, Worker->GetHostPlatform()))
			{
				OutWorkerExecutor = Executor;
				return Worker;
			}
		}
	}
	OutWorkerExecutor = nullptr;
	return nullptr;
}

IBuildWorkerRegistry* CreateBuildWorkerRegistry()
{
	return new FBuildWorkerRegistry();
}

} // UE::DerivedData::Private
