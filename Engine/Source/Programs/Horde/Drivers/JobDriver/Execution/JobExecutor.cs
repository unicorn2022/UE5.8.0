// Copyright Epic Games, Inc. All Rights Reserved.

using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using EpicGames.Core;
using EpicGames.Horde;
using EpicGames.Horde.Agents.Leases;
using EpicGames.Horde.Artifacts;
using EpicGames.Horde.Jobs;
using EpicGames.Horde.Logs;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;
using Grpc.Core;
using JobDriver.Parser;
using JobDriver.Utility;
using Horde.Common.Rpc;
using HordeCommon;
using HordeCommon.Rpc.Messages;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using OpenTelemetry.Trace;
using StatusCode = Grpc.Core.StatusCode;

namespace JobDriver.Execution
{
	/// <summary>
	/// Information about a job step to be executed
	/// </summary>
	/// <param name="StepId">Unique identifier for the step</param>
	/// <param name="LogId">Log identifier for the step output</param>
	/// <param name="Name">Name of the step</param>
	/// <param name="Credentials">Credentials available to the step</param>
	/// <param name="Properties">Properties for the step</param>
	/// <param name="EnvVars">Environment variables for the step</param>
	/// <param name="Warnings">Whether warnings are enabled</param>
	/// <param name="Inputs">Input dependencies for the step</param>
	/// <param name="OptionalInputs">Optional input dependencies for the step</param>
	/// <param name="OutputNames">Names of outputs produced by the step</param>
	/// <param name="PublishOutputs">Indices of outputs to publish</param>
	/// <param name="Artifacts">Artifact definitions for the step</param>
	public record class JobStepInfo
	(
		JobStepId StepId,
		LogId LogId,
		string Name,
		IReadOnlyDictionary<string, string> Credentials,
		IReadOnlyDictionary<string, string> Properties,
		IReadOnlyDictionary<string, string> EnvVars,
		bool? Warnings,
		IReadOnlyList<string> Inputs,
		IReadOnlyList<string> OptionalInputs,
		IReadOnlyList<string> OutputNames,
		IList<int> PublishOutputs,
		IReadOnlyList<RpcCreateGraphArtifactRequest> Artifacts
	)
	{
		/// <summary>
		/// Constructor from an RPC response
		/// </summary>
		public JobStepInfo(RpcBeginStepResponse response)
			: this(JobStepId.Parse(response.StepId), LogId.Parse(response.LogId), response.Name, response.Credentials, response.Properties, response.EnvVars, response.Warnings, response.Inputs, response.OptionalInputs, response.OutputNames, response.PublishOutputs, response.Artifacts)
		{
		}
	}

	/// <summary>
	/// Options for creating a job executor
	/// </summary>
	public class JobExecutorOptions
	{
		/// <summary>
		/// The Horde client instance
		/// </summary>
		public IHordeClient HordeClient { get; }

		/// <summary>
		/// Working directory for the executor
		/// </summary>
		public DirectoryReference WorkingDir { get; }

		/// <summary>
		/// Processes to terminate before/after execution
		/// </summary>
		public IReadOnlyList<ProcessToTerminate>? ProcessesToTerminate { get; }

		/// <summary>
		/// Identifier for the job
		/// </summary>
		public JobId JobId { get; }

		/// <summary>
		/// Identifier for the batch within the job
		/// </summary>
		public JobStepBatchId BatchId { get; }

		/// <summary>
		/// Identifier for the lease
		/// </summary>
		public LeaseId LeaseId { get; }

		/// <summary>
		/// Options for the job
		/// </summary>
		public RpcJobOptions JobOptions { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public JobExecutorOptions(IHordeClient hordeClient, DirectoryReference workingDir, IReadOnlyList<ProcessToTerminate>? processesToTerminate, JobId jobId, JobStepBatchId batchId, LeaseId leaseId, RpcJobOptions jobOptions)
		{
			HordeClient = hordeClient;
			WorkingDir = workingDir;
			ProcessesToTerminate = processesToTerminate;
			JobId = jobId;
			BatchId = batchId;
			LeaseId = leaseId;
			JobOptions = jobOptions;
		}
	}

	/// <summary>
	/// Abstract base class for job executors. Handles running BuildGraph scripts on an agent.
	/// </summary>
	public abstract class JobExecutor : IDisposable
	{
#pragma warning disable CS1591
		protected class ExportedNode
		{
			public string Name { get; set; } = String.Empty;
			public bool RunEarly { get; set; }
			public bool? AllowRetry { get; set; }
			public bool? Warnings { get; set; }
			public List<string> Inputs { get; set; } = new List<string>();
			public List<string> OptionalInputs { get; set; } = [];
			public List<string> Outputs { get; set; } = new List<string>();
			public List<string> InputDependencies { get; set; } = new List<string>();
			public List<string> OptionalInputDependencies { get; set; } = [];
			public List<string> OrderDependencies { get; set; } = new List<string>();
			public Dictionary<string, string> Annotations { get; set; } = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
		}

		protected class ExportedGroup
		{
			public List<string> Types { get; set; } = new List<string>();
			public List<ExportedNode> Nodes { get; set; } = new List<ExportedNode>();
		}

		protected class ExportedAggregate
		{
			public string Name { get; set; } = String.Empty;
			public List<string> Nodes { get; set; } = new List<string>();
		}

		protected class ExportedLabel
		{
			public string? Name { get; set; }
			public string? Category { get; set; }
			public string? UgsBadge { get; set; }
			public string? UgsProject { get; set; }
			public RpcLabelChange Change { get; set; } = RpcLabelChange.Current;
			public List<string> RequiredNodes { get; set; } = new List<string>();
			public List<string> IncludedNodes { get; set; } = new List<string>();
		}

		protected class ExportedBadge
		{
			public string Name { get; set; } = String.Empty;
			public string? Project { get; set; }
			public int Change { get; set; }
			public string? Dependencies { get; set; }
		}

		protected class ExportedArtifact
		{
			public string Name { get; set; } = String.Empty;
			public string? Type { get; set; }
			public string? Description { get; set; }
			public string? BasePath { get; set; }
			public List<string> Keys { get; set; } = new List<string>();
			public List<string> Metadata { get; set; } = new List<string>();
			public string NodeName { get; set; } = String.Empty;
			public string OutputName { get; set; } = String.Empty;
		}

		protected class ExportedGraph
		{
			public List<ExportedGroup> Groups { get; set; } = new List<ExportedGroup>();
			public List<ExportedAggregate> Aggregates { get; set; } = new List<ExportedAggregate>();
			public List<ExportedLabel> Labels { get; set; } = new List<ExportedLabel>();
			public List<ExportedBadge> Badges { get; set; } = new List<ExportedBadge>();
			public List<ExportedArtifact> Artifacts { get; set; } = new List<ExportedArtifact>();
		}
#pragma warning restore CS1591

		class TraceEvent
		{
			public string Name { get; set; } = "Unknown";
			public string? SpanId { get; set; }
			public string? ParentId { get; set; }
			public string? Service { get; set; }
			public string? Resource { get; set; }
			public DateTimeOffset StartTime { get; set; }
			public DateTimeOffset FinishTime { get; set; }
			public Dictionary<string, string>? Metadata { get; set; }

			[JsonIgnore]
			public int Index { get; set; }
		}

		class TraceEventList
		{
			public List<TraceEvent> Spans { get; set; } = new List<TraceEvent>();
		}

		class TraceSpan
		{
			public string? Name { get; set; }
			public string? SpanId { get; set; }
			public string? ParentId { get; set; }
			public string? Service { get; set; }
			public string? Resource { get; set; }
			public long Start { get; set; }
			public long Finish { get; set; }
			public Dictionary<string, string>? Properties { get; set; }
			public List<TraceSpan>? Children { get; set; }

			public void AddChild(TraceSpan child)
			{
				Children ??= new List<TraceSpan>();
				Children.Add(child);
			}
		}

		class TestDataItem
		{
			public string Key { get; set; } = String.Empty;
			public Dictionary<string, object> Data { get; set; } = new Dictionary<string, object>();
		}

		class TestData
		{
			public List<TestDataItem> Items { get; set; } = new List<TestDataItem>();
		}

		class ReportData
		{
			public RpcReportScope Scope { get; set; }
			public RpcReportPlacement Placement { get; set; }
			public string Name { get; set; } = String.Empty;
			public string Content { get; set; } = String.Empty;
			public string FileName { get; set; } = String.Empty;
			public string? Message { get; set; }
			public string? Severity { get; set; }
		}

		const string ScriptArgumentPrefix = "-Script=";
		const string TargetArgumentPrefix = "-Target=";

		string PreprocessedScript => $"{EnginePath}/Saved/Horde/Preprocessed.xml";
		string PreprocessedSchema => $"{EnginePath}/Saved/Horde/Preprocessed.xsd";

#pragma warning disable CS1591
		protected List<string> Targets { get; set; } = new List<string>();
		protected string? ScriptFileName { get; set; }
		protected bool PreprocessScript { get; set; }
		protected bool SavePreprocessedScript { get; set; }
		protected string EnginePath => String.IsNullOrEmpty(Batch.EnginePath)? "Engine" : Batch.EnginePath;
		protected Tracer Tracer { get; }
#pragma warning restore CS1591

		/// <summary>
		/// Logger for the local agent process (as opposed to job logger)
		/// </summary>
		protected ILogger Logger { get; }

#pragma warning disable CS1591
		protected IHordeClient HordeClient { get; }
		protected DirectoryReference WorkingDir { get; }
		protected JobId JobId { get; }
		protected JobStepBatchId BatchId { get; }
		protected LeaseId LeaseId { get; }
		protected IReadOnlyList<ProcessToTerminate>? ProcessesToTerminate { get; }
		protected RpcBeginBatchResponse Batch { get; private set; }
		protected List<string> AdditionalArguments { get; set; } = new List<string>();
		protected bool AllowTargetChanges { get; set; }
		protected bool CompileAutomationTool { get; set; } = true;
		protected RpcJobOptions JobOptions { get; }
		protected JobRpc.JobRpcClient JobRpc { get; private set; }
		protected Dictionary<string, string> RemapAgentTypes { get; set; } = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
		protected Dictionary<string, string> EnvVars { get; set; } = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
#pragma warning restore CS1591

		/// <summary>
		/// How often to poll the server checking if a step has been aborted
		/// Exposed as internal to ease testing.
		/// </summary>
		internal TimeSpan _stepAbortPollInterval = TimeSpan.FromSeconds(5);

		/// <summary>
		/// How long to wait before retrying a failed step abort check request
		/// </summary>
		internal TimeSpan _stepAbortPollRetryDelay = TimeSpan.FromSeconds(30);

		/// <summary>
		/// Cancellation source for the job executor. Will be cancelled when <see cref="_abortHandle"/> is signaled
		/// </summary>
		/// <remarks>
		/// Exposed as internal to ease testing.
		/// </remarks>
		internal CancellationTokenSource _abortCancellationSource = new();

		/// <summary>
		/// The event handle that will be signaled for the job executor to abort.
		/// If the event is not created the process can still be canceled by the parent process but without the chance to complete 
		/// </summary>
		/// <remarks>
		/// On Windows will be a shared named event created by the job handler parent process.
		/// On Linux and macOS will be a local event that is signaled upon receiving a SIGTERM signal. 
		/// </remarks>
		/// <remarks>
		/// Exposed as internal to ease testing.
		/// </remarks>
		internal EventWaitHandle? _abortHandle;

		const string AbortEventKeyName = "UE_HORDE_JOB_EXECUTOR_ABORT_EVENT_NAME";

		readonly PosixSignalRegistration? _sigTermHandler;

		/// <summary>
		/// Constructor
		/// </summary>
		protected JobExecutor(JobExecutorOptions options, Tracer tracer, ILogger logger)
		{
			HordeClient = options.HordeClient;
			WorkingDir = options.WorkingDir;
			JobId = options.JobId;
			BatchId = options.BatchId;
			LeaseId = options.LeaseId;
			ProcessesToTerminate = options.ProcessesToTerminate;

			Batch = null!; // Set in InitializeAsync()

			JobOptions = options.JobOptions;
			JobRpc = null!; // Set in InitializeAsync()

			Tracer = tracer;
			Logger = logger;

			(_abortHandle, _sigTermHandler) = CreateAbortEvent(Environment.GetEnvironmentVariable(AbortEventKeyName), SigTermHandler);
		}

		private static (EventWaitHandle?, PosixSignalRegistration?) CreateAbortEvent(string? name, Action<PosixSignalContext> handler)
		{
			EventWaitHandle? ewh = null;
			PosixSignalRegistration? psr = null;

			if (OperatingSystem.IsWindows())
			{
				if (!String.IsNullOrEmpty(name))
				{
					try
					{
						EventWaitHandle.TryOpenExisting(name, out ewh);
					}
					catch
					{
						// ignored
					}
				}
				else
				{
					// To ease testing on Windows; there won't be a shared event on Windows during tests
					ewh = new EventWaitHandle(false, EventResetMode.ManualReset);
				}
			}
			else if (OperatingSystem.IsLinux() || OperatingSystem.IsMacOS())
			{
				psr = PosixSignalRegistration.Create(PosixSignal.SIGTERM, handler);
				ewh = new EventWaitHandle(false, EventResetMode.ManualReset);
			}

			return (ewh, psr);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		/// <summary>
		/// Disposes managed resources
		/// </summary>
		protected virtual void Dispose(bool disposing)
		{
			if (disposing)
			{
				_sigTermHandler?.Dispose();
				_abortHandle?.Dispose();
				_abortCancellationSource.Dispose();
			}
		}

		void SigTermHandler(PosixSignalContext context)
		{
			if (_abortHandle == null)
			{
				return;
			}

			try
			{
				_abortHandle.Set();
				context.Cancel = true;
			}
			catch
			{
				// ignored
			}
		}

		/// <summary>
		/// Executes all steps in the job batch
		/// </summary>
		public async Task ExecuteAsync(ILogger logger, CancellationToken cancellationToken)
		{
			using TelemetrySpan span = Tracer.StartActiveSpan($"{nameof(JobExecutor)}.{nameof(ExecuteAsync)}");
			span.SetAttribute("horde.job.id", JobId.ToString());
			span.SetAttribute("horde.job.batch_id", BatchId.ToString());
			span.SetAttribute("horde.job.executor", JobOptions.Executor);
			span.SetAttribute("horde.lease.id", LeaseId.ToString());
			JobOptions.DecorateSpan(span);

			using CancellationTokenSource stopAbortTask = new();
			Task abortTask = CancelOnEventAsync(_abortHandle, _abortCancellationSource, logger, stopAbortTask.Token);
			using CancellationTokenSource combined = CancellationTokenSource.CreateLinkedTokenSource(_abortCancellationSource.Token, cancellationToken);

			try
			{
				await ExecuteInternalAsync(logger, combined.Token);
			}
			finally
			{
				await stopAbortTask.CancelAsync();
				await abortTask;
			}
		}

		static Task CancelOnEventAsync(EventWaitHandle? eventHandle, CancellationTokenSource cancellationSource, ILogger logger, CancellationToken cancellationToken = default)
		{
			return Task.Run(async () =>
			{
				if (eventHandle != null)
				{
					WaitHandle[] handles = [eventHandle, cancellationToken.WaitHandle];
					int index = WaitHandle.WaitAny(handles);
					if (index == Array.IndexOf(handles, eventHandle))
					{
						logger.LogWarning("Received a cancellation request from the parent job handler process");
						await cancellationSource.CancelAsync();
					}
				}
			}, CancellationToken.None);
		}

		/// <summary>
		/// Internal execution logic for the job batch, polling for and running steps
		/// </summary>
		protected virtual async Task ExecuteInternalAsync(ILogger logger, CancellationToken cancellationToken)
		{
			string? cleanupVar = Environment.GetEnvironmentVariable("UE_HORDE_JOB_BATCH_CLEANUP");
			if (cleanupVar != null && Int32.TryParse(cleanupVar, out int v) && v != 0)
			{
				logger.LogInformation("Cleaning up failed job batch.");
				await FinalizeAsync(logger, CancellationToken.None);
				return;
			}

			JobRpc.JobRpcClient jobRpc = await HordeClient.CreateGrpcClientAsync<JobRpc.JobRpcClient>(cancellationToken);

			// Create a storage client for this session
			logger.LogInformation("Executing jobId {JobId}, batchId {BatchId}, leaseId {LeaseId} with executor {Name}", JobId, BatchId, LeaseId, JobOptions.Executor);

			// Start executing the current batch
			RpcBeginBatchResponse batch = await jobRpc.BeginBatchAsync(new RpcBeginBatchRequest(JobId, BatchId, LeaseId), cancellationToken: cancellationToken);
			try
			{
				await ExecuteBatchAsync(batch, logger, NullLogger.Instance, cancellationToken);
			}
			catch (Exception ex)
			{
				if (cancellationToken.IsCancellationRequested && IsCancellationException(ex))
				{
					if (!HordeClient.HasValidAccessToken())
					{
						logger.LogError(ex, "Connection to the server was lost; step aborted.");
					}
					else
					{
						logger.LogInformation(ex, "Step was aborted");
					}
					throw;
				}
				else
				{
					logger.LogError(ex, "Exception while executing lease {LeaseId}: {Ex}", LeaseId, ex.Message);
				}
			}

			// If this lease was cancelled, don't bother updating the job state.
			cancellationToken.ThrowIfCancellationRequested();

			// Mark the batch as complete
			await jobRpc.FinishBatchAsync(new RpcFinishBatchRequest(JobId, BatchId, LeaseId), cancellationToken: cancellationToken);
			logger.LogInformation("Done.");
		}

		/// <summary>
		/// Executes a batch
		/// </summary>
		async Task ExecuteBatchAsync(RpcBeginBatchResponse batch, ILogger logger, ILogger localLogger, CancellationToken cancellationToken)
		{
			JobRpc.JobRpcClient rpcClient = await HordeClient.CreateGrpcClientAsync<JobRpc.JobRpcClient>(cancellationToken);

			await TerminateProcessHelper.TerminateProcessesAsync(TerminateCondition.BeforeBatch, WorkingDir, ProcessesToTerminate, logger, cancellationToken);

			// Try to initialize the executor
			logger.LogInformation("Initializing executor...");
			using (logger.BeginIndentScope("  "))
			{
				await InitializeAsync(batch, logger, cancellationToken);
			}

			try
			{
				// Execute the steps
				logger.LogInformation("Executing steps...");
				for (; ; )
				{
					// Get the next step to execute
					RpcBeginStepResponse stepResponse = await rpcClient.BeginStepAsync(new RpcBeginStepRequest(JobId, BatchId, LeaseId), cancellationToken: cancellationToken);
					if (stepResponse.State == RpcBeginStepResponse.Types.Result.Waiting)
					{
						logger.LogInformation("Waiting for dependency to be ready");
						await Task.Delay(TimeSpan.FromSeconds(20.0), cancellationToken);
						continue;
					}
					else if (stepResponse.State == RpcBeginStepResponse.Types.Result.Complete)
					{
						logger.LogInformation("No more steps to execute; finalizing lease.");
						break;
					}
					else if (stepResponse.State != RpcBeginStepResponse.Types.Result.Ready)
					{
						logger.LogError("Unexpected step state: {StepState}", stepResponse.State);
						break;
					}

					JobStepInfo step = new JobStepInfo(stepResponse);

					// Get current disk space available. This will allow us to more easily spot steps that eat up a lot of disk space.
					string? driveName;
					if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
					{
						driveName = Path.GetPathRoot(WorkingDir.FullName);
					}
					else
					{
						driveName = WorkingDir.FullName;
					}

					float availableFreeSpace = 0;
					if (driveName != null)
					{
						try
						{
							DriveInfo info = new DriveInfo(driveName);
							availableFreeSpace = (1.0f * info.AvailableFreeSpace) / 1024 / 1024 / 1024;
						}
						catch (Exception ex)
						{
							logger.LogWarning(ex, "Unable to query disk info for path '{DriveName}'", driveName);
						}
					}

					// Print the new state
					Stopwatch stepTimer = Stopwatch.StartNew();

					logger.LogInformation("Starting job {JobId}, batch {BatchId}, step {StepId} (Drive Space Left: {DriveSpaceRemaining} GB)", JobId, BatchId, step.StepId, availableFreeSpace.ToString("F1"));

					// Create a trace span
					using TelemetrySpan span = Tracer.StartActiveSpan("Execute");
					span.SetAttribute("horde.job.step.id", step.StepId.ToString());
					span.SetAttribute("horde.job.step.name", step.Name);
					span.SetAttribute("horde.job.step.log_id", step.LogId.ToString());

					// Update the context to include information about this step
					JobStepOutcome stepOutcome;
					JobStepState stepState;
					using (logger.BeginIndentScope("  "))
					{
						// Start writing to the log file
#pragma warning disable CA2000 // Dispose objects before losing scope
						await using (JobStepLogger stepLogger = new JobStepLogger(HordeClient, step.LogId, localLogger, JobId, BatchId, step.StepId, step.Warnings, LogLevel.Debug, logger))
						{
							// Execute the task
							using CancellationTokenSource stepPollCancelSource = new CancellationTokenSource();
							using CancellationTokenSource stepAbortSource = new CancellationTokenSource();
							TaskCompletionSource<bool> stepFinishedSource = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
							Task stepPollTask = Task.Run(() => PollForStepAbortAsync(rpcClient, JobId, BatchId, step.StepId, stepAbortSource, stepFinishedSource.Task, logger, stepPollCancelSource.Token), cancellationToken);

							try
							{
								ILogger forwardingLogger = new DefaultLoggerIndentHandler(stepLogger);
								(stepOutcome, stepState) = await ExecuteStepAsync(step, forwardingLogger, cancellationToken, stepAbortSource.Token);
							}
							finally
							{
								// Will get called even when cancellation token for the lease/batch fires
								stepFinishedSource.SetResult(true); // Tell background poll task to stop
								await stepPollTask;

								// Kill any processes spawned by the step
								try
								{
									await TerminateProcessHelper.TerminateProcessesAsync(TerminateCondition.AfterStep, WorkingDir, ProcessesToTerminate, logger, CancellationToken.None);
								}
								catch (Exception ex)
								{
									logger.LogWarning(ex, "Exception while terminating processes: {Message}", ex.Message);
								}

								// Wait for the logger to finish
								await stepLogger.StopAsync();
							}

							// Reflect the warnings/errors in the step outcome
							if (stepOutcome > stepLogger.Outcome)
							{
								stepOutcome = stepLogger.Outcome;
							}
						}
#pragma warning restore CA2000 // Dispose objects before losing scope

						// Update the server with the outcome from the step
						logger.LogInformation("Marking step as complete (Outcome={Outcome}, State={StepState})", stepOutcome, stepState);
						await rpcClient.UpdateStepAsync(new RpcUpdateStepRequest(JobId, BatchId, step.StepId, stepState, stepOutcome), cancellationToken: cancellationToken);
					}

					// Print the finishing state
					stepTimer.Stop();
					logger.LogInformation("Completed in {Time}", stepTimer.Elapsed);
				}
			}
			catch (Exception ex)
			{
				if (cancellationToken.IsCancellationRequested && IsCancellationException(ex))
				{
					logger.LogError("Lease was aborted");
				}
				else
				{
					logger.LogError(ex, "Exception while executing batch: {Ex}", ex);
				}
			}

			// Terminate any processes which are still running
			try
			{
				await TerminateProcessHelper.TerminateProcessesAsync(TerminateCondition.AfterBatch, WorkingDir, ProcessesToTerminate, logger, CancellationToken.None);
			}
			catch (Exception ex)
			{
				logger.LogWarning(ex, "Exception while terminating processes: {Message}", ex.Message);
			}

			// Clean the environment
			logger.LogInformation("Finalizing...");
			using (logger.BeginIndentScope("  "))
			{
				await FinalizeAsync(logger, CancellationToken.None);
			}
		}

		/// <summary>
		/// Executes a step
		/// </summary>
		/// <param name="step">Step to execute</param>
		/// <param name="stepLogger">Logger for the step</param>
		/// <param name="cancellationToken">Cancellation token to abort the batch</param>
		/// <param name="stepCancellationToken">Cancellation token to abort only this individual step</param>
		/// <returns>Async task</returns>
		internal async Task<(JobStepOutcome, JobStepState)> ExecuteStepAsync(JobStepInfo step, ILogger stepLogger, CancellationToken cancellationToken, CancellationToken stepCancellationToken)
		{
			using CancellationTokenSource combined = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, stepCancellationToken);
			try
			{
				JobStepOutcome stepOutcome = await RunAsync(step, stepLogger, combined.Token);
				return (stepOutcome, JobStepState.Completed);
			}
			catch (Exception ex)
			{
				if (cancellationToken.IsCancellationRequested && IsCancellationException(ex))
				{
					stepLogger.LogError("The step was cancelled by batch/lease");
					throw;
				}

				if (stepCancellationToken.IsCancellationRequested && IsCancellationException(ex))
				{
					stepLogger.LogError(KnownLogEvents.Horde_BuildHealth_Ignore, "The step was intentionally cancelled");
					return (JobStepOutcome.Failure, JobStepState.Aborted);
				}

				stepLogger.LogError(ex, "Exception while executing step: {Ex}", ex);
				return (JobStepOutcome.Failure, JobStepState.Completed);
			}
		}

		/// <summary>
		/// Determine if the given exception was triggered due to a cancellation event
		/// </summary>
		/// <param name="ex">The exception to check</param>
		/// <returns>True if the exception is a cancellation exception</returns>
		static bool IsCancellationException(Exception ex)
		{
			if (ex is OperationCanceledException)
			{
				return true;
			}

			RpcException? rpcException = ex as RpcException;
			if (rpcException != null && rpcException.StatusCode == StatusCode.Cancelled)
			{
				return true;
			}

			return false;
		}

		internal async Task PollForStepAbortAsync(JobRpc.JobRpcClient rpcClient, JobId jobId, JobStepBatchId batchId, JobStepId stepId, CancellationTokenSource stepCancelSource, Task finishedTask, ILogger leaseLogger, CancellationToken cancellationToken)
		{
			while (!finishedTask.IsCompleted)
			{
				TimeSpan waitTime = _stepAbortPollInterval;
				try
				{
					RpcGetStepResponse res = await rpcClient.GetStepAsync(new RpcGetStepRequest(jobId, batchId, stepId), cancellationToken: cancellationToken);
					if (res.AbortRequested)
					{
						leaseLogger.LogDebug("Step was aborted by server (JobId={JobId} BatchId={BatchId} StepId={StepId})", jobId, batchId, stepId);
						await stepCancelSource.CancelAsync();
						break;
					}
				}
				catch (RpcException ex)
				{
					// Don't let a single RPC failure abort the running step as there can be intermittent errors on the server
					// For example temporary downtime or overload
					leaseLogger.LogError(ex, "Poll for step abort failed (JobId={JobId} BatchId={BatchId} StepId={StepId}). Retrying...", jobId, batchId, stepId);
					waitTime = _stepAbortPollRetryDelay;
				}

				await Task.WhenAny(Task.Delay(waitTime, cancellationToken), finishedTask);
			}
		}

		/// <summary>
		/// Initializes the executor with the batch response from the server
		/// </summary>
		public virtual async Task InitializeAsync(RpcBeginBatchResponse batch, ILogger logger, CancellationToken cancellationToken)
		{
			using TelemetrySpan span = Tracer.StartActiveSpan($"{nameof(JobExecutor)}.{nameof(InitializeAsync)}");
			
			Batch = batch;
			JobRpc = await HordeClient.CreateGrpcClientAsync<JobRpc.JobRpcClient>(cancellationToken);

			EnvVars[HordeHttpClient.HordeUrlEnvVarName] = HordeClient.ServerUrl.ToString();

			string? accessToken = await HordeClient.GetAccessTokenAsync(false, cancellationToken);
			if (accessToken != null)
			{
				EnvVars[HordeHttpClient.HordeTokenEnvVarName] = accessToken;
			}

			// Setup the agent type
			foreach (KeyValuePair<string, string> envVar in Batch.Environment)
			{
				EnvVars[envVar.Key] = envVar.Value;
			}

			// Figure out if we're running as an admin
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				if (IsUserAdministrator())
				{
					logger.LogInformation("Running as an elevated user.");
				}
				else
				{
					logger.LogInformation("Running as an restricted user.");
				}
			}

			// Get the BuildGraph arguments
			foreach (string argument in Batch.Arguments)
			{
				const string RemapAgentTypesPrefix = "-RemapAgentTypes=";
				if (argument.StartsWith(RemapAgentTypesPrefix, StringComparison.OrdinalIgnoreCase))
				{
					foreach (string map in argument.Substring(RemapAgentTypesPrefix.Length).Split(','))
					{
						int colonIdx = map.IndexOf(':', StringComparison.Ordinal);
						if (colonIdx != -1)
						{
							RemapAgentTypes[map.Substring(0, colonIdx)] = map.Substring(colonIdx + 1);
						}
					}
				}
				else if (argument.StartsWith(ScriptArgumentPrefix, StringComparison.OrdinalIgnoreCase))
				{
					ScriptFileName = argument.Substring(ScriptArgumentPrefix.Length);
				}
				else if (argument.Equals("-Preprocess", StringComparison.OrdinalIgnoreCase))
				{
					PreprocessScript = true;
				}
				else if (argument.Equals("-SavePreprocessed", StringComparison.OrdinalIgnoreCase))
				{
					SavePreprocessedScript = true;
				}
				else if (argument.Equals("-AllowTargetChanges", StringComparison.OrdinalIgnoreCase))
				{
					AllowTargetChanges = true;
				}
				else if (argument.StartsWith(TargetArgumentPrefix, StringComparison.OrdinalIgnoreCase))
				{
					Targets.Add(argument.Substring(TargetArgumentPrefix.Length));
				}
				else
				{
					AdditionalArguments.Add(argument);
				}
			}
			if (Batch.PreflightChange != 0)
			{
				AdditionalArguments.Add($"-set:PreflightChange={Batch.PreflightChange}");
			}
		}

		static bool IsUserAdministrator()
		{
			if (!OperatingSystem.IsWindows())
			{
				return false;
			}

			try
			{
				using (WindowsIdentity identity = WindowsIdentity.GetCurrent())
				{
					WindowsPrincipal principal = new WindowsPrincipal(identity);
					return principal.IsInRole(WindowsBuiltInRole.Administrator);
				}
			}
			catch
			{
				return false;
			}
		}

		/// <summary>
		/// Runs a single job step
		/// </summary>
		public virtual async Task<JobStepOutcome> RunAsync(JobStepInfo step, ILogger logger, CancellationToken cancellationToken)
		{
			if (step.Name == "Setup Build")
			{
				if (await SetupAsync(step, logger, cancellationToken))
				{
					return JobStepOutcome.Success;
				}
				else
				{
					return JobStepOutcome.Failure;
				}
			}
			else
			{
				if (await ExecuteAsync(step, logger, cancellationToken))
				{
					return JobStepOutcome.Success;
				}
				else
				{
					return JobStepOutcome.Failure;
				}
			}
		}

		/// <summary>
		/// Finalizes the executor after all steps have completed
		/// </summary>
		public virtual Task FinalizeAsync(ILogger jobLogger, CancellationToken cancellationToken)
		{
			using TelemetrySpan span = Tracer.StartActiveSpan($"{nameof(JobExecutor)}.{nameof(FinalizeAsync)}");
			return Task.CompletedTask;
		}

		static DirectoryReference GetAutomationToolDir(DirectoryReference sharedStorageDir)
		{
			return DirectoryReference.Combine(sharedStorageDir, "UAT");
		}

		/// <summary>
		/// Copies pre-built AutomationTool binaries from shared storage into the workspace
		/// </summary>
		protected void CopyAutomationTool(DirectoryReference sharedStorageDir, DirectoryReference workspaceDir, ILogger logger)
		{
			DirectoryReference buildDir = GetAutomationToolDir(sharedStorageDir);

			FileReference[] automationToolPaths = new FileReference[]
			{
				FileReference.Combine(buildDir, $"{EnginePath}/Binaries/DotNET/AutomationTool.exe"),
				FileReference.Combine(buildDir, $"{EnginePath}/Binaries/DotNET/AutomationTool/AutomationTool.dll")
			};

			if (automationToolPaths.Any(automationTool => FileReference.Exists(automationTool)))
			{
				logger.LogInformation("Copying AutomationTool binaries from '{BuildDir}' to '{WorkspaceDir}", buildDir, workspaceDir);
				foreach (FileReference sourceFile in DirectoryReference.EnumerateFiles(buildDir, "*", SearchOption.AllDirectories))
				{
					FileReference targetFile = FileReference.Combine(workspaceDir, sourceFile.MakeRelativeTo(buildDir));
					if (FileReference.Exists(targetFile))
					{
						FileUtils.ForceDeleteFile(targetFile);
					}
					DirectoryReference.CreateDirectory(targetFile.Directory);
					FileReference.Copy(sourceFile, targetFile);
				}
				CompileAutomationTool = false;
			}
		}

		/// <summary>
		/// Deletes cached BuildGraph manifests from the workspace
		/// </summary>
		protected void DeleteCachedBuildGraphManifests(DirectoryReference workspaceDir, ILogger logger)
		{
			DirectoryReference manifestDir = DirectoryReference.Combine(workspaceDir, $"{EnginePath}/Saved/BuildGraph");
			if (DirectoryReference.Exists(manifestDir))
			{
				try
				{
					FileUtils.ForceDeleteDirectoryContents(manifestDir);
				}
				catch (Exception ex)
				{
					logger.LogWarning(ex, "Unable to delete contents of {ManifestDir}", manifestDir.ToString());
				}
			}
		}

		/// <summary>
		/// Sets up the build environment for a step. Implemented by derived executors.
		/// </summary>
		protected abstract Task<bool> SetupAsync(JobStepInfo step, ILogger logger, CancellationToken cancellationToken);

		/// <summary>
		/// Executes a step. Implemented by derived executors.
		/// </summary>
		protected abstract Task<bool> ExecuteAsync(JobStepInfo step, ILogger logger, CancellationToken cancellationToken);

		const string SetupStepName = "setup";
		const string BuildGraphTempStorageDir = "BuildGraph";

		IStorageNamespace GetStorageNamespace(NamespaceId namespaceId, string? token)
		{
			return HordeClient.GetStorageNamespace(namespaceId, token);
		}

		/// <summary>
		/// Sets up a build step by exporting the BuildGraph definition and uploading it to the server
		/// </summary>
		protected virtual async Task<bool> SetupAsync(JobStepInfo step, DirectoryReference workspaceDir, bool? useP4, ILogger logger, CancellationToken cancellationToken)
		{
			FileReference definitionFile = FileReference.Combine(workspaceDir, $"{EnginePath}/Saved/Horde/Exported.json");

			StringBuilder arguments = new StringBuilder($"BuildGraph");
			if (ScriptFileName != null)
			{
				arguments.AppendArgument(ScriptArgumentPrefix, ScriptFileName);
			}
			arguments.AppendArgument("-HordeExport=", definitionFile.FullName);
			arguments.AppendArgument("-ListOnly");

			if (!AllowTargetChanges)
			{
				foreach (string target in Targets)
				{
					arguments.AppendArgument(TargetArgumentPrefix + target);
				}
			}

			//Arguments.AppendArgument("-TokenSignature=", JobId.ToString());
			foreach (string additionalArgument in AdditionalArguments)
			{
				arguments.AppendArgument(additionalArgument);
			}

			FileReference? preprocessedScriptFile = null;
			FileReference? preprocessedSchemaFile = null;
			if (PreprocessScript || SavePreprocessedScript)
			{
				preprocessedScriptFile = FileReference.Combine(workspaceDir, PreprocessedScript);
				arguments.AppendArgument("-Preprocess=", preprocessedScriptFile.FullName);

				preprocessedSchemaFile = FileReference.Combine(workspaceDir, PreprocessedSchema);
				arguments.AppendArgument("-Schema=", preprocessedSchemaFile.FullName);
			}

			int exitCode = await ExecuteAutomationToolAsync(step, workspaceDir, arguments.ToString(), useP4, logger, cancellationToken);
			if (exitCode != 0)
			{
				logger.LogError(KnownLogEvents.ExitCode, "BuildGraph setup terminated with non-zero exit code: {ExitCode}", exitCode);
				return false;
			}

			List<FileReference> buildGraphFiles = new List<FileReference>();
			buildGraphFiles.Add(definitionFile);
			if (preprocessedScriptFile != null)
			{
				buildGraphFiles.Add(preprocessedScriptFile);
			}
			if (preprocessedSchemaFile != null)
			{
				buildGraphFiles.Add(preprocessedSchemaFile);
			}

			{
				using TelemetrySpan span = Tracer.StartActiveSpan("TempStorage");
				const string Prefix = "horde.temp_storage.";
				span.SetAttribute(Prefix + "action", "upload");
				
				// Create the artifact
				ArtifactName artifactName = TempStorage.GetArtifactNameForNode(SetupStepName);
				ArtifactType artifactType = ArtifactType.StepOutput;

				RpcCreateJobArtifactRequestV2 artifactRequest = new RpcCreateJobArtifactRequestV2();
				artifactRequest.JobId = JobId.ToString();
				artifactRequest.StepId = step.StepId.ToString();
				artifactRequest.Name = artifactName.ToString();
				artifactRequest.Type = artifactType.ToString();

				RpcCreateJobArtifactResponseV2 artifact = await JobRpc.CreateArtifactV2Async(artifactRequest, cancellationToken: cancellationToken);
				ArtifactId artifactId = ArtifactId.Parse(artifact.Id);
				logger.LogInformation("Creating output artifact {ArtifactId} '{ArtifactName}' ({ArtifactType}) with ref {RefName} in namespace {NamespaceId}", artifactId, artifactName, artifactType, artifact.RefName, artifact.NamespaceId);
				DecorateSpanWithArtifact(span, Prefix, artifactRequest, artifact);

				// Write the data
				IStorageNamespace storage = GetStorageNamespace(new NamespaceId(artifact.NamespaceId), artifact.Token);

				Stopwatch timer = Stopwatch.StartNew();

				IHashedBlobRef<DirectoryNode> rootNodeRef;
				await using (IBlobWriter blobWriter = storage.CreateBlobWriter(artifact.RefName, cancellationToken: cancellationToken))
				{
					DirectoryNode buildGraphNode = new DirectoryNode();
					await buildGraphNode.AddFilesAsync(workspaceDir, buildGraphFiles, blobWriter, cancellationToken: cancellationToken);
					IHashedBlobRef<DirectoryNode> outputNodeRef = await blobWriter.WriteBlobAsync(buildGraphNode, cancellationToken);

					DirectoryNode rootNode = new DirectoryNode();
					rootNode.AddDirectory(new DirectoryEntry(BuildGraphTempStorageDir, buildGraphNode.Length, outputNodeRef));

					rootNodeRef = await blobWriter.WriteBlobAsync(rootNode, cancellationToken);
				}
				await storage.AddRefAsync(artifact.RefName, rootNodeRef, new RefOptions(), cancellationToken);

				logger.LogInformation("Upload took {Time:n1}s", timer.Elapsed.TotalSeconds);
			}

			RpcUpdateGraphRequest updateGraph = await ParseGraphUpdateAsync(definitionFile, logger, cancellationToken);
			await JobRpc.UpdateGraphAsync(updateGraph, null, null, cancellationToken);

			HashSet<string> validTargets = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
			validTargets.Add("Setup Build");
			validTargets.UnionWith(updateGraph.Groups.SelectMany(x => x.Nodes).Select(x => x.Name));
			validTargets.UnionWith(updateGraph.Aggregates.Select(x => x.Name));

			foreach (string target in Targets)
			{
				if (!validTargets.Contains(target))
				{
					logger.LogInformation("Target '{Target}' does not exist in the graph.", target);
				}
			}

			return true;
		}

		private async Task<RpcUpdateGraphRequest> ParseGraphUpdateAsync(FileReference definitionFile, ILogger logger, CancellationToken cancellationToken)
		{
			JsonSerializerOptions options = new JsonSerializerOptions();
			options.PropertyNameCaseInsensitive = true;
			options.Converters.Add(new JsonStringEnumConverter());

			ExportedGraph graph = JsonSerializer.Deserialize<ExportedGraph>(await FileReference.ReadAllBytesAsync(definitionFile, cancellationToken), options)!;

			RpcUpdateGraphRequest updateGraph = new RpcUpdateGraphRequest();
			updateGraph.JobId = JobId.ToString();

			List<string> missingAgentTypes = new List<string>();
			foreach (ExportedGroup exportedGroup in graph.Groups)
			{
				string? agentTypeName = null;
				foreach (string validAgentTypeName in exportedGroup.Types)
				{
					string? thisAgentTypeName;
					if (!RemapAgentTypes.TryGetValue(validAgentTypeName, out thisAgentTypeName))
					{
						thisAgentTypeName = validAgentTypeName;
					}

					if (Batch.ValidAgentTypes.Contains(thisAgentTypeName))
					{
						agentTypeName = thisAgentTypeName;
						break;
					}
				}

				if (agentTypeName == null)
				{
					agentTypeName = exportedGroup.Types.FirstOrDefault() ?? "Unspecified";
					foreach (ExportedNode node in exportedGroup.Nodes)
					{
						missingAgentTypes.Add($"  {node.Name} ({String.Join(", ", exportedGroup.Types)})");
					}
				}

				RpcCreateGroupRequest createGroup = new RpcCreateGroupRequest();
				createGroup.AgentType = agentTypeName;

				foreach (ExportedNode exportedNode in exportedGroup.Nodes)
				{
					RpcCreateNodeRequest createNode = new RpcCreateNodeRequest();
					createNode.Name = exportedNode.Name;
					if (exportedNode.Inputs != null)
					{
						createNode.Inputs.Add(exportedNode.Inputs);
					}
					if (exportedNode.OptionalInputs != null)
					{
						createNode.OptionalInputs.Add(exportedNode.OptionalInputs);
					}
					if (exportedNode.Outputs != null)
					{
						createNode.Outputs.Add(exportedNode.Outputs);
					}
					if (exportedNode.InputDependencies != null)
					{
						createNode.InputDependencies.Add(exportedNode.InputDependencies);
					}
					if (exportedNode.OptionalInputDependencies != null)
					{
						createNode.OptionalInputDependencies.Add(exportedNode.OptionalInputDependencies);
					}
					if (exportedNode.OrderDependencies != null)
					{
						createNode.OrderDependencies.Add(exportedNode.OrderDependencies);
					}
					createNode.RunEarly = exportedNode.RunEarly;
					createNode.AllowRetry = exportedNode.AllowRetry;
					createNode.Warnings = exportedNode.Warnings;
					createNode.Priority = (int)Priority.Normal;
					createNode.Annotations.Add(exportedNode.Annotations);
					createGroup.Nodes.Add(createNode);
				}
				updateGraph.Groups.Add(createGroup);
			}

			if (missingAgentTypes.Count > 0)
			{
				logger.LogInformation("The following nodes cannot be executed in this stream due to missing agent types:");
				foreach (string missingAgentType in missingAgentTypes)
				{
					logger.LogInformation("{Node}", missingAgentType);
				}
			}

			foreach (ExportedAggregate exportedAggregate in graph.Aggregates)
			{
				RpcCreateAggregateRequest createAggregate = new RpcCreateAggregateRequest();
				createAggregate.Name = exportedAggregate.Name;
				createAggregate.Nodes.AddRange(exportedAggregate.Nodes);
				updateGraph.Aggregates.Add(createAggregate);
			}

			foreach (ExportedLabel exportedLabel in graph.Labels)
			{
				RpcCreateLabelRequest createLabel = new RpcCreateLabelRequest();
				if (exportedLabel.Name != null)
				{
					createLabel.DashboardName = exportedLabel.Name;
				}
				if (exportedLabel.Category != null)
				{
					createLabel.DashboardCategory = exportedLabel.Category;
				}
				if (exportedLabel.UgsBadge != null)
				{
					createLabel.UgsName = exportedLabel.UgsBadge;
				}
				if (exportedLabel.UgsProject != null)
				{
					createLabel.UgsProject = exportedLabel.UgsProject;
				}
				createLabel.Change = exportedLabel.Change;
				createLabel.RequiredNodes.AddRange(exportedLabel.RequiredNodes);
				createLabel.IncludedNodes.AddRange(exportedLabel.IncludedNodes);
				updateGraph.Labels.Add(createLabel);
			}

			Dictionary<string, ExportedNode> nameToNode = graph.Groups.SelectMany(x => x.Nodes).ToDictionary(x => x.Name, x => x);
			foreach (ExportedBadge exportedBadge in graph.Badges)
			{
				RpcCreateLabelRequest createLabel = new RpcCreateLabelRequest();
				createLabel.UgsName = exportedBadge.Name;

				string? project = exportedBadge.Project;
				if (project != null && project.StartsWith("//", StringComparison.Ordinal))
				{
					int nextIdx = project.IndexOf('/', 2);
					if (nextIdx != -1)
					{
						nextIdx = project.IndexOf('/', nextIdx + 1);
						if (nextIdx != -1 && !project.Substring(nextIdx).Equals("/...", StringComparison.Ordinal))
						{
							createLabel.UgsProject = project.Substring(nextIdx + 1);
						}
					}
				}

				if (exportedBadge.Change == Batch.Change || exportedBadge.Change == 0)
				{
					createLabel.Change = RpcLabelChange.Current;
				}
				else if (exportedBadge.Change == Batch.CodeChange)
				{
					createLabel.Change = RpcLabelChange.Code;
				}
				else
				{
					logger.LogWarning("Badge is set to display for changelist {Change}. This is neither the current changelist ({CurrentChange}) or the current code changelist ({CurrentCodeChange}).", exportedBadge.Change, Batch.Change, Batch.CodeChange);
				}

				if (exportedBadge.Dependencies != null)
				{
					createLabel.RequiredNodes.AddRange(exportedBadge.Dependencies.Split(new char[] { ';' }, StringSplitOptions.RemoveEmptyEntries));

					HashSet<string> dependencies = new HashSet<string>();
					foreach (string requiredNode in createLabel.RequiredNodes)
					{
						GetRecursiveDependencies(requiredNode, nameToNode, dependencies);
					}
					createLabel.IncludedNodes.AddRange(dependencies);
				}
				updateGraph.Labels.Add(createLabel);
			}

			foreach (ExportedArtifact exportedArtifact in graph.Artifacts)
			{
				RpcCreateGraphArtifactRequest createArtifact = new RpcCreateGraphArtifactRequest();

				createArtifact.Name = exportedArtifact.Name;
				createArtifact.Type = exportedArtifact.Type ?? String.Empty;
				createArtifact.Description = exportedArtifact.Description ?? String.Empty;
				createArtifact.BasePath = exportedArtifact.BasePath ?? String.Empty;
				createArtifact.Keys.AddRange(exportedArtifact.Keys);
				createArtifact.Metadata.AddRange(exportedArtifact.Metadata);
				createArtifact.NodeName = exportedArtifact.NodeName;
				createArtifact.OutputName = exportedArtifact.OutputName;

				updateGraph.Artifacts.Add(createArtifact);
			}

			return updateGraph;
		}

		private static void GetRecursiveDependencies(string name, Dictionary<string, ExportedNode> nameToNode, HashSet<string> dependencies)
		{
			ExportedNode? node;
			if (nameToNode.TryGetValue(name, out node) && dependencies.Add(node.Name))
			{
				foreach (string inputDependency in node.InputDependencies)
				{
					GetRecursiveDependencies(inputDependency, nameToNode, dependencies);
				}
			}
		}

		/// <summary>
		/// Executes a build step by running BuildGraph with the given workspace
		/// </summary>
		protected async Task<bool> ExecuteAsync(JobStepInfo step, DirectoryReference workspaceDir, bool? useP4, ILogger logger, CancellationToken cancellationToken)
		{
			StringBuilder arguments = new StringBuilder("BuildGraph");
			if (PreprocessScript)
			{
				FileReference localPreprocessedScript = FileReference.Combine(workspaceDir, PreprocessedScript);
				arguments.AppendArgument(ScriptArgumentPrefix, localPreprocessedScript.FullName);

				FileReference localPreprocessedSchema = FileReference.Combine(workspaceDir, PreprocessedSchema);
				arguments.AppendArgument("-ImportSchema=", localPreprocessedSchema.FullName);

				ArtifactName artifactName = TempStorage.GetArtifactNameForNode(SetupStepName);

				RpcGetJobArtifactRequest artifactRequest = new RpcGetJobArtifactRequest();
				artifactRequest.JobId = JobId.ToString();
				artifactRequest.StepId = step.StepId.ToString();
				artifactRequest.Name = artifactName.ToString();
				artifactRequest.Type = ArtifactType.StepOutput.ToString();

				RpcGetJobArtifactResponse artifact = await JobRpc.GetArtifactAsync(artifactRequest, cancellationToken: cancellationToken);

				NamespaceId namespaceId = new NamespaceId(artifact.NamespaceId);
				RefName refName = new RefName(artifact.RefName);

				logger.LogInformation("Reading preprocessed script from {NamespaceId}:{RefName}", namespaceId, refName);

				IStorageNamespace storage = GetStorageNamespace(namespaceId, artifact.Token);

				DirectoryNode node = await storage.ReadRefTargetAsync<DirectoryNode>(refName, cancellationToken: cancellationToken);
				DirectoryNode? buildGraphDir = await node.TryOpenDirectoryAsync(BuildGraphTempStorageDir, cancellationToken: cancellationToken);
				if (buildGraphDir != null)
				{
					await buildGraphDir.ExtractAsync(new DirectoryInfo(workspaceDir.FullName), new ExtractOptions(), logger, cancellationToken);
					logger.LogInformation("Copying preprocessed script from {BuildGraphFolderName} into {OutputDir}", BuildGraphTempStorageDir, workspaceDir);
				}
				else
				{
					logger.LogInformation("Bundle has no {BuildGraphFolderName} folder; not copying any files", BuildGraphTempStorageDir);
				}
			}
			else if (ScriptFileName != null)
			{
				arguments.AppendArgument(ScriptArgumentPrefix, ScriptFileName);
			}

			if (!AllowTargetChanges)
			{
				foreach (string target in Targets)
				{
					arguments.AppendArgument(TargetArgumentPrefix + target);
				}
			}

			arguments.AppendArgument("-SingleNode=", step.Name);
			//			Arguments.AppendArgument("-TokenSignature=", JobId.ToString());

			foreach (string additionalArgument in AdditionalArguments)
			{
				if (!PreprocessScript || !additionalArgument.StartsWith("-set:", StringComparison.OrdinalIgnoreCase))
				{
					arguments.AppendArgument(additionalArgument);
				}
			}

			bool result = await ExecuteWithTempStorageAsync(step, workspaceDir, arguments.ToString(), useP4, logger, cancellationToken);
			return result;
		}
		
		/// <summary>
		/// Creates and uploads an artifact for a job step
		/// </summary>
		protected async Task CreateArtifactAsync(JobStepId stepId, ArtifactName name, ArtifactType type, DirectoryReference baseDir, IReadOnlyList<FileInfo> files, ILogger logger, CancellationToken cancellationToken)
		{
			try
			{
				RpcCreateJobArtifactRequestV2 artifactRequest = new RpcCreateJobArtifactRequestV2();
				artifactRequest.JobId = JobId.ToString();
				artifactRequest.StepId = stepId.ToString();
				artifactRequest.Name = name.ToString();
				artifactRequest.Type = type.ToString();

				long totalSize = files.Sum(x => x.Length);

				RpcCreateJobArtifactResponseV2 artifact = await JobRpc.CreateArtifactV2Async(artifactRequest, cancellationToken: cancellationToken);
				ArtifactId artifactId = ArtifactId.Parse(artifact.Id);
				logger.LogInformation("Creating artifact {ArtifactId} '{ArtifactName}' ({ArtifactType}) from {NumFiles:n0} files ({TotalSize:n1}mb). Namespace {NamespaceId}, ref {RefName} ({Link})", artifactId, name, type, files.Count, totalSize / (1024.0 * 1024.0), artifact.NamespaceId, artifact.RefName, $"{HordeClient.ServerUrl}/api/v1/storage/{artifact.NamespaceId}/refs/{artifact.RefName}");

				IStorageNamespace storage = GetStorageNamespace(new NamespaceId(artifact.NamespaceId), artifact.Token);
				Stopwatch timer = Stopwatch.StartNew();

				IHashedBlobRef<DirectoryNode> rootRef;
				await using (IBlobWriter blobWriter = storage.CreateBlobWriter(new RefName(artifact.RefName)))
				{
					try
					{
						DirectoryNode dir = new DirectoryNode();
						await dir.AddFilesAsync(baseDir, files, blobWriter, progress: new UpdateStatsLogger(logger), cancellationToken: cancellationToken);
						rootRef = await blobWriter.WriteBlobAsync(dir, cancellationToken: cancellationToken);
					}
					catch (Exception ex)
					{
						Logger.LogInformation(ex, "Error uploading files for artifact {ArtifactId}", artifactId);
						throw;
					}
				}
				await storage.AddRefAsync(new RefName(artifact.RefName), rootRef, cancellationToken: cancellationToken);

				timer.Stop();
				logger.LogInformation("Uploaded artifact {ArtifactId} in {Time:n1}s ({Rate:n1}mb/s, {RateMbps:n1}mbps)", artifactId, timer.Elapsed.TotalSeconds, totalSize / (timer.Elapsed.TotalSeconds * 1024.0 * 1024.0), (totalSize * 8.0) / (timer.Elapsed.TotalSeconds * 1024.0 * 1024.0));
			}
			catch (Exception ex)
			{
				logger.LogInformation(ex, "Error creating artifact '{Type}'", type);
			}
		}

		private async Task<bool> ExecuteWithTempStorageAsync(JobStepInfo step, DirectoryReference workspaceDir, string arguments, bool? useP4, ILogger logger, CancellationToken cancellationToken)
		{
			DirectoryReference manifestDir = DirectoryReference.Combine(workspaceDir, $"{EnginePath}/Saved/BuildGraph");
			DirectoryReference unrealRootDir = GetUnrealRootDirectoryFromWorkspace(workspaceDir);

			// Create the mapping of tag names to file sets
			Dictionary<string, HashSet<FileReference>> tagNameToFileSet = new Dictionary<string, HashSet<FileReference>>();

			// Read all the input tags for this node, and build a list of referenced input storage blocks
			HashSet<TempStorageBlockRef> inputStorageBlocks = new HashSet<TempStorageBlockRef>();
			foreach (string input in step.Inputs)
			{
				int slashIdx = input.IndexOf('/', StringComparison.Ordinal);
				if (slashIdx == -1)
				{
					logger.LogError("Missing slash from node input: {Input}", input);
					return false;
				}

				string nodeName = input.Substring(0, slashIdx);
				string tagName = input.Substring(slashIdx + 1);

				TempStorageTagManifest fileList = await TempStorage.RetrieveTagAsync(HordeClient, JobId, step.StepId, nodeName, tagName, manifestDir, logger, cancellationToken);
				tagNameToFileSet[tagName] = fileList.ToFileSet(unrealRootDir);
				inputStorageBlocks.UnionWith(fileList.Blocks);
			}

			HashSet<string> optionalInputs = new(step.OptionalInputs);
			foreach (string optionalInput in optionalInputs)
			{
				int slashIdx = optionalInput.IndexOf('/', StringComparison.Ordinal);
				if (slashIdx == -1)
				{
					// Bad data (malformed format) is a programming error and should fail the step,
					// unlike retrieval failures which are expected when the producing step has failed.
					logger.LogError("Missing slash from node optional input: {OptionalInput}", optionalInput);
					return false;
				}

				string nodeName = optionalInput.Substring(0, slashIdx);
				string tagName = optionalInput.Substring(slashIdx + 1);

				TempStorageTagManifest fileList;
				try
				{
					fileList = await TempStorage.RetrieveTagAsync(HordeClient, JobId, step.StepId, nodeName, tagName, manifestDir, logger, cancellationToken);
				}
				catch (Exception ex) when (ex is not OperationCanceledException)
				{
					if (Environment.GetEnvironmentVariable("UE_HORDE_LOG_OPTIONAL_INPUT_FAILURE_AS_WARNING") == "1")
					{
						logger.LogWarning(ex, "Failed to read optional input {OptionalInput} from temp storage: {Message}. The producing step may have failed. Continuing without it", optionalInput, ex.Message);
					}
					else
					{
						logger.LogInformation(ex, "Failed to read optional input {OptionalInput} from temp storage: {Message}. The producing step may have failed. Continuing without it", optionalInput, ex.Message);
					}
					continue;
				}
				tagNameToFileSet[tagName] = fileList.ToFileSet(unrealRootDir);
				inputStorageBlocks.UnionWith(fileList.Blocks);
			}

			// Read the manifests for all the input storage blocks
			Dictionary<TempStorageBlockRef, TempStorageBlockManifest> inputManifests = new Dictionary<TempStorageBlockRef, TempStorageBlockManifest>();

			{
				using TelemetrySpan span = Tracer.StartActiveSpan("TempStorage");
				const string Prefix = "horde.temp_storage.";
				span.SetAttribute(Prefix + "action", "download");
				
				Stopwatch timer = Stopwatch.StartNew();
				span.SetAttribute(Prefix + "blocks", inputStorageBlocks.Count);
				foreach (TempStorageBlockRef inputStorageBlock in inputStorageBlocks)
				{
					TempStorageBlockManifest manifest = await TempStorage.RetrieveBlockAsync(HordeClient, JobId, step.StepId, inputStorageBlock.NodeName, inputStorageBlock.OutputName, workspaceDir, manifestDir, logger, cancellationToken);
					inputManifests[inputStorageBlock] = manifest;
				}
				span.SetAttribute(Prefix + "size", inputManifests.Sum(x => x.Value.GetTotalSize()));
				logger.LogInformation("Download took {Time:n1}s", timer.Elapsed.TotalSeconds);
			}

			// Read all the input storage blocks, keeping track of which block each file came from
			Dictionary<string, (TempStorageFile, TempStorageBlockRef)> inputPathToSource = new Dictionary<string, (TempStorageFile, TempStorageBlockRef)>(FileReference.Comparer);
			foreach ((TempStorageBlockRef inputStorageBlock, TempStorageBlockManifest inputManifest) in inputManifests)
			{
				foreach (TempStorageFile inputFile in inputManifest.Files)
				{
					(TempStorageFile File, TempStorageBlockRef BlockRef) source;
					if (!inputPathToSource.TryGetValue(inputFile.RelativePath, out source))
					{
						inputPathToSource.Add(inputFile.RelativePath, (inputFile, inputStorageBlock));
					}
					else if (!inputFile.Equals(source.File) && !TempStorage.IsDuplicateBuildProduct(inputFile.ToFileReference(workspaceDir)))
					{
						logger.LogInformation("File '{File}' was produced by {InputBlock} and {CurrentBlock}", inputFile.RelativePath, inputStorageBlock.ToString(), source.BlockRef.ToString());
					}
				}
			}

			// Run UAT
			int exitCode = await ExecuteAutomationToolAsync(step, workspaceDir, arguments, useP4, logger, cancellationToken);
			if (exitCode != 0)
			{
				logger.LogError(KnownLogEvents.ExitCode, "AutomationTool (UAT) terminated with non-zero exit code: {ExitCode}", exitCode);
				return false;
			}

			// Read all the output manifests
			foreach (string tagName in step.OutputNames)
			{
				FileReference tagFileListLocation = TempStorage.GetTagManifestLocation(manifestDir, step.Name, tagName);
				logger.LogInformation("Reading local file list from {File}", tagFileListLocation.FullName);

				TempStorageTagManifest fileList = TempStorageTagManifest.Load(tagFileListLocation);
				tagNameToFileSet[tagName] = fileList.ToFileSet(unrealRootDir);
			}

			// Check that none of the inputs have been clobbered
			Dictionary<string, string> modifiedFiles = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
			foreach (TempStorageFile file in inputManifests.Values.SelectMany(x => x.Files))
			{
				string? message;
				if (!modifiedFiles.ContainsKey(file.RelativePath) && !file.Compare(unrealRootDir, out message))
				{
					modifiedFiles.Add(file.RelativePath, message);
				}
			}
			if (modifiedFiles.Count > 0)
			{
				const int NumFiles = 50;

				string modifiedFileList = "";
				if (modifiedFiles.Count < NumFiles)
				{
					modifiedFileList = String.Join("\n", modifiedFiles.Select(x => x.Value));
				}
				else
				{
					modifiedFileList = String.Join("\n", modifiedFiles.Take(NumFiles).Select(x => x.Value));
					modifiedFileList += $"{Environment.NewLine}...and {modifiedFiles.Count - NumFiles} more.";
				}

				logger.LogInformation("Build product(s) from a previous step have been modified:\n{FileList}", modifiedFileList);
			}

			// Determine all the output files which are required to be copied to temp storage (because they're referenced by nodes in another agent)
			HashSet<FileReference> referencedOutputFiles = new HashSet<FileReference>();
			foreach (int publishOutput in step.PublishOutputs)
			{
				string tagName = step.OutputNames[publishOutput];
				referencedOutputFiles.UnionWith(tagNameToFileSet[tagName]);
			}

			// Find a block name for all new outputs
			Dictionary<FileReference, string> outputFileToBlockName = new Dictionary<FileReference, string>();
			for (int idx = 0; idx < step.OutputNames.Count; idx++)
			{
				string tagName = step.OutputNames[idx];

				string outputNameWithoutHash = tagName.TrimStart('#');
				bool isDefaultOutput = outputNameWithoutHash.Equals(step.Name, StringComparison.OrdinalIgnoreCase);

				HashSet<FileReference> files = tagNameToFileSet[tagName];
				foreach (FileReference file in files)
				{
					if (file.IsUnderDirectory(unrealRootDir))
					{
						if (isDefaultOutput)
						{
							if (!outputFileToBlockName.ContainsKey(file))
							{
								outputFileToBlockName[file] = "";
							}
						}
						else
						{
							string? blockName;
							if (outputFileToBlockName.TryGetValue(file, out blockName) && blockName.Length > 0)
							{
								outputFileToBlockName[file] = $"{blockName}+{outputNameWithoutHash}";
							}
							else
							{
								outputFileToBlockName[file] = outputNameWithoutHash;
							}
						}
					}
				}
			}

			// Invert the dictionary to make a mapping of storage block to the files each contains
			Dictionary<string, HashSet<FileReference>> outputStorageBlockToFiles = new Dictionary<string, HashSet<FileReference>>();
			foreach (KeyValuePair<FileReference, string> pair in outputFileToBlockName)
			{
				HashSet<FileReference>? files;
				if (!outputStorageBlockToFiles.TryGetValue(pair.Value, out files))
				{
					files = new HashSet<FileReference>();
					outputStorageBlockToFiles.Add(pair.Value, files);
				}
				files.Add(pair.Key);
			}

			// Write all the storage blocks, and update the mapping from file to storage block
			{
				using TelemetrySpan span = Tracer.StartActiveSpan("TempStorage");
				const string Prefix = "horde.temp_storage.";
				span.SetAttribute(Prefix + "action", "upload");
				
				// Create the artifact
				RpcCreateJobArtifactRequestV2 artifactRequest = new RpcCreateJobArtifactRequestV2();
				artifactRequest.JobId = JobId.ToString();
				artifactRequest.StepId = step.StepId.ToString();
				artifactRequest.Name = TempStorage.GetArtifactNameForNode(step.Name).ToString();
				artifactRequest.Type = ArtifactType.StepOutput.ToString();

				long totalSize = outputFileToBlockName.Keys.Sum(x => x.ToFileInfo().Length);

				RpcCreateJobArtifactResponseV2 artifact = await JobRpc.CreateArtifactV2Async(artifactRequest, cancellationToken: cancellationToken);
				ArtifactId artifactId = ArtifactId.Parse(artifact.Id);
				logger.LogInformation("Creating artifact {ArtifactId} '{ArtifactName}' ({ArtifactType}) from {NumFiles:n0} files ({TotalSize:n1}mb). Namespace: {NamespaceId}, ref {RefName} ({RefUrl})", artifactId, artifactRequest.Name, ArtifactType.StepOutput, outputFileToBlockName.Count, totalSize / (1024.0 * 1024.0), artifact.NamespaceId, artifact.RefName, $"{HordeClient.ServerUrl.ToString().TrimEnd('/')}/api/v1/storage/{artifact.NamespaceId}/refs/{artifact.RefName}");
				DecorateSpanWithArtifact(span, Prefix, artifactRequest, artifact);

				IStorageNamespace storage = GetStorageNamespace(new NamespaceId(artifact.NamespaceId), artifact.Token);

				// Upload the data
				Stopwatch timer = Stopwatch.StartNew();

				IHashedBlobRef<DirectoryNode> outputNodeRef;
				await using (IBlobWriter blobWriter = storage.CreateBlobWriter(artifact.RefName, cancellationToken: cancellationToken))
				{
					DirectoryNode outputNode = new DirectoryNode();

					// Create all the output blocks
					Dictionary<FileReference, TempStorageBlockRef> outputFileToStorageBlock = new Dictionary<FileReference, TempStorageBlockRef>();
					foreach (KeyValuePair<string, HashSet<FileReference>> pair in outputStorageBlockToFiles)
					{
						TempStorageBlockRef outputBlock = new TempStorageBlockRef(step.Name, pair.Key);
						foreach (FileReference file in pair.Value)
						{
							outputFileToStorageBlock.Add(file, outputBlock);
						}
						if (pair.Value.Any(x => referencedOutputFiles.Contains(x)))
						{
							outputNode.AddDirectory(await TempStorage.ArchiveBlockAsync(manifestDir, step.Name, pair.Key, unrealRootDir, pair.Value.ToArray(), blobWriter, logger, cancellationToken));
						}
					}

					// Create all the output tags
					foreach (string outputName in step.OutputNames)
					{
						HashSet<FileReference> files = tagNameToFileSet[outputName];

						HashSet<TempStorageBlockRef> storageBlocks = new HashSet<TempStorageBlockRef>();
						foreach (FileReference file in files)
						{
							TempStorageBlockRef? storageBlock;
							if (outputFileToStorageBlock.TryGetValue(file, out storageBlock))
							{
								storageBlocks.Add(storageBlock);
							}
						}

						outputNode.AddFile(await TempStorage.ArchiveTagAsync(manifestDir, step.Name, outputName, unrealRootDir, files, storageBlocks.ToArray(), blobWriter, logger, cancellationToken));
					}

					outputNodeRef = await blobWriter.WriteBlobAsync(outputNode, cancellationToken: cancellationToken);
				}

				// Write the final node
				await storage.AddRefAsync(artifact.RefName, outputNodeRef, new RefOptions(), cancellationToken: cancellationToken);
				logger.LogInformation("Uploaded artifact {ArtifactId} in {Time:n1}s ({Rate:n1}mb/s, {RateMbps:n1}mbps)", artifactId, timer.Elapsed.TotalSeconds, totalSize / (timer.Elapsed.TotalSeconds * 1024.0 * 1024.0), (totalSize * 8.0) / (timer.Elapsed.TotalSeconds * 1024.0 * 1024.0));
			}

			// Create all the named artifacts. TODO: Merge this with regular temp storage artifacts?
			logger.LogInformation("Uploading {NumArtifacts} artifacts", step.Artifacts.Count);
			foreach (RpcCreateGraphArtifactRequest graphArtifact in step.Artifacts)
			{
				logger.LogInformation("Uploading artifact {Name} using output {Output}", graphArtifact.Name, graphArtifact.OutputName);

				HashSet<FileReference>? files;
				if (!tagNameToFileSet.TryGetValue(graphArtifact.OutputName, out files))
				{
					logger.LogWarning("Missing output fileset for artifact {Name} (output={OutputName})", graphArtifact.Name, graphArtifact.OutputName);
					continue;
				}

				{
					using TelemetrySpan span = Tracer.StartActiveSpan("Artifact");
					const string Prefix = "horde.artifact.";
					span.SetAttribute(Prefix + "action", "upload");
					span.SetAttribute(Prefix + "name", graphArtifact.Name);
					span.SetAttribute(Prefix + "type", graphArtifact.Type);
					
					// Create the artifact
					RpcCreateJobArtifactRequestV2 artifactRequest = new RpcCreateJobArtifactRequestV2();
					artifactRequest.JobId = JobId.ToString();
					artifactRequest.StepId = step.StepId.ToString();
					artifactRequest.Name = graphArtifact.Name;
					artifactRequest.Type = graphArtifact.Type;
					artifactRequest.Description = graphArtifact.Description;
					artifactRequest.Keys.AddRange(graphArtifact.Keys);
					artifactRequest.Metadata.AddRange(graphArtifact.Metadata);

					List<FileInfo> fileList = files.Select(x => x.ToFileInfo()).ToList();
					long totalSize = fileList.Sum(x => x.Length);

					RpcCreateJobArtifactResponseV2 artifact = await JobRpc.CreateArtifactV2Async(artifactRequest, cancellationToken: cancellationToken);
					ArtifactId artifactId = ArtifactId.Parse(artifact.Id);
					logger.LogInformation("Creating artifact {ArtifactId} '{ArtifactName}' ({ArtifactType}) from {NumFiles:n0} files ({TotalSize:n1}mb). Namespace {NamespaceId}, ref {RefName} ({RefUrl})", artifactId, artifactRequest.Name, ArtifactType.StepOutput, fileList.Count, totalSize / (1024.0 * 1024.0), artifact.NamespaceId, artifact.RefName, $"{HordeClient.ServerUrl.ToString().TrimEnd('/')}/api/v1/storage/{artifact.NamespaceId}/refs/{artifact.RefName}");

					DecorateSpanWithArtifact(span, Prefix, artifactRequest, artifact);

					IStorageNamespace storage = GetStorageNamespace(new NamespaceId(artifact.NamespaceId), artifact.Token);

					// Upload the data
					Stopwatch timer = Stopwatch.StartNew();

					IHashedBlobRef<DirectoryNode> outputNodeRef;
					await using (IBlobWriter blobWriter = storage.CreateBlobWriter(artifact.RefName, cancellationToken: cancellationToken))
					{
						DirectoryNode outputNode = new DirectoryNode();

						DirectoryReference baseDir = DirectoryReference.Combine(workspaceDir, graphArtifact.BasePath);
						if (DirectoryReference.Exists(baseDir))
						{
							await outputNode.AddFilesAsync(baseDir, files, blobWriter, cancellationToken: cancellationToken);
						}
						else
						{
							logger.LogWarning("Base path for artifact {ArtifactName} does not exist ({Path})", graphArtifact.Name, baseDir);
						}

						outputNodeRef = await blobWriter.WriteBlobAsync(outputNode, cancellationToken: cancellationToken);
					}

					// Write the final node
					await storage.AddRefAsync(artifact.RefName, outputNodeRef, new RefOptions(), cancellationToken: cancellationToken);

					timer.Stop();
					logger.LogInformation("Uploaded artifact {ArtifactId} in {Time:n1}s ({Rate:n1}mb/s, {RateMbps:n1}mbps)", artifactId, timer.Elapsed.TotalSeconds, totalSize / (timer.Elapsed.TotalSeconds * 1024.0 * 1024.0), (totalSize * 8.0) / (timer.Elapsed.TotalSeconds * 1024.0 * 1024.0));
				}
			}

			return true;
		}

		/// <summary>
		/// Executes the Unreal AutomationTool (UAT) with the given arguments
		/// </summary>
		protected async Task<int> ExecuteAutomationToolAsync(JobStepInfo step, DirectoryReference workspaceDir, string? arguments, bool? useP4, ILogger logger, CancellationToken cancellationToken)
		{
			int result;
			using TelemetrySpan span = Tracer.StartActiveSpan("BuildGraph");
			span.SetAttribute("horde.job.step.id", step.StepId.ToString());
			span.SetAttribute("horde.job.step.name", step.Name);

			if (!CompileAutomationTool)
			{
				arguments += " -NoCompile";
			}

			if (useP4 is false)
			{
				arguments += " -NoP4";
			}
			
			span.SetAttribute("horde.args", arguments);

			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				result = await ExecuteCommandAsync(step, workspaceDir, Environment.GetEnvironmentVariable("COMSPEC") ?? "cmd.exe", $"/C \"\"{workspaceDir}\\{EnginePath}\\Build\\BatchFiles\\RunUAT.bat\" {arguments}\"", span, logger, cancellationToken);
			}
			else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
			{
				string args = $"\"{workspaceDir}/{EnginePath}/Build/BatchFiles/RunUAT.sh\" {arguments}";

				if (JobOptions.UseWine is true)
				{
					args = $"\"{workspaceDir}/{EnginePath}/Build/BatchFiles/RunWineUAT.sh\" {arguments}";
				}

				result = await ExecuteCommandAsync(step, workspaceDir, "/bin/bash", args, span, logger, cancellationToken);
			}
			else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
			{
				result = await ExecuteCommandAsync(step, workspaceDir, "/bin/sh", $"\"{workspaceDir}/{EnginePath}/Build/BatchFiles/RunUAT.sh\" {arguments}", span, logger, cancellationToken);
			}
			else
			{
				throw new Exception("Unsupported platform");
			}

			CompileAutomationTool = false;
			return result;
		}

		/// <summary>
		/// Discovers and loads ignore patterns from the workspace into the given parser
		/// </summary>
		protected async Task LoadIgnorePatternsAsync(LogEventParser parser, DirectoryReference workspaceDir, ILogger logger)
		{
			List<FileReference> patternFiles;
			if (Batch.EnableExpandedIgnorePatterns)
			{
				patternFiles = LogEventParser.DiscoverIgnorePatternFiles(workspaceDir, EnginePath);
			}
			else
			{
				patternFiles = LogEventParser.DiscoverLegacyIgnorePatternFiles(workspaceDir, EnginePath);
			}

			await parser.ReadIgnorePatternsAsync(patternFiles);

			// Read additional config-specified ignore pattern files (expanded mode only)
			if (Batch.EnableExpandedIgnorePatterns)
			{
				HashSet<FileReference> discoveredFiles = new HashSet<FileReference>(patternFiles);
				List<FileReference> configFiles = ValidateConfigPatternFiles(workspaceDir, discoveredFiles, logger);
				if (configFiles.Count > 0)
				{
					await parser.ReadIgnorePatternsAsync(configFiles);
				}
			}

			logger.LogInformation("Loaded {PatternCount} ignore patterns from workspace", parser.IgnorePatterns.Count);
		}

		List<FileReference> ValidateConfigPatternFiles(DirectoryReference workspaceDir, HashSet<FileReference> discoveredFiles, ILogger logger)
		{
			string workspacePrefix = workspaceDir.FullName.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
			List<FileReference> validFiles = new List<FileReference>();

			foreach (string relativePath in Batch.IgnorePatternFiles)
			{
				string normalizedPath = Path.GetFullPath(Path.Combine(workspaceDir.FullName, relativePath));
				if (!normalizedPath.StartsWith(workspacePrefix, StringComparison.OrdinalIgnoreCase))
				{
					logger.LogWarning("Ignoring config-specified pattern file outside workspace: {Path}", relativePath);
					continue;
				}

				FileReference configPatternFile = new FileReference(normalizedPath);
				if (discoveredFiles.Contains(configPatternFile))
				{
					logger.LogDebug("Config-specified file already discovered by auto-scan, skipping: {File}", configPatternFile);
					continue;
				}

				validFiles.Add(configPatternFile);
			}

			return validFiles;
		}

		static FileReference GetCleanupScript(DirectoryReference workspaceDir)
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				return FileReference.Combine(workspaceDir, "Cleanup.bat");
			}
			else
			{
				return FileReference.Combine(workspaceDir, "Cleanup.sh");
			}
		}

		static FileReference GetLeaseCleanupScript(DirectoryReference workspaceDir)
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				return FileReference.Combine(workspaceDir, "CleanupLease.bat");
			}
			else
			{
				return FileReference.Combine(workspaceDir, "CleanupLease.sh");
			}
		}

		internal IReadOnlyDictionary<string, string> GetEnvVars()
		{
			return new Dictionary<string, string>(EnvVars);
		}

		/// <summary>
		/// Runs the lease cleanup script in the workspace if present
		/// </summary>
		protected static async Task ExecuteLeaseCleanupScriptAsync(DirectoryReference workspaceDir, ILogger logger)
		{
			using (LogParser parser = new LogParser(logger))
			{
				FileReference leaseCleanupScript = GetLeaseCleanupScript(workspaceDir);
				await ExecuteCleanupScriptAsync(leaseCleanupScript, parser, logger);
			}
		}

		/// <summary>
		/// Terminates processes matching the configured termination rules
		/// </summary>
		protected async Task TerminateProcessesAsync(TerminateCondition condition, ILogger logger)
		{
			try
			{
				await TerminateProcessHelper.TerminateProcessesAsync(condition, WorkingDir, ProcessesToTerminate, logger, CancellationToken.None);
			}
			catch (Exception ex)
			{
				logger.LogWarning(ex, "Exception while terminating processes: {Message}", ex.Message);
			}
		}

		static async Task ExecuteCleanupScriptAsync(FileReference cleanupScript, LogParser filter, ILogger logger)
		{
			if (FileReference.Exists(cleanupScript))
			{
				filter.WriteLine($"Executing cleanup script: {cleanupScript}");

				string fileName;
				string arguments;
				if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
				{
					fileName = "C:\\Windows\\System32\\Cmd.exe";
					arguments = $"/C \"{cleanupScript}\"";
				}
				else
				{
					fileName = "/bin/sh";
					arguments = $"\"{cleanupScript}\"";
				}

				try
				{
					using (CancellationTokenSource cancellationSource = new CancellationTokenSource())
					{
						cancellationSource.CancelAfter(TimeSpan.FromSeconds(30.0));
						await ExecuteProcessAsync(fileName, arguments, null, filter, logger, cancellationSource.Token);
					}
				}
				catch (OperationCanceledException)
				{
					filter.WriteLine("Cleanup script did not complete within allotted time. Aborting.");
				}
				finally
				{
					try
					{
						FileUtils.ForceDeleteFile(cleanupScript);
					}
					catch (Exception ex)
					{
						logger.LogWarning(KnownLogEvents.Horde_BuildHealth_Ignore, ex, "Failed to delete cleanup script {CleanupScript}", cleanupScript.FullName);
					}
				}
			}
		}

		static async Task<int> ExecuteProcessAsync(string fileName, string arguments, IReadOnlyDictionary<string, string>? newEnvironment, LogParser filter, ILogger logger, CancellationToken cancellationToken)
		{
			logger.LogInformation("Executing {File} {Arguments}", fileName.QuoteArgument(), arguments);

			using (CancellationTokenSource cancellationSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken))
			{
				using (ManagedProcessGroup processGroup = new ManagedProcessGroup())
				{
					using (ManagedProcess process = new ManagedProcess(processGroup, fileName, arguments, null, newEnvironment, null, ProcessPriorityClass.Normal))
					{
						bool hasExited = false;
						try
						{
							async Task WaitForExitOrCancelAsync()
							{
								await process.WaitForExitAsync(cancellationToken);
								hasExited = true;
								cancellationSource.CancelAfter(TimeSpan.FromSeconds(60.0));
							}

							Task cancelTask = WaitForExitOrCancelAsync();
							await process.CopyToAsync((buffer, offset, length) => filter.WriteData(buffer.AsMemory(offset, length)), 4096, cancellationSource.Token);
							await cancelTask;
						}
						catch (OperationCanceledException) when (hasExited)
						{
							logger.LogWarning("Process exited without closing output pipes; they may have been inherited by a child process that is still running.");
						}
						return process.ExitCode;
					}
				}
			}
		}

		/// <summary>
		/// Execute a process inside a Linux container
		/// </summary>
		/// <param name="arguments">Arguments</param>
		/// <param name="mountDirs">Directories to mount inside the container for read/write</param>
		/// <param name="newEnvironment">Environment variables</param>
		/// <param name="filter">Log parser</param>
		/// <param name="logger">Logger</param>
		/// <param name="cancellationToken">Cancellation token for the call</param>
		/// <returns></returns>
		/// <exception cref="Exception"></exception>
		async Task<int> ExecuteProcessInContainerAsync(string arguments, List<DirectoryReference> mountDirs, IReadOnlyDictionary<string, string>? newEnvironment, LogParser filter, ILogger logger, CancellationToken cancellationToken)
		{
			if (!RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
			{
				throw new Exception("Only Linux is supported for executing a process inside a container");
			}

			if (String.IsNullOrEmpty(JobOptions.Container.ImageUrl))
			{
				throw new Exception("Image URL is null or empty");
			}

			// Default to "docker" executable and assume it's available on the path
			string executable = JobOptions.Container.ContainerEngineExecutable ?? "docker";

			uint linuxUid = LinuxInterop.getuid();
			uint linuxGid = LinuxInterop.getgid();

			List<string> containerArgs = new()
				{
					"run",
					"--tty", // Allocate a pseudo-TTY
					$"--name horde-job-{JobId}-{BatchId}", // Better name for debugging purposes
					"--rm", // Ensure container is removed after run
					$"--user {linuxUid}:{linuxGid}" // Run container as current user (important for mounted dirs)
				};

			foreach (DirectoryReference mountDir in mountDirs)
			{
				containerArgs.Add($"--volume {mountDir.FullName}:{mountDir.FullName}:rw");
			}

			if (newEnvironment != null)
			{
				string envFilePath = Path.GetTempFileName();
				StringBuilder sb = new();
				foreach ((string key, string value) in newEnvironment)
				{
					sb.AppendLine($"{key}={value}");
				}
				await File.WriteAllTextAsync(envFilePath, sb.ToString(), cancellationToken);
				containerArgs.Add("--env-file=" + envFilePath);
			}

			if (!String.IsNullOrEmpty(JobOptions.Container.ExtraArguments))
			{
				containerArgs.Add(JobOptions.Container.ExtraArguments);
			}

			containerArgs.Add(JobOptions.Container.ImageUrl);
			string containerArgStr = String.Join(' ', containerArgs);
			arguments = containerArgStr + " " + arguments;

			logger.LogInformation("Executing {File} {Arguments} in container", executable.QuoteArgument(), arguments);

			// Skip forwarding of env vars as they are explicitly set above as arguments to container run
			return await ExecuteProcessAsync(executable, arguments, new Dictionary<string, string>(), filter, logger, cancellationToken);
		}

		private static List<DirectoryReference> GetContainerMountDirs(DirectoryReference workspaceDir, IReadOnlyDictionary<string, string> envVars)
		{
			List<DirectoryReference> dirs = new();
			dirs.Add(workspaceDir);
			if (envVars.TryGetValue("UE_SDKS_ROOT", out string? autoSdkDirPath))
			{
				dirs.Add(new DirectoryReference(autoSdkDirPath));
			}
			return dirs;
		}

		private static string ConformEnvironmentVariableName(string name)
		{
			if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				// Non-windows platforms don't allow dashes in variable names. The engine platform layer substitutes underscores for them.
				return name.Replace('-', '_');
			}
			return name;
		}

		async Task<int> ExecuteCommandAsync(JobStepInfo step, DirectoryReference workspaceDir, string fileName, string arguments, TelemetrySpan buildGraphSpan, ILogger jobLogger, CancellationToken cancellationToken)
		{
			DirectoryReference unrealRootDir = GetUnrealRootDirectoryFromWorkspace(workspaceDir);

			// Method for expanding environment variable properties related to this step
			Dictionary<string, string> properties = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
			properties.Add("RootDir", unrealRootDir.FullName);

			// Combine all the supplied environment variables together
			Dictionary<string, string> newEnvVars = new Dictionary<string, string>(StringComparer.Ordinal);
			foreach (KeyValuePair<string, string> envVar in EnvVars)
			{
				newEnvVars[ConformEnvironmentVariableName(envVar.Key)] = StringUtils.ExpandProperties(envVar.Value, properties);
			}
			foreach (KeyValuePair<string, string> envVar in step.EnvVars)
			{
				newEnvVars[ConformEnvironmentVariableName(envVar.Key)] = StringUtils.ExpandProperties(envVar.Value, properties);
			}
			foreach (KeyValuePair<string, string> envVar in step.Credentials)
			{
				newEnvVars[ConformEnvironmentVariableName(envVar.Key)] = envVar.Value;
			}

			// Add all the other Horde-specific variables
			newEnvVars["IsBuildMachine"] = "1";

			DirectoryReference logDir = DirectoryReference.Combine(workspaceDir, $"{EnginePath}/Programs/AutomationTool/Saved/Logs");
			FileUtils.ForceDeleteDirectoryContents(logDir);
			newEnvVars["uebp_LogFolder"] = logDir.FullName;

			DirectoryReference telemetryDir = DirectoryReference.Combine(workspaceDir, $"{EnginePath}/Programs/AutomationTool/Saved/Telemetry");
			FileUtils.ForceDeleteDirectoryContents(telemetryDir);
			newEnvVars["UE_TELEMETRY_DIR"] = telemetryDir.FullName;

			DirectoryReference testDataDir = DirectoryReference.Combine(workspaceDir, $"{EnginePath}/Programs/AutomationTool/Saved/TestData");
			FileUtils.ForceDeleteDirectoryContents(testDataDir);
			newEnvVars["UE_TESTDATA_DIR"] = testDataDir.FullName;

			FileReference graphUpdateFile = FileReference.Combine(workspaceDir, $"{EnginePath}/Saved/Horde/Graph.json");
			FileUtils.ForceDeleteFile(graphUpdateFile);
			newEnvVars["UE_HORDE_GRAPH_UPDATE"] = graphUpdateFile.FullName;

			try
			{
				// TODO: These are AWS specific, this should be extended to handle more clouds or for licensees to be able to set these
				newEnvVars["UE_HORDE_AVAILABILITY_ZONE"] = Amazon.Util.EC2InstanceMetadata.AvailabilityZone ?? "";
				newEnvVars["UE_HORDE_REGION"] = Amazon.Util.EC2InstanceMetadata.Region?.DisplayName ?? "";
			}
			catch
			{
			}

			newEnvVars["UE_HORDE_LEASEID"] = LeaseId.ToString();
			newEnvVars["UE_HORDE_JOBID"] = JobId.ToString();
			newEnvVars["UE_HORDE_BATCHID"] = BatchId.ToString();
			newEnvVars["UE_HORDE_STEPID"] = step.StepId.ToString();
			newEnvVars["UE_HORDE_STREAMID"] = Batch.StreamId;

			// Enable structured logging output
			newEnvVars["UE_LOG_JSON_TO_STDOUT"] = "1";

			// Pass the location of the cleanup script to the job
			FileReference cleanupScript = GetCleanupScript(workspaceDir);
			newEnvVars["UE_HORDE_CLEANUP"] = cleanupScript.FullName;

			// Pass the location of the cleanup script to the job
			FileReference leaseCleanupScript = GetLeaseCleanupScript(workspaceDir);
			newEnvVars["UE_HORDE_LEASE_CLEANUP"] = leaseCleanupScript.FullName;

			// Set up the shared working dir
			newEnvVars["UE_HORDE_SHARED_DIR"] = DirectoryReference.Combine(WorkingDir, "Saved").FullName;

			// Disable the S3DDC. This is technically a Fortnite-specific setting, but affects a large number of branches and is hard to retrofit. 
			// Setting here for now, since it's likely to be temporary.
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				newEnvVars["UE-S3DataCachePath"] = "None";
			}

			// Log all the environment variables to the log
			HashSet<string> credentialKeys = new HashSet<string>(step.Credentials.Keys.Select(credentialKey => ConformEnvironmentVariableName(credentialKey)), StringComparer.OrdinalIgnoreCase);
			credentialKeys.Add(ConformEnvironmentVariableName(HordeHttpClient.HordeTokenEnvVarName));

			foreach (KeyValuePair<string, string> envVar in newEnvVars.OrderBy(x => x.Key))
			{
				string value = "[redacted]";
				if (!credentialKeys.Contains(envVar.Key))
				{
					value = envVar.Value;
				}
				jobLogger.LogInformation("Setting env var: {Key}={Value}", envVar.Key, value);
			}

			// Add all the old environment variables into the list
			foreach (object? envVar in Environment.GetEnvironmentVariables())
			{
				System.Collections.DictionaryEntry entry = (System.Collections.DictionaryEntry)envVar!;
				string key = entry.Key.ToString()!;
				if (!newEnvVars.ContainsKey(key))
				{
					newEnvVars[key] = entry.Value!.ToString()!;
				}
			}

			// Clear out the telemetry directory
			if (DirectoryReference.Exists(telemetryDir))
			{
				FileUtils.ForceDeleteDirectoryContents(telemetryDir);
			}

			int exitCode;
			using (LogParser filter = new LogParser(jobLogger))
			{
				await LoadIgnorePatternsAsync(filter, workspaceDir, jobLogger);
				await ExecuteCleanupScriptAsync(cleanupScript, filter, jobLogger);
				try
				{
					if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux) && JobOptions?.Container?.Enabled is true)
					{
						List<DirectoryReference> mountDirs = GetContainerMountDirs(workspaceDir, newEnvVars);
						exitCode = await ExecuteProcessInContainerAsync(arguments, mountDirs, newEnvVars, filter, jobLogger, cancellationToken);
					}
					else
					{
						exitCode = await ExecuteProcessAsync(fileName, arguments, newEnvVars, filter, jobLogger, cancellationToken);
					}
				}
				finally
				{
					await ExecuteCleanupScriptAsync(cleanupScript, filter, jobLogger);
				}
				filter.Flush();
			}

			DirectoryInfo telemetryDirInfo = telemetryDir.ToDirectoryInfo();
			if (telemetryDirInfo.Exists)
			{
				List<FileInfo> telemetryFiles = new List<FileInfo>();
				try
				{
					await CreateTelemetryFilesAsync(step.Name, telemetryDirInfo, telemetryFiles, buildGraphSpan, jobLogger, cancellationToken);
				}
				catch (Exception ex)
				{
					jobLogger.LogInformation(ex, "Unable to parse/upload telemetry files: {Message}", ex.Message);
				}

				await CreateArtifactAsync(step.StepId, TempStorage.GetArtifactNameForNode(step.Name), ArtifactType.StepTrace, workspaceDir, telemetryFiles, jobLogger, cancellationToken);

				foreach (FileInfo telemetryFile in telemetryFiles)
				{
					FileUtils.ForceDeleteFile(telemetryFile);
				}
			}

			DirectoryInfo testDataDirInfo = testDataDir.ToDirectoryInfo();
			if (testDataDirInfo.Exists)
			{
				List<FileInfo> testDataFiles = new List<FileInfo>();

				Dictionary<string, object> combinedTestData = new Dictionary<string, object>();
				foreach (FileInfo testDataFile in testDataDirInfo.EnumerateFiles("*.json", SearchOption.AllDirectories))
				{
					jobLogger.LogInformation("Reading test data {TestDataFile}", testDataFile);
					testDataFiles.Add(testDataFile);

					TestData testData;
					using (FileStream stream = testDataFile.OpenRead())
					{
						JsonSerializerOptions options = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
						testData = (await JsonSerializer.DeserializeAsync<TestData>(stream, options, cancellationToken))!;
					}

					foreach (TestDataItem item in testData.Items)
					{
						if (combinedTestData.ContainsKey(item.Key))
						{
							jobLogger.LogWarning("Key '{Key}' already exists - ignoring", item.Key);
						}
						else
						{
							jobLogger.LogDebug("Adding data with key '{Key}'", item.Key);
							combinedTestData.Add(item.Key, item.Data);
						}
					}
				}

				jobLogger.LogInformation("Found {NumResults} test results", combinedTestData.Count);
				await UploadTestDataAsync(step.StepId, combinedTestData);

				await CreateArtifactAsync(step.StepId, TempStorage.GetArtifactNameForNode(step.Name), ArtifactType.StepTestData, workspaceDir, testDataFiles, jobLogger, cancellationToken);
			}

			DirectoryInfo logDirInfo = logDir.ToDirectoryInfo();
			if (logDirInfo.Exists)
			{
				FileInfo[] artifactFiles = logDirInfo.GetFiles("*", SearchOption.AllDirectories);
				await CreateArtifactAsync(step.StepId, TempStorage.GetArtifactNameForNode(step.Name), ArtifactType.StepSaved, workspaceDir, artifactFiles, jobLogger, cancellationToken);

				foreach (FileReference reportFile in artifactFiles.Select(x => new FileReference(x)).Where(x => x.HasExtension(".report.json")))
				{
					try
					{
						await CreateReportAsync(step.StepId, reportFile, jobLogger);
					}
					catch (Exception ex)
					{
						jobLogger.LogWarning("Unable to upload report: {Message}", ex.Message);
					}
				}
			}

			if (FileReference.Exists(graphUpdateFile))
			{
				jobLogger.LogInformation("Parsing graph update from {File}", graphUpdateFile);

				RpcUpdateGraphRequest updateGraph = await ParseGraphUpdateAsync(graphUpdateFile, jobLogger, cancellationToken);
				foreach (RpcCreateGroupRequest group in updateGraph.Groups)
				{
					jobLogger.LogInformation("  AgentType: {Name}", group.AgentType);
					foreach (RpcCreateNodeRequest node in group.Nodes)
					{
						jobLogger.LogInformation("    Node: {Name}", node.Name);
					}
				}

				await JobRpc.UpdateGraphAsync(updateGraph, null, null, cancellationToken);

				IEnumerable<RpcCreateNodeRequest> allNodes = updateGraph.Groups.SelectMany(x => x.Nodes);
				HashSet<string> publishOutputNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
				publishOutputNames.UnionWith(allNodes.SelectMany(x => x.Inputs));
				publishOutputNames.UnionWith(allNodes.SelectMany(x => x.OptionalInputs));
				foreach (string publishOutputName in publishOutputNames)
				{
					jobLogger.LogInformation("Required output: {OutputName}", publishOutputName);
				}
				foreach (string stepOutputName in step.OutputNames)
				{
					jobLogger.LogInformation("Output from current step: {OutputName}", stepOutputName);
				}

				for (int idx = 0; idx < step.OutputNames.Count; idx++)
				{
					if (!step.PublishOutputs.Contains(idx) && publishOutputNames.Contains(step.OutputNames[idx]))
					{
						jobLogger.LogInformation("Added new publish output on {Name}", step.OutputNames[idx]);
						step.PublishOutputs.Add(idx);
					}
				}
			}

			return exitCode;
		}

		async Task CreateTelemetryFilesAsync(string stepName, DirectoryInfo telemetryDir, List<FileInfo> telemetryFiles, TelemetrySpan buildGraphSpan, ILogger jobLogger, CancellationToken cancellationToken)
		{
			List<TraceEventList> telemetryList = new List<TraceEventList>();
			foreach (FileInfo telemetryFile in telemetryDir.EnumerateFiles("*.json"))
			{
				FileReference telemetryFileRef = new FileReference(telemetryFile);

				jobLogger.LogInformation("Reading telemetry from {File}", telemetryFileRef);
				byte[] data = await FileReference.ReadAllBytesAsync(telemetryFileRef, cancellationToken);

				TraceEventList telemetry = JsonSerializer.Deserialize<TraceEventList>(data.AsSpan())!;
				if (telemetry.Spans.Count > 0)
				{
					string defaultServiceName = telemetryFileRef.GetFileNameWithoutAnyExtensions();
					foreach (TraceEvent span in telemetry.Spans)
					{
						span.Service ??= defaultServiceName;
					}
					telemetryList.Add(telemetry);
				}

				telemetryFiles.Add(telemetryFile);
			}

			List<TraceEvent> telemetrySpans = new List<TraceEvent>();
			foreach (TraceEventList telemetryEventList in telemetryList.OrderBy(x => x.Spans.First().StartTime).ThenBy(x => x.Spans.Last().FinishTime))
			{
				foreach (TraceEvent span in telemetryEventList.Spans)
				{
					if (span.FinishTime - span.StartTime > TimeSpan.FromMilliseconds(1.0))
					{
						span.Index = telemetrySpans.Count;
						telemetrySpans.Add(span);
					}
				}
			}

			if (telemetrySpans.Count > 0)
			{
				TraceSpan rootSpan = new TraceSpan();
				rootSpan.Name = stepName;

				Stack<TraceSpan> stack = new Stack<TraceSpan>();
				stack.Push(rootSpan);

				List<TraceSpan> spansWithExplicitParent = new List<TraceSpan>();
				Dictionary<string, TraceSpan> spans = new Dictionary<string, TraceSpan>(StringComparer.OrdinalIgnoreCase);

				foreach (TraceEvent traceEvent in telemetrySpans.OrderBy(x => x.StartTime).ThenByDescending(x => x.FinishTime).ThenBy(x => x.Index))
				{
					TraceSpan newSpan = new TraceSpan();
					newSpan.Name = traceEvent.Name;
					newSpan.SpanId = traceEvent.SpanId;
					newSpan.ParentId = traceEvent.ParentId;
					newSpan.Service = traceEvent.Service;
					newSpan.Resource = traceEvent.Resource;
					newSpan.Start = traceEvent.StartTime.UtcTicks;
					newSpan.Finish = traceEvent.FinishTime.UtcTicks;
					if (traceEvent.Metadata != null && traceEvent.Metadata.Count > 0)
					{
						newSpan.Properties = traceEvent.Metadata;
					}

					if (!String.IsNullOrEmpty(newSpan.SpanId))
					{
						spans.Add(newSpan.SpanId, newSpan);
					}

					TraceSpan stackTop = stack.Peek();
					while (stack.Count > 1 && newSpan.Start >= stackTop.Finish)
					{
						stack.Pop();
						stackTop = stack.Peek();
					}

					if (String.IsNullOrEmpty(newSpan.ParentId))
					{
						if (stack.Count > 1 && newSpan.Finish > stackTop.Finish)
						{
							jobLogger.LogInformation("Trace event name='{Name}', service'{Service}', resource='{Resource}' has invalid finish time ({SpanFinish} < {StackFinish})", newSpan.Name, newSpan.Service, newSpan.Resource, newSpan.Finish, stackTop.Finish);
							newSpan.Finish = stackTop.Finish;
						}

						stackTop.AddChild(newSpan);
					}
					else
					{
						spansWithExplicitParent.Add(newSpan);
					}

					stack.Push(newSpan);
				}

				foreach (TraceSpan span in spansWithExplicitParent)
				{
					if (span.ParentId != null && spans.TryGetValue(span.ParentId, out TraceSpan? parentSpan))
					{
						parentSpan.AddChild(span);
					}
					else
					{
						Logger.LogInformation("Parent {ParentId} of span {SpanId} was not found.", span.ParentId, span.SpanId);
					}
				}

				rootSpan.Start = rootSpan.Children!.First().Start;
				rootSpan.Finish = rootSpan.Children!.Last().Finish;

				FileReference traceFile = FileReference.Combine(new DirectoryReference(telemetryDir), "Trace.json");
				using (FileStream stream = FileReference.Open(traceFile, FileMode.Create))
				{
					JsonSerializerOptions options = new JsonSerializerOptions { DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull };
					await JsonSerializer.SerializeAsync(stream, rootSpan, options, cancellationToken);
				}
				telemetryFiles.Add(traceFile.ToFileInfo());

				CreateTracingData(buildGraphSpan, rootSpan);
			}
		}

		private async Task CreateReportAsync(JobStepId stepId, FileReference reportFile, ILogger logger)
		{
			byte[] data = await FileReference.ReadAllBytesAsync(reportFile);

			JsonSerializerOptions options = new JsonSerializerOptions();
			options.PropertyNameCaseInsensitive = true;
			options.Converters.Add(new JsonStringEnumConverter());

			ReportData report = JsonSerializer.Deserialize<ReportData>(data, options)!;
			if (String.IsNullOrEmpty(report.Name))
			{
				logger.LogWarning("Missing 'Name' field in report data");
				return;
			}

			if (String.IsNullOrEmpty(report.Content) && !String.IsNullOrEmpty(report.FileName))
			{
				FileReference reportDataFile = FileReference.Combine(reportFile.Directory, report.FileName);
				if (!FileReference.Exists(reportDataFile))
				{
					logger.LogWarning("Cannot find file '{File}' referenced by report data", report.FileName);
					return;
				}
				report.Content = await FileReference.ReadAllTextAsync(reportDataFile);
			}

			RpcCreateReportRequest request = new RpcCreateReportRequest();
			request.JobId = JobId.ToString();
			request.BatchId = BatchId.ToString();
			request.StepId = stepId.ToString();
			request.Scope = report.Scope;
			request.Placement = report.Placement;
			request.Name = report.Name;
			request.Content = report.Content;
			request.Message = report.Message ?? String.Empty;
			request.Severity = report.Severity ?? String.Empty;
			await JobRpc.CreateReportAsync(request);
		}

		private TelemetrySpan CreateTracingData(TelemetrySpan parent, TraceSpan span)
		{
			TelemetrySpan newSpan = Tracer.StartSpan(span.Name ?? "TraceJsonUnknown", SpanKind.Internal, parent, startTime: new DateTime(span.Start, DateTimeKind.Utc));
			newSpan.SetAttribute("service.name", span.Service);
			newSpan.SetAttribute("resource.name_ot", span.Resource);

			if (span.Properties != null)
			{
				foreach (KeyValuePair<string, string> pair in span.Properties)
				{
					newSpan.SetAttribute(pair.Key, pair.Value);
				}
			}
			if (span.Children != null)
			{
				foreach (TraceSpan child in span.Children)
				{
					CreateTracingData(newSpan, child);
				}
			}

			newSpan.End(new DateTime(span.Finish, DateTimeKind.Utc));
			return newSpan;
		}
		
		private static void DecorateSpanWithArtifact(TelemetrySpan span, string prefix, RpcCreateJobArtifactRequestV2 request, RpcCreateJobArtifactResponseV2 response)
		{
			span.SetAttribute(prefix + "artifact.name", request.Name);
			span.SetAttribute(prefix + "artifact.type", request.Type);
			span.SetAttribute(prefix + "artifact.job_id", request.JobId);
			span.SetAttribute(prefix + "artifact.step_id", request.StepId);
			
			span.SetAttribute(prefix + "artifact.id", response.Id);
			span.SetAttribute(prefix + "artifact.ref_name", response.RefName);
			span.SetAttribute(prefix + "artifact.namespace_id", response.NamespaceId);
		}

		/// <summary>
		/// Uploads test data for a job step to the server
		/// </summary>
		public async Task UploadTestDataAsync(JobStepId jobStepId, IEnumerable<KeyValuePair<string, object>> pairs)
		{
			if (pairs.Any())
			{
				using (AsyncClientStreamingCall<RpcUploadTestDataRequest, RpcUploadTestDataResponse> call = JobRpc.UploadTestData())
				{
					foreach (KeyValuePair<string, object> pair in pairs)
					{
						JsonSerializerOptions options = new JsonSerializerOptions();
						options.PropertyNameCaseInsensitive = true;
						options.Converters.Add(new JsonStringEnumConverter());
						byte[] data = JsonSerializer.SerializeToUtf8Bytes(pair.Value, options);

						RpcUploadTestDataRequest request = new RpcUploadTestDataRequest();
						request.JobId = JobId.ToString();
						request.JobStepId = jobStepId.ToString();
						request.Key = pair.Key;
						request.Value = Google.Protobuf.ByteString.CopyFrom(data);
						await call.RequestStream.WriteAsync(request);
					}
					await call.RequestStream.CompleteAsync();
					await call.ResponseAsync;
				}
			}
		}

		/// <summary>
		/// Gets the Unreal root directory from a workspace directory
		/// </summary>
		protected DirectoryReference GetUnrealRootDirectoryFromWorkspace(DirectoryReference workspaceDir)
		{
			// The Unreal root directory is the parent of the Engine directory
			return DirectoryReference.Combine(workspaceDir, EnginePath).ParentDirectory!;
		}
	}

	/// <summary>
	/// Factory interface for creating job executors
	/// </summary>
	public interface IJobExecutorFactory
	{
		/// <summary>
		/// Name of the executor type
		/// </summary>
		string Name { get; }

		/// <summary>
		/// Creates a new executor instance for the given workspace configuration
		/// </summary>
		Task<JobExecutor> CreateExecutorAsync(RpcAgentWorkspace workspaceInfo, RpcAgentWorkspace? autoSdkWorkspaceInfo, JobExecutorOptions options, CancellationToken cancellationToken);
	}

	static class JobExecutorHelpers
	{
		public static async Task ExecuteAsync(IHordeClient hordeClient, DirectoryReference workingDir, LeaseId leaseId, ExecuteJobTask executeTask, IEnumerable<IJobExecutorFactory> executorFactories, DriverSettings driverSettings, ILogger logger, CancellationToken cancellationToken)
		{
			// Create an executor for this job
			string executorName = String.IsNullOrEmpty(executeTask.JobOptions.Executor) ? driverSettings.Executor : executeTask.JobOptions.Executor;

			IJobExecutorFactory? executorFactory = executorFactories.FirstOrDefault(x => x.Name.Equals(executorName, StringComparison.OrdinalIgnoreCase));
			if (executorFactory == null)
			{
				throw new InvalidOperationException($"Unable to find executor '{executorName}'");
			}

			JobId jobId = JobId.Parse(executeTask.JobId);
			JobStepBatchId batchId = JobStepBatchId.Parse(executeTask.BatchId);

			if (driverSettings.UseWine)
			{
				executeTask.JobOptions.UseWine = true;
			}
			JobExecutorOptions options = new (hordeClient, workingDir, driverSettings.ProcessesToTerminate, jobId, batchId, leaseId, executeTask.JobOptions);

			using JobExecutor executor = await executorFactory.CreateExecutorAsync(executeTask.Workspace, executeTask.AutoSdkWorkspace, options, cancellationToken);
			await executor.ExecuteAsync(logger, cancellationToken);
		}
	}
}
