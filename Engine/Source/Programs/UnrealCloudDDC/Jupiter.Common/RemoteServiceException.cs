// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace Jupiter.Common
{
	public class RemoteServiceException : Exception
	{
		public RemoteServiceException(string message, Exception cause) : base(message, cause)
		{
		}
	}
}
