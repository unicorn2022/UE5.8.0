// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cloud/MetaHumanTDSUtils.h"

#include "HttpModule.h"
#include "HttpManager.h"
#include "PlatformHttp.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "Logging/StructuredLog.h"
#include "HAL/IConsoleManager.h"

#include "Cloud/MetaHumanCloudServicesSettings.h"

DEFINE_LOG_CATEGORY(LogMetaHumanTDSUtils);

namespace UE::MetaHuman::TDSUtils
{

FScopedExchangeCodeHandler::FScopedExchangeCodeHandler(const FString& InTemplateHost, const FString& InTemplateName, const FString& InClientId)
	: FExchangeCodeHandler(InTemplateHost, InTemplateName, InClientId)
{
	Acquire();
}

FScopedExchangeCodeHandler::~FScopedExchangeCodeHandler()
{
	Release();
}

namespace Private
{
	const TSharedRef<IHttpRequest> CreateGetRequest(const FString& InUrl)
	{
		const TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
		Request->SetTimeout(30.f);
		Request->SetVerb(TEXT("GET"));
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
		Request->SetURL(InUrl);

		return Request;
	}

	/**
	 * Puts given TDS future into flight and waits until it's completed.
	 */
	template<typename TFutureType, typename TFunc>
	void PutInFlight(const FString& InMethodName, TFuture<TFutureType>&& InFuture, TFunc InCallback)
	{
		UE_LOGFMT(LogMetaHumanTDSUtils, Display, "Calling {TDSMethod}", InMethodName);

		if (!InFuture.IsValid())
		{
			UE_LOGFMT(LogMetaHumanTDSUtils, Error, "Unable to execute call {TDSMethod}, invalid arguments", InMethodName);
			return;
		}

		bool bInFlight = true;

		InFuture.Next([&bInFlight, InCallback](const auto&... Args) mutable
			{
				bInFlight = false;
				InCallback(Forward<decltype(Args)>(Args)...);
			});

		while (bInFlight)
		{
			FHttpModule::Get().GetHttpManager().Tick(0.1f);
			FPlatformProcess::Sleep(0.05f);
		}
	}

	bool AcquireAccount_Sync(const FString& InTemplateHost, const FString& InTemplateName, const FString& InClientId, FString& OutAccountId)
	{
		PutInFlight(TEXT("AcquireAccount"), AcquireAccount(InTemplateHost, InTemplateName), [&OutAccountId](const FTdsAcquireAccountResult& Result)
		{
			if (Result.HasError())
			{
				UE_LOGFMT(LogMetaHumanTDSUtils, Error, "TDS acquire account error: {ErrorMessage}", Result.GetError());
				return;
			}

			const FMetaHumanTDSAcquiredAccountInfo& AccountInfo = Result.GetValue();

			UE_LOGFMT(LogMetaHumanTDSUtils, Display, "Using TDS account ID: {AccountId}, email: {Email}", AccountInfo.EpicAccountId, AccountInfo.Email);
			OutAccountId = AccountInfo.EpicAccountId;
		});

		return !OutAccountId.IsEmpty();
	}

	bool GetExchangeCode_Sync(const FString& InTemplateHost, const FString& InAccoundId, const FString& InClientId, FString& OutExchangeCode)
	{
		PutInFlight(TEXT("GetExchangeCode"), GetExchangeCode(InTemplateHost, InAccoundId, InClientId), [&OutExchangeCode](const FTdsGetExchangeCodeResult& Result)
		{
			if (Result.HasError())
			{
				UE_LOGFMT(LogMetaHumanTDSUtils, Error, "TDS get exchange code error: {ErrorMessage}", Result.GetError());
				return;
			}

			UE_LOGFMT(LogMetaHumanTDSUtils, Display, "Exchange code successfully obtained");
			OutExchangeCode = Result.GetValue();
		});

		return !OutExchangeCode.IsEmpty();
	}

	bool ReleaseAccount_Sync(const FString& InTemplateHost, const FString& InAccountId)
	{
		bool bReleaseOk = false;

		PutInFlight(TEXT("ReleaseAccount"), ReleaseAccount(InTemplateHost, InAccountId), [&bReleaseOk](const FTdsReleaseAccountResult& Result)
		{
			if (Result.HasError())
			{
				UE_LOGFMT(LogMetaHumanTDSUtils, Error, "TDS release account error: {ErrorMessage}", Result.GetError());
			}
			else
			{
				UE_LOGFMT(LogMetaHumanTDSUtils, Display, "TDS account successfully released");
				bReleaseOk = true;
			}
		});

		return bReleaseOk;
	}

	void UpdateExchangeCodeCvar(const FString& InExchangeCode)
	{
		if (IConsoleVariable* const ExchangeCodeCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("MetaHuman.Cloud.Config.ExchangeCode")))
		{
			ExchangeCodeCVar->Set(*InExchangeCode);
		}
	}
}

TFuture<FTdsAcquireAccountResult> AcquireAccount(const FString& InTemplateHost, const FString& InTemplateName)
{
	if (InTemplateHost.IsEmpty() || InTemplateName.IsEmpty())
	{
		return {};
	}

	TSharedPtr<TPromise<FTdsAcquireAccountResult>> Promise = MakeShared<TPromise<FTdsAcquireAccountResult>>();

	const TSharedRef<IHttpRequest> HttpRequest = Private::CreateGetRequest
	(
		InTemplateHost / FString::Format(TEXT("/v1/pool/setAccountInUse?templateName={0}"),
		{
			FPlatformHttp::UrlEncode(InTemplateName)
		})
	);

	HttpRequest->OnProcessRequestComplete().BindLambda([Promise](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
	{
		if (!bSuccess || !Response.IsValid())
		{
			Promise->SetValue(MakeError(TEXT("Invalid response")));
			return;
		}

		if (!EHttpResponseCodes::IsOk(Response->GetResponseCode()))
		{
			Promise->SetValue(MakeError(FString::Format(TEXT("Invalid response code, got {0}"), { Response->GetResponseCode() })));
			return;
		}

		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(Response->GetContentAsString());
		TSharedPtr<FJsonObject> ResponseJson;

		if (!FJsonSerializer::Deserialize(JsonReader, ResponseJson))
		{
			Promise->SetValue(MakeError(TEXT("Unable to parse response")));
			return;
		}

		FMetaHumanTDSAcquiredAccountInfo AccountInfo;

		// Retrieve Epic Account ID from the response
		const TArray<TSharedPtr<FJsonValue>>* ResultsJsonArray;

		if (!ResponseJson->TryGetArrayField(TEXT("results"), ResultsJsonArray))
		{
			Promise->SetValue(MakeError(TEXT("Missing 'results'")));
			return;
		}

		if (ResultsJsonArray->Num() != 1)
		{
			Promise->SetValue(MakeError(TEXT("Expected exactly one object in the 'results' response")));
			return;
		}

		const TSharedPtr<FJsonObject>& ResultObject = ResultsJsonArray->operator[](0)->AsObject();

		FString AccessToken;

		// Only verify that access token actually exists, no need to record it
		if (!ResultObject->TryGetStringField(TEXT("accessToken"), AccessToken))
		{
			Promise->SetValue(MakeError(TEXT("Missing `accessToken`")));
			return;
		}

		const TSharedPtr<FJsonObject>* EpicAccountInfoObject;

		if (!ResultObject->TryGetObjectField(TEXT("epicAccountInfo"), EpicAccountInfoObject))
		{
			Promise->SetValue(MakeError(TEXT("Missing `epicAccountInfo`")));
			return;
		}

		if (!(*EpicAccountInfoObject)->TryGetStringField(TEXT("epicAccountId"), AccountInfo.EpicAccountId))
		{
			Promise->SetValue(MakeError(TEXT("Missing `epicAccountId`")));
			return;
		}

		if (!(*EpicAccountInfoObject)->TryGetStringField(TEXT("email"), AccountInfo.Email))
		{
			Promise->SetValue(MakeError(TEXT("Missing `email`")));
			return;
		}

		if (!(*EpicAccountInfoObject)->TryGetStringField(TEXT("password"), AccountInfo.Password))
		{
			Promise->SetValue(MakeError(TEXT("Missing `password`")));
			return;
		}

		Promise->SetValue(MakeValue(AccountInfo));
	});
	HttpRequest->ProcessRequest();

	return Promise->GetFuture();
}

TFuture<FTdsGetExchangeCodeResult> GetExchangeCode(const FString& InTemplateHost, const FString& InAccountId, const FString& InClientId)
{
	if (InTemplateHost.IsEmpty() || InAccountId.IsEmpty() || InClientId.IsEmpty())
	{
		return {};
	}

	TSharedPtr<TPromise<FTdsGetExchangeCodeResult>> Promise = MakeShared<TPromise<FTdsGetExchangeCodeResult>>();

	const TSharedRef<IHttpRequest> HttpRequest = Private::CreateGetRequest
	(
		InTemplateHost / FString::Format(TEXT("/v1/account/{0}/exchange_code?consumingClientId={1}"),
		{
			FPlatformHttp::UrlEncode(InAccountId),
			FPlatformHttp::UrlEncode(InClientId)
		})
	);

	HttpRequest->OnProcessRequestComplete().BindLambda([Promise](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
	{
		if (!bSuccess || !Response.IsValid())
		{
			Promise->SetValue(MakeError(TEXT("Invalid response")));
			return;
		}

		if (!EHttpResponseCodes::IsOk(Response->GetResponseCode()))
		{
			Promise->SetValue(MakeError(FString::Format(TEXT("Invalid response code, got {0}"), { Response->GetResponseCode() })));
			return;
		}

		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(Response->GetContentAsString());
		TSharedPtr<FJsonObject> ResponseJson;

		if (!FJsonSerializer::Deserialize(JsonReader, ResponseJson))
		{
			Promise->SetValue(MakeError(TEXT("Unable to parse response")));
			return;
		}

		FString ExchangeCode;

		if (!ResponseJson->TryGetStringField(TEXT("exchange_code"), ExchangeCode))
		{
			Promise->SetValue(MakeError(TEXT("Missing `exchange_code`")));
			return;
		}

		Promise->SetValue(MakeValue(ExchangeCode));
	});
	HttpRequest->ProcessRequest();

	return Promise->GetFuture();
}

TFuture<FTdsReleaseAccountResult> ReleaseAccount(const FString& InTemplateHost, const FString& InAccountId)
{
	if (InTemplateHost.IsEmpty() || InAccountId.IsEmpty())
	{
		return {};
	}

	TSharedPtr<TPromise<FTdsReleaseAccountResult>> Promise = MakeShared<TPromise<FTdsReleaseAccountResult>>();

	const TSharedRef<IHttpRequest> HttpRequest = Private::CreateGetRequest
	(
		InTemplateHost / FString::Format(TEXT("/v1/pool/setPoolInformation?epicAccountId={0}"),
		{
			FPlatformHttp::UrlEncode(InAccountId)
		})
	);

	HttpRequest->OnProcessRequestComplete().BindLambda([Promise](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
	{
		if (!bSuccess || !Response.IsValid())
		{
			Promise->SetValue(MakeError(TEXT("Invalid response")));
			return;
		}

		if (!EHttpResponseCodes::IsOk(Response->GetResponseCode()))
		{
			Promise->SetValue(MakeError(FString::Format(TEXT("Invalid response code, got {0}"), { Response->GetResponseCode() })));
			return;
		}

		Promise->SetValue(MakeValue());
	});
	HttpRequest->ProcessRequest();

	return Promise->GetFuture();
}


/**
 * Actual implementation of TDS exchange authentication.
 */
class FExchangeCodeHandler::FImpl
{
public:
	FImpl(const FString& InTemplateHost, const FString& InTemplateName, const FString& InClientId)
		: TemplateHost(InTemplateHost)
		, TemplateName(InTemplateName)
		, ClientId(InClientId)
	{
	}

	void Acquire()
	{
		SetExchangeCode(TEXT(""));
		EpicAccountId.Empty();

		if (Private::AcquireAccount_Sync(TemplateHost, TemplateName, ClientId, EpicAccountId))
		{
			if (Private::GetExchangeCode_Sync(TemplateHost, EpicAccountId, ClientId, ExchangeCode))
			{
				SetExchangeCode(ExchangeCode);
			}
		}
	}

	bool HasExchangeCode() const
	{
		return !ExchangeCode.IsEmpty();
	}

	void Release()
	{
		// Clear the exchange code just so we don't end up using stale value
		SetExchangeCode(TEXT(""));

		// If no account was acquired, nothing to release
		if (EpicAccountId.IsEmpty())
		{
			return;
		}

		Private::ReleaseAccount_Sync(TemplateHost, EpicAccountId);
	}

private:
	FString TemplateHost;
	FString TemplateName;
	FString ClientId;

	// Account that we're currently operating on.
	FString EpicAccountId;

	// Token needed for the authentication
	FString ExchangeCode;

	/**
	 * Assigns exchange code value to this struct and MetaHumanSDK.
	 */
	void SetExchangeCode(const FString& InExchangeCode)
	{
		ExchangeCode = InExchangeCode;
		Private::UpdateExchangeCodeCvar(ExchangeCode);
	}
};

FExchangeCodeHandler::FExchangeCodeHandler(const FString& InTemplateHost, const FString& InTemplateName, const FString& InClientId)
	: Impl(MakeUnique<FExchangeCodeHandler::FImpl>(InTemplateHost, InTemplateName, InClientId))
{
}

FExchangeCodeHandler::~FExchangeCodeHandler() = default;

void FExchangeCodeHandler::Acquire()
{
	Impl->Acquire();
}

void FExchangeCodeHandler::Release()
{
	Impl->Release();
}

bool FExchangeCodeHandler::HasExchangeCode() const
{
	return Impl->HasExchangeCode();
}

} // namespace UE::MetaHuman::TDSUtils

FString UMetaHumanTDSUtils::AcquireTdsAccount(const FString& InTemplateHost, const FString& InTemplateName, const FString& InClientId)
{
	using namespace UE::MetaHuman::TDSUtils::Private;

	FString ClientId = InClientId;

	if (ClientId.IsEmpty())
	{
		const UMetaHumanCloudServicesSettings* Settings = GetDefault<UMetaHumanCloudServicesSettings>();

		ClientId = Settings->ProdEosConstants.ClientCredentialsId;
	}

	UpdateExchangeCodeCvar(TEXT(""));
	FString AccountId;

	if (AcquireAccount_Sync(InTemplateHost, InTemplateName, ClientId, AccountId))
	{
		FString ExchangeCode;

		if (GetExchangeCode_Sync(InTemplateHost, AccountId, ClientId, ExchangeCode))
		{
			UpdateExchangeCodeCvar(ExchangeCode);
			
			return AccountId;
		}
	}

	return {};
}

bool UMetaHumanTDSUtils::ReleaseTdsAccount(const FString& InTemplateHost, const FString& InAccountId)
{
	using namespace UE::MetaHuman::TDSUtils::Private;

	UpdateExchangeCodeCvar(TEXT(""));
	
	return ReleaseAccount_Sync(InTemplateHost, InAccountId);
}
