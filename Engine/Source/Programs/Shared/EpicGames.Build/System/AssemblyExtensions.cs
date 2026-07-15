// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Linq;
using System.Reflection;

namespace UnrealBuildBase
{
	/// <summary>
	/// Extensions to Assembly
	/// </summary>
	public static class AssemblyExtensions
	{
		/// <summary>
		/// Safely get all loaded types from an assembly.
		/// </summary>
		/// <param name="assembly">Assembly to use</param>
		/// <returns></returns>
		public static Type[] SafeGetLoadedTypes(this Assembly assembly)
		{
			try
			{
				return assembly.GetTypes();
			}
			catch (ReflectionTypeLoadException e)
			{
				return [.. e.Types.OfType<Type>()];
			}
		}
	}
}
