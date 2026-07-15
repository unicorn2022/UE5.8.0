// Copyright Epic Games, Inc. All Rights Reserved.

using BenchmarkDotNet.Running;

namespace EpicGames.Benchmarks;

internal class Program
{
	static void Main(string[] args)
	{
		_ = BenchmarkRunner.Run(typeof(Program).Assembly, args: args);
	}
}
