// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_TESTS
#include "Verse/VersePathRegistry.h"

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/StringFwd.h"
#include "Containers/StringView.h"
#include "Misc/AutomationTest.h"

// Used to iterate quickly so we can swap multiple tests to run as smoke tests
static constexpr EAutomationTestFlags TestFilter = EAutomationTestFlags::EngineFilter;

// Provides a non-anonymous name for friendship to test internals without exposing them to users
class FVersePathRegistryTestsBase : public FAutomationTestBase
{
public:
	FVersePathRegistryTestsBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	// Gives simple means to break immediately when tests fail
	virtual void AddError(const FString& InError, int32 StackOffset) override
	{
		FAutomationTestBase::AddError(InError, StackOffset);
	}

	void VerifyAppendEvents(const UE::VersePathRegistry::FVersePathRegistry& Registry, const UE::VersePathRegistry::FRegistryEvents& Events,
		const TArrayView<const FStringView> PathsToAdd, const TArrayView<const FStringView> PathsToRemove = {}, const TArrayView<const FStringView> PathsToUpdate = {},
		const TArrayView<UE::VersePathRegistry::FPathHandle> PathHandlesToRemove = {}, const TArrayView<UE::VersePathRegistry::FPathHandle> PathHandlesToUpdate = {});

	void RunBuilderTests();
	void RunAccessibilityTests();
};

void FVersePathRegistryTestsBase::VerifyAppendEvents(const UE::VersePathRegistry::FVersePathRegistry& Registry, const UE::VersePathRegistry::FRegistryEvents& Events,
	const TArrayView<const FStringView> PathsToAdd, const TArrayView<const FStringView> PathsToRemove, const TArrayView<const FStringView> PathsToUpdate,
	const TArrayView<UE::VersePathRegistry::FPathHandle> PathHandlesToRemove, const TArrayView<UE::VersePathRegistry::FPathHandle> PathHandlesToUpdate)
{
	using namespace UE::VersePathRegistry;

	if (TestEqual(TEXT("Expected added path events"), Events.AddedPaths.Num(), PathsToAdd.Num()))
	{
		for (FPathHandle AddedPathHandle : Events.AddedPaths)
		{
			FPathData AddedPath;
			TestTrue(TEXT("Added handles should be valid"), Registry.IsValidHandle(AddedPathHandle));
			TestTrue(TEXT("Expect to be able to find added path data by event path handle"), Registry.TryGetPathData(AddedPathHandle, AddedPath));
			TestTrue(TEXT("Expect added path data to match something that was added via builder"), PathsToAdd.Contains(AddedPath.Path));
		}
	}

	if (TestEqual(TEXT("Expected removed path events"), Events.RemovedPaths.Num(), PathsToRemove.Num()))
	{
		for (const TPair<FPathHandle, FString>& Pair : Events.RemovedPaths)
		{
			const FPathHandle RemovedPathHandle = Pair.Key;
			const FString RemovedPath = Pair.Value;
			FPathData RemovedPathData;
			TestTrue(TEXT("Expected to be able to find removed handle from registry"), PathHandlesToRemove.Contains(RemovedPathHandle));
			TestFalse(TEXT("Removed handles should not be valid"), Registry.IsValidHandle(RemovedPathHandle));
			TestFalse(TEXT("Expect to not be able to find a removed path data by event path handle"), Registry.TryGetPathData(RemovedPathHandle, RemovedPathData));
			TestEqual(TEXT("Expect to not be able to find a removed path by event path"), Registry.FindPath(RemovedPath), FPathHandle::Null);
		}
	}
	
	if (TestEqual(TEXT("Expected updated path events"), Events.UpdatedPaths.Num(), PathsToUpdate.Num()))
	{
		for (TPair<FPathHandle, FPathHandle> UpdatedPathPair : Events.UpdatedPaths)
		{
			FPathData UpdatedPath;
			const FPathHandle OldHandle = UpdatedPathPair.Key;
			TestTrue(TEXT("Expected to be able to find old updated handle from registry"), PathHandlesToUpdate.Contains(OldHandle));
			TestFalse(TEXT("Old updated handles should not be valid"), Registry.IsValidHandle(OldHandle));
			TestFalse(TEXT("Expect to not be able to find a an old updated path data by event path handle"), Registry.TryGetPathData(OldHandle, UpdatedPath));

			const FPathHandle NewHandle = UpdatedPathPair.Value;
			TestTrue(TEXT("New updated handles should be valid"), Registry.IsValidHandle(NewHandle));
			TestTrue(TEXT("Expect to be able to find added path data by event path handle"), Registry.TryGetPathData(NewHandle, UpdatedPath));
			TestTrue(TEXT("Expect added path data to match something that was added via builder"), PathsToUpdate.Contains(UpdatedPath.Path));
		}
	}
};

void FVersePathRegistryTestsBase::RunBuilderTests()
{
	using namespace UE::VersePathRegistry;

	auto VerifyBuilderPathsAreRegistered = [this](const FVersePathRegistry& Registry, const FRegistryBuilder& RegistryBuilder)
		{
			for (const FRegistryBuilder::FPathDesc& Desc : RegistryBuilder.PathDescs)
			{
				FPathData PathData;
				TestTrue(TEXT("Expected to find registered path data"), Registry.TryGetPathData(Desc.Path, PathData));
				TestEqual(TEXT("Expected paths to match"), PathData.Path, Desc.Path);
				TestEqual(TEXT("Expected Kinds to match"), PathData.Kind, Desc.Kind);

				if (TestEqual(TEXT("Expected Parents to match"), PathData.Parent.IsNull(), Desc.Parent.IsEmpty()) && !Desc.Parent.IsEmpty())
				{
					FPathData ParentPathData;
					TestTrue(TEXT("Expected to find registered parent data"), Registry.TryGetPathData(Desc.Parent, ParentPathData));
					TestEqual(TEXT("Expected Parent paths to match"), ParentPathData.Path, Desc.Parent);
				}
			}
		};

	// Confirm that we can maintain builder per package and use those to rebuild a registry
	const TCHAR* PackageA_ClassPath = TEXT("/Verse.org/Class");
	const TCHAR* PackageA_ClassDataPath = TEXT("/Verse.org/Class/Data");
	const TCHAR* PackageA_ClassFunctionPath = TEXT("/Verse.org/Class/Function");
	const TCHAR* PackageA_ModulePath = TEXT("/Verse.org/Module");
	const TCHAR* PackageA_ModuleFunctionPath = TEXT("/Verse.org/Module/Function");

	// Theoretically, Verse won't let users publish code where two packages define the same verse paths. However,
	// this isn't restricted by the language or compilation toolchain, and Epic internally does provide packages 
	// with overlapping paths (i.e. two packages that define the same module) so the registry needs to support this 
	// usecase, even if it will be minimalized in the future.
	const TCHAR* SharedRootModulePath = TEXT("/Verse.org");
	const TCHAR* SharedModulePath = TEXT("/Verse.org/SharedModule");

	const TCHAR* PackageA_SharedModuleSubModulePath = TEXT("/Verse.org/SharedModule/PackageASubModule");
	const TCHAR* PackageA_SharedModuleSubModuleClassPath = TEXT("/Verse.org/SharedModule/PackageASubModule/Class");
	const TCHAR* PackageA_SharedModuleFunctionPath = TEXT("/Verse.org/SharedModule/PackageAFunction");
	const TCHAR* PackageA_SharedModuleClassPath = TEXT("/Verse.org/SharedModule/PackageAClass");
	const TCHAR* PackageA_SharedModuleClassDataPath = TEXT("/Verse.org/SharedModule/PackageAClass/Data");

	FRegistryBuilder PackageABuilder(TEXT("PackageA"));
	{
		TestTrue(TEXT("Expect path registration to succeed"), PackageABuilder.RegisterPath(
			{ .Path = SharedRootModulePath, .Parent = {}, .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal }));
		TestTrue(TEXT("Expect path registration to succeed"), PackageABuilder.RegisterPath(
			{ .Path = PackageA_ClassPath, .Parent = SharedRootModulePath, .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal }));
		TestTrue(TEXT("Expect path registration to succeed"), PackageABuilder.RegisterPath(
			{ .Path = PackageA_ClassDataPath, .Parent = PackageA_ClassPath, .Kind = EPathKind::Data, .ReadAccess = EAccess::Internal }));
		TestTrue(TEXT("Expect path registration to succeed"), PackageABuilder.RegisterPath(
			{ .Path = PackageA_ClassFunctionPath, .Parent = PackageA_ClassPath, .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal }));

		TestTrue(TEXT("Expect path registration to succeed"), PackageABuilder.RegisterPath(
			{ .Path = PackageA_ModulePath, .Parent = SharedRootModulePath, .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal }));
		TestTrue(TEXT("Expect path registration to succeed"), PackageABuilder.RegisterPath(
			{ .Path = PackageA_ModuleFunctionPath, .Parent = PackageA_ModulePath, .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal }));

		TestTrue(TEXT("Expect path registration to succeed"), PackageABuilder.RegisterPath(
			{ .Path = SharedModulePath, .Parent = SharedRootModulePath, .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal }));
		TestTrue(TEXT("Expect path registration to succeed"), PackageABuilder.RegisterPath(
			{ .Path = PackageA_SharedModuleSubModulePath, .Parent = SharedModulePath, .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal }));
		TestTrue(TEXT("Expect path registration to succeed"), PackageABuilder.RegisterPath(
			{ .Path = PackageA_SharedModuleSubModuleClassPath, .Parent = PackageA_SharedModuleSubModulePath, .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal }));

		TestTrue(TEXT("Expect path registration to succeed"), PackageABuilder.RegisterPath(
			{ .Path = PackageA_SharedModuleFunctionPath, .Parent = SharedModulePath, .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal }));
		TestTrue(TEXT("Expect path registration to succeed"), PackageABuilder.RegisterPath(
			{ .Path = PackageA_SharedModuleClassPath, .Parent = SharedModulePath, .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal }));
		TestTrue(TEXT("Expect path registration to succeed"), PackageABuilder.RegisterPath(
			{ .Path = PackageA_SharedModuleClassDataPath, .Parent = PackageA_SharedModuleClassPath, .Kind = EPathKind::Data, .ReadAccess = EAccess::Internal }));
	}

	const TCHAR* PackageB_ClassPath = TEXT("/Verse.org/ClassB");
	const TCHAR* PackageB_ClassDataPath = TEXT("/Verse.org/ClassB/Data");
	const TCHAR* PackageB_ClassFunctionPath = TEXT("/Verse.org/ClassB/Function");
	const TCHAR* PackageB_ModulePath = TEXT("/Verse.org/ModuleB");
	const TCHAR* PackageB_ModuleFunctionPath = TEXT("/Verse.org/ModuleB/Function");

	const TCHAR* PackageB_SharedModuleSubModulePath = TEXT("/Verse.org/SharedModule/PackageBSubModule");
	const TCHAR* PackageB_SharedModuleSubModuleClassPath = TEXT("/Verse.org/SharedModule/PackageBSubModule/Class");
	const TCHAR* PackageB_SharedModuleFunctionPath = TEXT("/Verse.org/SharedModule/PackageBFunction");
	const TCHAR* PackageB_SharedModuleClassPath = TEXT("/Verse.org/SharedModule/PackageBClass");
	const TCHAR* PackageB_SharedModuleClassDataPath = TEXT("/Verse.org/SharedModule/PackageBClass/Data");

	FRegistryBuilder PackageBBuilder(TEXT("PackageB"));
	{
		TestTrue(TEXT("Expect path registration to succeed"), PackageBBuilder.RegisterPath(
			{ .Path = SharedRootModulePath, .Parent = {}, .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal }));
		TestTrue(TEXT("Expect path registration to succeed"), PackageBBuilder.RegisterPath(
			{ .Path = PackageB_ClassPath, .Parent = SharedRootModulePath, .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal }));
		TestTrue(TEXT("Expect path registration to succeed"), PackageBBuilder.RegisterPath(
			{ .Path = PackageB_ClassDataPath, .Parent = PackageB_ClassPath, .Kind = EPathKind::Data, .ReadAccess = EAccess::Internal }));
		TestTrue(TEXT("Expect path registration to succeed"), PackageBBuilder.RegisterPath(
			{ .Path = PackageB_ClassFunctionPath, .Parent = PackageB_ClassPath, .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal }));

		TestTrue(TEXT("Expect path registration to succeed"), PackageBBuilder.RegisterPath(
			{ .Path = PackageB_ModulePath, .Parent = SharedRootModulePath, .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal }));
		TestTrue(TEXT("Expect path registration to succeed"), PackageBBuilder.RegisterPath(
			{ .Path = PackageB_ModuleFunctionPath, .Parent = PackageB_ModulePath, .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal }));

		TestTrue(TEXT("Expect path registration to succeed"), PackageBBuilder.RegisterPath(
			{ .Path = SharedModulePath, .Parent = SharedRootModulePath, .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal }));
		TestTrue(TEXT("Expect path registration to succeed"), PackageBBuilder.RegisterPath(
			{ .Path = PackageB_SharedModuleSubModulePath, .Parent = SharedModulePath, .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal }));
		TestTrue(TEXT("Expect path registration to succeed"), PackageBBuilder.RegisterPath(
			{ .Path = PackageB_SharedModuleSubModuleClassPath, .Parent = PackageB_SharedModuleSubModulePath, .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal }));

		TestTrue(TEXT("Expect path registration to succeed"), PackageBBuilder.RegisterPath(
			{ .Path = PackageB_SharedModuleFunctionPath, .Parent = SharedModulePath, .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal }));
		TestTrue(TEXT("Expect path registration to succeed"), PackageBBuilder.RegisterPath(
			{ .Path = PackageB_SharedModuleClassPath, .Parent = SharedModulePath, .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal }));
		TestTrue(TEXT("Expect path registration to succeed"), PackageBBuilder.RegisterPath(
			{ .Path = PackageB_SharedModuleClassDataPath, .Parent = PackageB_SharedModuleClassPath, .Kind = EPathKind::Data, .ReadAccess = EAccess::Internal }));
	}

	{
		FRegistryEvents Events;
		FVersePathRegistry Registry;
		{
			Registry.Append(PackageABuilder, &Events);
			VerifyBuilderPathsAreRegistered(Registry, PackageABuilder);

			TArray<const FStringView> ExpectedAddedPackages =
			{
				PackageA_ClassPath, PackageA_ClassDataPath, PackageA_ClassFunctionPath,
				PackageA_ModulePath, PackageA_ModuleFunctionPath,
				SharedRootModulePath, SharedModulePath,
				PackageA_SharedModuleSubModulePath, PackageA_SharedModuleSubModuleClassPath, PackageA_SharedModuleFunctionPath,
				PackageA_SharedModuleClassPath, PackageA_SharedModuleClassDataPath
			};
			VerifyAppendEvents(Registry, Events, ExpectedAddedPackages);

			// Register the same package on top of the same registry should produce the same results
			Registry.Append(PackageABuilder, &Events);
			VerifyBuilderPathsAreRegistered(Registry, PackageABuilder);
			TestEqual(TEXT("Expected to have the exact same number of paths registered as the builder."), Registry.Num(), PackageABuilder.Num());
		}
	}

	// Appending two package builders in different orders shouldn't matter when rebuilding a registry from builders
	{
		FRegistryEvents Events;
		FVersePathRegistry RegistryAThenB;
		{
			RegistryAThenB.Append(PackageABuilder, &Events);
			VerifyBuilderPathsAreRegistered(RegistryAThenB, PackageABuilder);

			TArray<const FStringView> ExpectedAddedPackages =
			{
				PackageA_ClassPath, PackageA_ClassDataPath, PackageA_ClassFunctionPath,
				PackageA_ModulePath, PackageA_ModuleFunctionPath,
				SharedRootModulePath, SharedModulePath,
				PackageA_SharedModuleSubModulePath, PackageA_SharedModuleSubModuleClassPath, PackageA_SharedModuleFunctionPath,
				PackageA_SharedModuleClassPath, PackageA_SharedModuleClassDataPath
			};
			VerifyAppendEvents(RegistryAThenB, Events, ExpectedAddedPackages);
		}
		{
			RegistryAThenB.Append(PackageBBuilder, &Events);
			VerifyBuilderPathsAreRegistered(RegistryAThenB, PackageBBuilder);
			TArray<const FStringView> ExpectedAddedPackages =
			{
				PackageB_ClassPath, PackageB_ClassDataPath, PackageB_ClassFunctionPath,
				PackageB_ModulePath, PackageB_ModuleFunctionPath,
				// SharedRootModulePath, SharedModulePath, <-- Should already have been registered
				PackageB_SharedModuleSubModulePath, PackageB_SharedModuleSubModuleClassPath, PackageB_SharedModuleFunctionPath,
				PackageB_SharedModuleClassPath, PackageB_SharedModuleClassDataPath
			};
			TArray<const FStringView> ExpectedRemovedPackages = { };
			TArray<const FStringView> ExpectedUpdatedPackages = { };
			VerifyAppendEvents(RegistryAThenB, Events, ExpectedAddedPackages, ExpectedRemovedPackages, ExpectedUpdatedPackages);

			// Since we expect no packages to have been removed, ensure we still have all the first builders packages in the registry
			VerifyBuilderPathsAreRegistered(RegistryAThenB, PackageABuilder);
		}

		FVersePathRegistry RegistryBThenA;
		{
			RegistryBThenA.Append(PackageBBuilder, &Events);
			VerifyBuilderPathsAreRegistered(RegistryBThenA, PackageBBuilder);

			TArray<const FStringView> ExpectedAddedPackages =
			{
				PackageB_ClassPath, PackageB_ClassDataPath, PackageB_ClassFunctionPath,
				PackageB_ModulePath, PackageB_ModuleFunctionPath,
				SharedRootModulePath, SharedModulePath,
				PackageB_SharedModuleSubModulePath, PackageB_SharedModuleSubModuleClassPath, PackageB_SharedModuleFunctionPath,
				PackageB_SharedModuleClassPath, PackageB_SharedModuleClassDataPath
			};
			VerifyAppendEvents(RegistryBThenA, Events, ExpectedAddedPackages);
		}
		{
			RegistryBThenA.Append(PackageABuilder, &Events);
			VerifyBuilderPathsAreRegistered(RegistryBThenA, PackageABuilder);
			TArray<const FStringView> ExpectedAddedPackages =
			{
				PackageA_ClassPath, PackageA_ClassDataPath, PackageA_ClassFunctionPath,
				PackageA_ModulePath, PackageA_ModuleFunctionPath,
				// SharedRootModulePath, SharedModulePath, <-- Should already have been registered
				PackageA_SharedModuleSubModulePath, PackageA_SharedModuleSubModuleClassPath, PackageA_SharedModuleFunctionPath,
				PackageA_SharedModuleClassPath, PackageA_SharedModuleClassDataPath
			};
			TArray<const FStringView> ExpectedRemovedPackages = { };
			TArray<const FStringView> ExpectedUpdatedPackages =	{ };
			VerifyAppendEvents(RegistryBThenA, Events, ExpectedAddedPackages, ExpectedRemovedPackages, ExpectedUpdatedPackages);

			// Since we expect no packages to have been removed, ensure we still have all the first builders packages in the registry
			VerifyBuilderPathsAreRegistered(RegistryBThenA, PackageBBuilder);
		}

		if (TestEqual(TEXT("The order of appends should not affect the final path count"), RegistryAThenB.Num(), RegistryBThenA.Num()))
		{
			for (const TPair<FString, uint32>& Pair : RegistryAThenB.PathToPathDataIndex)
			{
				const FString& PathA = Pair.Key;
				const FPathData& PathDataA = RegistryAThenB.PathDatas[Pair.Value];

				FPathData PathDataB;
				if (TestTrue(TEXT("Expected to find registered path data"), RegistryBThenA.TryGetPathData(PathA, PathDataB)))
				{
					TestEqual(TEXT("Expected Path to match"), PathDataA.Path, PathDataB.Path);
					TestEqual(TEXT("Expected Kind to match"), PathDataA.Kind, PathDataB.Kind);
					TestEqual(TEXT("Expected ReadAccess to match"), PathDataA.ReadAccess, PathDataB.ReadAccess);
					TestEqual(TEXT("Expected WriteAccess to match"), PathDataA.WriteAccess, PathDataB.WriteAccess);
					TestEqual(TEXT("Expected ConstructorAccess to match"), PathDataA.ConstructorAccess, PathDataB.ConstructorAccess);

					if (TestEqual(TEXT("Expected Parent nullness to match"), PathDataA.Parent.IsNull(), PathDataB.Parent.IsNull()) && !PathDataA.Parent.IsNull())
					{
						FPathData ParentDataA;
						TestTrue(TEXT("Expected to find registered parent path data"), RegistryAThenB.TryGetPathData(PathDataA.Parent, ParentDataA));
						FPathData ParentDataB;
						TestTrue(TEXT("Expected to find registered parent path data"), RegistryBThenA.TryGetPathData(PathDataB.Parent, ParentDataB));
						TestEqual(TEXT("Expected Path to match"), ParentDataA.Path, ParentDataB.Path);
						TestEqual(TEXT("Expected Kind to match"), ParentDataA.Kind, ParentDataB.Kind);
						TestEqual(TEXT("Expected ReadAccess to match"), ParentDataA.ReadAccess, ParentDataB.ReadAccess);
						TestEqual(TEXT("Expected WriteAccess to match"), ParentDataA.WriteAccess, ParentDataB.WriteAccess);
						TestEqual(TEXT("Expected ConstructorAccess to match"), ParentDataA.ConstructorAccess, ParentDataB.ConstructorAccess);
						TestEqual(TEXT("Expected Parent to match"), ParentDataA.Parent.IsNull(), ParentDataB.Parent.IsNull());
					}
				}
			}
		}
	}
}

void FVersePathRegistryTestsBase::RunAccessibilityTests()
{
	using namespace UE::VersePathRegistry;
	struct FAccessibilityTestCase
	{
		struct FTest
		{
			FString Context;
			FString Target;
			TOptional<bool> bCanRead;
			TOptional<bool> bCanWrite;
			TOptional<bool> bCanConstruct;
		};
		TArray<FRegistryBuilder::FPathDesc> RegisteredPaths;
		TArray<FTest> Tests;
	};

	TArray<FAccessibilityTestCase> TestCases =
	{
		{
			.RegisteredPaths =
			{
				/*
					A := class:
						myMember<public>:int
					B := class:
						myFunction(param:A):int=
							param.myMember
				*/
				{ .Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{ .Path = TEXT("/Verse.org/A"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal },
				{ .Path = TEXT("/Verse.org/A/myMember"), .Parent = TEXT("/Verse.org/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Public },
				{ .Path = TEXT("/Verse.org/B"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal },
				{ .Path = TEXT("/Verse.org/B/myFunction"), .Parent = TEXT("/Verse.org/B"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{ .Context = TEXT("/Verse.org/A"), .Target = TEXT("/Verse.org/A/myMember"), .bCanRead = true },
				{ .Context = TEXT("/Verse.org/B/myFunction"), .Target = TEXT("/Verse.org/A/myMember"), .bCanRead = true },
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					A := class:
						myMember<private>:int = 7
					B := class(A):
						myFunction():int=
							myMember1
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/A"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/A/myMember"), .Parent = TEXT("/Verse.org/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Private  },
				{.Path = TEXT("/Verse.org/B"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal, .ClassSuper = TEXT("/Verse.org/A"), },
				{.Path = TEXT("/Verse.org/B/myFunction"), .Parent = TEXT("/Verse.org/B"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/A"), .Target = TEXT("/Verse.org/A/myMember"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/B/myFunction"), .Target = TEXT("/Verse.org/A/myMember"), .bCanRead = false },
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					A := class:
						myMember<private>:int = 7
					B := class(A):
						myFunction():int=
							myMember
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/A"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/A/myMember"), .Parent = TEXT("/Verse.org/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Private },
				{.Path = TEXT("/Verse.org/B"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal, .ClassSuper = TEXT("/Verse.org/A"),  },
				{.Path = TEXT("/Verse.org/B/myFunction"), .Parent = TEXT("/Verse.org/B"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/A"), .Target = TEXT("/Verse.org/A/myMember"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/B/myFunction"), .Target = TEXT("/Verse.org/A/myMember"), .bCanRead = false },
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					A := class:
						myFunction<private>():int=
							5
					B := class(A):
						myFunction():int=
							myFunction()
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/A"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/A/myFunction"), .Parent = TEXT("/Verse.org/A"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Private },
				{.Path = TEXT("/Verse.org/B"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal, .ClassSuper = TEXT("/Verse.org/A"), },
				{.Path = TEXT("/Verse.org/B/myFunction"), .Parent = TEXT("/Verse.org/B"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/A"), .Target = TEXT("/Verse.org/A/myFunction"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/B/myFunction"), .Target = TEXT("/Verse.org/A/myFunction"), .bCanRead = false },
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					A := class:
						myMember<protected>:int = 7
					B := class(A):
						myFunction():int=
							myMember
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/A"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/A/myMember"), .Parent = TEXT("/Verse.org/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Protected },
				{.Path = TEXT("/Verse.org/B"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal, .ClassSuper = TEXT("/Verse.org/A"),  },
				{.Path = TEXT("/Verse.org/B/myFunction"), .Parent = TEXT("/Verse.org/B"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/A"), .Target = TEXT("/Verse.org/A/myMember"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/B/myFunction"), .Target = TEXT("/Verse.org/A/myMember"), .bCanRead = true },
			}
		},
		{
		    .RegisteredPaths = 
		    {
		        /*
		            A := class:
		                myMember<protected>:int = 8
		            B := class:
		                myFunction(param:A):int=
		                    param.myMember
		        */
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
		        { .Path = TEXT("/Verse.org/A"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal },
		        { .Path = TEXT("/Verse.org/A/myMember"), .Parent = TEXT("/Verse.org/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Protected },
		        { .Path = TEXT("/Verse.org/B"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal },
				{ .Path = TEXT("/Verse.org/B/myFunction"), .Parent = TEXT("/Verse.org/B"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
		    },
		    .Tests = 
		    {
		        { .Context = TEXT("/Verse.org/A"), .Target = TEXT("/Verse.org/A/myMember"), .bCanRead = true },
		        { .Context = TEXT("/Verse.org/B/myFunction"), .Target = TEXT("/Verse.org/A/myMember"), .bCanRead = false },
		    }
		},
		{
			.RegisteredPaths =
			{
				/*
					vmodule(X):
						snippet:
							A := class:
								# default access := internal
								myMember:int
							B := class:
								myFunction(a:A):int=
									a.myMember
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/X"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal,  },
				{.Path = TEXT("/Verse.org/X/A"), .Parent = TEXT("/Verse.org/X"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal},
				{.Path = TEXT("/Verse.org/X/A/myMember"), .Parent = TEXT("/Verse.org/X/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/X/B"), .Parent = TEXT("/Verse.org/X"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal, },
				{.Path = TEXT("/Verse.org/X/B/myFunction"), .Parent = TEXT("/Verse.org/X/B"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/X"), .Target = TEXT("/Verse.org/X/A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/X"), .Target = TEXT("/Verse.org/X/B"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/X/A"), .Target = TEXT("/Verse.org/X/A/myMember"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/X/B/myFunction"), .Target = TEXT("/Verse.org/X/A/myMember"), .bCanRead = true },
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					vmodule(X):
						snippet:
							A<public> := class:
								# default access := internal
								myMember:int
					vmodule(Y):
						snippet:
							using{X}
							B := class:
								myFunction(a:A):int=
									a.myMember
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/X"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/X/A"), .Parent = TEXT("/Verse.org/X"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/X/A/myMember"), .Parent = TEXT("/Verse.org/X/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/Y"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/Y/B"), .Parent = TEXT("/Verse.org/Y"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/Y/B/myFunction"), .Parent = TEXT("/Verse.org/Y/B"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/X"), .Target = TEXT("/Verse.org/X/A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/Y"), .Target = TEXT("/Verse.org/Y/B"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/Y/B/myFunction"), .Target = TEXT("/Verse.org/X/A/myMember"), .bCanRead = false },
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					vmodule(X):
						snippet:
							A := class:
								# default access := internal
								myFunction():int=
									5
							B := class(A):
								myOtherFunction():int=
									myFunction()
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/X"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/X/A"), .Parent = TEXT("/Verse.org/X"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/X/A/myFunction"), .Parent = TEXT("/Verse.org/X/A"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/X/B"), .Parent = TEXT("/Verse.org/X"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal, .ClassSuper = TEXT("/Verse.org/X/A") },
				{.Path = TEXT("/Verse.org/X/B/myOtherFunction"), .Parent = TEXT("/Verse.org/X/B"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/X"), .Target = TEXT("/Verse.org/X/A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/X"), .Target = TEXT("/Verse.org/X/B"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/X/B/myOtherFunction"), .Target = TEXT("/Verse.org/X/A/myFunction"), .bCanRead = true },
			}
		},
		// Scoped tests
		{
			.RegisteredPaths =
			{
				/*
					A<public> := module:
						class_in_A<public> := class:
							myB<private>:B.class_in_B = B.class_in_B{}
					B<public> := module:
						class_in_B<scoped{A}> := class:
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/A"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/A/class_in_A"), .Parent = TEXT("/Verse.org/A"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/A/class_in_A/myB"), .Parent = TEXT("/Verse.org/A/class_in_A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Private },
				{.Path = TEXT("/Verse.org/B"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/B/class_in_B"), .Parent = TEXT("/Verse.org/B"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Scoped, .ReadScopeAccessPaths = {TEXT("/Verse.org/A")}},
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/A"), .Target = TEXT("/Verse.org/A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/A/class_in_A"), .Target = TEXT("/Verse.org/A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/A/class_in_A/myB"), .Target = TEXT("/Verse.org/A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/A"), .Target = TEXT("/Verse.org/A/class_in_A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/A/class_in_A"), .Target = TEXT("/Verse.org/A/class_in_A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/A/class_in_A/myB"), .Target = TEXT("/Verse.org/A/class_in_A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/A"), .Target = TEXT("/Verse.org/A/class_in_A/myB"), .bCanRead = false },
				{.Context = TEXT("/Verse.org/A/class_in_A"), .Target = TEXT("/Verse.org/A/class_in_A/myB"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/A/class_in_A/myB"), .Target = TEXT("/Verse.org/A/class_in_A/myB"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/B"), .Target = TEXT("/Verse.org/B"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/B/class_in_B"), .Target = TEXT("/Verse.org/B"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/B"), .Target = TEXT("/Verse.org/B/class_in_B"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/B/class_in_B"), .Target = TEXT("/Verse.org/B/class_in_B"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/B/class_in_B"), .Target = TEXT("/Verse.org/A/class_in_A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/A/class_in_A"), .Target = TEXT("/Verse.org/B/class_in_B"), .bCanRead = true }, // Only true because of scoping
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					A<public> := module:
						class_in_A<public> := class:
							myB<private>:B.class_in_B = B.class_in_B{}
					B<public> := module:
						class_in_B := class:
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/A"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/A/class_in_A"), .Parent = TEXT("/Verse.org/A"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/A/class_in_A/myB"), .Parent = TEXT("/Verse.org/A/class_in_A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Private },
				{.Path = TEXT("/Verse.org/B"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/B/class_in_B"), .Parent = TEXT("/Verse.org/B"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/A"), .Target = TEXT("/Verse.org/A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/A/class_in_A"), .Target = TEXT("/Verse.org/A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/A/class_in_A/myB"), .Target = TEXT("/Verse.org/A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/A"), .Target = TEXT("/Verse.org/A/class_in_A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/A/class_in_A"), .Target = TEXT("/Verse.org/A/class_in_A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/A/class_in_A/myB"), .Target = TEXT("/Verse.org/A/class_in_A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/A"), .Target = TEXT("/Verse.org/A/class_in_A/myB"), .bCanRead = false },
				{.Context = TEXT("/Verse.org/A/class_in_A"), .Target = TEXT("/Verse.org/A/class_in_A/myB"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/A/class_in_A/myB"), .Target = TEXT("/Verse.org/A/class_in_A/myB"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/B"), .Target = TEXT("/Verse.org/B"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/B/class_in_B"), .Target = TEXT("/Verse.org/B"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/B"), .Target = TEXT("/Verse.org/B/class_in_B"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/B/class_in_B"), .Target = TEXT("/Verse.org/B/class_in_B"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/B/class_in_B"), .Target = TEXT("/Verse.org/A/class_in_A"), .bCanRead = true },
				{.Context = TEXT("/Verse.org/A/class_in_A"), .Target = TEXT("/Verse.org/B/class_in_B"), .bCanRead = false }, // Now false as class_in_b is internal
			}
		},
		// var tests
		{
			.RegisteredPaths =
			{
				/*
					Module<public> := module:
						A<public> := class:
							var<public> IntArray<public>:[]int = array{1,2,3}
						foo():void =
							X := A { }
							set X.IntArray = array{1,2,3}
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/Module"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/Module/A"), .Parent = TEXT("/Verse.org/Module"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/Module/A/IntArray"), .Parent = TEXT("/Verse.org/Module/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Public, .WriteAccess = EAccess::Public, .Flags = EFlags::Var },
				{.Path = TEXT("/Verse.org/Module/foo"), .Parent = TEXT("/Verse.org/Module"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/Module/foo"), .Target = TEXT("/Verse.org/Module/A/IntArray"), .bCanRead = true, .bCanWrite = true },
			}
		},
		{
			.RegisteredPaths =
			{
				// Pretty sure the error verse provides is somewhat confused but really it's complaining about the write access being insufficient
				/*
					Module<public> := module:
						A<public> := class:
							var<private> IntArray<public>:[]int = array{1,2,3}
						foo():void =
							X := A { }
							set X.IntArray = array{1,2,3} # V3509: This assignment expects a value of type false, but the assigned value is an incompatible value of type []type{_X:int where 1 <= _X, _X <= 3}.
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/Module"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/Module/A"), .Parent = TEXT("/Verse.org/Module"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/Module/A/IntArray"), .Parent = TEXT("/Verse.org/Module/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Public, .WriteAccess = EAccess::Private, .Flags = EFlags::Var },
				{.Path = TEXT("/Verse.org/Module/foo"), .Parent = TEXT("/Verse.org/Module"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/Module/foo"), .Target = TEXT("/Verse.org/Module/A/IntArray"), .bCanRead = true, .bCanWrite = false },
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					Module<public> := module:
						A<public> := class:
							var<public> IntArray<private>:[]int = array{1,2,3}  
						foo():void =
							X := A { }
							set X.IntArray = array{1,2,3} # V3593: Invalid access of private data 
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/Module"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/Module/A"), .Parent = TEXT("/Verse.org/Module"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/Module/A/IntArray"), .Parent = TEXT("/Verse.org/Module/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Private, .WriteAccess = EAccess::Public, .Flags = EFlags::Var },
				{.Path = TEXT("/Verse.org/Module/foo"), .Parent = TEXT("/Verse.org/Module"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				// Invalid (Won't compile / Registry Asserts). Write cannot be more visible than Read
				//{.Context = TEXT("/Verse.org/Module/foo"), .Target = TEXT("/Verse.org/Module/A/IntArray"), .bCanRead = false, .bCanWrite = false },
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					Module<public> := module:
						A<public> := class:
							var<public> IntArray<internal>:[]int = array{1,2,3}
						foo():void =
							X := A { }
							set X.IntArray = array{1,2,3}
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/Module"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/Module/A"), .Parent = TEXT("/Verse.org/Module"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/Module/A/IntArray"), .Parent = TEXT("/Verse.org/Module/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Internal, .WriteAccess = EAccess::Public, .Flags = EFlags::Var },
				{.Path = TEXT("/Verse.org/Module/foo"), .Parent = TEXT("/Verse.org/Module"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/Module/foo"), .Target = TEXT("/Verse.org/Module/A/IntArray"), .bCanRead = true, .bCanWrite = true },
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					Module<public> := module:
						A<public> := class:
							var<public> IntArray<private>:[]int = array{1,2,3}
							foo():void =
								set A{}.IntArray = array{1,2,3}
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/Module"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/Module/A"), .Parent = TEXT("/Verse.org/Module"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/Module/A/IntArray"), .Parent = TEXT("/Verse.org/Module/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Private, .WriteAccess = EAccess::Public, .Flags = EFlags::Var },
				{.Path = TEXT("/Verse.org/Module/A/foo"), .Parent = TEXT("/Verse.org/Module/A"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/Module/A/foo"), .Target = TEXT("/Verse.org/Module/A/IntArray"), .bCanRead = true, .bCanWrite = true },
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					Module<public> := module:
						A<public> := class:
							var<protected> IntArray<internal>:[]int = array{1,2,3}
						foo():void =
							set A{}.IntArray = array{1,2,3}
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/Module"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/Module/A"), .Parent = TEXT("/Verse.org/Module"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/Module/A/IntArray"), .Parent = TEXT("/Verse.org/Module/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Internal, .WriteAccess = EAccess::Protected, .Flags = EFlags::Var },
				{.Path = TEXT("/Verse.org/Module/foo"), .Parent = TEXT("/Verse.org/Module"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/Module/foo"), .Target = TEXT("/Verse.org/Module/A/IntArray"), .bCanRead = true, .bCanWrite = false },
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					Module<public> := module:
						A<public> := class:
							var<protected> IntArray<internal>:[]int = array{1,2,3}
							foo():void =
								set A{}.IntArray = array{1,2,3}
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/Module"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/Module/A"), .Parent = TEXT("/Verse.org/Module"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/Module/A/IntArray"), .Parent = TEXT("/Verse.org/Module/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Internal, .WriteAccess = EAccess::Protected, .Flags = EFlags::Var },
				{.Path = TEXT("/Verse.org/Module/A/foo"), .Parent = TEXT("/Verse.org/Module/A"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/Module/A/foo"), .Target = TEXT("/Verse.org/Module/A/IntArray"), .bCanRead = true, .bCanWrite = true },
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					Module<public> := module:
						A<public> := class:
							var<protected> IntArray<internal>:[]int = array{1,2,3}
						B<public> := class(A):
							foo():void =
								set A{}.IntArray = array{1,2,3}
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/Module"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/Module/A"), .Parent = TEXT("/Verse.org/Module"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/Module/A/IntArray"), .Parent = TEXT("/Verse.org/Module/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Internal, .WriteAccess = EAccess::Protected, .Flags = EFlags::Var },
				{.Path = TEXT("/Verse.org/Module/B"), .Parent = TEXT("/Verse.org/Module"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal, .ClassSuper = TEXT("/Verse.org/Module/A") },
				{.Path = TEXT("/Verse.org/Module/B/foo"), .Parent = TEXT("/Verse.org/Module/B"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/Module/B/foo"), .Target = TEXT("/Verse.org/Module/A/IntArray"), .bCanRead = true, .bCanWrite = true },
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					ModuleA<public> := module:
						A<public> := class:
							var<internal> IntArray<public>:[]int = array{1,2,3}
					ModuleB<public> := module:
						foo():void =
							set ModuleA.A{}.IntArray = array{1,2,3}
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/ModuleA"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/ModuleA/A"), .Parent = TEXT("/Verse.org/ModuleA"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/ModuleA/A/IntArray"), .Parent = TEXT("/Verse.org/ModuleA/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Public, .WriteAccess = EAccess::Internal, .Flags = EFlags::Var },
				{.Path = TEXT("/Verse.org/ModuleB"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/ModuleB/foo"), .Parent = TEXT("/Verse.org/ModuleB"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/ModuleB/foo"), .Target = TEXT("/Verse.org/ModuleA/A/IntArray"), .bCanRead = true, .bCanWrite = false },
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					ModuleA<public> := module:
						A<public> := class:
							var<public> IntArray<internal>:[]int = array{1,2,3} # Invalid. Write cannot be more visible than Read
					ModuleB<public> := module:
						foo():void =
							set ModuleA.A{}.IntArray = array{1,2,3}
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/ModuleA"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/ModuleA/A"), .Parent = TEXT("/Verse.org/ModuleA"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/ModuleA/A/IntArray"), .Parent = TEXT("/Verse.org/ModuleA/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Internal, .WriteAccess = EAccess::Public, .Flags = EFlags::Var },
				{.Path = TEXT("/Verse.org/ModuleB"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/ModuleB/foo"), .Parent = TEXT("/Verse.org/ModuleB"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				// Invalid (Won't compile / Registry Asserts). Write cannot be more visible than Read
				//{.Context = TEXT("/Verse.org/ModuleB/foo"), .Target = TEXT("/Verse.org/ModuleA/A/IntArray"), .bCanRead = false, .bCanWrite = false },
			}
		},
		// constructability
		{
			.RegisteredPaths =
			{
				/*
					X := module:
						A<public> := class<public>:
							myMember<internal>:int=0
					Y := module:
						B := class:
							myFunction(a:X.A):void=
								X.A{}
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/X"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/X/A"), .Parent = TEXT("/Verse.org/X"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Public, .ConstructorAccess = EAccess::Public },
				{.Path = TEXT("/Verse.org/X/A/myMember"), .Parent = TEXT("/Verse.org/X/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/Y"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/Y/B"), .Parent = TEXT("/Verse.org/Y"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/Y/B/myFunction"), .Parent = TEXT("/Verse.org/Y/B"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/X"), .Target = TEXT("/Verse.org/X/A"), .bCanConstruct = true },
				{.Context = TEXT("/Verse.org/X"), .Target = TEXT("/Verse.org/Y/B"), .bCanConstruct = false }, // Normal accessibility rules 
				{.Context = TEXT("/Verse.org/X"), .Target = TEXT("/Verse.org/Y"), .bCanConstruct = false }, // Nonsensical
				{.Context = TEXT("/Verse.org/X/A/myMember"), .Target = TEXT("/Verse.org/X/A"), .bCanConstruct = true }, // Nonsensical. True because they share the same scope. Should we error users who give bad input?
				{.Context = TEXT("/Verse.org/Y/B"), .Target = TEXT("/Verse.org/X/A"), .bCanConstruct = true },
				{.Context = TEXT("/Verse.org/Y/B/myFunction"), .Target = TEXT("/Verse.org/X/A"), .bCanConstruct = true },
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					X := module:
						A<public> := class<internal>:
							myMember<internal>:int=0
					Y := module:
						B := class:
							myFunction(a:X.A):void=
								X.A{}
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/X"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/X/A"), .Parent = TEXT("/Verse.org/X"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Public, .ConstructorAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/X/A/myMember"), .Parent = TEXT("/Verse.org/X/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/Y"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/Y/B"), .Parent = TEXT("/Verse.org/Y"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/Y/B/myFunction"), .Parent = TEXT("/Verse.org/Y/B"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/X"), .Target = TEXT("/Verse.org/X/A"), .bCanConstruct = true },
				{.Context = TEXT("/Verse.org/X"), .Target = TEXT("/Verse.org/Y/B"), .bCanConstruct = false }, // Normal accessibility rules 
				{.Context = TEXT("/Verse.org/Y/B"), .Target = TEXT("/Verse.org/X/A"), .bCanConstruct = false },
				{.Context = TEXT("/Verse.org/Y/B/myFunction"), .Target = TEXT("/Verse.org/X/A"), .bCanConstruct = false },
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					X := module:
						A<public> := class<scoped{Y}>:
							myMember<internal>:int=0
					Y := module:
						B := class:
							myFunction(a:X.A):void=
								X.A{}
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/X"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/X/A"), .Parent = TEXT("/Verse.org/X"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Public, .ConstructorAccess = EAccess::Scoped, .ConstructorScopeAccessPaths = {TEXT("/Verse.org/Y")}},
				{.Path = TEXT("/Verse.org/X/A/myMember"), .Parent = TEXT("/Verse.org/X/A"), .Kind = EPathKind::Data, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/Y"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/Y/B"), .Parent = TEXT("/Verse.org/Y"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/Y/B/myFunction"), .Parent = TEXT("/Verse.org/Y/B"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },
			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/X"), .Target = TEXT("/Verse.org/X/A"), .bCanConstruct = true },
				{.Context = TEXT("/Verse.org/X"), .Target = TEXT("/Verse.org/Y/B"), .bCanConstruct = false }, // Normal accessibility rules 
				{.Context = TEXT("/Verse.org/Y/B"), .Target = TEXT("/Verse.org/X/A"), .bCanConstruct = true }, // Scoped
				{.Context = TEXT("/Verse.org/Y/B/myFunction"), .Target = TEXT("/Verse.org/X/A"), .bCanConstruct = true }, // Scoped
			}
		},
		{
			.RegisteredPaths =
			{
				/*
					X := module:
						A<public> := class<abstract>:
						myFunction():void=
							A{}
				*/
				{.Path = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Public  },
				{.Path = TEXT("/Verse.org/X"), .Parent = TEXT("/Verse.org"), .Kind = EPathKind::Module, .ReadAccess = EAccess::Internal },
				{.Path = TEXT("/Verse.org/X/A"), .Parent = TEXT("/Verse.org/X"), .Kind = EPathKind::Class, .ReadAccess = EAccess::Public, .Flags = EFlags::Abstract },
				{.Path = TEXT("/Verse.org/X/myFunction"), .Parent = TEXT("/Verse.org/X/A"), .Kind = EPathKind::Function, .ReadAccess = EAccess::Internal },

			},
			.Tests =
			{
				{.Context = TEXT("/Verse.org/X/myFunction"), .Target = TEXT("/Verse.org/X/A"), .bCanConstruct = false }, // Abstract
			}
		},
	};

	// Test cases
	{
		for (FAccessibilityTestCase& TestCase : TestCases)
		{
			FVersePathRegistry Registry;

			// Populate registry
			FRegistryBuilder RegistryBuilder(TEXT("AccessibilityTestPackage"));
			for (const FRegistryBuilder::FPathDesc& PathDesc : TestCase.RegisteredPaths)
			{
				check(RegistryBuilder.RegisterPath(PathDesc));
			}

			Registry.Append(RegistryBuilder);

			// Run Tests
			for (const FAccessibilityTestCase::FTest& Test : TestCase.Tests)
			{
				if (Test.bCanWrite.IsSet())
				{
					bool bActualResult = Registry.CanWrite(Test.Context, Test.Target);
					TestTrue(FString::Printf(TEXT("%s %s be writable from %s"), *Test.Target, (*Test.bCanWrite ? TEXT("should") : TEXT("should not")), *Test.Context), bActualResult == *Test.bCanWrite);
				}
				if (Test.bCanRead.IsSet())
				{
					bool bActualResult = Registry.CanRead(Test.Context, Test.Target);
					TestTrue(FString::Printf(TEXT("%s %s be readable from %s"), *Test.Target, (*Test.bCanRead ? TEXT("should") : TEXT("should not")), *Test.Context), bActualResult == *Test.bCanRead);
				}
				if (Test.bCanConstruct.IsSet())
				{
					bool bActualResult = Registry.CanConstruct(Test.Context, Test.Target);
					TestTrue(FString::Printf(TEXT("%s %s be constructable from %s"), *Test.Target, (*Test.bCanConstruct ? TEXT("should") : TEXT("should not")), *Test.Context), bActualResult == *Test.bCanConstruct);
				}
			}
		}
	}
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FVersePathRegistryBuilderTests, FVersePathRegistryTestsBase, "System.AssetRegistry.VersePathRegistry",
	EAutomationTestFlags_ApplicationContextMask | TestFilter);
bool FVersePathRegistryBuilderTests::RunTest(const FString& Parameters)
{
	RunBuilderTests();
	RunAccessibilityTests();

	return true;
}
#endif // WITH_TESTS
