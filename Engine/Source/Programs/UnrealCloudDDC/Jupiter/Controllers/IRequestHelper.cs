// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Security.Claims;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;

namespace Jupiter.Controllers
{
	public interface IRequestHelper
	{
		public Task<ActionResult?> HasAccessToScopeAsync(ClaimsPrincipal user, HttpRequest request, AccessScope scope, JupiterAclAction[] aclActions);

		public bool IsPublicPort(HttpContext context);
		IActionResult? EnsureSingleRangeRequest(HttpContext context);
	}

	public class AuthorizationException : Exception
	{
		public ActionResult Result { get; }

		public AuthorizationException(ActionResult result, string errorMessage) : base(errorMessage)
		{
			Result = result;
		}
	}
}
