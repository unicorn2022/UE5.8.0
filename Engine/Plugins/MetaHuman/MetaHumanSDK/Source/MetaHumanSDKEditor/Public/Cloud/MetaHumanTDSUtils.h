// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Templates/ValueOrError.h"
#include "Delegates/Delegate.h"
#include "Async/Future.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MetaHumanTDSUtils.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMetaHumanTDSUtils, Log, All);

#define UE_API METAHUMANSDKEDITOR_API

/**
 * Utilities to contact TDS service.
 */
namespace UE::MetaHuman::TDSUtils
{
	/**
	 * Information about the acquired account.
	 */
	struct FMetaHumanTDSAcquiredAccountInfo
	{
		FString EpicAccountId;
		FString Email;
		FString Password;
	};

	using FTdsAcquireAccountResult = TValueOrError<FMetaHumanTDSAcquiredAccountInfo, FString>;
	UE_API TFuture<FTdsAcquireAccountResult> AcquireAccount(const FString& InTemplateHost, const FString& InTemplateName);

	using FTdsGetExchangeCodeResult = TValueOrError<FString, FString>;
	UE_API TFuture<FTdsGetExchangeCodeResult> GetExchangeCode(const FString& InTemplateHost, const FString& InAccountId, const FString& InClientId);

	using FTdsReleaseAccountResult = TValueOrError<void, FString>;
	UE_API TFuture<FTdsReleaseAccountResult> ReleaseAccount(const FString& InTemplateHost, const FString& InAccountId);

	/**
	 * Utility struct to help with obtaining TDS exchange code and setting it on the
	 * MetaHumanSDK for user authentication. Runs all of the HTTP requests synchronously.
	 * 
	 * Note that the exchange code only lasts for a short period of time, so be sure to
	 * trigger the authentication code within that time frame.
	 */
	class FExchangeCodeHandler
	{
	public:
		UE_API FExchangeCodeHandler(const FString& InTemplateHost, const FString& InTemplateName, const FString& InClientId);
		UE_API virtual ~FExchangeCodeHandler();

		/**
		 * Locks in the first available TDS account to use for authentication. Next, it will obtain
		 * exchange code and store it, so exchange code authentication after that point should be possible.
		 */
		UE_API void Acquire();

		/**
		 * Returns true if exchange code was set successfully, otherwise false.
		 */
		UE_API bool HasExchangeCode() const;

		/**
		 * Returns the acquired TDS account back to the pool and clears the exchange code.
		 */
		UE_API void Release();

	private:
		class FImpl;
		TUniquePtr<FImpl> Impl;
	};

	/**
	 * Same as FExchangeCodeHandler, just scoped (ctor - acquire account, dtor - release account).
	 */
	class FScopedExchangeCodeHandler : public FExchangeCodeHandler
	{
	public:
		UE_API FScopedExchangeCodeHandler(const FString& InTemplateHost, const FString& InTemplateName, const FString& InClientId);
		UE_API virtual ~FScopedExchangeCodeHandler();
	};
}

/**
 * Utility class for acquiring and releasing TDS accounts.
 */
UCLASS()
class UE_API UMetaHumanTDSUtils : public UBlueprintFunctionLibrary
{
public:
	GENERATED_BODY()

	/**
	 * Acquires a TDS account and sets the exchange code to be used for authentication.
	 * 
	 * @param InTemplateHost	Address of the TDS service
	 * @param InTemplateName	Template name of the TDS account
	 * @param InClientId	Service application ID to target. Set to empty to use the default
	 * @return	Accound ID of acquired TDS account.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|TDS")
	static FString AcquireTdsAccount(const FString& InTemplateHost, const FString& InTemplateName, const FString& InClientId = FString(TEXT("")));

	/**
	 * Releases the account back to the pool.
	 * 
	 * @param InTemplateHost	Address of the TDS service
	 * @param InAccountId	Account ID to release
	 * @return True if account was released, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|TDS")
	static bool ReleaseTdsAccount(const FString& InTemplateHost, const FString& InAccountId);
};

#undef UE_API
