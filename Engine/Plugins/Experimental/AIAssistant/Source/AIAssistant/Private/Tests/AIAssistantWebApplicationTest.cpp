// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Delegates/Delegate.h"
#include "Editor.h"
#include "Internationalization/Culture.h"
#include "IWebBrowserWindow.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersion.h"
#include "Misc/Optional.h"
#include "Misc/Paths.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/Package.h"

#include "AIAssistantConfig.h"
#include "AIAssistantMessageUtils.h"
#include "AIAssistantTransactionBufferManager.h"
#include "AIAssistantTextMessage.h"
#include "AIAssistantToolResponse.h"
#include "AIAssistantWebApi.h"
#include "AIAssistantWebApplication.h"
#include "AIAssistantWebJavaScriptResultDelegate.h"
#include "Tests/AIAssistantFakeToolset.h"
#include "Tests/AIAssistantFakeWebJavaScriptDelegateBinder.h"
#include "Tests/AIAssistantFakeWebJavaScriptExecutor.h"
#include "Tests/AIAssistantFakeWebApi.h"
#include "Tests/AIAssistantTestObject.h"
#include "Tests/AIAssistantTestFlags.h"
#include "Tests/AIAssistantUefnModeConsoleVar.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::AIAssistant
{
	// Create a user message.
	static FAddMessageToConversationOptions CreateUserMessage()
	{
		FAddMessageToConversationOptions Options;
		auto& Message = Options.Message;
		Message.MessageRole = EMessageRole::User;
		auto& MessageContentItem = Message.MessageContent.Emplace_GetRef();
		MessageContentItem.bVisibleToUser = true;
		MessageContentItem.ContentType = EMessageContentType::Text;
		MessageContentItem.Content.Emplace<FTextMessageContent>();
		MessageContentItem.Content.Get<FTextMessageContent>().Text = TEXT("hello");
		return Options;
	}

	// Create a navigation request for opening a link in the main frame.
	static FWebNavigationRequest CreateWebNavigationRequestForLink()
	{
		FWebNavigationRequest Request;
		Request.bIsRedirect = false;
		Request.bIsMainFrame = true;
		Request.bIsExplicitTransition = false;
		Request.TransitionSource = EWebTransitionSource::Link;
		Request.TransitionSourceQualifier = EWebTransitionSourceQualifier::Unknown;
		return Request;
	}

	// Fake web API with a fake JavaScript execution environment.
	struct FFakeWebApiWithFakeJavaScriptEnvironment
	{
		FFakeWebApiWithFakeJavaScriptEnvironment() :
			WebApi(MakeShared<FFakeWebApi>(
				JavaScriptExecutor, JavaScriptDelegateBinder, UnbindDelegate))
		{
		}

		FFakeWebJavaScriptExecutor JavaScriptExecutor;
		FFakeWebJavaScriptDelegateBinder JavaScriptDelegateBinder;
		FSimpleMulticastDelegate UnbindDelegate;
		TSharedPtr<FFakeWebApi> WebApi;
	};

	// Factory that tracks created FWebApi instances.
	struct FFakeWebApiTracker
	{
		TArray<TSharedPtr<FFakeWebApiWithFakeJavaScriptEnvironment>> WebApis;

		TFunction<TSharedPtr<FWebApi>()> CreateFactory()
		{
			return [this]() -> TSharedPtr<FWebApi>
				{
					auto WebApiContainer = MakeShared<FFakeWebApiWithFakeJavaScriptEnvironment>();
					WebApis.Add(WebApiContainer);
					return WebApiContainer->WebApi;
				};
		}
	};

	// Creates a web application with a default configuration while tracking web API instances.
	template <typename FWebApplicationType>
	struct TWebApplicationWithTracker
	{
		TWebApplicationWithTracker(
			FAutomationTestBase& InTestBase, const FString& DevOptionsRawJson = FString()) :
			Config(FConfig::Load()),
			WebApplication(
				MakeShared<FWebApplicationType>(WebApiTracker.CreateFactory(), DevOptionsRawJson)),
			TestBase(InTestBase)
		{
		}

		// Navigate to the main URL.
		void NavigateToMainUrl()
		{
			WebApiCountBeforeNavigation = WebApiTracker.WebApis.Num();
			WebApplication->OnBeforeNavigation(
				Config.MainUrl, CreateWebNavigationRequestForLink());
			WebApplication->OnPageLoadComplete();
			(void)TestBase.TestEqual(
				TEXT("NotLoaded"), WebApplication->GetLoadState(),
				FWebApplication::ELoadState::NotLoaded);
		}

		// Get the most recently created fake web API and report an error if one isn't found.
		TSharedPtr<FFakeWebApi> GetWebApi() const
		{
			TSharedPtr<FFakeWebApi> WebApi;
			int32 NumberOfWebApis = WebApiTracker.WebApis.Num();
			if (NumberOfWebApis > 0)
			{
				WebApi = WebApiTracker.WebApis[NumberOfWebApis - 1]->WebApi;
			}
			(void)TestBase.TestTrue(TEXT("HasWebApi"), WebApi ? true : false);
			return WebApi;
		}

		// Ensure IsInitialized() is called to make sure the web API is available.
		bool TestExpectIsInitialized()
		{
			auto FakeWebApi = GetWebApi();
			FWebApiBoolResult Result;
			Result.bValue = true;
			return FakeWebApi && FakeWebApi->TestExpectAsyncFunctionCallAndComplete(
				TestBase, TEXT(""), *FFakeWebApi::GetWebApiAvailableFunction(), TEXT(""),
				*Result.ToJson(false), false);
		}

		// Ensure the locale was updated and complete the execution.
		bool TestExpectUpdateGlobalLocale()
		{
			auto FakeWebApi = GetWebApi();
			return FakeWebApi && FakeWebApi->TestExpectAsyncFunctionCallAndComplete(
				TestBase, nullptr, TEXT("updateGlobalLocale"),
				*FString::Printf(
					TEXT("\"%s\""), *FInternationalization::Get().GetCurrentLanguage()->GetName()),
				TEXT(""), false);
		}

		// Ensure the agent environment was added returning a reference to the fake handle if an agent
		// environment was added.
		TUniquePtr<FAgentEnvironmentHandle> TestExpectAddAgentEnvironment(
			TOptional<bool> ExpectedUefnMode = TOptional<bool>())
		{
			auto AgentEnvironmentHandle = MakeUnique<FAgentEnvironmentHandle>();
			AgentEnvironmentHandle->Hash.Hash = TEXT("fake_hash");
			AgentEnvironmentHandle->Id.Id = TEXT("fake_id");
			auto FakeWebApi = GetWebApi();
			bool bExpectedUefnMode =
				ExpectedUefnMode.IsSet() ? ExpectedUefnMode.GetValue() : IsUefnMode();
			return FakeWebApi && FakeWebApi->TestExpectAsyncFunctionCallAndComplete(
				TestBase, nullptr, TEXT("addAgentEnvironment"),
				*WebApplication->GetAgentEnvironment(bExpectedUefnMode)->ToJson(false),
				*AgentEnvironmentHandle->ToJson(false), false)
				? MoveTemp(AgentEnvironmentHandle)
				: TUniquePtr<FAgentEnvironmentHandle>();
		}

		// Ensure an agent environment was set.
		bool TestExpectSetAgentEnvironment(const FAgentEnvironmentId& AgentEnvironmentId)
		{
			auto FakeWebApi = GetWebApi();
			return FakeWebApi && FakeWebApi->TestExpectAsyncFunctionCallAndComplete(
				TestBase, nullptr, TEXT("setAgentEnvironment"), *AgentEnvironmentId.ToJson(false),
				TEXT(""), false);
		}

		// Verify the application was initialized.
		bool TestInitialized()
		{
			bool bIsInitialized =
				TestBase.TestEqual(TEXT("CreatedOneWebApi"), WebApiTracker.WebApis.Num(), WebApiCountBeforeNavigation + 1);
			bIsInitialized = TestExpectIsInitialized() && bIsInitialized;
			bIsInitialized = TestExpectUpdateGlobalLocale() && bIsInitialized;
			bIsInitialized = TestBase.TestEqual(
				TEXT("StillNotLoaded"), WebApplication->GetLoadState(),
				FWebApplication::ELoadState::NotLoaded) && bIsInitialized;

			auto AgentEnvironmentHandle = TestExpectAddAgentEnvironment();
			bIsInitialized =
				AgentEnvironmentHandle.IsValid() &&
				TestExpectSetAgentEnvironment(AgentEnvironmentHandle->Id) &&
				bIsInitialized;
			bIsInitialized = TestBase.TestEqual(
				TEXT("Loaded"), WebApplication->GetLoadState(),
				FWebApplication::ELoadState::Complete) && bIsInitialized;
			return bIsInitialized;
		}

		// Ensure the application is initialized by navigating to the main URL and verifying
		// initializers have been called.
		bool EnsureInitialized()
		{
			NavigateToMainUrl();
			return TestInitialized();
		}

		// Verify that a conversation was created and complete the method.
		bool TestExpectCreateConversation()
		{
			auto WebApi = GetWebApi();
			return WebApi && WebApi->TestExpectAsyncFunctionCallAndComplete(
				TestBase, nullptr, TEXT("createConversation"), TEXT(""), TEXT("{}"), false);
		}

		// Expect a JSON error 
		static const TCHAR* ExpectJavaScriptError(FAutomationTestBase& TestBase)
		{
			static const TCHAR* ErrorJson = TEXT(R"json({"error": "fake error"})json");
			TestBase.AddExpectedMessage(
				ErrorJson, EAutomationExpectedMessageFlags::MatchType::Contains, 1, false);
			return ErrorJson;
		}

		FFakeWebApiTracker WebApiTracker;
		FConfig Config;
		TSharedPtr<FWebApplicationType> WebApplication;
		FAutomationTestBase& TestBase;
		int32 WebApiCountBeforeNavigation = 0;
	};

	class FSpyWebApplication : public FWebApplication
	{
	public:
		FSpyWebApplication(
			TFunction<TSharedPtr<FWebApi>()> InWebApiFactory,
			const FString& DevOptionsRawJson = FString()) :
			FWebApplication(MoveTemp(InWebApiFactory), DevOptionsRawJson)
		{
		}

		virtual ~FSpyWebApplication() override = default;

		void SetRegistry(TSharedPtr<UE::ToolsetRegistry::FToolsetRegistry> InRegistry)
		{
			Registry = InRegistry;
		}

		virtual UE::ToolsetRegistry::FToolsetRegistry& GetToolsetRegistry() override
		{
			check(Registry);
			return *Registry;
		}

		using FWebApplication::OnConversationUpdate;
		using FWebApplication::OnPendingFileDecision;
		using FWebApplication::IsConversationCancelled;
		using FWebApplication::BuildPendingFileListOptions;

		void AddUserMessageToConversation(
			FAddMessageToConversationOptions&& Options) override {
			AddUserMessageToConversationCalls.Add(Options);
			if (bCallBaseAddUserMessageToConversation)
			{
				FWebApplication::AddUserMessageToConversation(MoveTemp(Options));
			}
		}

		bool ProcessMessage(const FMessage& Message, const FString& ConversationId) override
		{
			bool bProcessedMessage = true;
			if (bCallBaseProcessMessage)
			{
				bProcessedMessage = FWebApplication::ProcessMessage(Message, ConversationId);
			}
			if (bProcessedMessage) ProcessMessageCalls.Add(Message);
			return bProcessedMessage;
		}

		TFuture<TValueOrError<void, FString>> ProcessToolCallContent(
			const FToolCallContent& ToolCall, const FString& ConversationId) override
		{
			ProcessToolCallContentCalls.Add(ToolCall);
			if (bCallBaseProcessToolCallContent || ProcessToolCallContentFakeResults.IsEmpty())
			{
				return FWebApplication::ProcessToolCallContent(ToolCall, ConversationId);
			}
			TSharedPtr<TPromise<TValueOrError<void, FString>>> ResultPromise;
			bool Successful = ProcessToolCallContentFakeResults.Dequeue(ResultPromise);
			check(Successful);
			return ResultPromise->GetFuture();
		}

		TSharedPtr<UE::ToolsetRegistry::FToolsetRegistry> Registry;

		bool bCallBaseAddUserMessageToConversation = true;
		TArray<FAddMessageToConversationOptions> AddUserMessageToConversationCalls;

		bool bCallBaseProcessMessage = true;
		TArray<FMessage> ProcessMessageCalls;

		bool bCallBaseProcessToolCallContent = true;
		TArray<FToolCallContent> ProcessToolCallContentCalls;
		TQueue<TSharedPtr<TPromise<TValueOrError<void, FString>>>>
			ProcessToolCallContentFakeResults;
	};

	using FWebApplicationWithTracker = TWebApplicationWithTracker<FWebApplication>;
	using FSpyWebApplicationWithTracker = TWebApplicationWithTracker<FSpyWebApplication>;

	// Creates and initializes a spy web application with a toolset registry.
	struct FInitializedSpyWebApplication
	{
		TSharedPtr<UE::ToolsetRegistry::FToolsetRegistry> Registry;
		FSpyWebApplicationWithTracker Tracker;
		TSharedPtr<FSpyWebApplication> WebApplication;

		explicit FInitializedSpyWebApplication(
				FAutomationTestBase& TestBase, const FString& DevOptionsRawJson = FString())
			: Registry(MakeShared<UE::ToolsetRegistry::FToolsetRegistry>()),
			  Tracker(TestBase, DevOptionsRawJson)
		{
			Tracker.WebApplication->SetRegistry(Registry);
			WebApplication = Tracker.WebApplication;
		}

		bool EnsureInitialized()
		{
			return Tracker.EnsureInitialized();
		}
	};

	// RAII wrapper for transaction buffers in tests.
	struct FScopedTransactionBuffer
	{
		FString BufferName;
		TObjectPtr<UTransBuffer> Buffer;

		explicit FScopedTransactionBuffer(const FString& InBufferName)
			: BufferName(InBufferName),
			  Buffer(FTransactionBufferManager::GetOrCreateTransactionBuffer(
				InBufferName))
		{
		}

		~FScopedTransactionBuffer()
		{
			FTransactionBufferManager::DestroyTransactionBuffer(BufferName);
		}
	};

	// Verify that conversation ID options match the expected values.
	static bool TestConversationId(
		FAutomationTestBase& TestBase,
		const TOptional<FConversationId>& ConversationId,
		const FString& ExpectedId)
	{
		if (!TestBase.TestTrue(
			TEXT("ConversationId_IsSet"), ConversationId.IsSet()))
		{
			return false;
		}
		(void)TestBase.TestEqual(
			TEXT("ConversationId_Id"),
			ConversationId->Id, ExpectedId);
		(void)TestBase.TestEqual(
			TEXT("ConversationId_Type"),
			ConversationId->Type, TEXT("ConversationId"));
		return true;
	}

	// Create a test package with a modified object recorded in a transaction.
	static void RecordTestTransaction(const TCHAR* PackageName)
	{
		UPackage* TestPackage = CreatePackage(PackageName);
		UAIAssistantTestObject* TestObj = NewObject<UAIAssistantTestObject>(
			TestPackage, TEXT("TestObject"), RF_Transactional);
		GEditor->BeginTransaction(
			FText::FromString(TEXT("Test Transaction")));
		TestObj->Modify();
		TestObj->TestValue = 1;
		GEditor->EndTransaction();
	}

	BEGIN_DEFINE_SPEC(
		FAIAssistantWebApplicationTestCreateWebApiFactory,
		"AI.Assistant.WebApplication.CreateWebApiFactory",
		AIAssistantTest::Flags)
	END_DEFINE_SPEC(FAIAssistantWebApplicationTestCreateWebApiFactory)

	void FAIAssistantWebApplicationTestCreateWebApiFactory::Define()
	{
		It(TEXT("should create a WebApi"), [this]
			{
				FFakeWebJavaScriptExecutor JavaScriptExecutor;
				FFakeWebJavaScriptDelegateBinder JavaScriptDelegateBinder;
				FSimpleMulticastDelegate UnbindDelegate;
				auto Factory = FWebApplication::CreateWebApiFactory(
					JavaScriptExecutor, JavaScriptDelegateBinder, UnbindDelegate);
				if (!TestTrue(TEXT("FactoryIsSet"), Factory.IsSet())) return;

				auto WebApi = Factory();
				if (!TestTrue(TEXT("FactoryCreatedWebApi"), WebApi.IsValid())) return;

				// Ensure the web API is bound to the JavaScript environment.
				(void)TestEqual(
					TEXT("BoundApiToJavaScriptEnvironment"),
					JavaScriptDelegateBinder.BoundObjects.Num(), 1);
				// Make sure the web API is using the supplied executor.
				WebApi->UpdateGlobalLocale(TEXT("us-en"));
				(void)TestNotEqual(
					TEXT("ExecutedFunction"), JavaScriptExecutor.ExecutedJavaScriptText.Num(), 0);
			});

		It(TEXT("should create a unique WebApi"), [this]
			{
				FFakeWebJavaScriptExecutor JavaScriptExecutor;
				FFakeWebJavaScriptDelegateBinder JavaScriptDelegateBinder;
				FSimpleMulticastDelegate UnbindDelegate;
				auto Factory = FWebApplication::CreateWebApiFactory(
					JavaScriptExecutor, JavaScriptDelegateBinder, UnbindDelegate);
				if (!TestTrue(TEXT("FactoryIsSet"), Factory.IsSet())) return;

				auto WebApi = Factory();
				(void)TestTrue(TEXT("FactoryCreatedWebApi"), WebApi.IsValid());

				auto AnotherWebApi = Factory();
				(void)TestTrue(TEXT("FactoryCreatedNewWebApi"), AnotherWebApi.IsValid());
				(void)TestNotEqual(TEXT("FactoryCreatesNewWebApis"), WebApi, AnotherWebApi);
			});

		It(TEXT("should create a WebApi with callbacks that can be canceled"), [this]
			{
				FFakeWebJavaScriptExecutor JavaScriptExecutor;
				FFakeWebJavaScriptDelegateBinder JavaScriptDelegateBinder;
				FSimpleMulticastDelegate UnbindDelegate;
				auto Factory = FWebApplication::CreateWebApiFactory(
					JavaScriptExecutor, JavaScriptDelegateBinder, UnbindDelegate);
				if (!TestTrue(TEXT("FactoryIsSet"), Factory.IsSet())) return;

				auto WebApi = Factory();
				if (!TestTrue(TEXT("FactoryCreatedWebApi"), WebApi.IsValid())) return;

				auto Result = WebApi->CreateConversation();
				UnbindDelegate.Broadcast();

				auto NothingOrError = Result.Consume();
				if (!TestTrue(TEXT("HasError"), NothingOrError.HasError())) return;
				(void)TestEqual(
					TEXT("Canceled"), NothingOrError.GetError(),
					UAIAssistantWebJavaScriptResultDelegate::CanceledError);
			});
	}

	BEGIN_DEFINE_SPEC(
		FAIAssistantWebApplicationTestGetAgentEnvironment,
		"AI.Assistant.WebApplication.GetAgentEnvironment",
		AIAssistantTest::Flags)
	END_DEFINE_SPEC(FAIAssistantWebApplicationTestGetAgentEnvironment)

	void FAIAssistantWebApplicationTestGetAgentEnvironment::Define()
	{
		It(TEXT("should return UEFN environment name when bUseUefnMode is true"), [this]
			{
				FWebApplicationWithTracker WebApplicationWithTracker(*this);
				auto WebApplication = WebApplicationWithTracker.WebApplication;
				auto AgentEnvironment = WebApplication->GetAgentEnvironment(true);
				(void)TestEqual(
					TEXT("EnvironmentName"),
					AgentEnvironment->Descriptor.EnvironmentName, TEXT("UEFN"));
				(void)TestEqual(
					TEXT("EnvironmentVersion"),
					AgentEnvironment->Descriptor.EnvironmentVersion,
					FEngineVersion::Current().ToString());
			});

		It(TEXT("should return UE environment name when bUseUefnMode is false"), [this]
			{
				FWebApplicationWithTracker WebApplicationWithTracker(*this);
				auto WebApplication = WebApplicationWithTracker.WebApplication;
				auto AgentEnvironment = WebApplication->GetAgentEnvironment(false);
				(void)TestEqual(
					TEXT("EnvironmentName"),
					AgentEnvironment->Descriptor.EnvironmentName, TEXT("UE"));
				(void)TestEqual(
					TEXT("EnvironmentVersion"),
					AgentEnvironment->Descriptor.EnvironmentVersion,
					FEngineVersion::Current().ToString());
			});

		It(TEXT("should handle devOptions"), [this]
			{
				// Create web application with devOptions.
				FString DevOptionsRawJson =
					TEXT(R"json({"testOption":"testValue","anotherOption":42})json");
				FFakeWebApiTracker WebApiTracker;
				auto WebApplication = MakeShared<FWebApplication>(
					WebApiTracker.CreateFactory(),
					DevOptionsRawJson);

				// Verify the agent environment.
				auto AgentEnvironment = WebApplication->GetAgentEnvironment(false);
				(void)TestEqual(
					TEXT("EnvironmentName"),
					AgentEnvironment->Descriptor.EnvironmentName, TEXT("UE"));
				(void)TestEqual(
					TEXT("EnvironmentVersion"),
					AgentEnvironment->Descriptor.EnvironmentVersion,
					FEngineVersion::Current().ToString());
				(void)TestEqual(
					TEXT("DevOptions"),
					AgentEnvironment->Descriptor.DevOptionsRawJson,
					DevOptionsRawJson);
			});
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestCreateUserMessageNoHiddenContext,
		"AI.Assistant.WebApplication.CreateUserMessageNoHiddenContext",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestCreateUserMessageNoHiddenContext::RunTest(
		const FString& UnusedParameters)
	{
		const TCHAR* VisibleMessage = TEXT("hello");
		auto MessageOptions = CreateUserMessage(VisibleMessage, TEXT(""));
		(void)TestFalse(TEXT("ConversationId"), MessageOptions.ConversationId.IsSet());
		auto& Message = MessageOptions.Message;
		(void)TestEqual(TEXT("MessageRole"), Message.MessageRole, EMessageRole::User);
		if (!TestEqual(TEXT("MessageContentNum"), Message.MessageContent.Num(), 1)) return false;

		auto& MessageContent = Message.MessageContent[0];
		if (!(TestEqual(TEXT("ContentType"), MessageContent.ContentType,
				EMessageContentType::Text) &&
			TestTrue(TEXT("VisibleToUser"), MessageContent.bVisibleToUser)))
		{
			return false;
		}
		auto& TextMessageContent = MessageContent.Content.Get<FTextMessageContent>();
		(void)TestEqual(TEXT("VisibleText"), TextMessageContent.Text, VisibleMessage);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestCreateUserMessageHiddenContext,
		"AI.Assistant.WebApplication.CreateUserMessageHiddenContext",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestCreateUserMessageHiddenContext::RunTest(
		const FString& UnusedParameters)
	{
		const TCHAR* VisibleMessage = TEXT("hello");
		const TCHAR* HiddenContext = TEXT("hidden context");
		auto MessageOptions = CreateUserMessage(VisibleMessage, HiddenContext);
		(void)TestFalse(TEXT("ConversationId"), MessageOptions.ConversationId.IsSet());
		auto& Message = MessageOptions.Message;
		(void)TestEqual(TEXT("MessageRole"), Message.MessageRole, EMessageRole::User);
		if (!TestEqual(TEXT("MessageContentNum"), Message.MessageContent.Num(), 2)) return false;

		auto& VisibleMessageContent = Message.MessageContent[0];
		auto& HiddenMessageContent = Message.MessageContent[1];
		if (!(
			TestEqual(
				TEXT("VisibleContentType"), VisibleMessageContent.ContentType,
				EMessageContentType::Text) &&
			TestTrue(TEXT("VisibleContentVisibleToUser"), VisibleMessageContent.bVisibleToUser) &&
			TestEqual(
				TEXT("HiddenContentType"), HiddenMessageContent.ContentType,
				EMessageContentType::Text) &&
			TestFalse(TEXT("HiddenContentVisibleToUser"), HiddenMessageContent.bVisibleToUser)))
		{
			return false;
		}
		auto& VisibleTextMessageContent = VisibleMessageContent.Content.Get<FTextMessageContent>();
		(void)TestEqual(TEXT("VisibleText"), VisibleTextMessageContent.Text, VisibleMessage);
	
		auto& HiddenTextMessageContent = HiddenMessageContent.Content.Get<FTextMessageContent>();
		(void)TestEqual(TEXT("HiddenText"), HiddenTextMessageContent.Text, HiddenContext);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestConstruct,
		"AI.Assistant.WebApplication.Construct",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestConstruct::RunTest(const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		(void)TestEqual(
			TEXT("LoadState"),
			WebApplication->GetLoadState(),
			FWebApplication::ELoadState::NotLoaded);
		return TestEqual(
			TEXT("NoWebApis"),
			WebApplicationWithTracker.WebApiTracker.WebApis.Num(),
			0);
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestOnBeforeNavigation,
		"AI.Assistant.WebApplication.OnBeforeNavigation",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestOnBeforeNavigation::RunTest(const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		WebApplication->OnBeforeNavigation(
			WebApplicationWithTracker.Config.MainUrl, CreateWebNavigationRequestForLink());
		(void)TestEqual(
			TEXT("StillLoading"), WebApplication->GetLoadState(),
			FWebApplication::ELoadState::NotLoaded);
	
		WebApplication->OnBeforeNavigation(
			TEXT("https://not.assistant.url"), CreateWebNavigationRequestForLink());
		(void)TestEqual(
			TEXT("StillLoading"), WebApplication->GetLoadState(),
			FWebApplication::ELoadState::NotLoaded);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestAgentEnvironmentRestoredAfterReload,
		"AI.Assistant.WebApplication.AgentEnvironmentRestoredAfterReload",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestAgentEnvironmentRestoredAfterReload::RunTest(
		const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;

		// Initial load. Environment should be configured.
		if (!WebApplicationWithTracker.EnsureInitialized()) return false;

		// Simulate the navigation triggered by sign-in.
		WebApplication->OnBeforeNavigation(
			WebApplicationWithTracker.Config.MainUrl, CreateWebNavigationRequestForLink());

		// The page reloads after sign-in completes.
		if (!WebApplicationWithTracker.EnsureInitialized()) return false;

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestOnPageLoadError,
		"AI.Assistant.WebApplication.OnPageLoadError",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestOnPageLoadError::RunTest(const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		(void)WebApplication->OnBeforeNavigation(
			WebApplicationWithTracker.Config.MainUrl, CreateWebNavigationRequestForLink());
		WebApplication->OnPageLoadError();
		(void)TestEqual(
			TEXT("ErrorOccurred"), WebApplication->GetLoadState(),
			FWebApplication::ELoadState::Error);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestPageLoadNonMainPage,
		"AI.Assistant.WebApplication.PageLoadNonMainPage",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestPageLoadNonMainPage::RunTest(
		const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		const TCHAR* NonAssistantPage = TEXT("https://unrealengine.com");
		(void)WebApplication->OnBeforeNavigation(
			NonAssistantPage, CreateWebNavigationRequestForLink());
		WebApplication->OnPageLoadComplete();
		(void)TestEqual(
			TEXT("NotLoaded"), WebApplication->GetLoadState(),
			FWebApplication::ELoadState::NotLoaded);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestPageLoadMainPage,
		"AI.Assistant.WebApplication.PageLoadMainPage",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestPageLoadMainPage::RunTest(const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		return WebApplicationWithTracker.EnsureInitialized();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestPageLoadMainPageIsInitializedFails,
		"AI.Assistant.WebApplication.PageLoadMainPageIsInitializedFails",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestPageLoadMainPageIsInitializedFails::RunTest(
		const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		WebApplicationWithTracker.NavigateToMainUrl();

		auto FakeWebApi = WebApplicationWithTracker.GetWebApi();
		if (!FakeWebApi) return false;

		FWebApiBoolResult Result;
		(void)FakeWebApi->TestExpectAsyncFunctionCallAndComplete(
			*this, TEXT(""), *FFakeWebApi::GetWebApiAvailableFunction(),
			TEXT(""), *Result.ToJson(false), false);

		(void)TestEqual(
			TEXT("NotLoaded"), WebApplication->GetLoadState(),
			FWebApplication::ELoadState::NotLoaded);

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestPageLoadMainPageIsInitializedFailsWithError,
		"AI.Assistant.WebApplication.PageLoadMainPageIsInitializedFailsWithError",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestPageLoadMainPageIsInitializedFailsWithError::RunTest(
		const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		WebApplicationWithTracker.NavigateToMainUrl();

		const TCHAR* ErrorJson = FWebApplicationWithTracker::ExpectJavaScriptError(*this);
		auto FakeWebApi = WebApplicationWithTracker.GetWebApi();
		if (!FakeWebApi) return false;

		(void)FakeWebApi->TestExpectAsyncFunctionCallAndComplete(
			*this, TEXT(""), *FFakeWebApi::GetWebApiAvailableFunction(),
			TEXT(""), ErrorJson, true);

		(void)TestEqual(
			TEXT("FailedToLoad"), WebApplication->GetLoadState(),
			FWebApplication::ELoadState::Error);

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestPageLoadMainPageUpdateEnvironmentFails,
		"AI.Assistant.WebApplication.PageLoadMainPageUpdateEnvironmentFails",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestPageLoadMainPageUpdateEnvironmentFails::RunTest(
		const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		WebApplicationWithTracker.NavigateToMainUrl();

		(void)WebApplicationWithTracker.TestExpectIsInitialized();
		(void)WebApplicationWithTracker.TestExpectUpdateGlobalLocale();

		const TCHAR* ErrorJson = FWebApplicationWithTracker::ExpectJavaScriptError(*this);
		auto FakeWebApi = WebApplicationWithTracker.GetWebApi();
		if (!FakeWebApi) return false;
	
		(void)FakeWebApi->TestExpectAsyncFunctionCallAndComplete(
			*this, nullptr, TEXT("addAgentEnvironment"),
			*WebApplication->GetAgentEnvironment(IsUefnMode())->ToJson(false),
			ErrorJson, true);

		(void)TestEqual(
			TEXT("SetAgentEnvironmentNotCalled"),
			FakeWebApi->FindExecutedAsyncFunctions(nullptr, TEXT("setAgentEnvironment")).Num(), 0);

		(void)TestEqual(
			TEXT("FailedToLoad"), WebApplication->GetLoadState(),
			FWebApplication::ELoadState::Error);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestChangeUefnMode,
		"AI.Assistant.WebApplication.ChangeUefnMode",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestChangeUefnMode::RunTest(const FString& UnusedParameters)
	{
		auto UefnModeConsoleVariableRestorer = MakeShared<ScopedUefnModeConsoleVariableRestorer>();
		auto WebApplicationWithTracker = MakeShared<FWebApplicationWithTracker>(*this);
		if (!WebApplicationWithTracker->EnsureInitialized()) return false;

		// Clear executed functions.
		auto FakeWebApi = WebApplicationWithTracker->GetWebApi();
		if (!FakeWebApi) return false;
		FakeWebApi->ExecutedAsyncFunctions.Empty();

		// Change UEFN mode.
		bool bExpectedUefnMode = !IsUefnMode();
		auto* UefnModeVariable = FindUefnModeConsoleVariable();
		if (!TestTrue(TEXT("UEFNVar"), UefnModeVariable ? true : false)) return false;
		UefnModeVariable->Set(bExpectedUefnMode);

		// Wait for UEFN mode to change and make sure that it was updated.
		AddCommand(
			new FDelayedFunctionLatentCommand(
				[this, UefnModeConsoleVariableRestorer, WebApplicationWithTracker,
				bExpectedUefnMode]
				{
					auto AgentEnvironmentHandle =
						WebApplicationWithTracker->TestExpectAddAgentEnvironment(
							TOptional<bool>(bExpectedUefnMode));
					return AgentEnvironmentHandle.IsValid() &&
						WebApplicationWithTracker->TestExpectSetAgentEnvironment(
							AgentEnvironmentHandle->Id) &&
						TestEqual(
							TEXT("Loaded"),
							WebApplicationWithTracker->WebApplication->GetLoadState(),
							FWebApplication::ELoadState::Complete);
				},
				ConsoleVariableUpdateDelayInSeconds));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestCultureChanged,
		"AI.Assistant.WebApplication.CultureChanged",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestCultureChanged::RunTest(const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		if (!WebApplicationWithTracker.EnsureInitialized()) return false;

		auto WebApi = WebApplicationWithTracker.GetWebApi();
		if (!WebApi) return false;
		WebApi->ExecutedAsyncFunctions.Empty();

		FInternationalization::Get().OnCultureChanged().Broadcast();
		return WebApplicationWithTracker.TestExpectUpdateGlobalLocale();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestCreateConversationNotLoaded,
		"AI.Assistant.WebApplication.CreateConversationNotLoaded",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestCreateConversationNotLoaded::RunTest(
		const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		// If the application isn't loaded, creating a conversation should do nothing.
		WebApplication->CreateConversation();

		(void)TestEqual(
			TEXT("NoWebApis"), WebApplicationWithTracker.WebApiTracker.WebApis.Num(), 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestCreateConversationSuccessful,
		"AI.Assistant.WebApplication.CreateConversationSuccessful",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestCreateConversationSuccessful::RunTest(
		const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		if (!WebApplicationWithTracker.EnsureInitialized()) return false;

		WebApplication->CreateConversation();
		auto WebApi = WebApplicationWithTracker.GetWebApi();
		if (!WebApi) return false;

		(void)WebApplicationWithTracker.TestExpectCreateConversation();
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestCreateConversationFailure,
		"AI.Assistant.WebApplication.CreateConversationFailure",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestCreateConversationFailure::RunTest(
		const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		if (!WebApplicationWithTracker.EnsureInitialized()) return false;

		WebApplication->CreateConversation();
		auto WebApi = WebApplicationWithTracker.GetWebApi();
		if (!WebApi) return false;

		const TCHAR* ErrorJson = FWebApplicationWithTracker::ExpectJavaScriptError(*this);
		(void)WebApi->TestExpectAsyncFunctionCallAndComplete(
			*this, nullptr, TEXT("createConversation"), TEXT(""), ErrorJson, true);
		return true;
	}

	BEGIN_DEFINE_SPEC(
		FAIAssistantWebApplicationOnConversationUpdate,
		"AI.Assistant.WebApplication.OnConversationUpdate",
		AIAssistantTest::Flags)

		// Create a conversation for these tests.
		static FConversation CreateConversation(bool bAddToolResponse)
		{
			FConversation Conversation;
			Conversation.Descriptor.ConversationId = MakeConversationId(1);
			Conversation.Messages.Add(MakeAgentTextMessage(0, TEXT("How are you today?")));
			FToolCallContent ToolCallContent =
				MakeToolCallContent(
					FString(ToolsetName) + TEXT(".") + ToolName,
					TEXT("{}"), MakeTuple(1, 0));
			Conversation.Messages.Add(MakeAgentToolCallMessage(1, { ToolCallContent }));
			if (bAddToolResponse)
			{
				Conversation.Messages.Add(MakeUserToolResponseMessage(
					2,
					{ MakeToolResponseContent(
						ToolCallContent.ToolCallId.GetValue(), ToolCallContent.Name,
						TEXT(R"json({"value":"hey"})json"),
						TOptional(true), TOptional<FString>()) }));
			}
			Conversation.Messages.Add(MakeUserTextMessage(3, TEXT("I'm fine, and you?")));
			return Conversation;
		}

		// Update the conversation.
		bool UpdateConversation(const FConversation& Conversation)
		{
			auto& MaybeConversationId = Conversation.Descriptor.ConversationId;
			if (!TestTrue(TEXT("Has Conversation ID"), MaybeConversationId.IsSet())) return false;
			const FConversationId& Id = Conversation.Descriptor.ConversationId.GetValue();

			if (!(WebApplication && WebApi)) return false;
			auto Future = WebApplication->OnConversationUpdate(Id);
			(void)WebApi->TestExpectAsyncFunctionCallAndComplete(
				*this, nullptr, TEXT("getConversation"), *Id.ToJson(false),
				*Conversation.ToJson(false), false);
			return TestTrue(TEXT("OnConversationUpdate_Success"), Future.Get().HasValue());
		}

		void EnsureToolCalled()
		{
			if (!FakeToolset) return;
			const auto& Results = FakeToolset->ToolResultsByName.Find(ToolName);
			TestTrue(TEXT("Tool called"), Results && Results->Num() == 1);
		}

		void EnsureToolNotCalled()
		{
			if (!FakeToolset) return;
			const auto& Results = FakeToolset->ToolResultsByName.Find(ToolName);
			TestTrue(TEXT("Tool not called"), !Results || Results->Num() == 0);
		}

	private:
		TSharedPtr<FFakeToolset> FakeToolset;
		TSharedPtr<TPromise<TValueOrError<FString, FString>>> ToolResult;
		TSharedPtr<UE::ToolsetRegistry::FToolsetRegistry> Registry;
		TOptional<FSpyWebApplicationWithTracker> WebApplicationWithTracker;
		TSharedPtr<FSpyWebApplication> WebApplication;
		TSharedPtr<FFakeWebApi> WebApi;

	private:
		static constexpr TCHAR ToolsetName[] = TEXT("Greet");
		static constexpr TCHAR ToolName[] = TEXT("Yo");
	END_DEFINE_SPEC(FAIAssistantWebApplicationOnConversationUpdate)

	void FAIAssistantWebApplicationOnConversationUpdate::Define()
	{
		Describe(TEXT("ProcessMessages"), [this]() -> void
		{
			BeforeEach([this]() -> void
			{
				Registry = MakeShared<UE::ToolsetRegistry::FToolsetRegistry>();
				FakeToolset = MakeShared<FFakeToolset>(ToolsetName);
				Registry->RegisterToolset(FakeToolset);
				ToolResult = FakeToolset->AddFakeToolCall(ToolName);

				WebApplicationWithTracker.Emplace(*this);
				WebApplication = WebApplicationWithTracker->WebApplication;
				WebApplication->SetRegistry(Registry);
				if (!WebApplicationWithTracker->EnsureInitialized()) return;
				WebApi = WebApplicationWithTracker->GetWebApi();
				TestTrue(TEXT("Has Web API"), WebApi.IsValid());
			});

			AfterEach([this]() -> void
			{
				WebApi.Reset();
				WebApplication.Reset();
				WebApplicationWithTracker.Reset();
				Registry.Reset();
				// Complete the tool promise otherwise it will check on destruction.
				if (!FakeToolset->ToolResultsByName.Find(ToolName))
				{
					ToolResult->EmplaceValue(MakeError(TEXT("canceled")));
				}
				FakeToolset.Reset();
			});

			It(TEXT("Should process messages"), [this]() -> void
			{
				FConversation Conversation = CreateConversation(true);
				// Trigger OnConversationUpdate and ensure that the correct messages are sent down.
				if (!UpdateConversation(Conversation)) return;

				if (WebApplication &&
					TestEqual(
						TEXT("OnConversationUpdate_CallCount"),
						WebApplication->ProcessMessageCalls.Num(), 4))
				{
					for (int i = 0; i < Conversation.Messages.Num(); ++i)
					{
						(void)TestEqual(
							*FString::Printf(TEXT("OnConversationUpdate_Message%d"), i),
							WebApplication->ProcessMessageCalls[i].ToJson(true),
							Conversation.Messages[i].ToJson(true));
					}
				}
				EnsureToolNotCalled();
			});

			It(TEXT("Should not process messages with no new messages"), [this]() -> void
			{
				FConversation Conversation = CreateConversation(true);
				if (!UpdateConversation(Conversation)) return;
				if (!WebApplication) return;
				WebApplication->ProcessMessageCalls.Empty();

				if (!UpdateConversation(Conversation)) return;
				TestEqual(
					TEXT("ProcessMessageCalls"), WebApplication->ProcessMessageCalls.Num(), 0);
				EnsureToolNotCalled();
			});

			It(TEXT("Should call tools with no response"), [this]() -> void
			{
				FConversation Conversation = CreateConversation(false);
				if (!ToolResult) return;
				ToolResult->SetValue(MakeValue(TEXT("{}")));
				if (!UpdateConversation(Conversation)) return;
				EnsureToolCalled();
			});
		});
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationProcessMessageTextContentMessage,
		"AI.Assistant.WebApplication.ProcessMessage.TextContentMessage",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationProcessMessageTextContentMessage::RunTest(
		const FString& UnusedParameters)
	{
		auto Registry = MakeShared<UE::ToolsetRegistry::FToolsetRegistry>();
		FSpyWebApplicationWithTracker WebApplicationWithTracker(*this);
		WebApplicationWithTracker.WebApplication->SetRegistry(Registry);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		if (!WebApplicationWithTracker.EnsureInitialized()) return false;

		WebApplication->ProcessMessage(
			MakeAgentTextMessage(0, TEXT("How are you today?")),
			TEXT("test_conversation"));
		WebApplication->ProcessMessage(
			MakeUserTextMessage(1, TEXT("I'm fine, and you?")),
			TEXT("test_conversation"));

		// No tool calls should be made for text content messages.
		TestTrue(TEXT("ProcessMessageTextContentMessage"),
			WebApplication->ProcessToolCallContentCalls.IsEmpty());

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationProcessMessageToolCallMessage,
		"AI.Assistant.WebApplication.ProcessMessage.ToolCallMessage",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationProcessMessageToolCallMessage::RunTest(
		const FString& UnusedParameters)
	{
		auto Registry = MakeShared<UE::ToolsetRegistry::FToolsetRegistry>();
		FSpyWebApplicationWithTracker WebApplicationWithTracker(*this);
		WebApplicationWithTracker.WebApplication->SetRegistry(Registry);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		if (!WebApplicationWithTracker.EnsureInitialized()) return false;

		FToolCallContent PeanutButter = MakeToolCallContent(
			TEXT("Sandwich.SpreadPeanutButter"),
			TEXT(R"json({"type": "crunchy"})json"),
			MakeTuple(1, 1));
		FToolCallContent Jelly = MakeToolCallContent(
			TEXT("Sandwich.SpreadJelly"),
			TEXT(R"json({"flavor": "strawberry"})json"),
			MakeTuple(1, 2));

		FMessage AgentToolCallMessage = MakeAgentToolCallMessage(1, { PeanutButter, Jelly });

		WebApplication->bCallBaseProcessToolCallContent = false;
		WebApplication->ProcessToolCallContentFakeResults.Enqueue(
			MakeShared<TPromise<TValueOrError<void, FString>>>(
				MakeFulfilledPromise<TValueOrError<void, FString>>(MakeValue())));
		WebApplication->ProcessToolCallContentFakeResults.Enqueue(
			MakeShared<TPromise<TValueOrError<void, FString>>>(
				MakeFulfilledPromise<TValueOrError<void, FString>>(MakeValue())));
		WebApplication->ProcessMessage(AgentToolCallMessage, TEXT("test_conversation"));

		// Check that each tool call was processed.
		if (TestEqual(
			TEXT("ProcessMessageWithToolCalls_ProcessToolCallContentCount"),
			WebApplication->ProcessToolCallContentCalls.Num(), 2))
		{
			(void)TestEqual(
				TEXT("ProcessMessageWithToolCalls_ProcessToolCallContentCount"),
				WebApplication->ProcessToolCallContentCalls[0].ToJson(true),
				PeanutButter.ToJson(true));
			(void)TestEqual(
				TEXT("ProcessMessageWithToolCalls_ProcessToolCallContentCount"),
				WebApplication->ProcessToolCallContentCalls[1].ToJson(true),
				Jelly.ToJson(true));
		}

		// Running the same tools again should do nothing.
		WebApplication->ProcessToolCallContentCalls.Empty();
		WebApplication->ProcessMessage(AgentToolCallMessage, TEXT("test_conversation"));
		TestEqual(
			TEXT("ProcessMessageWithToolCalls_ProcessToolCallContentCount"),
			WebApplication->ProcessToolCallContentCalls.Num(), 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationProcessMessageOnlyProcessRequiredToolCalls,
		"AI.Assistant.WebApplication.ProcessMessage.OnlyProcessRequiredToolCalls",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationProcessMessageOnlyProcessRequiredToolCalls::RunTest(
		const FString& UnusedParameters)
	{
		auto Registry = MakeShared<UE::ToolsetRegistry::FToolsetRegistry>();
		FSpyWebApplicationWithTracker WebApplicationWithTracker(*this);
		WebApplicationWithTracker.WebApplication->SetRegistry(Registry);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		if (!WebApplicationWithTracker.EnsureInitialized()) return false;

		FToolCallContent PeanutButter = MakeToolCallContent(
			TEXT("Sandwich.SpreadPeanutButter"),
			TEXT(R"json({"type": "crunchy"})json"),
			MakeTuple(1, 1));
		FToolCallContent Jelly = MakeToolCallContent(
			TEXT("Sandwich.SpreadJelly"),
			TEXT(R"json({"flavor": "strawberry"})json"),
			MakeTuple(1, 2));
		Jelly.ResponseRequired = false;

		FMessage AgentToolCallMessage = MakeAgentToolCallMessage(1, { PeanutButter, Jelly });

		WebApplication->bCallBaseProcessToolCallContent = false;
		WebApplication->ProcessToolCallContentFakeResults.Enqueue(
			MakeShared<TPromise<TValueOrError<void, FString>>>(
				MakeFulfilledPromise<TValueOrError<void, FString>>(MakeValue())));
		WebApplication->ProcessToolCallContentFakeResults.Enqueue(
			MakeShared<TPromise<TValueOrError<void, FString>>>(
				MakeFulfilledPromise<TValueOrError<void, FString>>(MakeValue())));
		WebApplication->ProcessMessage(AgentToolCallMessage, TEXT("test_conversation"));

		// Check that the tool that did not require a response was not processed.
		if (TestEqual(
			TEXT("ProcessMessageWithToolCalls_ProcessToolCallContentCount"),
			WebApplication->ProcessToolCallContentCalls.Num(), 1))
		{
			(void)TestEqual(
				TEXT("ProcessMessageWithToolCalls_ProcessToolCallContentCount"),
				WebApplication->ProcessToolCallContentCalls[0].ToJson(true),
				PeanutButter.ToJson(true));
		}

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationProcessMessageProcessOnlyOnce,
		"AI.Assistant.WebApplication.ProcessMessage.ProcessOnlyOnce",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationProcessMessageProcessOnlyOnce::RunTest(
		const FString& UnusedParameters)
	{
		auto Registry = MakeShared<UE::ToolsetRegistry::FToolsetRegistry>();
		FSpyWebApplicationWithTracker WebApplicationWithTracker(*this);
		WebApplicationWithTracker.WebApplication->SetRegistry(Registry);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		if (!WebApplicationWithTracker.EnsureInitialized()) return false;

		// Message with tool call and valid ID.
		FToolCallContent PeanutButter = MakeToolCallContent(
			TEXT("Sandwich.SpreadPeanutButter"),
			TEXT(R"json({"type": "crunchy"})json"),
			MakeTuple(1, 1));
		FMessage AgentToolCallMessage = MakeAgentToolCallMessage(1, { PeanutButter });

		// Set up fake ProcessToolCallContent result.
		WebApplication->bCallBaseProcessToolCallContent = false;
		WebApplication->ProcessToolCallContentFakeResults.Enqueue(
			MakeShared<TPromise<TValueOrError<void, FString>>>(
				MakeFulfilledPromise<TValueOrError<void, FString>>(MakeValue())));

		// First call should process.
		WebApplication->ProcessMessage(AgentToolCallMessage, TEXT("test_conversation"));
		(void)TestEqual(
			TEXT("First ProcessMessage should call ProcessToolCallContent once"),
			WebApplication->ProcessToolCallContentCalls.Num(), 1);

		// Second call with same ID should be ignored.
		WebApplication->ProcessMessage(AgentToolCallMessage, TEXT("test_conversation"));
		if (TestEqual(
			TEXT("Second ProcessMessage with duplicate MessageId should be ignored"),
			WebApplication->ProcessToolCallContentCalls.Num(), 1))
		{
			(void)TestEqual(
				TEXT("ProcessToolCallContent should be passed the SpreadPeanutButter tool"),
				WebApplication->ProcessToolCallContentCalls[0].ToJson(true),
				PeanutButter.ToJson(true));
		}

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationProcessMessageExitGracefullyOnError,
		"AI.Assistant.WebApplication.ProcessMessage.ExitGracefullyOnError",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationProcessMessageExitGracefullyOnError::RunTest(
		const FString& UnusedParameters)
	{
		auto Registry = MakeShared<UE::ToolsetRegistry::FToolsetRegistry>();
		FSpyWebApplicationWithTracker WebApplicationWithTracker(*this);
		WebApplicationWithTracker.WebApplication->SetRegistry(Registry);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		if (!WebApplicationWithTracker.EnsureInitialized()) return false;

		// Messages with tool calls and valid IDs.
		FToolCallContent PeanutButter = MakeToolCallContent(
			TEXT("Sandwich.SpreadPeanutButter"),
			TEXT(R"json({"type": "crunchy"})json"),
			MakeTuple(1, 1));
		FToolCallContent Jelly = MakeToolCallContent(
			TEXT("Sandwich.SpreadJelly"),
			TEXT(R"json({"flavor": "strawberry"})json"),
			MakeTuple(1, 2));
		FMessage AgentToolCallMessage = MakeAgentToolCallMessage(1, { PeanutButter, Jelly });

		// Set up fake ProcessToolCallContent result, but leave the promise unfulfilled.
		auto PeanutButterPromise = MakeShared<TPromise<TValueOrError<void, FString>>>();
		WebApplication->bCallBaseProcessToolCallContent = false;
		WebApplication->bCallBaseAddUserMessageToConversation = false;
		WebApplication->ProcessToolCallContentFakeResults.Enqueue(PeanutButterPromise);

		// Process tool call and wait for it to complete.
		WebApplication->ProcessMessage(AgentToolCallMessage, TEXT("test_conversation"));

		// Fulfill the promise after the WebApplication has been shut down.
		PeanutButterPromise->SetValue(MakeError(TEXT("Peanut butter exploded")));

		if (TestEqual(
			TEXT("ProcessToolCallContent should only be called for the first message"),
			WebApplication->ProcessToolCallContentCalls.Num(), 1))
		{
			(void)TestEqual(
				TEXT("ProcessMessageWithToolCalls_ProcessToolCallContentCount"),
				WebApplication->ProcessToolCallContentCalls[0].ToJson(true),
				PeanutButter.ToJson(true));
		}

		// Jelly should receive an abort tool response.
		FString JellyToolCallId = MakeToolCallId(1, 2);
		TValueOrError<FString, FString> AbortResult =
			MakeError(FString(TEXT("Tool call aborted due to previous failure")));
		auto ExpectedAbortResponse = CreateToolResponseMessage(
			Jelly.Name, JellyToolCallId, AbortResult);
		if (TestEqual(
			TEXT("AddUserMessageToConversation should be called for Jelly's abort response"),
			WebApplication->AddUserMessageToConversationCalls.Num(), 1))
		{
			(void)TestEqual(
				TEXT("Abort response should match expected format"),
				WebApplication->AddUserMessageToConversationCalls[0].ToJson(true),
				ExpectedAbortResponse.ToJson(true));
		}

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationProcessMessageExitGracefullyOnShutdown,
		"AI.Assistant.WebApplication.ProcessMessage.ExitGracefullyOnShutdown",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationProcessMessageExitGracefullyOnShutdown::RunTest(
		const FString& UnusedParameters)
	{
		auto Registry = MakeShared<UE::ToolsetRegistry::FToolsetRegistry>();
		FSpyWebApplicationWithTracker WebApplicationWithTracker(*this);
		WebApplicationWithTracker.WebApplication->SetRegistry(Registry);
		if (!WebApplicationWithTracker.EnsureInitialized()) return false;

		// Messages with tool calls and valid IDs.
		FToolCallContent PeanutButter = MakeToolCallContent(
			TEXT("Sandwich.SpreadPeanutButter"),
			TEXT(R"json({"type": "crunchy"})json"),
			MakeTuple(1, 1));
		FMessage AgentToolCallMessage = MakeAgentToolCallMessage(1, { PeanutButter });

		// Set up fake ProcessToolCallContent result.
		auto PeanutButterPromise = MakeShared<TPromise<TValueOrError<void, FString>>>();
		WebApplicationWithTracker.WebApplication->bCallBaseProcessToolCallContent = false;
		WebApplicationWithTracker.WebApplication->ProcessToolCallContentFakeResults.Enqueue(
			PeanutButterPromise);

		// Process tool call and wait for it to complete.
		WebApplicationWithTracker.WebApplication->ProcessMessage(
			AgentToolCallMessage, TEXT("test_conversation"));

		// Fulfill the promise after the WebApplication has been shut down.
		WebApplicationWithTracker.WebApplication.Reset();
		PeanutButterPromise->SetValue(MakeValue());

		// Since the object has already been torn down at this point, the best we can do is verify
		// that the engine does not crash.

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationProcessToolCallContentSuccessfulCall,
		"AI.Assistant.WebApplication.ProcessToolCallContent.SuccessfulCall",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationProcessToolCallContentSuccessfulCall::RunTest(
		const FString& UnusedParameters)
	{
		auto Registry = MakeShared<UE::ToolsetRegistry::FToolsetRegistry>();
		FSpyWebApplicationWithTracker WebApplicationWithTracker(*this);
		WebApplicationWithTracker.WebApplication->SetRegistry(Registry);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		if (!WebApplicationWithTracker.EnsureInitialized()) return false;

		// Messages with tool calls and valid IDs.
		FToolCallContent PeanutButter = MakeToolCallContent(
			TEXT("Sandwich.SpreadPeanutButter"),
			TEXT(R"json({"type": "crunchy"})json"),
			MakeTuple(1, 1));

		// Set up dummy toolset.
		auto SandwichToolset = MakeShared<FFakeToolset>("Sandwich");
		auto PeanutButterToolCall = SandwichToolset->AddFakeToolCall(TEXT("SpreadPeanutButter"));
		Registry->RegisterToolset(SandwichToolset);
		WebApplication->bCallBaseAddUserMessageToConversation = false;

		// Process tool call and fulfill the promise successfully.
		WebApplication->ProcessToolCallContent(PeanutButter, TEXT("test_conversation"));
		PeanutButterToolCall->SetValue(MakeValue(FFakeToolset::SUCCESSFUL_RESULT));

		// Expected response.
		FString ToolCallId = MakeToolCallId(1, 1);
		TValueOrError<FString, FString> ToolResult = MakeValue(FFakeToolset::SUCCESSFUL_RESULT);
		auto ExpectedResponse = CreateToolResponseMessage(
			PeanutButter.Name, ToolCallId, ToolResult);

		if (TestEqual(
			TEXT("ProcessToolCallContent should be called once"),
			WebApplication->AddUserMessageToConversationCalls.Num(), 1))
		{
			TestEqual(
				TEXT("ProcessToolCallContent should add the expected message to the conversation"),
				WebApplication->AddUserMessageToConversationCalls[0].ToJson(true),
				ExpectedResponse.ToJson(true));
		}

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationProcessToolCallContentErroneousCall,
		"AI.Assistant.WebApplication.ProcessToolCallContent.ErroneousCall",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationProcessToolCallContentErroneousCall::RunTest(
		const FString& UnusedParameters)
	{
		auto Registry = MakeShared<UE::ToolsetRegistry::FToolsetRegistry>();
		FSpyWebApplicationWithTracker WebApplicationWithTracker(*this);
		WebApplicationWithTracker.WebApplication->SetRegistry(Registry);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		if (!WebApplicationWithTracker.EnsureInitialized()) return false;

		// Messages with tool calls and valid IDs.
		FToolCallContent PeanutButter = MakeToolCallContent(
			TEXT("Sandwich.SpreadPeanutButter"),
			TEXT(R"json({"type": "crunchy"})json"),
			MakeTuple(1, 1));

		// Set up dummy toolset.
		auto SandwichToolset = MakeShared<FFakeToolset>("Sandwich");
		auto PeanutButterToolCallPromise =
			SandwichToolset->AddFakeToolCall(TEXT("SpreadPeanutButter"));
		Registry->RegisterToolset(SandwichToolset);

		// Process tool call and wait for it to complete.
		WebApplication->bCallBaseAddUserMessageToConversation = false;
		WebApplication->ProcessToolCallContent(PeanutButter, TEXT("test_conversation"));

		// Fulfill the promise with error value.
		FString ToolErrorJsonString = R"json({ "error": "Sandwich caught fire, somehow" })json";
		PeanutButterToolCallPromise->SetValue(MakeError(ToolErrorJsonString));

		// Verify that an error response was produced.
		FString ToolCallId = MakeToolCallId(1, 1);
		TValueOrError<FString, FString> ToolResult = MakeError(ToolErrorJsonString);
		auto ExpectedResponse = CreateToolResponseMessage(PeanutButter.Name, ToolCallId, ToolResult);
		if (TestEqual(
			TEXT("ProcessToolCallContent should not be called after shutdown"),
			WebApplication->AddUserMessageToConversationCalls.Num(), 1))
		{
			(void)TestEqual(
				TEXT("ProcessToolCallContent should not be called after shutdown"),
				WebApplication->AddUserMessageToConversationCalls[0].ToJson(true),
				ExpectedResponse.ToJson(true));
		}

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestAddUserMessageToConversationNotLoaded,
		"AI.Assistant.WebApplication.AddUserMessageToConversationNotLoaded",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestAddUserMessageToConversationNotLoaded::RunTest(
		const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;

		WebApplication->AddUserMessageToConversation(CreateUserMessage());
		(void)TestEqual(
			TEXT("NoWebApis"), WebApplicationWithTracker.WebApiTracker.WebApis.Num(), 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestAddUserMessageToConversation,
		"AI.Assistant.WebApplication.AddUserMessageToConversation",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestAddUserMessageToConversation::RunTest(
		const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		if (!WebApplicationWithTracker.EnsureInitialized()) return false;

		WebApplication->AddUserMessageToConversation(CreateUserMessage());
		auto WebApi = WebApplicationWithTracker.GetWebApi();
		if (!WebApi) return false;

		(void)WebApi->TestExpectAsyncFunctionCallAndComplete(
			*this, nullptr, TEXT("addMessageToConversation"), *CreateUserMessage().ToJson(false),
			TEXT("{}"), false);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestAddUserMessageToConversationBeforeLoad,
		"AI.Assistant.WebApplication.AddUserMessageToConversationBeforeLoad",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestAddUserMessageToConversationBeforeLoad::RunTest(
		const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;

		WebApplication->AddUserMessageToConversation(CreateUserMessage());

		if (!WebApplicationWithTracker.EnsureInitialized()) return false;

		auto WebApi = WebApplicationWithTracker.GetWebApi();
		if (!WebApi) return false;

		(void)WebApi->TestExpectAsyncFunctionCallAndComplete(
			*this, nullptr, TEXT("addMessageToConversation"), *CreateUserMessage().ToJson(false),
			TEXT("{}"), false);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestAddUserMessageToConversationAfterNewConversation,
		"AI.Assistant.WebApplication.AddUserMessageToConversationAfterNewConversation",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestAddUserMessageToConversationAfterNewConversation::RunTest(
		const FString& UnusedParameters)
	{
		FWebApplicationWithTracker WebApplicationWithTracker(*this);
		auto WebApplication = WebApplicationWithTracker.WebApplication;
		if (!WebApplicationWithTracker.EnsureInitialized()) return false;

		WebApplication->CreateConversation();
		WebApplication->AddUserMessageToConversation(CreateUserMessage());

		auto WebApi = WebApplicationWithTracker.GetWebApi();
		if (!WebApi) return false;

		(void)TestEqual(
			TEXT("NoMessageAddedToConversation"),
			WebApi->FindExecutedAsyncFunctions(
				nullptr, TEXT("addMessageToConversation")).Num(), 0);

		(void)WebApplicationWithTracker.TestExpectCreateConversation();

		(void)WebApi->TestExpectAsyncFunctionCallAndComplete(
			*this, nullptr, TEXT("addMessageToConversation"), *CreateUserMessage().ToJson(false),
			TEXT("{}"), false);
		return true;
	}
	
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FAIAssistantWebApplicationTestStaleEnvironmentUpdateIgnored,
		"AI.Assistant.WebApplication.StaleEnvironmentUpdateIgnored",
		AIAssistantTest::Flags);

	bool FAIAssistantWebApplicationTestStaleEnvironmentUpdateIgnored::RunTest(
		const FString& UnusedParameters)
	{
		FInitializedSpyWebApplication Setup(*this);
		if (!Setup.EnsureInitialized()) return false;

		const TSharedPtr<FFakeWebApi> FakeWebApi = Setup.Tracker.GetWebApi();
		if (!FakeWebApi) return false;
		FakeWebApi->ExecutedAsyncFunctions.Empty();

		// register toolsets to trigger overlapping environment updates
		Setup.Registry->RegisterToolset(MakeShared<FFakeToolset>(TEXT("First")));
		Setup.Registry->RegisterToolset(MakeShared<FFakeToolset>(TEXT("Second")));

		auto AddCalls = FakeWebApi->FindExecutedAsyncFunctions(
			nullptr, TEXT("addAgentEnvironment"));
		if (!TestEqual(TEXT("TwoAddCalls"), AddCalls.Num(), 2)) return false;

		// message should be blocked while reconfiguring environment
		Setup.WebApplication->AddUserMessageToConversation(CreateUserMessage());

		// block on stale environment update
		FakeWebApi->CompleteAsyncFunction(AddCalls[0], *FAgentEnvironmentHandle().ToJson(false), false);
		(void)TestEqual(TEXT("MessageStillBlocked"),
			FakeWebApi->FindExecutedAsyncFunctions(nullptr, TEXT("addMessageToConversation")).Num(), 0);

		// unblock on last environment update
		FakeWebApi->CompleteAsyncFunction(AddCalls[1], *FAgentEnvironmentHandle().ToJson(false), false);
		(void)TestEqual(TEXT("MessageSentAfterLatest"),
			FakeWebApi->FindExecutedAsyncFunctions(nullptr, TEXT("addMessageToConversation")).Num(), 1);

		return true;
	}

	BEGIN_DEFINE_SPEC(
		FAIAssistantWebApplicationTestBuildPendingFileListOptions,
		"AI.Assistant.WebApplication.BuildPendingFileListOptions",
		AIAssistantTest::Flags)
	END_DEFINE_SPEC(FAIAssistantWebApplicationTestBuildPendingFileListOptions)

	void FAIAssistantWebApplicationTestBuildPendingFileListOptions::Define()
	{
		It(TEXT("should set conversation ID and return empty files with null buffer"), [this]
			{
				FInitializedSpyWebApplication Setup(*this);
				if (!Setup.EnsureInitialized()) return;

				const FString ConversationId = TEXT("test_conversation_123");
				FUpdatePendingFileListOptions Options =
					Setup.WebApplication->BuildPendingFileListOptions(
						ConversationId, nullptr);

				if (!TestConversationId(*this, Options.ConversationId, ConversationId))
				{
					return;
				}
				(void)TestEqual(TEXT("Files_Empty"), Options.Files.Num(), 0);
			});

		It(TEXT("should set conversation ID and return empty files with empty transaction buffer"), [this]
			{
				FInitializedSpyWebApplication Setup(*this);
				if (!Setup.EnsureInitialized()) return;

				const FString BufferName = TEXT("test_buffer_for_pending_files");
				FScopedTransactionBuffer ScopedBuffer(BufferName);

				const FString ConversationId = TEXT("test_conversation_456");
				FUpdatePendingFileListOptions Options =
					Setup.WebApplication->BuildPendingFileListOptions(
						ConversationId, ScopedBuffer.Buffer);

				if (!TestConversationId(*this, Options.ConversationId, ConversationId))
				{
					return;
				}
				(void)TestEqual(TEXT("Files_Empty"), Options.Files.Num(), 0);
			});

		It(TEXT("should return file metadata for modified objects in the transaction buffer"), [this]
			{
				FInitializedSpyWebApplication Setup(*this);
				if (!Setup.EnsureInitialized()) return;

				const FString BufferName = TEXT("test_buffer_with_transactions");
				FScopedTransactionBuffer ScopedBuffer(BufferName);

				// Set as override so transactions are recorded in our buffer.
				FTransactionBufferManager::SetOverrideBuffer(ScopedBuffer.Buffer);
				RecordTestTransaction(TEXT("/Temp/TestBuildPendingFileList"));
				FTransactionBufferManager::RestoreGlobalBuffer();

				// Build pending file list options.
				const FString ConversationId = TEXT("test_conversation_populated");
				FUpdatePendingFileListOptions Options =
					Setup.WebApplication->BuildPendingFileListOptions(
						ConversationId, ScopedBuffer.Buffer);

				if (!TestConversationId(*this, Options.ConversationId, ConversationId))
				{
					return;
				}

				// Verify at least one file is in the list.
				if (!TestTrue(TEXT("HasFiles"), Options.Files.Num() > 0))
				{
					return;
				}

				// Verify file metadata is correctly populated.
				bool bFoundTestPackage = false;
				for (const FPendingFileMetadata& FileMetadata : Options.Files)
				{
					if (FileMetadata.FullPath.Contains(
						TEXT("TestBuildPendingFileList")))
					{
						bFoundTestPackage = true;
						(void)TestEqual(
							TEXT("DisplayName"),
							FileMetadata.DisplayName,
							FPaths::GetCleanFilename(FileMetadata.FullPath));
						(void)TestEqual(
							TEXT("Status"),
							FileMetadata.Status,
							EPendingFileStatus::Modified);
						break;
					}
				}
				(void)TestTrue(TEXT("FoundTestPackage"), bFoundTestPackage);
			});
	}

	BEGIN_DEFINE_SPEC(
		FAIAssistantWebApplicationTestOnPendingFileDecision,
		"AI.Assistant.WebApplication.OnPendingFileDecision",
		AIAssistantTest::Flags)
	END_DEFINE_SPEC(FAIAssistantWebApplicationTestOnPendingFileDecision)

	void FAIAssistantWebApplicationTestOnPendingFileDecision::Define()
	{
		Describe(TEXT("when accepting changes"), [this]
		{
			It(TEXT("should destroy the transaction buffer for the conversation"), [this]
			{
				FInitializedSpyWebApplication Setup(*this);
				if (!Setup.EnsureInitialized()) return;

				const FString ConversationId = TEXT("accept_test_conversation");

				// Create a transaction buffer for this conversation.
				TObjectPtr<UTransBuffer> TransactionBuffer =
					FTransactionBufferManager::GetOrCreateTransactionBuffer(ConversationId);
				(void)TestTrue(TEXT("BufferCreated"),
					TransactionBuffer != nullptr);

				// Build the decision options.
				FOnPendingFileDecisionOptions Options;
				Options.ConversationId.Id = ConversationId;
				Options.ConversationId.Type = TEXT("ConversationId");
				Options.bAccepted = true;

				// Accept the changes.
				Setup.WebApplication->OnPendingFileDecision(Options);

				// Verify the buffer was destroyed.
				(void)TestTrue(TEXT("BufferDestroyed"),
					FTransactionBufferManager::GetTransactionBuffer(
						ConversationId) == nullptr);
			});
		});

		Describe(TEXT("when rejecting changes"), [this]
		{
			It(TEXT("should undo all transactions and destroy the buffer"), [this]
			{
				FInitializedSpyWebApplication Setup(*this);
				if (!Setup.EnsureInitialized()) return;

				const FString ConversationId = TEXT("reject_test_conversation");

				// Create a transaction buffer with a transaction to undo.
				TObjectPtr<UTransBuffer> TransactionBuffer =
					FTransactionBufferManager::GetOrCreateTransactionBuffer(ConversationId);
				(void)TestTrue(TEXT("BufferCreated"),
					TransactionBuffer != nullptr);

				// Record a transaction so CanUndo() returns true.
				FTransactionBufferManager::SetOverrideBuffer(
					TransactionBuffer);
				RecordTestTransaction(
					TEXT("/Temp/TestRejectPendingFile"));
				FTransactionBufferManager::RestoreGlobalBuffer();

				(void)TestTrue(TEXT("CanUndoBeforeReject"),
					TransactionBuffer->CanUndo());

				// Build the decision options.
				FOnPendingFileDecisionOptions Options;
				Options.ConversationId.Id = ConversationId;
				Options.ConversationId.Type = TEXT("ConversationId");
				Options.bAccepted = false;

				// Reject the changes.
				Setup.WebApplication->OnPendingFileDecision(Options);

				// Verify the buffer was destroyed.
				(void)TestTrue(TEXT("BufferDestroyed"),
					FTransactionBufferManager::GetTransactionBuffer(
						ConversationId) == nullptr);
			});
		});

		Describe(TEXT("when no transaction buffer exists"), [this]
		{
			It(TEXT("should not crash when no buffer exists for the conversation"), [this]
			{
				FInitializedSpyWebApplication Setup(*this);
				if (!Setup.EnsureInitialized()) return;

				const FString ConversationId = TEXT("no_buffer_conversation");

				// Verify no buffer exists.
				(void)TestTrue(TEXT("NoBufferExists"),
					FTransactionBufferManager::GetTransactionBuffer(ConversationId) == nullptr);

				// Build the decision options.
				FOnPendingFileDecisionOptions Options;
				Options.ConversationId.Id = ConversationId;
				Options.ConversationId.Type = TEXT("ConversationId");
				Options.bAccepted = false;

				// Should not crash.
				Setup.WebApplication->OnPendingFileDecision(Options);
			});
		});
	}

	BEGIN_DEFINE_SPEC(
		FAIAssistantWebApplicationTestProcessToolCallContentTransactionBuffer,
		"AI.Assistant.WebApplication.ProcessToolCallContent.TransactionBuffer",
		AIAssistantTest::Flags)
	END_DEFINE_SPEC(FAIAssistantWebApplicationTestProcessToolCallContentTransactionBuffer)

	void FAIAssistantWebApplicationTestProcessToolCallContentTransactionBuffer::Define()
	{
		It(TEXT("should set override before tool execution and restore after"), [this]
		{
			FInitializedSpyWebApplication Setup(
				*this, TEXT(R"json({"EnableUndoBuffer":true})json"));
			if (!Setup.EnsureInitialized()) return;

			const FString ConversationId = TEXT("txn_buffer_test_conversation");

			// Set up tool call.
			FToolCallContent PeanutButter = MakeToolCallContent(
				TEXT("Sandwich.SpreadPeanutButter"),
				TEXT(R"json({"type": "crunchy"})json"),
				MakeTuple(1, 1));

			// Set up dummy toolset with an unfulfilled promise.
			auto SandwichToolset =
				MakeShared<FFakeToolset>("Sandwich");
			auto PeanutButterToolCall =
				SandwichToolset->AddFakeToolCall(TEXT("SpreadPeanutButter"));
			Setup.Registry->RegisterToolset(SandwichToolset);
			Setup.WebApplication->bCallBaseAddUserMessageToConversation = false;

			// Verify no override is active before the call.
			(void)TestFalse(TEXT("NoOverrideBeforeCall"),
				FTransactionBufferManager::IsOverrideActive());

			// Process tool call (promise not yet fulfilled).
			Setup.WebApplication->ProcessToolCallContent(PeanutButter, ConversationId);

			// Verify override is active during tool execution.
			(void)TestTrue(TEXT("OverrideActiveDuringToolExecution"),
				FTransactionBufferManager::IsOverrideActive());

			// Verify a transaction buffer was created for this conversation.
			TObjectPtr<UTransBuffer> TransactionBuffer =
				FTransactionBufferManager::GetTransactionBuffer(ConversationId);
			(void)TestTrue(TEXT("TransactionBufferCreated"),
				TransactionBuffer != nullptr);

			// Fulfill the tool call.
			PeanutButterToolCall->SetValue(MakeValue(FFakeToolset::SUCCESSFUL_RESULT));

			// Verify override has been restored after tool completion.
			(void)TestFalse(TEXT("OverrideRestoredAfterToolComplete"),
				FTransactionBufferManager::IsOverrideActive());

			// Clean up.
			FTransactionBufferManager::DestroyTransactionBuffer(ConversationId);
		});
	}

	BEGIN_DEFINE_SPEC(
		FAIAssistantWebApplicationConversationUpdateRouting,
		"AI.Assistant.WebApplication.ConversationUpdateRouting",
		AIAssistantTest::Flags)

		// Invoke the registered conversation-update callback with a JSON event payload.
		// Must be called after EnsureInitialized() so that the handler is registered.
		void FireConversationUpdateEvent(
			const FString& ConversationId,
			EConversationUpdateType UpdateType)
		{
			FireConversationUpdateEventRaw(ConversationId, *LexToString(UpdateType));
		}

		// Overload for tests that deliberately send an unrecognized type string.
		void FireConversationUpdateEvent(
			const FString& ConversationId,
			const TCHAR* UpdateTypeString)
		{
			FireConversationUpdateEventRaw(ConversationId, UpdateTypeString);
		}

	private:
		void FireConversationUpdateEventRaw(
			const FString& ConversationId,
			const TCHAR* UpdateTypeString)
		{
			TSharedPtr<FFakeWebApi> FakeWebApi = WebApplicationWithTracker->GetWebApi();
			if (!FakeWebApi) return;
			FString EventJson = MakeConversationUpdateEventJson(*ConversationId, UpdateTypeString);
			// RegisteredCallbackHandlerIds[0] is the conversation-update handler;
			// it is registered synchronously during RegisterForConversationUpdates().
			if (!TestTrue(TEXT("HasConversationUpdateHandler"),
				FakeWebApi->RegisteredCallbackHandlerIds.Num() > 0))
			{
				return;
			}
			FakeWebApi->InvokeCallback(
				FakeWebApi->RegisteredCallbackHandlerIds[0], EventJson, false);
		}

	private: 
		// FSpyWebApplication cannot be the last to hold  the ToolsetRegistry SharedPtr 
		// because its base class holds a reference to it (via the OnToolsetRegisteredCallbackHandle)
		TSharedPtr<UE::ToolsetRegistry::FToolsetRegistry> ToolsetRegistry;
		TOptional<FSpyWebApplicationWithTracker> WebApplicationWithTracker;
		TSharedPtr<FSpyWebApplication> WebApplication;
		TSharedPtr<FFakeWebApi> WebApi;

	END_DEFINE_SPEC(FAIAssistantWebApplicationConversationUpdateRouting)

	void FAIAssistantWebApplicationConversationUpdateRouting::Define()
	{
		BeforeEach([this]()
		{
			ToolsetRegistry = MakeShared<UE::ToolsetRegistry::FToolsetRegistry>();
			WebApplicationWithTracker.Emplace(*this);
			WebApplication = WebApplicationWithTracker->WebApplication;
			WebApplication->SetRegistry(ToolsetRegistry);
			if (!WebApplicationWithTracker->EnsureInitialized()) return;
			WebApi = WebApplicationWithTracker->GetWebApi();
			TestTrue(TEXT("HasWebApi"), WebApi.IsValid());
		});

		AfterEach([this]()
		{
			WebApi.Reset();
			WebApplication.Reset();
			WebApplicationWithTracker.Reset();
			ToolsetRegistry.Reset();
		});

		Describe(TEXT("when a stopped event arrives"), [this]
		{
			It(TEXT("should mark the conversation as cancelled"), [this]
			{
				FString ConversationId = TEXT("conv-123");
				FireConversationUpdateEvent(ConversationId, EConversationUpdateType::Stopped);
				if (!WebApplication) return;
				(void)TestTrue(
					TEXT("ConversationCancelled"),
					WebApplication->IsConversationCancelled(ConversationId));
			});

			It(TEXT("should not trigger getConversation"), [this]
			{
				if (!WebApi) return;
				WebApi->ExecutedAsyncFunctions.Empty();
				FireConversationUpdateEvent(FString(TEXT("conv-123")), EConversationUpdateType::Stopped);
				(void)TestEqual(
					TEXT("GetConversationNotCalled"),
					WebApi->FindExecutedAsyncFunctions(nullptr, TEXT("getConversation")).Num(),
					0);
			});
		});

		Describe(TEXT("when a messagesUpdated event arrives"), [this]
		{
			It(TEXT("should trigger getConversation"), [this]
			{
				if (!WebApi) return;
				WebApi->ExecutedAsyncFunctions.Empty();
				FString ConversationId = TEXT("conv-456");
				FireConversationUpdateEvent(ConversationId, EConversationUpdateType::MessagesUpdated);
				(void)TestEqual(
					TEXT("GetConversationCalled"),
					WebApi->FindExecutedAsyncFunctions(
						nullptr, TEXT("getConversation")).Num(),
					1);
			});

			It(TEXT("should not mark the conversation as cancelled"), [this]
			{
				if (!WebApplication || !WebApi) return;
				FString ConversationId = TEXT("conv-456");
				FireConversationUpdateEvent(ConversationId, EConversationUpdateType::MessagesUpdated);
				(void)TestFalse(
					TEXT("ConversationNotCancelled"),
					WebApplication->IsConversationCancelled(ConversationId));
			});

			It(TEXT("should clear a prior cancellation once the conversation fetch completes"),
				[this]
			{
				if (!WebApplication || !WebApi) return;
				FString ConversationId = TEXT("conv-456");

				// Simulate a prior stop-generation for this conversation.
				FireConversationUpdateEvent(ConversationId, EConversationUpdateType::Stopped);
				if (!TestTrue(TEXT("CancelledBeforeUpdate"),
					WebApplication->IsConversationCancelled(ConversationId)))
				{
					return;
				}

				// Now a new messagesUpdated event arrives (new generation round).
				WebApi->ExecutedAsyncFunctions.Empty();
				FireConversationUpdateEvent(ConversationId, EConversationUpdateType::MessagesUpdated);

				// Complete the getConversation call with an empty conversation.
				FConversationId Id;
				Id.Id = ConversationId;
				Id.Type = TEXT("ConversationId");
				FConversation EmptyConversation;
				EmptyConversation.Descriptor.ConversationId.Emplace(Id);
				(void)WebApi->TestExpectAsyncFunctionCallAndComplete(
					*this, nullptr, TEXT("getConversation"), *Id.ToJson(false),
					*EmptyConversation.ToJson(false), false);

				(void)TestFalse(
					TEXT("CancellationCleared"),
					WebApplication->IsConversationCancelled(ConversationId));
			});
		});

		Describe(TEXT("when an unknown event kind arrives"), [this]
		{
			It(TEXT("should not trigger getConversation"), [this]
			{
				if (!WebApi) return;
				WebApi->ExecutedAsyncFunctions.Empty();
				FireConversationUpdateEvent(TEXT("conv-789"), TEXT("someUnknownKind"));
				(void)TestEqual(
					TEXT("GetConversationNotCalled"),
					WebApi->FindExecutedAsyncFunctions(nullptr, TEXT("getConversation")).Num(),
					0);
			});

			It(TEXT("should not mark the conversation as cancelled"), [this]
			{
				if (!WebApplication) return;
				FString ConversationId = TEXT("conv-789");
				FireConversationUpdateEvent(ConversationId, TEXT("someUnknownKind"));
				(void)TestFalse(
					TEXT("ConversationNotCancelled"),
					WebApplication->IsConversationCancelled(ConversationId));
			});
		});
	}
}

#endif
