// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Online/OnlineServicesOSSAdapter.h"
#include "Online/SessionsOSSAdapter.h"
#include "Online/AuthOSSAdapter.h"
#include "Online/OnlineIdOSSAdapter.h"
#include "Online/OnlineServicesRegistry.h"

#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineSessionInterface.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace UE::Online;

/**
 * Verifies that FSessionsOSSAdapter::UpdateSessionSettingsImpl correctly
 * propagates mutated settings to the V1 subsystem.
 *
 * Regression test for FORT-1059667: BuildV1SettingsForUpdate was reading
 * stale session settings from the session object instead of using the
 * computed UpdatedV2Settings. This caused the V1 UpdateSession call to
 * receive unchanged settings, and on backends that diff old/new (like EOS),
 * the OnComplete callback would never fire.
 *
 * With the Null subsystem the callback always fires, but the critical
 * assertion is that the V1 named session's NumPublicConnections and
 * NumPrivateConnections actually reflect the updated value.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSessionsOSSAdapterUpdateSettingsTest,
	"System.Engine.Online.OnlineServicesOSSAdapter.Sessions.UpdateSettingsCallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSessionsOSSAdapterUpdateSettingsTest::RunTest(const FString& Parameters)
{
	// Use a game-defined service type to avoid conflicts with any active registrations
	constexpr EOnlineServices TestServicesType = EOnlineServices::GameDefined_0;
	constexpr int32 RegistryPriority = 100;

	// --- Step 1: Get the Null subsystem via the engine's subsystem manager ---

	IOnlineSubsystem* NullSubsystem = IOnlineSubsystem::Get(FName(TEXT("NULL")));
	if (NullSubsystem == nullptr)
	{
		AddError(TEXT("Failed to get Null OnlineSubsystem. Is OnlineSubsystemNull enabled?"));
		return false;
	}

	// Login player 0 to create a unique user ID
	IOnlineIdentityPtr NullIdentity = NullSubsystem->GetIdentityInterface();
	if (!NullIdentity.IsValid())
	{
		AddError(TEXT("Null subsystem has no identity interface."));
		return false;
	}

	NullIdentity->Login(0, FOnlineAccountCredentials());

	FUniqueNetIdPtr UserUniqueId = NullIdentity->GetUniquePlayerId(0);
	if (!UserUniqueId.IsValid())
	{
		AddError(TEXT("Failed to get UniquePlayerId after login."));
		return false;
	}

	// --- Step 2: Register ID registries for the test service type ---

	FOnlineAccountIdRegistryOSSAdapter* AccountIdRegistry = new FOnlineAccountIdRegistryOSSAdapter(TestServicesType);
	FOnlineSessionIdRegistryOSSAdapter* SessionIdRegistry = new FOnlineSessionIdRegistryOSSAdapter(TestServicesType);
	FOnlineSessionInviteIdRegistryOSSAdapter* InviteIdRegistry = new FOnlineSessionInviteIdRegistryOSSAdapter(TestServicesType);

	FOnlineIdRegistryRegistry::Get().RegisterAccountIdRegistry(TestServicesType, AccountIdRegistry, RegistryPriority);
	FOnlineIdRegistryRegistry::Get().RegisterSessionIdRegistry(TestServicesType, SessionIdRegistry, RegistryPriority);
	FOnlineIdRegistryRegistry::Get().RegisterSessionInviteIdRegistry(TestServicesType, InviteIdRegistry, RegistryPriority);

	// --- Step 3: Create and initialize the OSSAdapter ---

	IOnlineSessionPtr NullSessionInt = NullSubsystem->GetSessionInterface();

	TSharedRef<FOnlineServicesOSSAdapter> Adapter = MakeShared<FOnlineServicesOSSAdapter>(
		TestServicesType, TEXT("OSSAdapter"), FName(TEXT("TestAdapter")), NullSubsystem);

	Adapter->RegisterComponents();
	Adapter->Initialize();
	Adapter->PostInitialize();

	// Cleanup helper - ensures registries and adapter are torn down on any exit path
	auto Cleanup = [&Adapter, &NullIdentity, TestServicesType, RegistryPriority]()
	{
		Adapter->Shutdown();
		FOnlineIdRegistryRegistry::Get().UnregisterAccountIdRegistry(TestServicesType, RegistryPriority);
		FOnlineIdRegistryRegistry::Get().UnregisterSessionIdRegistry(TestServicesType, RegistryPriority);
		FOnlineIdRegistryRegistry::Get().UnregisterSessionInviteIdRegistry(TestServicesType, RegistryPriority);
		NullIdentity.Reset();
	};

	// Register the user's unique ID to get a valid FAccountId
	FAccountId TestAccountId = AccountIdRegistry->FindOrAddHandle(UserUniqueId->AsShared());
	if (!TestAccountId.IsValid())
	{
		AddError(TEXT("Failed to create FAccountId from user unique ID."));
		Cleanup();
		return false;
	}

	// --- Step 4: Get the V2 sessions interface ---

	TSharedPtr<ISessions> Sessions = Adapter->GetSessionsInterface();
	if (!Sessions.IsValid())
	{
		AddError(TEXT("OSSAdapter has no sessions interface."));
		Cleanup();
		return false;
	}

	const FName SessionName = FName(TEXT("FORT1059667_TestSession"));
	constexpr uint32 OriginalMaxConnections = 8;
	constexpr uint32 UpdatedMaxConnections = 16;

	// --- Step 5: Create a session with NumMaxConnections = 8 ---

	bool bCreateComplete = false;
	bool bCreateSuccess = false;

	{
		FCreateSession::Params CreateParams;
		CreateParams.LocalAccountId = TestAccountId;
		CreateParams.SessionName = SessionName;
		CreateParams.SessionSettings.NumMaxConnections = OriginalMaxConnections;
		CreateParams.SessionSettings.bAllowNewMembers = true;
		CreateParams.SessionSettings.SchemaName = FName(TEXT("TestSchema"));
		CreateParams.bIsLANSession = true;

		TOnlineAsyncOpHandle<FCreateSession> CreateHandle = Sessions->CreateSession(MoveTemp(CreateParams));
		CreateHandle.OnComplete([&bCreateComplete, &bCreateSuccess](const TOnlineResult<FCreateSession>& Result)
		{
			bCreateComplete = true;
			bCreateSuccess = Result.IsOk();
		});
	}

	// Tick to process the queued operation (Null fires callback synchronously)
	Adapter->Tick(0.016f);

	if (!bCreateComplete)
	{
		AddError(TEXT("CreateSession did not complete after tick."));
		Cleanup();
		return false;
	}

	TestTrue(TEXT("CreateSession succeeded"), bCreateSuccess);

	// Verify V2 session exists after create
	{
		TOnlineResult<FGetSessionByName> GetResult = Sessions->GetSessionByName({ SessionName });
		TestTrue(TEXT("V2 session exists after create"), GetResult.IsOk());
	}

	// Verify initial V1 settings
	{
		const FNamedOnlineSession* V1Session = NullSessionInt->GetNamedSession(SessionName);
		TestNotNull(TEXT("V1 session exists after create"), V1Session);
		if (V1Session)
		{
			TestEqual(TEXT("Initial V1 NumPublicConnections"), (uint32)V1Session->SessionSettings.NumPublicConnections, OriginalMaxConnections);
			TestEqual(TEXT("Initial V1 NumPrivateConnections"), (uint32)V1Session->SessionSettings.NumPrivateConnections, OriginalMaxConnections);
		}
	}

	// --- Step 6: Update session settings - change NumMaxConnections to 16 ---

	bool bUpdateComplete = false;
	bool bUpdateSuccess = false;

	{
		FUpdateSessionSettings::Params UpdateParams;
		UpdateParams.LocalAccountId = TestAccountId;
		UpdateParams.SessionName = SessionName;
		UpdateParams.Mutations.NumMaxConnections = UpdatedMaxConnections;

		TOnlineAsyncOpHandle<FUpdateSessionSettings> UpdateHandle = Sessions->UpdateSessionSettings(MoveTemp(UpdateParams));
		UpdateHandle.OnComplete([&bUpdateComplete, &bUpdateSuccess](const TOnlineResult<FUpdateSessionSettings>& Result)
		{
			bUpdateComplete = true;
			bUpdateSuccess = Result.IsOk();
		});
	}

	// Tick to process the update operation
	Adapter->Tick(0.016f);

	TestTrue(TEXT("UpdateSessionSettings completed (callback fired)"), bUpdateComplete);
	TestTrue(TEXT("UpdateSessionSettings succeeded"), bUpdateSuccess);

	// --- Step 7: Verify V2 session has updated settings ---

	{
		TOnlineResult<FGetSessionByName> GetResult = Sessions->GetSessionByName({ SessionName });
		TestTrue(TEXT("Session still exists after update"), GetResult.IsOk());
		if (GetResult.IsOk())
		{
			TestEqual(TEXT("Updated V2 NumMaxConnections"), GetResult.GetOkValue().Session->GetSessionSettings().NumMaxConnections, UpdatedMaxConnections);
		}
	}

	// --- Step 8: Verify V1 session has updated settings (the fix-specific assertion) ---
	//
	// Before the fix: BuildV1SettingsForUpdate read from the V2 session object whose
	// NumMaxConnections is 0 (WriteV2SessionSettingsFromV1NamedSession does not
	// round-trip NumMaxConnections), so V1 received 0/0 instead of 16/16.
	// After the fix: the computed UpdatedV2Settings (with the mutation applied) is
	// passed explicitly, so V1 correctly receives 16/16.

	{
		const FNamedOnlineSession* V1Session = NullSessionInt->GetNamedSession(SessionName);
		TestNotNull(TEXT("V1 session still exists after update"), V1Session);
		if (V1Session)
		{
			TestEqual(TEXT("[FIX] V1 NumPublicConnections after update"), (uint32)V1Session->SessionSettings.NumPublicConnections, UpdatedMaxConnections);
			TestEqual(TEXT("[FIX] V1 NumPrivateConnections after update"), (uint32)V1Session->SessionSettings.NumPrivateConnections, UpdatedMaxConnections);

			UE_LOGF(LogTemp, Log, "[FORT-1059667] V1 NumPublicConnections=%d, NumPrivateConnections=%d (expected %d)",
				V1Session->SessionSettings.NumPublicConnections,
				V1Session->SessionSettings.NumPrivateConnections,
				UpdatedMaxConnections);
		}
	}

	// --- Cleanup ---

	// Destroy V1 session before adapter shutdown (adapter clears its delegate bindings in Shutdown)
	NullSessionInt->DestroySession(SessionName);
	Cleanup();

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
