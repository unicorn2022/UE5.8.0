// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkFaceControl.h"

#include "LiveLinkFaceSource.h"
#include "Control/ControlMessenger.h"

using namespace UE::CaptureManager;

DEFINE_LOG_CATEGORY(LogLiveLinkFaceControl);

#define LOCTEXT_NAMESPACE "LiveLinkFaceControl"

FLiveLinkFaceControl::FLiveLinkFaceControl(FGuid InSourceGuid, FString InControlHost, const uint16 InControlPort, const uint16 InStreamPort, FOnSelectRemoteSubject InOnSelectRemoteSubject, FOnStreamingStarted InOnStreamingStarted)
	: bIsConnected(false)
	, bStopping(false)
	, ControlMessenger(MakeUnique<FControlMessenger>())
	, ServerName(LOCTEXT("UnknownServerName", "Unknown"))
	, Host(MoveTemp(InControlHost))
	, Port(InControlPort)
	, SourceGuid(MoveTemp(InSourceGuid))
	, StreamPort(InStreamPort)
	, SelectRemoteSubjectDelegate(MoveTemp(InOnSelectRemoteSubject))
	, StreamingStartedDelegate(MoveTemp(InOnStreamingStarted))
{
	ControlMessenger->RegisterDisconnectHandler(FControlMessenger::FOnDisconnect::CreateRaw(this, &FLiveLinkFaceControl::OnDisconnect));
}

FLiveLinkFaceControl::~FLiveLinkFaceControl()
{
	if (Thread.IsValid())
	{
		Thread->Kill(true);
		Thread.Reset();
	}
}

void FLiveLinkFaceControl::Start()
{
	UE_LOGF(LogLiveLinkFaceControl, Verbose, "Starting control thread")
	bStopping = false;
	Thread.Reset(FRunnableThread::Create(this, TEXT("LiveLinkFaceControl"), TPri_Normal));
}

void FLiveLinkFaceControl::RestartConnection()
{
	if (bIsConnected)
	{
		UE_LOGF(LogLiveLinkFaceControl, Verbose, "Restarting connection")
		StopSession();
	}
}

const FText& FLiveLinkFaceControl::GetServerName() const
{
	return ServerName;
}

const FText& FLiveLinkFaceControl::GetServerModel() const
{
	return ServerModel;
}

const FText& FLiveLinkFaceControl::GetServerPlatform() const
{
	return ServerPlatform;
}

void FLiveLinkFaceControl::Stop()
{
	bStopping = true;
}

bool FLiveLinkFaceControl::StartSession()
{
	UE_LOGF(LogLiveLinkFaceControl, Verbose, "Connecting to control server")
	
	if (Connect() == false)
	{
		return false;
	}
			
	if (StartControlSession() == false)
	{
		Disconnect();
		return false;
	}

	if (GetControlServerInformation() == false)
	{
		StopControlSession();
		Disconnect();
		return false;
	}
	
	if (GetSubjects() == false)
	{
		StopControlSession();
		Disconnect();
		return false;
	}

	if (!SelectRemoteSubjectDelegate.IsBound())
	{
		UE_LOGF(LogLiveLinkFaceControl, Error, "Select remote subject delegate not bound when starting session")
		return false;
	}

	const FRemoteSubject SelectedSubject = SelectRemoteSubjectDelegate.Execute(RemoteSubjects);
	if (StartStreaming(SelectedSubject) == false)
	{
		StopControlSession();
		Disconnect();
		return false;
	}

	StreamingStartedDelegate.ExecuteIfBound(SelectedSubject);
	
	return true;
}

void FLiveLinkFaceControl::StopSession()
{
	StopStreaming();
	StopControlSession();
	Disconnect();
}

bool FLiveLinkFaceControl::Connect()
{
	UE_LOGF(LogLiveLinkFaceControl, Verbose, "Starting Control Messenger")

	TProtocolResult StartResult = ControlMessenger->Start(Host, Port);
	
	if (StartResult.HasError())
	{
		UE_LOGF(LogLiveLinkFaceControl, Verbose, "Error starting control messenger %ls", *StartResult.StealError().GetMessage())
		return false;
	}

	return true;
}

void FLiveLinkFaceControl::Disconnect()
{
	ControlMessenger->Stop();
}

uint32 FLiveLinkFaceControl::Run()
{
	UE_LOGF(LogLiveLinkFaceControl, Verbose, "Control thread started running. bStopping is: %d", bStopping)
	while (!bStopping)
	{
		if (!bIsConnected)
		{
			UE_LOGF(LogLiveLinkFaceControl, Verbose, "Control is not connected. Establishing connection to CPS device")

			bIsConnected = StartSession();

			if (!bIsConnected)
			{
				UE_LOGF(LogLiveLinkFaceControl, Warning, "Failed to start streaming session. Retrying in five seconds")

				double Start = FPlatformTime::Seconds();
				while (FPlatformTime::Seconds() - Start < 5 && !bStopping)
				{
					FPlatformProcess::Sleep(0.1f);
				}
			}
		}
		FPlatformProcess::Sleep(0.1f);
	}

	// On shut down, if we're connected we need to try and close our control session
	if (bIsConnected)
	{
		StopSession();
	}

	UE_LOGF(LogLiveLinkFaceControl, Verbose, "Control thread returning")
	return 0;
}

bool FLiveLinkFaceControl::StartControlSession()
{
	UE_LOGF(LogLiveLinkFaceControl, Verbose, "Starting control session")
	TProtocolResult StartSessionResult = ControlMessenger->StartSession();
	if (StartSessionResult.HasError())
	{
		UE_LOGF(LogLiveLinkFaceControl, Error, "Error starting control session %ls", *StartSessionResult.StealError().GetMessage());
		return false;
	}

	UE_LOGF(LogLiveLinkFaceControl, Verbose, "Succesfully started control session")
	return true;
}

bool FLiveLinkFaceControl::GetControlServerInformation()
{
	UE_LOGF(LogLiveLinkFaceControl, Verbose, "Getting control server information")
	TProtocolResult GetServerInformationResult = ControlMessenger->GetServerInformation();
	if (GetServerInformationResult.HasError())
	{
		UE_LOGF(LogLiveLinkFaceControl, Warning, "Error getting control server information %ls", *GetServerInformationResult.StealError().GetMessage())
		return false;
	}
	FGetServerInformationResponse GetServerInformationResponse = GetServerInformationResult.StealValue();
	
	ServerName = FText::FromString(GetServerInformationResponse.GetName());
	UE_LOGF(LogLiveLinkFaceControl, Verbose, "Retrieved server name '%ls'", *ServerName.ToString())

	ServerModel = FText::FromString(GetServerInformationResponse.GetModel());

	ServerPlatform = FText::FromString(GetServerInformationResponse.GetPlatformName());

	return true;
}

bool FLiveLinkFaceControl::GetSubjects()
{
	UE_LOGF(LogLiveLinkFaceControl, Verbose, "Getting available streaming subjects from server")
	const FGetStreamingSubjectsRequest GetStreamingSubjectsRequest;
	TProtocolResult<FGetStreamingSubjectsResponse> GetSubjectsResult = ControlMessenger->SendRequest(GetStreamingSubjectsRequest);

	if (GetSubjectsResult.HasError())
	{
		UE_LOGF(LogLiveLinkFaceControl, Error, "Error getting streaming subjects %ls", *GetSubjectsResult.StealError().GetMessage());
		return false;
	}
	
	FRemoteSubjects NewRemoteSubjects;

	const FGetStreamingSubjectsResponse GetStreamingSubjectsResponse = GetSubjectsResult.StealValue();
	const TArray<FGetStreamingSubjectsResponse::FSubject> Subjects = GetStreamingSubjectsResponse.GetSubjects();
	for (const FGetStreamingSubjectsResponse::FSubject& Subject : Subjects)
	{
		const FString RemoteSubjectId = Subject.Id;

		if (RemoteSubjectId.IsEmpty())
		{
			UE_LOGF(LogLiveLinkFaceControl, Error, "Subject.Id can not be an empty string");
			continue;
		}

		FGetStreamingSubjectsResponse::FAnimationMetadata AnimationMetadata = Subject.AnimationMetadata;

		FRemoteSubject::EAnimationType AnimationType;
		if (AnimationMetadata.Type == TEXT("arkit"))
		{
			AnimationType = FRemoteSubject::EAnimationType::ArKit;
		}
		else if (AnimationMetadata.Type == TEXT("mha"))
		{
			AnimationType = FRemoteSubject::EAnimationType::Mha;
		}
		else
		{
			UE_LOGF(LogLiveLinkFaceControl, Error, "Animation metadata contains unknown animation type %ls", *AnimationMetadata.Type);
			continue;
		}
		
		TArray<FName> PropertyNames;
		for (const FString& ControlName : AnimationMetadata.Controls)
		{
			PropertyNames.Add(FName(ControlName));
		}

		FRemoteSubject RemoteSubject(Subject.Id, Subject.Name, AnimationType, AnimationMetadata.Version, MoveTemp(PropertyNames));
		NewRemoteSubjects.Add(RemoteSubject);
	}

	if (NewRemoteSubjects.IsEmpty())
	{
		UE_LOGF(LogLiveLinkFaceControl, Error, "Response to get subjects request contained no valid subjects");
		return false;
	}

	RemoteSubjects = NewRemoteSubjects;

	return true;
}

bool FLiveLinkFaceControl::StartStreaming(const FRemoteSubject& InRemoteSubject)
{
	UE_LOGF(LogLiveLinkFaceControl, Verbose, "Sending start streaming request to control server")
	TArray<FStartStreamingRequest::FSubject> Subjects;
	Subjects.Add(FStartStreamingRequest::FSubject(InRemoteSubject.Id, InRemoteSubject.Name));
	const FStartStreamingRequest StartStreamingRequest(StreamPort,Subjects);
	TProtocolResult<FStartStreamingResponse> StartStreamingResult = ControlMessenger->SendRequest(StartStreamingRequest);

	if (StartStreamingResult.HasError())
	{
		UE_LOGF(LogLiveLinkFaceControl, Error, "Error starting streaming %ls", *StartStreamingResult.StealError().GetMessage());
		return false;
	}

	UE_LOGF(LogLiveLinkFaceControl, Verbose, "Successfully requested streaming from control server")
	return true;
}

void FLiveLinkFaceControl::StopStreaming()
{
	const FStopStreamingRequest StopStreamingRequest;
	TProtocolResult<FStopStreamingResponse> StopStreamingResponse = ControlMessenger->SendRequest(StopStreamingRequest);

	if (StopStreamingResponse.HasError())
	{
		UE_LOGF(LogLiveLinkFaceControl, Warning, "Error sending stop streaming request %ls", *StopStreamingResponse.StealError().GetMessage());
	}
}

void FLiveLinkFaceControl::StopControlSession()
{
	const FStopSessionRequest StopSessionRequest;
	TProtocolResult<FStopSessionResponse> StopSessionResponse = ControlMessenger->SendRequest(StopSessionRequest);

	if (StopSessionResponse.HasError())
	{
		UE_LOGF(LogLiveLinkFaceControl, Warning, "Error stopping control session %ls", *StopSessionResponse.StealError().GetMessage());
	}
}

void FLiveLinkFaceControl::OnDisconnect(const FString& InCause)
{
	UE_LOGF(LogLiveLinkFaceControl, Verbose, "Control messenger disconnected. Cause: %ls", *InCause);
	bIsConnected = false;
}

#undef LOCTEXT_NAMESPACE