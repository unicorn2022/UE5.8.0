# Release Notes

## 2026-03-23

* EpicGames.OIDC: Prevent refresh token loss when process shuts down during token rotation (51945923)
* Horde Dashboard - Add "Open in Build Storage Tool" button for Cloud Artifacts (51945444)
* Pass job ID to label hover card ChangeButton for context menu
  ChangeButton in LabelHoverCard was missing job ID, causing Open Job
  Details and Copy Job links to point to /job/undefined. Pass jobId for
  current build and prev.jobId for previous build CL buttons. (51928097)
* [Horde Dashboard] Ensure unnamed pools aren't displayed in pool view filter results (51922279)
* Fix JobView label layout when no label columns defined (51919471)
* Label hover card with server-side details endpoint
  Add GET /api/v1/jobs/{jobId}/labels/{labelIndex}/details endpoint
  returning label state, failing steps with log events, build health
  issues (with Slack/Jira links), previous build status, and
  cross-stream merge flow in a single response.
  New shared LabelBadge and LabelHoverCard frontend components used
  across JobView, JobViewIncremental, JobViewAll, and JobDetailLabels.
  Compact badge sizing for non-failure labels, hover row highlights,
  inline issue modal on click, and configurable external issue URL. (51915812)
* Ignore file actions of type "unknown" when unshelving changes in PerforceMaterializer (51892769)
* Add template filter to job search (51889166)
* Sending step messages to databricks. (51864061)
* Disable expensive C# analyzers
  CA5390 and CA5403 are very expensive analyzers to run under some conditions, and provide very little value.
  This is especially noticable when building UGS with analyzers. Build time for that single .csproj went from 117s -> 19s when disabled.
  A full Horde.sln build 160s -> 92s. (51820071)
* Remove unused using directive in JobsController (51789024)
* Add optimized label details endpoint for hover card
  New GET /api/v1/jobs/{jobId}/labels/{labelIndex}/details endpoint that
  aggregates steps, log events, issues, previous build, and cross-stream
  merge chain in a single response. Replaces N+1 per-hover API calls. (51785105)
* Move Robomerge config from server config to global config for dynamic reload via globals.json (51782440)
* Add RoboMerge merge graph service to Build plugin
  Parses branchmap JSON files from Perforce and serves merge topology
  via REST API. Includes ticking service with change detection, merge
  chain computation, reachability index, and stream-level authorization. (51771667)
* EpicGames.OIDC: Use new name for token state file
  Recent bug fixes to this library fixed a race condition where long running processes would save stale token data. By changing the name of this file, any apps using the outdated version of this library can no longer interfere. This adds an initial browser login to authenticate but after that, new and old can better co-exist. MacOS provider is not affected since it addresses token data directly by provider name (no saving/loading of file with mixed provider state). (51761382)
* Simplify session conflict handling to use debug logging + batched Slack report
  Replace the unused auto-quarantine implementation with a lightweight Redis sorted set: the first 20 mismatches per agent log at error level subsequent ones log at debug. (51708749)
* Cloud Storage - Implemented the wrapper for listing file contents using ICloudStorage (51674981)
* Swap registration order of exception logger vs handler
  Surfaces the error messages in ExceptionLoggingMiddleware, which previously got silenced. This so we can control the format of "Unhandled exception" log lines. Once verified, we'll disable the generic ASP.NET based logger. (51674205)
* Add more generic xcode parsing to find pre-compile errors arising from the .xcodeproj files. (51673579)
* Always register BisectService for DI even when bisection is feature-flagged off
  BisectService already checks DisableBisection at runtime in TickAsync,
  so it is safe to register unconditionally. This ensures the controller
  and related services remain available for dependency injection. (51669557)
* EpicGames.OIDC: Use SingleInstanceMutex to avoid cross-thread lock release (51668299)

## 2026-03-12

* Feature flag bisection - disabled by default (51640744)
* EpicGames.OIDC: Fix concurrent refresh token handling
  Fix a race condition where multiple concurrent processes (UBT, UGS, HordeCmd) could race on OIDC refresh token rotation, causing invalid_grant errors when one process uses a token already rotated by another.
  * Always read the refresh token fresh from the backing store rather than caching it in memory
  * Add a machine-wide file lock around the refresh operation so only one process refreshes at a time
  * Retry with a re-read token on invalid_grant as a fallback for older binaries without the lock
  * Add tests for refresh retry logic and WindowsTokenStore DPAPI round-trip (51629776)
* Add ResolveSecret to TokenConfig.ClientId and ClientSecret (51622248)
* Fix error 'Change has shelved files associated with it and can't be deleted' during conforms (51622061)
* Handle missing log blobs gracefully in step history events
  When log blob data has been garbage collected from storage, the
  GetEventsAsync and GetBatchEventsAsync endpoints now catch
  StorageException and skip the unavailable event instead of
  returning a 500 error to the dashboard. (51592668)
* Make tool deployment retention limit configurable
  Adds a MaxDeployments property to ToolConfig (defaulting to 5, preserving existing behavior) to allow per-tool configuration of how many deployments are retained. (51589319)
* Set a default telemetry store for compute (51549153)
* Increase MongoDB batch size for agent reads to reduce getMore errors
  Set a batch size of 10000 when fetching agent documents from MongoDB to minimize getMore commands on secondary reads. The agents collection is updated frequently, making multi-batch cursor reads prone to errors when reading from replicas. (51504525)
* Fix build warnings in Storage and Build plugins
  Add missing XML doc comments to CollectionStatsService and GcMetricsCollection.
  Remove unused using directive in CollectionStatsService.
  Make BuildStatusResponse static in StorageController (CA1822).
  Suppress CA1505 on CreateOrUpdateWorkflowThreadAsync in SlackNotificationSink. (51465969)
* Only register TunnelService when tunnel port is configured
  Also make StopAsync log exceptions as warnings. (51465212)
* Fix Analyze build configuration having no effect when building via .csproj
  * .sln-based builds set the platform as "Any CPU" but direct .csproj builds do not unless you explicitly set this
  * This causes certain C# builds to fail reporting warnings from analyze configuration
  * Fix by conditioning only on Configuration, since Platform is irrelevant for these settings. This matches how the property is consumed in CI and locally. (51464192)

## 2026-03-05

* EpicGames.LogMatchers: Fix a static analysis warning by specifying ordinal string comparison when checking if a log line contains the callstack indicator. (51456221)
* Group crash callstack lines into single events in log matchers (51451038)
* Add Storage Dashboard frontend view (51433116)
* Add Storage Dashboard backend: GC metrics, collection stats, API endpoint (51423509)
* Add per-stream RetentionOptions with safety valves for job and artifact expiration (51411407)
* Fix crash in GetIssueEventsAsync when logIds contains invalid values (51397115)
* Add jobId, batchId, stepId, workflowId, fingerprintType params to debug create-test-issue endpoint (51393039)
* Add fingerprintType parameter to debug create-test-issue endpoint (51390872)
* Add debug endpoint for creating test issues with suspects (51379057)
* Suppress Slack @-mentions for low-context issue types (Hashed, Gauntlet) (51373817)
* Fix getMore failures in AgentCollection.FindAsync (51340180)
* Detect edge server change in PerforceMaterializer to force clean re-sync (51338317)
* EpicGames.Perforce: Fix unshelve not invalidating deleted files in workspace tracking (51298759)
* Fix empty pool notification spam, Only fire OnJobScheduled once per batch instead of every 5s queue refresh, preventing 73K+ entries accumulating in Redis batch queue (51292704)
* Fix OnJobScheduled handler leak in NotificationService.DisposeAsync (51292392)
* Improve truncation of over-sized log messages (51292253)
* Update Build Health documentation with comprehensive workflow reference and examples (51273164)
* Config loading fault tolerance: missing base references and diamond include scoping (51269740)

## 2026-02-26

* Make agent session conflict detection disabled as default (51262410)
* Invite users nominated to fix build health issue to Slack triage channel
  Add InviteUsersAsync call after posting nomination message in CreateOrUpdateWorkflowThreadAsync, ensuring the user receives a notification.
  Suppress already_in_channel warning in non-admin invite path, matching existing admin path behavior, to reduce noise in logs. (51241288)
* Add session conflict detection and auto-quarantine to agents
  Detect agents experiencing repeated session ID mismatches (e.g. cloned VMs sharing an agent ID) and automatically quarantine them after a configurable threshold. Conflicts are tracked in Redis using a sliding-window sorted set; once the threshold is reached the agent is disabled with a diagnostic comment listing the offending IPs. Suppresses log spam for already-quarantined agents and surfaces them in the daily Slack agent report. (51232325)
* Make log and HTTP blob uploading more robust
  - Use a bare SocketsHttpHandler for upload redirect clients instead of the Horde-server-tuned Polly retry/timeout policies
  - Prevent ServerLogger.DisposeAsync from propagating exceptions thrown by the tail task or blob writer
  - Fix redirect uploads to presigned URLs sending an empty body on retry by creating a fresh StreamContent and resetting stream position per attempt (51224648)
* Change conform check when assigning a job task to have up to date free disk space for the agent
  On lease completion disk free space property of the agent is updated to become available for the next lease assignment.
  Implements a minimal ISystemMetrics for macOS to support disk usage. (51212055)
* Limit size of log messages in server
  Too large log messages can cause issues with external log handling pipelines (containerd/FluentBit/OTel etc). This change prevents too deep destructuring of exceptions, a common source of over-sized messages.
  Also inserts a JSON formatter for Serilog that caps log entries by size, replacing them with a compact JSON stub with the bare minimum information. (51211255)
* Optimize FindIssuesV2 to use aggregate for distinct issue IDs and fetch spans only for paginated results (51210130)
* Tune the step events for job step history (51208415)
* Update TestIgnorePattern_StructuredProperties for LogValue
  Test now validates IgnoredLine as a LogValue with nested properties
  instead of top-level state entries. (51183653)
* Fix breadcrumbs on tools view (51172192)
* Add batch log events API and inline error/warning display in step history
  New POST /api/v1/logs/events endpoint for fetching events across multiple logs
  in a single request with severity filtering and count limiting. Step history
  panel shows expandable error/warning events inline with log links. (51164657)
* Add time-based persistent flushing to ServerLogger
  ServerLogger previously only flushed buffered log data to blob storage when the buffer exceeded 1 MB or at end-of-stream, leaving slow-streaming jobs without visible log updates for several minutes when the tail mechanism was inactive. (51159252)
* Fix preflight commit ID parsing (51158668)
* Fix up and unify change list preflight display (51157338)
* Add batch log events API and step history error/warning display
  Add POST /api/v1/logs/events endpoint for fetching events across multiple logs in a single request.
  Display error/warning events inline in step history table with expand/collapse buttons. (51157086)
* Ensure perforce service check (51155190)
* Fix preflight config check warning overflow (51154487)
* Fix compilation warnings (51100244)
* Broaden exception handling in StreamsController batch commit retrieval
  Catch all exceptions (not just PerforceException) when retrieving batch commits so failures are logged and reported via MissingCommitsHeader regardless of the error type. (51096190)
* Add include support to ComputeConfig for pool configuration
  Allows pool definitions to be split across multiple included files
  via the standard ConfigInclude mechanism. (51069581)
* Resolve diamond include dependencies in config system
  Detect and deduplicate diamond includes in the config preprocessor.
  When file A includes B and C and both B and C include D, D is now
  merged only once per [ConfigIncludeRoot] scope. A warning is emitted
  identifying both the first includer and the duplicate requester.
  Add ResolveWithOverridesAsync to IConfigService so callers can get
  the fully resolved GlobalConfig (with plugins) from a set of file
  overrides. Used by the pool migration diff endpoint to compare
  proposed pool configs against the current database.
  Propagate config warnings through preflight validation, pool-diff,
  and their corresponding dashboard UIs. (51064451)
* Add IgnorePatterns documentation (51054716)
* Batch fetch agent sessions from Redis when loading multiple agents
  Replace sequential per-agent Redis GET calls in AgentCollection with a single MGET, which reduces Redis round-trips. (51051204)

## 2026-02-19

* Handle client cancellation gracefully in DownloadSoftware gRPC method (51012208)
* Fix PerforceServiceCache not picking up stream view changes (50991273)
* Add redis-backed queue infrastructure for auto-RCA (50988984)
* Introducing standard const for invalid commit id (50957351)
* Shadow pool deletion from dashboard, add configVersion to dashboard config, fix IDE0052 warning (50944559)
* Move EnableExpandedIgnorePatterns from static BuildServerConfig to dynamic BuildConfig global config (50939977)
* Update TestExecutor to support loading ignore patterns (50933712)
* Add temporary Pool Migration dashboard page (50933156)
* Add TestExecutor case for matching ignore patterns (50932488)
* Emit telemetry on lease completion (50903649)
* Add OptionalRequires support for BuildGraph nodes (50865502)
* Adding computed stream property to use for synthesized value (branch & stream id) (50829790)
* V3 IncrementalPoolMigration - safe hybrid mode for incremental pool migration (50826729)

## 2026-02-12

* Make the perforce change exception raise error logging (50799777)
* When processing a single commit, exceptions can occur causing a cascading abandonment of other records. (50799254)
* Adding more support for ICommit interface (50784079)
* Upgrade to .NET 10 SDK in BuildHorde.xml
  Uses UE's bundled .NET SDK. (50776723)
* Update the version of dotnet used to build to 10.0. (50767114)
* [Horde Dashboard] Added raw `agent hours` metric to pool utilization report view and functionality to click bar chart segments for a fixed overview popup (50747397)
* [Horde Dashboard] Adding system storage view option to agent page columns (50710586)
* Add material-based grouping to ShaderIssueHandler
  This ensures all failures for the same material are tracked together.
  Fix LogShaderCompilers grouping by expanding ShaderEventMatcher to capture continuation lines and extract material paths. (50699041)
* Expanding base context to include some hardware capabilities, and replacing DataSource with new "TestIdentity" nomenclature. (50669442)
* Fix gRPC streaming errors in UpdateLogTail
  The UpdateLogTail bidirectional streaming RPC was throwing unhandled exceptions under certain conditions when clients closed connections (timeout, process termination, or intentional closure). (50651383)
* EpicGames.Oodle: Restructure to follow the standard pattern for importing native dependencies in dotnet and remove the custom assembly loader code now it's no longer required. Generate as much marshalling code at compile-time as possible. Only include the relevant platform native binaries if we're building for a specific RuntimeIdentifier. (50650488)
* Analytics Updating key stats record. (50617535)
* Backend library now supports cancellations. (50614189)
* [Horde Dashboard] Fixing improper property names for fetching storage metrics within `Agents` view (50558077)

## 2026-02-05

* Improve checkbox visibility in dark mode
  Brighten enabled checkbox border and add brighter hover state for better contrast.
  Add explicit disabled state border to distinguish from enabled.
  Fix label hover to match checkbox box hover behavior, preserving existing Fluent checked+hover state with :not(.is-checked) pattern. (50525707)
* [Horde Server][Analytics] Propagating full agent telemetry events to associated data sinks (50509499)
* Refactor SlackClient error handling to use enum-based behavior instead of boolean predicates
  * Logs "not_in_channel" error message at log level warning instead of error. This error can be common if the Horde Slack bot has not been invited to a channel.
  * For throw error behavior, it's assumed that the caller will take care of logging any exceptions (previously the exception was logged, and then re-thrown) (50483048)
* [Horde Server][Horde Dashboard][Analytics] Adding system drive stats to CapabilitiesService + displaying new drive stats on Horde Dashboard
  - Added new set of system drive related disk info events propagated through the CapabilitiesService (`C:\` on Windows and `/` on Unix). 
  - Added HordeDashboard boilerplate to support these new analytics events, and modified nomenclature of pre-existing "working drive" telemetry (50452739)
* Remove full URL from OpenTelemetry trace name in Horde Agent
  * It has too high cardinality when IDs, CLs etc get included
  * Added support for passing along a request option called "url.template" to let calling code provide a better name hint (50436066)
* Fix build health compiler warnings (50435967)
* Add the option to clean into RetrieveCloudArtifact so we can clean the folder for test jobs. (50403203)
* Dispose Perforce PoolConnectionHandle when there is an exception during login (50395731)
* Add optional helpSlack/helpEmail overrides to ErrorInfo for per-error support contact customization (50381085)
* [Horde Server][Horde Agent][Analytics] Propagation of Agent's system drive usage stats to Agent telemetry reporting
  Modifies the original set of system metrics propagated into mongodb to split the concept of agent disk (what we're currently tracking) and system disk (C:\ on Windows, '/' on Unix. This data will eventually be consumed by the Horde telemetry dashboard to show a 2nd graph tracking system disk size alongside the pre-existing agent disk size graph. (50354394)
* Make upgrade of Windows agent more robust with retry logic
  Add retry for file deletion and move operations during agent self-upgrade to handle lingering file locks after service stop. (50350263)
* Ensure we use the targeted framework (.net 8)
  By default we use LatestMajor which may not be desired. (50316173)
* Missed file for agent telemetry (50306429)

## 2026-01-29

* Add error handling for stuck step timeout check in JobExpirationService (50303167)
* Increase max command line length for process memory tracking (50300448)
* Fix race condition in agent relay long-poll mechanism (50299104)
* Fix compiler warning in WindowsProcessMemoryTrackerTests.cs (50296706)
* Output logs from child process memory tracking in the batch/lease logger (50296181)
* Add IRcaProcessor framework and config options. 
  Enables licensees to implement their own root cause analysis pipelines by introducing an extensible IRcaProcessor interface in HordeServer.Shared. (50284420)
* Add bidirectional commit queries, parallel scoring, and fingerprint fallback for issue processing
  Support ascending/descending sort order in ICommitCollection.FindAsync. Enable optional bidirectional suspect search that
  queries from both ends of the commit range to better identify root causes. Add parallel commit scoring with configurable
  concurrency. Add fingerprint-based fallback to match existing issues when no suspects found, preventing duplicate issues
  when commit range exceeds MaxChanges. All features are config-driven and default to off. (50256512)
* Remove vertical boundary lines from telemetry lease overlays and hide overlays entirely when all leases are too narrow (<4px) to be visually meaningful. (50250137)
* Add ACL-gated AI Analysis link to issue modal (50186747)
* Implement dashboard plugin permission check (50177903)
* Fix stuck job/lease when retrying or aborting running steps
  When retrying or aborting a running step, mark the step for abort and set the batch error to NoLongerNeeded or Cancelled. This signals that the lease should be cancelled. Also add a 24-hour timeout for stuck running steps that never complete, with periodic detection via JobExpirationService. (50164696)
* Adding stream aliasing feature backend and frontend (50161443)
* Fix sparklines cloning lease overlays (50160749)
* Updated perf record for use in new API endpoints with more context. (50148423)
* Update placeholder to handle between "New" and "Select..." labels based on admin access. (50124878)
* Add a tool tip for cancelled batch texts explaining the reasons (50099723)
* Propagate lease cancellation reason for cancelled job batches (50099511)
* Support both ordered and unordered commit id. (50084686)

## 2026-01-22

* Reducing the severity of noisy error on duplicate payload properties due to the benign nature. (50024919)
* Refactor lease outcome reason merging
  Also improve server-initiated termination handling by passing better defaults. (50023636)
* Exception logging for background process in metrics collection. (50018209)
* removing debug probe (50007684)
* Updating default view to align with telemetry (50006350)
* Add NoReasonReported lease outcome reason
  To handle cases where a lease is cancelled without an explicit reason from the agent. (49992140)
* Add support for change button to not require job object (49969938)
* Enable cache when requesting access token via EpicGames.OIDC
  Uses new caching functionality from 49949594 (49950769)
* Log reason for job driver performing cleanup (49950560)
* Expose LeaseOutcomeReason in GetJobBatchResponse
  Only fetches lease data when batch has an error to avoid unnecessary DB queries for successful batches. (49949816)
* EpicGames.OIDC: Add opt-in access token caching
  Currently only refresh tokens are cached to disk. This change adds the ability to also cache access tokens, reducing unnecessary calls when tokens are still valid.
  Still writes refresh tokens in the old format as "Providers" key, while also writing a new "Providers2" key which includes a cached access tokens. (49949594)
* Temporarily removing constraint of unused key in order to update sparse. (49930492)
* Add diagnostic logging to schedule gate job searches
  Track why jobs are skipped (failed, incomplete, or missing target) and log summary when all candidates are exhausted. (49899154)
* Introducing key stat performance records. (49898834)
* Much better dashboard commit cache behavior (49897270)
* Minor improvements to nullability check & safe fallback for ColumnAttribute with no name (49893700)
* Expanding capabilities of Analytics.Telemetry to simplify telemetry emissions & reduce duplication keys across code. (49892781)
* Removal of sparse constraint as it's less effective on queryPlanner version 1 (49890466)
* Add retry logic to TestAgentMessageLoopTcpAesAsync to handle CI load variability (49884945)
* Fix compiler warnings for JobDriver.Tests after upgrading MSTest (49884863)
* Adding new Performance Trends c# project (49874115)
* Exposing existing Session ID to the telemetry api (49862415)
* Adds IPluginDashboardAcls interface for plugins to control dashboard feature visibility based on user permissions (49857922)

## 2026-01-15

* Address issues with on prem Jira service build health (49818828)
* Add lease divisions in agent telemetry view (49814164)
* Use MSBuildThisFileDirectory for MetaData.cs path, this allows for dotnet run <project> to work as it uses a different working directory (49813633)
* Support Jira Cloud and On Premise for Build Health (49809016)
* Fix conform in PerforceMaterializer to run cleanup after sync and improve error handling (49767276)
* Add GetJobsAsync to HordeHttpClient (49700421)
* Expose optional force parameter for the device reservation deletion endpoint
  This will allow users to fully delete reservations and associated reservation blocks where appropriate (49690413)
* Fix Telemetry Service to use linked CancellationTokenSource for FLTMC command (49647092)
* Conforms that are scheduled due to low free disk space upon execution check if the agent's available disk space still requires the conform to be run (49624335)
* The task argument for jobs and conforms is passed using an environment variable (49619111)
* Create jobid,batchid,stepid column hierarchy within JobStepRef. (49604444)
* Adding metadata & specific summary table emissions (49602718)
* Cleaning up telemetry records to use base class for common attributes & supporting inheritance for AnalyticsTableGenerator. (49595510)
* Fix potential issue with CPU time calculations in agent
  Also wrap lease execution to ensure any exceptions are immediately surfaced. (49414139)
* Cloud Storage - Added another longer wait (this time for 60 minutes before retrying). We have seen extreme cases were issues take about an hour to settle and a retry in this scenario would have recovered the build rather then forcing us to rerun the build which takes much longer then this 1 hour wait time. (49372727)
* Instrument gate handling for schedules (49372088)
* Refreshed stale files from shelve for build break (49354227)
* Build health filter QoL improvements, preparation for wide users, and minor bug fixes:
  - Description field
  - Full ownership context returned with object
  - Dropdown to use seach & items ordered alphabetically
  - Dropdown tooltips to include owner, proper time format, and description
  - Labels more intuitive based on whether the owner has authority to CRUD build filters, or not
  - Syntheized job step outcomes for when batches fail (49353237)
* Description field & admin powers for Build health filter, as well as returning full owner object for better messaging around follow-ups. (49353218)
* Fix issue with agent tooltip on job timeline (49332510)
* Another take on duplicate json Telemetry key exceptions (49282234)
* Move ISystemMetrics to shared (49278929)
* Fix an analysis warning with EpicGames.Analytics.Generators. (49272410)
* Add a runtime watchdog to agent to monitor for GC or thread pool stalls (49209914)
* Fix exception during PerforceMaterializer clean when in dirty state
  Parallel directory deletion did assume workspace directory exists, which is not always true. (49204635)

## 2025-12-11

* Using nullable type for IJob as per CreateJobObject return specification. (49147810)
* Adding telemetry Post api for clients (49140339)
* Add ability to export basic query params for new build (49139846)
* JobStepRef telemetry emissions for skipped steps. (49134073)
* Add metadata to Analytics (49121215)
* Fixing default configs for Analytics Project. (49115652)
* JobDriver Perforce updates
  * Ensure PerforceMaterializer has support for and uses maxFileConcurrency and maxFileScanConcurrency as the ManagedWorkspace, ensures cleaning can run file operations in parallel
  * PerforceExecutor - conforms now obey the maxFileConcurrency option to ensure deletes are run in parallel as well (49107432)
* New Analytics projects (49087766)
* Time formatting pass (user pref based - UTC & 24h clock) (49087746)
* Adding configurable User1, 2, 3 status colors (49082227)
* Suppress telemetry events with duplicate keys (49049332)
* Fix situation where credentials would be printed to log on Mac/Linux because we compared conformed environment variable names against non-conformed credential key names.  Now doing the comparison with conformed values on both sides of the comparison. (49049136)
* Add append option for retrieving cloud artifacts (49046639)
* Ensure untracked files are removed in PerforceMaterializer (48991402)
* Add an EpicGames.Build.Tests project with tests for the MSBuild project parsing logic. (48985777)
* Pass along old project id to prevent full refresh. (48967771)
* More polish on default heights for right top edge alignment (48964876)
* Minor polish for sticky & more appropriate default height. (48961671)
* Initialize PerforceMetadataLogger even if retrieved after a sync (48953485)

## 2025-12-04

* Initialize PerforceMetadataLogger even if retrieved after a sync (48953485)
* Save lease metrics reported by agent to database (48944287)
* Add step report color hinting (48920015)
* Updating naming of request to be more appropriate & minor polish. (48919764)
* Global config flag to enable and disable RCA notifications to be sent (48910456)
* Use the correct Perforce metadata logger for PerforceMaterializer
  Refactored IWorkspaceMaterializer to support loggers. (48901078)
* Export Summary column to CSV report, and sorting step rows by capabilities. (48897489)
* BuildHealthFilter support (48861859)
* BuildHealthFilter collection + API endpoints (48860965)
* Improve logging when saving lease outcomes (48847790)
* Removing dev suffix. (48835366)
* Adding new columns for SubmittedBy, PreflightTag, Virtualized (string-bool), as well as UserId conversions from usernames. (48831291)
* Capture CPU seconds spent during a lease on the agent (48806744)
* Fix .NET 10 warnings (48803780)
* Store list of files for replicated Perforce changelists (48803432)
* Duration label cleanup. (48789113)
* Add the shared LogMatchers project and its tests to the Shared solution. (48783326)
* Support -x for opened (48770142)
* Align submitted naming with existing schema (48768458)
* [Converting commit telemetry stream to prod (48764675)
* Have the compile event matcher recognise extensionless files containing underscores
  UBT fails to recognise a diagnostic in files like "type_traits" (48741032)

## 2025-11-25

* Perf & tests for commit regex (48627869)
* Skip RCA escalation on a weekend. (48622101)
* Use p4 print for unshelving in PerforceMaterializer (48607376)
* EpicGames.Perforce: Add support for snapshot-based testing (48607305)
* Removal of old JobStepRef "skipped state" emission code in entirety in preparation for V2. (48573938)
* Add schedule filter to summary pane (48564369)
* Adding Epic Sandbox project to soluthon and improving conditional (48563137)
* EpicGames.Perforce: Improve fixture with deletes from shelve (48555287)
* Improve VSCode support (48552892)
* Null check for defensive programming. (48499986)
* Modify RCA escalation message to inform user that only category is required (48482600)
* Implement feedback from older swarm: code cleanliness (48479865)
* Support JobStepId tags in log renders (48466761)
* Timeout P4 issue rank queries after 2 minutes (48465686)
* Only require RCA category, don't escalate for missing summary (48459016)
* JobStepRef metadata to be emitted under more circumstances & ensure backwards compatibility on old result set. (48363489)
* Initial work centralizing some Horde environment variables and dashboard routes used by various C# code (48352701)
* Fix conform issue where mixing materializers caused deleted workspace dirs (48349168)
* Adding additional style icons. (48347457)
* Linked mode QoL features:
  - Added link action button to streams
  - Cascade link action button to the entire hierarchy (stream -> template -> step & label
  - Edge case saitated where when a stream is added, implicitly link all the hierarhcy
  - Only on the affirmative case, we do not issue this for the removal case (otherwise we would remove all hierarchy of the newly added stream)
  - Optimizations made to only issue hierarchy refresh when receipts have diffs (48347284)
* Toggle to filter out cancelled jobs from the toplevel history, allow for filtering of jobs based on whether it was scheduled or manually kicked, and a big fix for stale data bindings on D3. (48297767)
* Commit telemetry integration into the p4 cache service. (48296707)
* Improve issue matching across job steps in no suspects case (48293032)
* Use rootCauseOwnerInfo exclusively (48290903)

## 2025-11-14

* Fix compiler warning in IssueServiceTests (48281019)
* Make ModTime flag disabled by default for PerforceMaterializer (48280995)
* Add current user to dropdown hints for root cause owner (48275734)
* Move Me option for Root Cause Owner to top of selection to match existing UI layout (48275511)
* Avoid multiple occurences of the same user in Slack workflow root cause owner options (48275417)
* Skip RCA escalation for issues resolved by Horde (48257790)
* Fix issue determining latest change number in PerforceMaterializer
  Use workspace root instead of stream. (48246872)
* Display user name for rootCauseOwnerId in issue modal (48230199)
* Improve issue matching across job steps in no suspects case (48293032)
* Use rootCauseOwnerInfo exclusively (48290903)

## 2025-11-13

* Use a real stream client in PerforceMaterializer
  Previously, a classic workspace client was created with views copied from the stream. This is/was to allow Horde-based views to be added in addition. (48226301)
* Determine latest change number via p4 changes in PerforceMaterializer (48222459)
* Add test for sparse streams in PerforceMaterializer (48216462)
* Do not include duplicate issues in root cause summary escalation notifications, and do not require Root Commit Id for a notification to be closed (48216270)
* EpicGames.Perforce: Add sparse streams to fixture (48215972)
* EpicGames.Perforce: Add support for streamlog command (48215109)
* Add RCA owner input to dashbaord (48202681)
* Always set ResolvedById in Slack workflow for resolved issues (non-null fixCommitId) (48200054)
* JobStepRef parity changes. (48192973)
* Root Cause Summary Escalation Notifications to Slack (48167554)
* IssueTelemetry event name fix (48161594)
* Expose lease outcome reasons in REST API (48153944)
* Fix issue with case-sensitive materializer name comparison for conforms (48140462)
* Add lastSuccessTime and firstFailureTime to FindSuspectsForSpanAsync and use them for time delta gate.
  Fetching these times via GetAsync was causing exceptions to be raised when commits were out of stream and thus exiting suspect finding altogether. (48100206)
* Fix Horde server build by adding and removing paths to reflect the migration of log matchers to their own project. (48079442)
* Skipped steps as a summarized metric & manual invocation metadata for CSV. (48062467)
* Add options for controlling sync threads and batches in PerforceMaterializer
  Also add support for ModTime as a flag. (48029020)
* Updated RetrieveBlockAsync to no longer enable verbose logging on retries of extracting the directory node. We suspect that verbose logging was causing a large amount of overhead resulting in steps timing out and cancelling. We still enable hash verification of files though. (48024878)
* Remove dependency lib inclusion (48009192)
* Including ODBC reference (47977362)
* Updating dev environment dependencies due to transient dependecy postinstall changes. (47974458)
* Fix issue with logging of config exception stacktraces (47961496)
* Make awaiting on background notification tasks more robust
  - Catches any exception directly in the task
  - Simplify by using a list and only waiting in StopAsync (47960832)
* Fix config object append bug leading to infinite loop (47953303)
* Add reason to lease outcomes (47921082)
* Fixing an issue where we were disposing of the HttpResponse content before reading it. (47903704)

## 2025-11-06

* Fixing an issue where we were disposing of the HttpResponse content before reading it. (47903704)
* Send cancellation request to Job Driver process When then Job Handler receives a (47638012)
* Restore throwing cancellation exception when step is abort and run step clean up (47657684)
* Set the Job Driver cancellation event to nonsignaled after process exit (47659409)
* Update log message when the job executor receives cancellation signal (47728221)
* Add support for conforming with PerforceMaterializer (47733607)
* Correctly consume cascaded command option & simplified label styling. (47740983)
* Introduce parallel data source mode for new API endpoint (47741988)
* Update log message to include Agent ID when lease cannot be assigned due to conf (47776364)
* Add additional logging to conform check (47776588)
* Update message when lease cannot be assigned to agent due to conform being requi (47783624)
* Fixed an error where we retry a read request but re-use the same request message (47847344)
* Fixed a bug where we re-use the same HttpRequestMessage when retrying a WriteReq (47851781)
* Moving MultipartFormDataContent creation outside of the Polly retry loop. (47855479)
* Fixing issue where the HttpContent of the request would be re-used between retry (47858228)
* Adding context to the issue telemetry. (47862369)
* Optionally re-open issue when assigning to other user (47866841)
* Only assign issue to user if previously unassigned when marking fixed via Slack (47871287)
* Update message when lease cannot be assigned to agent due to a pending conform (47898336)

## 2025-10-30

* Add prevention suggestion wording to root cause summary placeholder (47573311)
* Link to step history panel in Slack thread when there is no regression range (47545130)
* Step Outcome view Label styling:
  - Updated modal noun for "Step Link", "View Step", and "Step" so that it now properly uses "Step" | "Label".
  - Updated tooltip onmouseover to use label where applicable
  - Specific label styling fo step name column (47525598)
* New workspace option, maxFileScanConcurrency, to control file scanning and writing independently (47506990)
* Remove index hint for listing jobs per stream
  After upgrading to DocumentDB 5, this index hint does more harm than good.
  Running manual explain on this exact query in production shows significant speedups. (47493366)
* Avoid logging too large config exception stacktraces
  Causes log lines to get dropped by log processor. (47490436)
* Add filter-by-tag functionality to the test automation hub. This adds a tag filter to the Test Automation Hub (V2) landing page and to the Phase View page. (47440781)
* Bugfix for backspaces causes a desync with the rendered state. (47434123)
* Move EnableJobTaskConformCheck from BuildServerConfig to BuildConfig (47418622)
* Step Outcome view QoL enhancements:
  - Persist view options to query url.
  - Disable auto-select all for templates.
  - Callout on dropdown moved down to avoid clashing with header dropdowns (when filter box changes).
  - Select all now available in stream dropdown.
    -- Code infrastructure is now there for templates, but is not connected due to server load issues that come from this.
  - Minor fix where clearAll was not being reflected.
  - Minor fix where UI properties were precipitating data refresh.
  - Possible fix for filter <> selection desync (47401550)
* Disable analyzers in Dockerfile build for Horde Server
  They should be run in the already existing but separate BuildGraph target for better parallelism. (47321321)
* Adding labels to option filters (47317161)
* Improve file count and download speed log lines in PerforceMaterializer (47313084)
* Make dirty checking more robust in PerforceMaterializer
  Prevents unnecessary p4 flushes. (47311776)
* Set env vars for UAT/BuildGraph in PerforceMaterializer (47267210)
* Minor change to improve the 'no data' case styling (47266542)

## 2025-10-23

* Make disposing of JobStepLogger more resilient
  - If a network error occurs during disposal it may prevent job step from being updated
  - Also make Polly retry on connect timeouts (speculative fix) (47256245)
* Fix free space calculation. This change allows the use of drives mounted as paths, not just drive letters. (47250181)
* Merge "Root Cause" Slack workflow into the "Mark Fixed" button (47233499)
* Batch API for JobStepRef (47213312)
* Added AllowRetry flag to build graph nodes. (47205038)
* Attach exception to trace span when agent relay allocations fail (47203856)
* Use ManagedWorkspace for AutoSDK when PerforceMaterializer is active
  AutoSDK heavily rely on Horde-based view filtering which is not implemented in PerforceMaterializer.
  This avoids massive syncs during experimentation. (47203029)
* Fix issue with multiple claims getting overwritten for built-in OAuth2
  - Only last claim was ever written
  - Applies when auth mode is set to Horde accounts (47200703)
* [Build Health Dashboard] Bring audit log dropdown inline with Horde standard, correctly set defaults, and update options based on backend fetches when live tailing (47179708)
* Adding isPreflight column to JobSummary data. (47175049)
* D3 container fixes for label/no-label toggle (47174819)
* Enable parallel test running for all Horde server related projects (47158966)
* Enable parallel test execution for HordeServer.Build.Tests and HordeServer.Compute.Tests (47154960)
* Add support for parallel test execution for database integration tests (MongoDB and Redis) (47150628)
* Check if a conform is required before asssigning a Job Task lease to an agent (47147172)
* State filter & minor fix for batch sync failed. (47111953)
* EpicGames.Perforce: Add support for modtime client option
  - Maintains the original Perforce timestamp when moved to/from the cache
  - Requires opting in via "method" setting for the workspace type, like how use of have table works (47099206)
* Adding step messages to dashboard step view, step details, log view, and history (47051698)
* Bake report step message/severtity into ref metadata (47041541)
* Adding job/step messages/severity to job reports (47015259)

## 2025-10-16

* Refactor issue config to be a root object (46971377)
* Add support for grouping issues on log channel boundaries (46967755)
* Updated Label telemetry to emit individual events in addition to summary. (46958593)
* Log info from FindOrAddIssueForSpanAsync to issue audit as well (46956201)
* Add ability to clean up orphaned agent session IDs (46930732)
* Add overload of MultiLog function to capture exception object and use it for suspect finding exceptions (46898218)
* Update labels for select/clear all buttons + remove the clear-all filtered subset behaviour (46893975)
* Added suspect finding info to issue audit log (46892792)
* Label support within table (46880003)
* Include job name in <title> for job details view (46840656)
* Build health make issue suspect max days and scoring time configuration, also bump defaults (46803980)
* Add Build Health node annotation to suppress warning issues (46803820)
* Use full stream name for tab title if available (46766042)
* Sending up schema version as well (46744094)
* Fix issue with assignment/acknowledgement (46734592)
* Increase per stream limit for historical views (46732352)
* Replace PerformanceCounter with WINAPI GetSystemTimes
* Updating API used for obtaining changelist results (46721751)

## 2025-10-09

* [Build Health Dashboard] Updating API used for obtaining changelist result (46721751)
* Fix issue with ignoring Horde views for Perforce materializer (46717212)
* Ignore Horde-based views in Perforce materializer temporarily (46714793)
* Fixing test to use proper where clause on any. (46705063)
* Missed an edge case for automatic schema upgrades - select all was implied on schema 1 for a template; we are now explicit in schema version 2+. (46701236)
* Fully qualified label names + categories. (46699620)
* [Build Health Dashboard][Step Outcome] Introduce POST API for changes in order to support single request of batch data (46699481)
* StepOutcome table supports N values for the same cell (46682375)
* Avoid infinite trace span for background task in Perforce cache (46667057)
* EpicGames.Perforce: Add OpenTelemetry tracing (46665044)
* Add RCA Change Backout category (46662844)
* Bumping SchemaVersion now we have added RCA data. (46607473)
* Updated RCA information to be included in the issue telemetry. (46605642)
* Apply tree filtering to current APIs (46605166)
* Add extended bearer token ACL (46601390)
* Surface Gauntlet ensure warnings and make the issue summary reflects having a callstack (46583249)
* Fix server warning in Slack notifications (46580566)
* Adding Root Cause Analysis Slack support (46580346)
* Lint support & unit test support (46580285)
* Collection of QoL UI features for interacting with dropdowns:
  - clear all + select all (for each group)
  -- filter context is applied
  - sticky search field
  - Jump to top/bottom of scrollable
  - Jump up group and down group
  - optimizations to dropdown to reduce load on heavy case (removal of ToolTipHost)
  - Compression of step names in query param (remove duplicates of data across all stepnames)
  -- query param schema version & auto-upgrades to v1
  - NaN fix for summary where no completed steps exist (46580282)
* Fix issue with issue generation delta comparison (46577182)
* Address issue update, issues (46576547)
* Do not generate a flood of Build Health issues for a small set of known Gauntlet log event warnings
  If we identify cases where we want these warnings, they should be defined as new log events, and force their warning in the issue handler
  Though, we have been unable to come up with a single case in exploration (46575909)
* Instrument CachedCommitSource.FindAsync (46518362)
* Downgrade OpenTelemetry.Instrumentation.StackExchangeRedis to match OTel version
  * 1.0.0-rc9.15 requires OpenTelemetry 1.9.0+
  * Everything compiles fine but fails silently at runtime unless you enable diagnostics logs for OTel (46502090)
* More tweaks to the Issue Modal for new functionality (46465830)
* Fix clearing root cause change in modal (46462618)
* Horde - Removed running of deleted test project HordeServer.Ddc.Tests.csproj (46462588)
* Fixing some issues with the issue view (46462221)
* Deleted HordeServer.DDC - This was never a supported way to use Horde as DDC cache. We recommend using Zen for most of these needs, or Cloud DDC if you need a cloud based geo replicated DDC. (46461947)
* Tweak deployment issue messaging (46460552)

## 2025-10-02

* Root Cause Analysis, dashboard side (46444423)
* Avoid duplicate id setting recursion (46432719)
* Adding support for Build Health duplicate issue flagging, server side (46432088)
* Added a fix where we would not delete the cleanup batch file if there is a timeout. (46423682)
* Improve tracing and logging for issue locks
* Root Cause Analysis server side (46416008)
* Added root commit to issues (46383352)
* Show changes from restricted in release tool (46372449)
* Linked selection mode, clear all filters functionality, and hierarchical clearing bug (edge case of last item). (46335934)
* Adding symbolic link support to vite configuration (46294583)
* Improve logging of AWS EC2 errors in fleet manager (46259788)
* Optimize CreateAgentObjectAsync to only fetch agent document if update fails (46254913)
* Updating the Horde retry warnings to no longer create Build Health issues. (46211569)
* Remove stream salt from Gauntlet issues as they are generating too many individual issues (46203692)
* JobLabelSummary telemetry (46137130)
* Added columns to CSV export to help with diagnosis. (46070819)
* Getting Horde Agent to work with an "Unreal root" hierarchy in a subfolder of the workspace root (46066712)
* Respect Horde-based views in PerforceMaterializer (46059466)
* EpicGames.Perforce: Add client name for stream view lookups (46059408)
* Remove Win64 agent type fallback for installer tools (46055811)
* Add ellipsis to job operation menu items that seem risky (46020531)
* Fixing telemetry to use a string version of the commit ID to avoid complex objects. (46007646)
* Quick UI fixes (46006113)
* Forward schedule-based claims when scheduled jobs are created (46000083)
* Add missing ExamplePluginController (45964397)
* Avoid logging entire stack trace for Horde-related compute allocation errors (45952021)
* Minor fix to missing relay error message (45951809)
* Add setting for overriding available RAM in agent (45907276)
* Reduce cookie size by removing tokens for OIDC and Okta auth (45906522)
* Text filtering & combo box conversion (45895296)
* Fixed coding standard violations from CL 45881300 (45883055)
* Added feature to Horde's issues to be grouped based on customizable rules (45881300)
* Temp Storage to allow the maximum number of exports to write in a single request to be configurable from the agent settings (45862422)
* Styling pass V3 & general UI improvements (45849344)
* Improve retry mechanism for Slack API client (45782122)
* Export to csv button. (45763412)
* Filter more files and improve description generation in release notes tool (45729945)
* Summary column & hours label for title & improving the console.log for change summary request count so we can see the *actual* request (45714224)
* JobSummary telemetry (45677295)
* Add flag to report diagnostics during execution of a step (45619267)
* Remove left-click to add highlighted text to search box in log view (45606471)
* Horde Dashboard: Put error handling in own component to keep root view from needing to be an observer (45543682)
* Increase title size to match changelist/date anchors & checkbox to hide date anchors (45535364)
* Move Job/Step logging to information level (45494424)
* Change Headers anchored by submit times (45494125)
* Expand streams/{streamId}/changes api to support batch semantics. It can take a manually specified set of changes to obtain. (45494088)
* Add using declaration to new JsonDocument reference to ensure disposal (45482577)
* Resolve workspace materializer through workspace config/type (45478698)
* Add support for file structured logging property to hashed issue handler (45441165)
* Add support for LatestChangeNumber constant in PerforceMaterializer (45437537)
* Add new PerforceMaterializer (45432379)
* EpicGames.Perforce: Add a secondary release stream to fixtures (45431610)
* Fix the DatabaseIntegrationTest class to properly forward dispose calls to the mongoDBRunner. (45403945)
* UseEffect in order to calculate proper height. (45387504)
* Selective dropdown updates on change & Step filters no longer being applied across streams (45340847)
* Backing out JobExecutor killing jobs on UAT process hangs, we need to tweak this logic as it's killing jobs too early. (45313353)
* Minor UI enhancements for 5.7 (45301672, 45280879)

## 2025-09-18

* Add example backend api calls for dashboard (45941624)
* Add Example Horde Server Plugin, Tests, and Dashboard (45940896)
* Text filtering & combo box conversion (45895296)
* Styling pass V3 & general UI improvements:
  - Styling for aborted
  - Styling for skipped
  - Sticky summary columns
  - Min width reduced for smaller screens
  - Skipped as red (+ view/ui options segregation refactor)
  - Warnings as summary success
  - Patch changelist UTC skew in UI
  - Fall back to job start when no changelist data is available from server
  - Clearing project cascades correctly (45849344)
* Export to csv button. (45763412)

## 2025-09-11

* Summary column & hours label for title & improving the console.log for change summary request count so we can see the *actual* request (45714224)
* Silence static analysis complaint about large function (45701060)
* Fix for MSVC compiler bug - provide all designated initializers for FRigVMCompiledInFunction including for predicates. (45698719)
* JobSummary telemetry (45677295)
* Remove left-click to add highlighted text to search box in log view (45606471)
* Put error handling in own component to keep root view from needing to be an observer (45543682)
* Increase title size to match changelist/date anchors & checkbox to hide date anchors (45535364)
* Move Job/Step logging to information level (45494424)
* Change Headers anchored by submit times (45494125)
* Expand streams/{streamId}/changes api to support batch semantics. It can take a manually specified set of changes to obtain. (45494088)
* Add using declaration to new JsonDocument reference to ensure disposal (45482577)
* Resolve workspace materializer through workspace config/type. Also add a factory for the PerforceMaterializer. (45478698)
* Add support for file structured logging property to hashed issue handler (45441165)
* Add support for LatestChangeNumber constant in PerforceMaterializer (45437537)
* Add new PerforceMaterializer. Uses plain workspaces in Perforce, currently optimized for single stream use. (45432379)
* Selective dropdown updates on change & Step filters no longer being applied across streams (45340847)
* Backing out JobExecutor killing jobs on UAT process hangs, we need to tweak this logic as it's killing jobs too early. (45313353)
* Minor UI:
  - Last refresh time
  - Snap to right (most recent date)
  - Add a 2 week option for time filter (45301672)
* Minor UI:
  - Disable date anchors in headers for preflights. Changelist <> Dates can fall out of sync easily, causing date header anchor instability. (45280879)

## 2025-08-28

* [Horde][BuildHealth][Dashboard] Minor UI enhancements for 5.7:
  - Disable date anchors in headers for preflights. Changelist <> Dates can fall out of sync easily, causing date header anchor instability. (45280879)
* Improve error message design for job view errors (45270337)
* [Horde][BuildHealth][Dashboard] Topnav to have build health link. (45255816)
* [Horde][BuildHealth][Dashboard] Filter option to prevent preflight data. (45255787)
* [Horde][BuildHealth][Dashboard] UI improvements for 5.7:
  - Max width for ultra wide monitors
  - No data for current filter label
  - Title that summarizes the filter (45255627)
* [HordeDashboard] Enables modifying tags of a reserved device (45241906)
* Horde Dashboard: Tweak job description display (45239969)
* Horde Dashboard: Show job description in summary when available (45239842)
* [Horde][BuildHealth][Dashboard] Memoization pass for render performance, minor highlighting improvements, and deletion of FluentUI table. (45238473)
* [Horde][BuildHealth][Server] Expose DateUtc on GetCommitResponse (45237652)
* Remove unused Password property from EpicGames.Horde.Streams.IWorkspaceType (45228808)
* Horde Dashboard: Reduce JobDetails V1 QA Fixes (45220314)
* Guard against multiple hang detection in same step and fix string decoding on signal (45203470)
* Temporary fix for hung job steps on farm, catch log signal that outputs in cases where UAT or subprocesses are hanging. (45198954)
* [Horde][BuildHleath][Dashboard] D3 StepOutcomeTable variant for improved render performance. (45197220)
* Horde Dashboard: Reduce JobDetails V1 (45195387)
* [Horde] Add GET endpoints for fetching devices to the Horde HTTP client (45127432)
* Added issue audit logging to new World Leak issue handler (45116295)
* Horde Dashboard: Add Epic copyright notice to files missing it (45110422)
* Renamed Type structured logging field to avoid overlap with audit log channel. (45106576)
* Pass down the interactive boolean to the request (45105852)
* EpicGames.Perforce: Add more metrics to ManagedWorkspace (45101670)
* [Horde][BuildHealth][Dashboard] Minor UI enhancements for:
  - Drop downs in Build Health View
  - Step Outcome Table tool-tips (45070406)

## 2025-08-21

* [Horde] Adds a PUT function for updating devices to the HordeHttpClient (45045274)
* [Horde][BuildHeatlh][Dashboard] Issue Details in modal, and update cell styling to use Horde compliant colours. (45036254)
* [Horde][BuildHealth][Dashboard] Multiple Stream selections, date headers, loading spinners on refreshes, URL query encoding fixes to include hierarchy, scroll-locking optimization, and various UI cleanup. (45035672)
* Add max retries to agent delete (45021946)
* Filter out anonymous pools in pools view (44988418)
* Cloud DDC - Sending parts as uniform object as the validation code on the C++ side prefers this (its a more optimal format of this object). Implemented a very explicit support for writing uniform objects, fixing so that the writer converts to uniform types if appopriate required a much more intrusive change. (44981589)
* Cloud DDC - Dispose of JsonDocument when converting CbObject to Json, was not aware it was disposable. Thank you static code analysis. (44981577)
* Cloud DDC - Job metadata field serialization fix (ObjectId types was not converted correctly to json). Removed 1 of the 2 json conversation paths we had as the one we now have did not have this issue. Also fixed issues with serializing Guids to cb object. Lastly this adds support from serializing a json object into a CbObject, there is quite a lot of type loss going to json so this is not perfect but its handy to have in tests. (44928734)
* Reduce the timeout when retrieving the system capabilities on macOS (44927820)
* Add raw log event data, job/step information, and issue grouping details to issue audit log. (44907754)
* Horde Agent: Fail step when AutomationTool process has a non-zero exit code (44901827)
* Support new EC2 tag for identifying agent's pool ID
  Older tag "Horde_Autoscale_Pool" identifying by name will still work but should be considered deprecated. (44854631)

## 2025-08-14

* Disable logging of UAT exit code in JobExecutor Reverts 44661300. (44854494)
* [Horde][BuildHealth][Dashboard] Job History time span filter & removal of job count. (44837915)
* Add logging to shutdown of disabled agents (44820850)
* Fix duplicate key error in AddBlobAsync when converting shadow blobs
  * Shadow blobs are placeholders for referenced but not-yet-uploaded blobs
  * Upsert filter included Shadow=true condition, causing duplicate key errors when blob existed with Shadow=false
  * Removed Shadow condition from filter to handle all cases (new blobs, shadow conversions, and updates)
  * Added tests for race conditions and concurrent shadow conversions (44788210)
* Remove unused setting for compute queue AWS metric in PoolConfig
  This has since long moved into a separate strategy - ComputeQueueAwsMetricStrategy. (44775313)
* [Horde][Analytics] Initial proof of concept for Build Health Dashboard View. (44772914)
* Horde Dashboard: repopulate adv add arg quotes as escaped quotes (44755079)
* Remove unused WithDatadog setting (44753720)
* Add a named HTTP client for storage with retries
  Under high load certain operations may fail against the storage backend (for example S3). "503 Slow down" is a common one which can be retried. (44750205)
* Cloud DDC - Added conversion of float fields when converting compact binary into json (44746011)
* [Horde] Update GetLegacyDeviceResponse to return device tags (44702624)
* Ensure unhandled exceptions are logged from controllers (44699863)
* Simplify running of health checks in Perforce load balancer (44695146)
* [Horde Dashboard] Adds support for viewing and editing device tags on the horde dashboard's device editor (44669255)
* [Horde] Adds configurable device tags
  Device tags are small bits of metadata that can be attached to each device
  When making a reservation, users can specify tags in a variety of ways
  Required Tags - Devices that do not have this tag will not be reserved
  Preferred Tags - Devices that have this tag will be selected over devices that do not, provided at least one is available
  Undesired Tags - Devices that have this tag will be de-prioritized over devices that do not
  Blocked Tags - Devices that have this tag will not be reserved (44669125)

## 2025-08-07

* Log an error when UAT terminates with a non-zero exit code
  The JobExecutor in the agent detects and flags job steps as failed when Unreal Automation Tool (UAT) terminates with a non-zero exit code. However, when inspecting job step logs, this failure is not immediately apparent because no explicit error messages were logged previously.
  This change adds an error log entry to make UAT failures more visible in the logs. (44661300)
* Escape redirect URI in OAuth login form for Horde accounts (44659453)
* Hydrate CachedCommitDoc objects created in fallback path (44597929)
* Horde Dashboard: Adjust advanced additional arg parsing (44597533)
* Copy StreamConfig tags to environment variables
  The name of environment variable will be the tag's name in uppercase and prefixed with "UE_HORDE_STREAMTAG_". 
  The value of environment variable will be either the string "True" or "False". (44554523)
* Horde Dashboard: Resolve old query logic with new hook (44481989)

## 2025-07-24

* Horde Dashboard: Remove PrintException Component (44281453)
* Horde Dashboard: Usage of ErrorModal vs ErrorBar (44281424)
* Reduce verbosity of suspect addition/removal in audit log. Start using debug level logging in issue audit log. (44280236)
* Add example of using Azure Key Vault to Secrets plugin. The Azure Key Vault Secrets Provider is provided as experimental and an example of how to implement a secret provider from another cloud provider. (44274851)
* A node that is modified and skipped during a build graph update does not fail the whole build. If there is a modified node with a skipped state during a build graph update the whole graph would previously be marked as failed, instead allow the build to continue by marking dependent nodes as skipped/failed but still allow other independent subtrees to be run. (44273401)
* Update code coverage to use JetBrains.dotCover.CommandLineTools. The previuous code coverage tool JetBrains.dotCover.GlobalTool has been deprecated. (44273037)
* Horde Dashboard: Better handling of advanced additional arguments in NewBuildV2 (44207109)
* Horde Dashboard: Put HelpModal back into top level nav (44142211)

## 2025-07-17

* Horde Dashboard: Clean up logic in LogRender.tsx (44106602)
* API encounters ".NET type System.Text.Json.JsonElement cannot be mapped to a BsonValue" on swagger API invocations. (44078324)
* Review change for LocalizationIssueHandler (44009728)
* Added stream information to localization issues created by Horde (44002634)
* Renamed the telemetry entry for the Arguments field in the Job event. This is to fix a problem where the format was being processed incorrectly causing entries to appear as null. It feels unecessary to deprecate the old name as these fields are not in wide use yet and it's better to keep things clean and only write out data once rather than have duplicate data. (43869880)
* EpicGames.Perforce: Log if untracked files are removed (43864355)
* Add filtering to Build Health Card in Summary tab
  Make issues and unpromoted observable to ensure HealthPanelIssues only re-renders when they change. (43771480)
* Skip client IP allow listing for allocated compute agents (43771211)
* Expose pool IDs through agent's HTTP management server (43734454)
* Add per-stream trace spans for ScheduleService.UpdateQueueAsync (43729468)
* Truncate long template names in new build modal dropdown (43712812)
* Remove legacy isSwarmTab variable and conditional displaying of job search button altogether (43704159)
* Display job search button in presubmit tab (43702416)
* Add missing capabilities data for Macs (43693319)
* Add tags to the StreamConfig to control whether scheduled jobs are enabled (43687954)
* Horde Dashboard: cancellation reason on user home (43655326)
* Horde Dashboard: job status feedback (43651240)

## 2025-06-19

* Avoid issue service exceptions during suspect finding (43629946)
* When a string cannot be resolved to a secret then throw an exception if the string contains the secret prefix (43615113)
* Reassign Horde issue to Jira user when Jira and issue owners differ (43614130)
* Change IPluginConfig.PostLoad to non-async (43610732)
* Add method to compute client for allocating a UBA cache server session (43610555)
* Horde Dashboard: portable search url inconsistency (43609394)
* Changed Horde Analytics dashbaord category name from SubmitTool to Submit (43607713)
* Fix broken UbaCrypto test (43607690)
* Added an example of SubmitTool metric and dashboard category to Horde Analytics (43607541)
* Add REST API endpoint for requesting a UBA cache server session (43581723)
* Add ACL actions for read/write access to UBA cache server (43574053)
* Use zeroed IV for UbaCrypto to match HTTP endpoint in UBA cache server (43554905)
* Horde Dashboard: job search url parameters (43545493)
* Horde Server: Fix v2 device reservation exception with unit test (43498441)
* Horde Dashboard: A few tweaks to make the agent modal telemetry more consistent (43496811)
* Add support in compute client for requesting a UBA cache server (43496230)
* Horde Dashboard: Agent telemetry fixes and improvements for Agent and Log views (43495864)
* Perforce passwords and tickets in the BuildConfig can be resolved to the value of a ISecret during post load replacing the need for them to be plain text values (43492230)
* Update UbaCrypto to match C++ impl (43491786)
* Use Win32 API for calculating memory telemetry on Windows
* Use t alias to shorten URLs in JobHandler
* Add shortened template alias to api/v1/jobs/streams/{streamId} endpoint (43433353)
* Default to read/write access for UBA cache for now until per user permissions are added (43390743)
* Horde Dashboard: Show "time to step" in trends panel (43350691)
* Fix device checkout expiration link and clarify instructions (43350167)
* Use new encrypted /addsession endpoint for allocating UBA cache sessions (43349167)
* Adding Horde job/step metatagging (43276253)
* Horde Server: Add job start time to JobStepRef response (43274814)
* Add C# implementation of UBA crypto (43257937)
* Horde Dashboard: Disable job step retries with expired TempStorage (43233123)
* Fix sorting agents by status (43230711)
* Support debugging jobs as scheduler (43229680)
* Allow sessions to get job responses and post job metadata (43228360)
* Add HTTP config values for UBA cache in compute cluster config (43225086)
* Add API to resolve a string to a secret property value (43224431)
* Update Secrets plugin with a new server configuration option WithAws that controls if the secret provider AwsParameterStore is to be enabled (43222303)
* Adding capabilities to the warning defaults, cleaning up tests, adding more warning+error strings for easier long term adoption & toggling. (43196850)
* Add a "No Suspects" emoji indication to build health slack threads that lack suspects (43191214)
* Use preferred colors for device status, slightly increased brightness threshold (43187852)
* Use code commit tag to get suspect CLs (43161937)

## 2025-06-05

* Add C# implementation of UBA crypto (43257937)
* Horde Dashboard: Disable job step retries with expired TempStorage (43233123)
* Fix sorting agents by status (43230711)
* Support debugging jobs as scheduler (43229680)
* Allow sessions to get job responses and post job metadata (43228360)
* Add HTTP config values for UBA cache in compute cluster config (43225086)
* Add API to resolve a string to a secret property value (43224431)
* Update Secrets plugin with a new server configuration option WithAws that controls if the secret provider AwsParameterStore is to be enabled (43222303)
* Adding capabilities to the warning defaults, cleaning up tests, adding more warning+error strings for easier long term adoption & toggling. (43196850)
* Add a "No Suspects" emoji indication to build health slack threads that lack suspects (43191214)
* Use preferred colors for device status
  Slightly increased brightness threshold (43187852)
* Use code commit tag to get suspect CLs (43161937)

## 2025-06-02

* Cloud DDC - Added cross bucket search endpoint (43145204)
* Horde Dashboard: Improved modal warning for downloading artifacts (43112786)
* Ensure clang and clang++ errors are matched (43108898)
* Horde Dashboard: Error Retry Callback (43108710)
* Use Razor template for /account (43107401)
* Add support for allocating a UBA cache server session for compute resource requests (43103819)
* Horde :  Fix ScheduledDowntime GetNext to return next or active downtime
  If the downtime is active return the start and finish times for the active schedule.
  If the downtime is not active return the start and finish times for the next day or next week schedule.
  Downtime Service Tick ignores next times that are in the past, which is for downtime that are non-repeating. (43099696)
* Fixed an exception that was occuring in the LocalizationIssueHandler (43075154)
* [UBA] Fixed C# static analyzer error in SchedulerImpl.cs (43053139)
* [UBA] Changed UbaExecutor to use Uba's scheduler. Reason is to be able to implement shadow processes and also dynamically add more processes when things like llvm pgo wants to distribute internal processes.  Moved logging out of ImmediateActionQueue and into base class ActionLogger. This made it possible to reuse logging logic for UbaExecutor (42998941)
* Horde Dashboard: Fixed HistoryModal.tsx hot reload issue (42743008)
* Horde Dashboard: Alt-text for project logos (42649235)
* Horde Dashboard: Click-to-dimiss for modals (42648635)
* Fix issue with agent modal not refreshing when modified, tweak disable comment attribution (42641626)
* Horde Dashboard: Require agent disable reason and auto postpend username (42603556)
* Horde Dashboard: Improve job template selection when on an exiting job (42596903)
* Horde Server: Don't require a user from user collection when creating devices, for service accounts (42593578)
* Display warning to surface rendering issues on large log files
* Disable trace level logging of agent channel data (42527178)
* Prevent per-cluster Perforce errors from aborting changelist replication loop.  With an invalid Perforce cluster or no available servers for that cluster, such an exception would bubble all the way up to the tick method. This lead to the replication loop restarting. (42483815)
* Update Remote Shader Compilation Horde docs with the new ini configuration. (42451263)
* Do not include steps with warnings in failed steps for job retries (42430419)
* Horde Dashboard: allow option for UserSelect to autofocus (42397046)
* Horde Dashboard: Fix autosubmit link when swarm url isn't defined (42384179)
* Fix publishing step for Horde installer considerations (42375246)
* Hprde Job Driver: Downgrade TempStorage retry error to warning, so it does not fail step (42342223)
* Horde : Add missing properties to agent.  Add RAM size for all platforms.  Add core count for ARM based Macs (42333756)
* Horde Storage: Downgrade a file missing exception to a warning as will retry and should not fail step (42299162)
* add support for engine version comparisons as preprocessor conditions (42297433)
* Make it so non-admin users can see, though not edit notices (42288524)
* Fix issue with default incremental pcb artifact (42249038)
* Ensure agent registration token is used in dedicated mode (42176590)
* Horde Server: Marshal metadata job step id by string (42102607)
* Horde Server: Adding job and step meta data tagging (42099956)
* Horde Dashboard: Add support for multiple plugin mount points (42096956)
* Add support for multiple report summaries (42058201)
* Horde : The Notices API Get method will return the start and finish times when values are set (42048584)
* Silence stderr from 'security' calls on Mac as these interfere with OidcToken --ResultToConsole. (42034179)
* Add KnownLogEvents.Horde_BuildHealth_Ignore to suppress build health issue generation regardless of log level (41916679)

## 2025-04-24

* Pass telemetry store id to Epic Telemetry backend. (41897043)
* Added IssueHandlersTests to Horde's unittests (starting with a LocalizationIssueHandler test) (41887180)
* Improved LocalizationIssueHandler to cover more incoming edgecases (when changing from UE_LOG to UE_LOGFMT) (41886613)
* Horde Storage: Do not throw errors for duplicate keys on shadow blobs as these are expected (41872054)
* Downgrade unknown batch error due to missing shelved CL to a warning (41871559)
* Horde Server: Downgrade issue escalation channel not existing to a warning (41871033)
* Revert back to display field for Analytics (41861309)
* Remove Microsoft.Management.Infrastructure from agent telemetry
  Removes the NuGet dependency for both agent and JobDriver. Using CIM is slow and also adds dependencies incompatible with Wine. (41860782)
* Remove use of Microsoft.Management.Infrastructure in agent service upgrade
  Using CIM is slow and also adds dependencies incompatible with Wine. (41860494)
* Horde Server: Adjust http timeouts and fix issue with storage backend and global http client timeout (41823434)
* Horde : Add docs on how to create an external secret provider (41816354)
* Add exception handling when reporting job metric so that it doesn't suppress notifications (41788849)
* Horde issues now support the annotations AutoAssignToUser as a fallback to AutoAssign (41768375)
* Add logging to debug job step warning/error notifcations coming through (41681308)
* Add cloud url copy operation to artifact split button (41645110)
* Add a new workstation mode to agent
  * An agent in workstation mode is expected to authenticate as the current user, and not via a pre-shared token like in a buildfarm scenario
  * Uses interactive auth via EpicGames.Oidc to authenticate (41643324)
* Log reason when compute task allocation fails in compute client (41640994)
* Test case-insensitivity for pool ID when matching resources for compute cluster (41640817)
* Add pool ID argument to "compute run" in Horde CLI tool (41640803)
* Skip executing conform leases if agent is running in workstation mode
  Workstation agents only deal with stateless compute leases. (41640666)
* Fix Commits Viewer and Robomerge links not appearing in slack messages (41612887)
* Log server profile used during agent startup (41600533)
* Log type of lease being executed in agent (41600484)
* Stop disposing of supplied HTTP handler in HordeClient
  When enabling interactive auth for the Horde agent, this error came up as the handler was improperly disposed.
  This fix addreses that as lifetime should be managed by caller, in this case, the service provider/DI. (41569693)
* Allow only compute tasks to be scheduled on workstation agents (41565320)
* Add Horde's user claim when issuing JWT tokens for built-in Horde auth mode (41533085)

## 2025-04-10

* Run tests for experimental plugin in Dockerfile builds (41526895)
* Add agent property for opting out from server-initiated updates of agent software
  In the case of agent running on workstation, this ability allows for more control and avoids the agent process restarting itself. (41526788)
* Do not run matchers on partial json log line entries.
  This prevents us from trying to match unreliable log lines which were only partially written out e.g. due to the process crashing mid output. (41526045)
* Fixed warnings in LocalizationIssueHandler.cs (41525967)
* Improved Horde's LocalizationIssueHandler to get ready for many C++ localization warnings to change to structured logs (UE_LOGFMT) (41519652)
* Change GlobalConfig.PostLoad and IPluginConfig.PostLoad methods to async
  This is to allow, in the future, config's to use async APIs for example to fetch secrets.
  Some tests had setup in constructors changed these to use the TestInitialize attribute. (41483040)
* Make update session call more robust
  * Catch HttpRequestException in addition to RpcException
  * Implement exponential backoff starting at 1s (max 20s)
  * Increase max retries from 3 to 8
  * Improve logging slightly to explain attempt count and delay (41414189)
* Add topological sort of plugins by their dependencies to call the PostLoad Method in the correct order (41410626)
* Add admin claim resolution for Horde auth mode
  Admin claims are now properly resolved when users sign in using Horde auth mode, matching the existing behavior in OIDC/Okta auth handlers. (41410594)
* Replace display field with unit field for Horde Analytics, and add Bytes, BytesPerSecond, Seconds, and Percentage options (41366205)
* Display the year when not current to reduce confusion when looking at historical jobs (41362940)
* Improve data fetching error and surface network connection as likely cause on job page (41362331)
* Fix step ETA times sometimes being in the past (41355458)
* Add better indicator for inidividual data points in Horde Analytics (41352087)
* Oidc Token - Added ability to read encrypted oidc configuration from a url. Similar to the horde url but encrypted and not exactly the same configuration layout. Is intended to be able to replace the horde url as a more generic option. --AuthConfigUrl for OidcToken.exe
  This should also hopefully cover most of the cases for submitting a oidc-configuration.json into source control.
  Also added --ConfigPath as a way to explicitly set the path to oidc-configuration.json if redistributing this outside of the ue engine sync.
  Lastly I added source code generators for json serialization and configuration builder to enable AOT compilation of OidcToken (which is not used yet). This also fixes most trimming warnings. (41351365)
* Fix issue with artifacts not properly loading between step navigation (41327584)
* Set minimum value for the Package and Content charts in Horde Analytics to zero (41316971)

## 2025-04-03

* Horde Analytics: Fix issue with sum values updating without matching on group (41278970)
* Improve tracing spans in job scheduler (41276718)
* Dashboard and Metrics tweak. Used the wrong metrics for the Package Dashboard. (41273073)
* Add Commit Viewer link to Slack message (41240323)
* Add Show Telemetry option to view on Conforms (41238287)
* Added the ability to specify/override the HordeTelemetryApi when using a HordeAnalyticscProvider in StudioTelemetry from either the commandline or the environment variables.
  Use -HordeTelemetryApi='api/v1/telemetry/xxxx' on the commandline or set UE_HORDE_TELEMETRY_API='api/v1/telemetry/xxxx' in the build environment.
  Moved some reporting aroudn in Horde Analytics to accomodate the changes:
  Moved IoStoreChunks and VirtualAsset telemetry for Fortnite to the product specfic API in Horde Analytics.
  Moved the Fortnite specifc metrics for Packages from Engine to the Fortnite specifc metrics file and dashboard.
  Removed traces of product names from Engine metrics and dashboard. (41228862)
* Add Wine override setting to JobDriver
  Applies to all jobs executed by this driver process, and overrides the value set by JobOption (which is better used for per-job settings). (41198833)
* Aded Creative pack chunk metrics to Horde analytics (41196875)
* Use agent's self-reported tool ID for tool selection during upgrades
  Agents now send this information to provide correct hinting for what tool to use. For example multi-platform, self-contained, or even custom tools for workstation-based agents. (41193761)
* Added Horde_TemplateName as a variable in the Horde Analytics dashboard (41193329)
* Added package metrics for product names
  Added Per Product sizes to Horde Analytics dashboard (41149815)
* Add an agent mode distinguishing between dedicated and workstation agents (41139404)
* Added Package view to Horde Analytics Dashboard. 
  This view will display sizes per package and total package sizes per install type ( Installed, Optional, IAS, IAD ).
  Added InstalType variable
  Added a space to Hardware Platfrom variable name (41136887)
* Added Package Metrics to Horde Analytics (41119806)
* Added IoStore Telemetry to Horde Analytics
  Changed the Target attribute name to Platform so it aligns with other use cases in the analytics backend
  Bumped the IoStoreChunk event schema to 8 (41114074)
* Revert updateFilterItems optimisations (41106999)

## 2025-03-27

* Remove log warnings in agent happening during a normal, clean shutdown (41102934)
* Use status color user preferences for error and warning issue colors and controls (41076656)
* Use short template query param to avoid URL char limit in job search (41075171)
* Improve empty shelf error message on preflights (41070379)
* Report agent's tool ID as a property (41065929)
* Add support for filtering by status and showing cancelled builds in job search (41028739)
* Run agent's management server on dedicated thread (41027669)
* Use a shared ticker update for assigning Jira build health issues (41021755)
* Added support for WITH_TESTS (41021713)
* Reference scheduled downtime from config homepage (41019958)
* Add documentation for Scheduled Downtime (41018403)
* Fix issue with device health report bombing (40989153)
* Rate limit jira to horde issue sync for slack and other considerations (40985841)
* Improve performance of updateFilterItems (40933175)
* Fix param name in XML comment (40907450)
* Fixed badly formed XML warning (40906936)
* Add shortened alias for templates query param on api/v1/jobs endpoint (40905858)

## 2025-03-13

* Add retry policy for blob writes when getting an upload redirect (40683860)
* Add issue summary badges to job label overview, capped at 20 (40652838)
* Perforce server page visual fixes & linking from status page (40650335)
* Dashboard side of device cleaning suipport (40650241)
* Updated supportsPartitionedWorkspace doc (40580744)
* Constrain jupiterdownloads to UGS (40577651)
* Improve description of OIDC client secret config value to clarify public vs confidential client requirements (40575283)
* Add tooltips to issue navigation buttons (40572897)
* Lower log level for socket send buffer teardown from warning to info. This reduces log "pollution" until a better communication channel for Horde-specific diagnostics can be implemented. (40543971)
* Make parameter boolean checks case insensitive on new build dialog (40540848)

## 2025-03-06

* Update approved licenses (HTML changes) (40539666)
* Properly encode zen backend into uartifact files (40510907)
* Add parent/child job relationships, dashboard side (40508326)
* Adding zen backend to artifacts controller uartifact generation via metadata (40502152)
* Add device cleaning information to devices endpoint (40463300)
* Remove incorrect return type metadata for PUT /api/v2/devices/{deviceId} (40461854)
* Added ability to specify backends in uartifact files and added a prototype of how it could be used in the create cloud artifact task to generate a uartifact file that UGS could sync. (40451830)
* Update notes on Epic's deployment (40349855)

## 2025-02-20

* Explain GitHub image registry auth better in docs (40164690)
* Add -snapshot parameter to BuildCookRun, which will retrieve the most recent snapshot published to Jupiter and cook incrementally on top of the fetched data. (40156692)
* Auto assign Horde issues to Jira owner when possible (40147283)
* Add support for automated device cleaning on CI and shared kits, server side changes (40143179)
* Extend compute socket test timeouts to eliminate flakiness Tests were intermittently failing when CI resources were under load. (40137698)
* Allow service accounts to modify devices (40134987)
* Added verification that interfaces have matching native interface base classes (40091963)
* Warn when OIDC is configured with confidential client as this will prevent command-line auth (40060901)
* Cloud DDC - Fixed exception when a object id is encountered as we convert it to json (39986461)

## 2025-02-13

* Return explicit 500 status for VCS operation failures in create job REST endpoint (39941708)
* Downgrade Slack message failure from error to warning log level (39940773)
* Handle gRPC client disconnects more gracefully for log tail streams (39940732)
* AutomationHub, use ref time instead of CL time in summaries when available (39912687)
* Automation summaries, use latest test data fime for a CL (39911090)
* Fix incorrect IMDSv2 detection in agent (39903273)

## 2025-02-06

* Do not forward cancellation token to cleanup job driver as this will interrupt it (39755347)
* Fix Linux runs. (39727551)
* Add specialized status message support and render as Markdown (39721461)
* Add breadcrumbs to templates main menu, to indicate all or partial selection in submenus (39633740)
* Refactor job handler lease exceptions to run batch cleanup code (39633282)
* Add UX to log view to skip to next block of issue lines (39633254)
* make C# code compilation not require a .csproj in the project being tested.
  - UAT will now also recurse in Builds directories of plugins (if they happen to have them). (39601473)

## 2025-01-30

* Validate ServerUrl for auth modes and load balancers in settings
  - Add ServerUrl validation check for non-anonymous auth modes to prevent redirect issues
  - Add middleware to detect reverse proxy/load balancer deployments
  - Log warning when ServerUrl is not configured in proxy environments
  - Forgetting to set this has been a source of issues when deploying Horde (39586905)
* Fixed build warning in CbField json serialization (39586077)
* Fixed incorrect json serialization of cb fields that had fields without names (mostly encountered when serializing an array). (39585891)
* Add triage issue severity workflow setting to enable warning/error only workflows (39563253)
* Return non-200 status code when health check fails in agent
  Also improve logging when failing to bind agent HTTP server to all interfaces. (39550702)
* Improve where optional attributes can appear in enum, class, and struct definitions. (39548575)
* Re-enable and fix tests for agent's HTTP server (39546007)
* Add small HTTP server in agent for administration and health checks
  In this initial version, the health check endpoint returns a basic status whether it is connected to the Horde server or not. (39518264)
* OidcToken - Added missing copyright to tests and fixed other analyze errors in oidc token (39513944)
* Adding artifact type expiry information to dashboard config query (39513838)
* OidcToken - Refactored token manager to allow for tests and added some functional tests (39512897)
* Cloud Artifacts - Support uploading files larger then 2GB - implemented support for compressed buffer serialization in C# to read from a stream thus supporting compressing buffers larger then 2GB, still allocating this all in memory so will use a lot of memory for large buffers. (39512887)
* Run job executor cleanup when the job batch process fails or hangs (39494168)
* EpicGames.Perforce: Handle ... syntax with file extension in view mappings (four dots) (39478199)
* PR #11833: Add throw if Refresh token was not returned in response. (39477147)
* Display N/A Start and Stop Times for User Notices (39438421)
* Add Select All to small Templates submenus (39438351)
* Show partial elements selected on Templates context menu (39438288)
* [EpicGames.UBA] Fixed so uba logger log scope logic works (39429420)
* Add database schema migration tool for MongoDB (39407820)
* Fixed ToolTips not working on List type parameters in stream configs (39407062)
* Prevent exception swallowing during job batch execution (39371897)
* Reverse job audit logs to be top down (39356984)
* Add job audit view to dashboard (39356298)
* Add job audit logging for tracing of the state machine (39354365,39346324)
* EpicGames.Core: Fix unsafe type cast in LoggerScopeCollection.Scope constructor (39343576)
* Improve logging for job step log writer errors (39323447)

## 2025-01-14

* Set the "background": true flag when creating new indexes, and replace the deletion of unused indexes with a warning message. We don't want old pods to delete indexes created by new pods. (39182560)
* AutomationHub now loads all streams by default when none are selected (39089285)
* Improve preflight autosubmit messaging (39088492)
* Horde Log search improvements (39061326)
* [UE-192322] Job dialog: improve navigation of long dropdown lists
* Add a "Copy Artifact ID" to artifact button (39054641)
* Few small changes to handling of chunked data nodes.
  - ChunkedDataNodeRef now implements IHashedNodeRef, and extension methods now allow extracting chunked data to streams or files.
  - Adding a FileEntry no longer requires an explicit length parameter (this is already contained in the payload data).
  - Rename FileEntry.StreamHash to FileEntry.Hash (39004911)
* Add telemetry spans to IssueService (38934459)
* Include OidcToken app in Horde's .sln file (38920316)
* Include lease ID in returned properties for allocated compute tasks (38917576)
* Expose lease ID as an env var for jobs (38906012)
* Send final capabilities update before agent session termination (38886583)

## 2024-12-17

* Pass through OIDC scopes for API authorization.  Allows requesting custom delegated scopes for tools using OidcToken, like UGS interacting with Horde.
* EpicGames.Core: Add extension method for redacting sensitive values in request URIs (38781177)
* Added a flag to allow agents to auto restart on failure.  When installing the agent via HordeAgent service install, you can specify -AutoRestartService=true/false, defaults to true. It will set the three failure recovery options to Restart the Service (38780557)
* Use consts for LogicalCores and PhysicalCores values (38779497)
* Collect system telemetry on Linux (CPU/mem/disk usage) (38779426)
* Support multiple templates in job redirector queries (38755881)
* Make job redirector default to latest step regardless of state, with a complete query param option (38754699)
* Add job/step information to device issue audit logging (38754008)
* Cloud DDC - Added missing documentation on HttpContent extensions (38751527)
* Cloud DDC - Fixed issue were put blob responses did not correctly support being converted to compact binary. This involves avoiding anon types so that we can flag the fields for cb serialization. Also moved the tooling to read compact binary from a http client from Jupiter into EpicGames.Serialization for easier access. (38748909)
* Lower timeout for ComputeSocket send buffer detach (38748804)
* Analytics documentation update to consider new plugins setup. (38731447)
* Cleanup ResolveWarning API to remove buildconfiguration delegate, and instead apply target defaults in new API. (38674255)

## 2024-12-09

* Disable low cardinality indexes for Storage.Blobs collection (38662017)
* Moved Clear Selection and All Templates to a single checkbox, and added a Select All/None for each category with at least 3 options. (38577053)
* Add queryable Horde analytics meta data for better performance and to limit data set for top/bottom metrics (38571863)
* Cloud DDC - Fixed build warning from change 38567473 (38567875)
* Cloud DDC - Fixed an issue serializing a CbObject that was a part of a poco type. The collection of CbFields was picked up the serializer meaning the object was written as an array with fields (which is not valid json). (38567473)
* Add support for enumerating analytics metrics and providing multi-group filters (38551348)
* Improvements to analytics top/bottom N capture (38547327)
* Improve agent telemetry display (38545837)
* Make Preferences, user icon, and Tools middle-clickable. (38542886)
* Add a bit more information when can't resolve preflight stream (38541374)
* Cloud DDC - Fixed an issue were the json converted value of a object id was a base64 blob instead of a hex encoded string as we normally do. (38540397)
* Cloud DDC - Fixed some issues when converting from a json object to a compact binary object in the builds api (38534145)
* Make OIDC scopes and mappings overridable
  - Due to .NET config only merging array entries when combining config sources, hardcoded OIDC defaults (scopes and claim mappings) cannot be overridden.
  - Added OidcAddDefaultScopesAndMappings flag to control whether defaults are applied (38440554)
* Added Content Metrics to Horde Analytics
  Added Content vizualization to Horde Analytics
  Records the total sizes and virtualiszation level of all source content (38440278)
* EpicGames.Slack: Fix compiler warnings (38439811)
* CpuCount and CpuMultiplier in AgentSettings used in LogicalCores property
  - Limit the number of logical processors to CpuCount or number of logical processors reported by the OS, whichever is the smallest
  - Scale the number of logical processors by CpuMultiplier
  - Change CpuCount to a nullable value type to know when it is to be used
  - Find the number of logical processors on Windows
  - Set UE_HORDE_CPU_COUNT to CpuCount when CpuCount is set (38438211)
* Fix crash in UHT by adding a default include path in case none were found (38374613)
* Fix regex in mongo log/config check to be more inclusive (38335134)
* Cloud DDC - Fixed build warnings with the newly added CbObjectId type (38332782)
* Cloud DDC - Added initial version of a build api, this is intended to be used by cooked output snapshots and unsync builds. Its practically very similar to how snapshots already work but with a added index of which blocks exists so that it can be queried from the server rather then rely on input of a previous build which should help make better choices that increases dedup. 
  This api also provides simple searching which is better then what we had for the DDC api (which just had enumeration under special circumstances) which is needed to identify which snapshot to use.
  Lastly I added support for ObjectId in C# as this isn't something we have needed before but is used within objects for this API (is a 12 byte identifier that is valid within a object but not globally unique, it is similar to a uuid type 4).
  The build store currently only supports Scylla and Memory implementations (e.g. no base cassandra or mongo) as we have concerns about those dbs ability to perform for this workload. (38329536)
* Fixed Localisation warnings to correctly add a filepath which can be used for identifying suspected CL's which caused the issue. (38329203)
* Fix issue with failed batches not being able to retry steps (38299359)
* Merging Horde changes up to CL 38271598. (38274254)
* Merging NuGet package upgrades to 5.5.
  Source changes:
  - 36793185 (Update Blake3)
  - 36994680 (EpicGames.Core: Pin System.Text.Json to 8.0.5)
  - 37007077 (Update Logging nuget packages)
  - 37007445 (Missed a file from the logger nuget update)
  - 37011466 (Update more nuget packages)
  - 37033668 (Update more nuget packages (all system or ms that don't depend on System.Drawing.Common))
  - 37097577 (Update nuget packages)
  - 37753709 (Update nuget packages)
* Fix missing IClock for dependency injection This missing dependency caused agent startup failure when EC2 mode was enabled. (38259940)
* Add support for IMDSv2 for AWS EC2 in agent.  Used for detecting spot interruptions and auto-scaling changes on a running agent. (38251784)
* PR #12533: Fix Horde configPath for globals.json with perforce:// uri not working (38221010)
* Add additional full download size info to the current BuildDiff report (38220701)

## 2024-11-19

* Rename default GetSparseClassData accessor to GetMutableSparseClassData to make mutability more obvious and to encourage use of more explicit accessor with EGetSparseClassDataMethod parameter - deprecating old version (38208446)
* [UBA]
  * Changed caching code to use new uba path which shares logic with vfs
  * Removed last parts of OutputStatsThresholdMs
  * Removed code wrapping root path instance now when we use handles instead (38199398)
* Fix not being able to set a device to base model if another model is set (38193884)
* Add support for device audit logs, frontend (38192783)
* Improve device audit messages to include the user changing properties (38192679)
* [UBT]
  * Removed system roots and autosdk root now when all modules should provide the roots they need (38192101)
* Add device audit logging, server side (38189896)
* Decrease test time for AES transport test.  Removes the largest buffer size to reduce test time. (38186094)
* Changed the Context and LoadingNames in EditorTelemetry to be consistent with the Span Names (38183893)
* Add TopN/BottomN sampling constraints for Horde Analytics (38181517)
* Add download with Unreal Toolbox preference and generalized artifact button (38181432)
* Add CLI argument for loading additional config files.  Allows specifying custom config file paths, enabling more flexible configuration in buildfarm environments instead of relying solely on built-in default locations. (38180737)


## 2024-11-15

* Prevent exceeding open file handle limit on MacOS when uploading data. (38125783)
* Remove EC2 IMDS server availability check during startup (38123434)
* Fix an issue where blobs uploaded out of order can cause references to be dropped. Now creates a "shadow" blob info record for any referenced blobs that don't exist, and converts that to a regular record when data is sent. (38121174)
* Preserve type URLs in Protobuf messages for upgrades and conforms during serialization. (38119696)
* Fix test flakiness in ComputeSocketTests. Increase test timeouts and disable parallelization to handle CPU contention better during parallel test execution. (38088741)
* Ensure agent stopping state is reported to server (38085676)

## 2024-11-14

* Handle EndOfStreamException in RecvOptionalAsync. Treat EndOfStreamException same as zero bytes read, returning false to indicate no data available rather than propagating the exception. (38077833)
* Forward completion event to inner in IdleTimeoutTransport (38077823)
* Change back to 16 read tasks for batch reads from storage. (38057408)
* Set maximum size of channels used for buffering data read from storage. If part of this pipeline is particularly slow (eg. I/O, network, CPU), is can cause large backlogs of buffered data which can kill performance or cause machine instability. Applying some back pressure resolves the issue without any loss in performance. (38051395)
* Log encryption used for compute tasks in lease log (38031944)
* Make port allocation for tests thread-safe (38016954)
* Add explicit clear selection to template chooser (38007851)
* Enable parallel test execution for EpicGames.Horde.Tests. Fixed race conditions by adding unique suffixes to shared test directories. (38002324)
* Allow leases running in a stream to access artifacts in that stream by default, if not overridden elsewhere. (37996421)
* Replace AesTransport with a new implementation. Uses random nonces for each message instead of a base nonce. Add TCP transport integration tests (37992625)
* Add parameter for controlling inactivity timeout for compute executions. For UBA-based compute tasks this value needs to be increased as CPU or I/O can at times get starved, leading to a timeout. (37960155)
* Job search improvements, link to related jobs from operations bar, step callout optimizations (37920079)
* Improve symbol indexing time; perform hash calculations in parallel, and upload aliases in smaller batches. (37898457)
* Hide agent install console window and skip uninstall registration in Windows Registry (37874147)
* Fix agent uninstall directory path when registry path is not used (37874091)
* Fix CLI tool upload using named blobs (37873422)
* Show critical log levels as errors in the log view (37845567)
* Add an IStorageWriter interface, which allows buffering and batching metadata update requests. Use this to upload artifact aliases in a single batch. (37843288)

## 2024-11-06

* Fix cached commit source decrementing number of remaining results to return even if commits are filtered out of the resulting list by commit tags. Prevents issues with new jobs not being able to identify code changelist to run at. (37833532)
* Add support for parallel uploads of bundles. Bundle locators are now assigned by the client, allowing the next bundle to be assembled before the previous one has finished uploading. (37808051)
* Include a flag in template responses indicating whether the user can run it. (37792772)
* Show steps with issues inline when there is a label selected (37791398)
* Fix PerforceServiceCache.SubscribeAsync() stalling out when more than 10 commits are parsed in a single iteration, due to changes being enumerated in the incorrect order. Now uses a custom query to search for commits in the correct order, and only uses cached commit metadata. (37790919)
* Add flag for registering agent for uninstall in Windows. If the agent is installed via Unreal Toolbox but later uninstalled via Windows add/remove programs, Toolbox won't be notified. Until the uninstall command can be routed through Toolbox, this flag prevents Windows from handling uninstallation directly. (37743944)
* Do not try to parse portable PDB files. These are not supported by symstore. (37743471)
* [UBT] CppCompileEnvironment to use CppCompileWarning instead of raw properties (37696325)
* Add option for using uninstall location from registry (37693256)
* Remove EpicGames.Perforce dependency from agent (37693017)
* Include toolbox.json in agent with process elevation (37692524)
* Fix storage client initialization in tool upload command. (37650068)
* Fix DI error with Horde client tool commands (37646970)

## 2024-10-31

* Use lowercase for processor architecture in agent property names (37575294)
* Fix lease manager ignoring session termination requests when no lease is active (37575102)
* Make LeaseManager testable in agent. Previously used FakeHordeServer reinstantiated and session result now contain a reason leading to the outcome. (37574891)
* Right Click for extra Agent Info options isn't communicated in the UI, leads to support overhead. Improves UX communication by adding a Pencil Icon Button to column header which also displays the context menu when clicked. (37566738)
* Fix issue with password not being sent when creating a new account if it is provided (37514227)
* Add install/uninstall commands for agent (37499477)
* Add better error message when agent IPC server cannot start (37499449)
* Maintain order when selecting deselecting steps (37476215)
* Fix some step retry logic (37472690)
* Expose CPU and OS architecture as agent properties. Needed to distinguish between x86_64 and aarch64 for upcoming support in UBA. (37443665)
* Add cluster to perforce config check (37429576)
* Reduce size of self-contained agent. Removes unused libs from published directory, such as Linux and macOS versions accidentally making their way in. (37428045)

## 2024-10-24

* Fallback to normal change number for BuildGraph invocation if code change is missing (37388671)
* Prevent send and possible exceptions in background send for ComputeSocket (37387020)
* Fix undefined access in job vew (37350938)
* Update dashboard install docs for Vite (37350502)
* Device view fixes (37350192)
* Tweaks to compact views to support hiding/showing names (37350158)
* Slowed the read rate to avoid out of memory issues. (37323730)
* Fix hanging chrome when missing job template, also surface job error (37322037)
* Set agent's default setting for EC2 support to disabled
* Adding robomerge track change messages to slack messages. (37266636)

## 2024-10-17

* Delete ephemeral agents that have been offline for more than X hours. This ignores checking for any outdated sessions and simply uses the last online timestamp instead. (37172088)
* Show logs event data as generating for running steps when missing (37125109)
* Always show the reason for why compute resources may not be available (37114936)
* Do not retry HTTP requests which throw an HttpRequestException. These generally represent a non-transient error, unlike HTTP status responses. In the case of bad server addresses or ports, requests may take a long time to timeout. Retrying them does not make sense, and creates very long delays before the result is reported. (37093049)
* Cloud DDC - Changed server timing metrics into a concurrent bag instead of list with locks to reduce lock time a bit. (37085986)
* Fix for calling Remote UFUNCTIONs from other Remote UFUNCTIONs. (37066038)
* Instrument ManagedWorkspace with OpenTelemetry tracing (37059773)
* Remove use of resource.name in spans resource.name is Datadog concept and doesn't play well presenting traces from OpenTelemetry in the Web UI. (37051252)
* Fix bug with OpenTelemetry settings not being base64 encoded (37049676)
* Add Google Cloud Storage support (37048260)
* Pass OpenTelemetry settings to agent driver subprocess (37047438)

## 2024-10-10

* EpicGames.Perforce: Assume and mark workspace having untracked files prior to starting a sync (36972802)
* Allow HTTP for OIDC discovery endpoints
* Log errors when mongodb config/log files contain non-latin characters as mongodb.exe cannot handle these paths (36930465)
* Improve description text for several tools bundled with Horde. Mention that P4V needs to be restarted after installing P4VUtils. (36912499)
* Use P4 syntax on artifact modal to download current folder (36912473)
* Fix issues with theming and login/setup view for installer build (36907272)
* Don't add all files to filter when there is no selection in the artifact modal to avoid overflowing the max request length (36906929)
* Add Horde's built-in user ID claim when issuing OIDC userinfo for Horde accounts (36899176)
* Ensure auto-conforming is requested only when not already pending (36894611)
* Distinguish between no matching compute resources vs all resources in use (36857071)
* Allow resetting blob id for length scan. (36848920)
* Add schedule auditing (36825106)
* Prevent infinite loop when cancelling during agent registration (36819203)
* Allow clients to specify a locator when uploading blobs. The server will not allow overwriting any existing blob, but allowing the client to determine the locator opens the door to it being able to write multiple blobs in parallel. (36813875)
* Grant claims to leases identifying them as running certain projects, streams, and templates. (36807664)
* Support running jobs in streams that do not have the engine directly under the stream root. The "EnginePath" property in the stream config can be used to configure the path to the engine folder. (36804827)
* Include a Version.json file in Unreal Toolbox builds which can be used to detect upgrades/downgrades. When auto-updating from builds with this file, only upgrades will be allowed. (36789633)
* Unreal Toolbox: Always show the settings dialog on launch unless the -Quiet argument is passed on the command line. This makes a more sensible default than requiring a -Settings argument. (36785739)
* Remove agent enrollment server json upon uninstall (36784558)
* Fix agent store updates upon cache invalidation/deletes (36739556)
* Move UnrealBuildTool.Tests into Engine\Programs\Shared (36735944)
* Fix issues with agent enrollment and deletion latency (36735037)
* Fix bug where leases weren't immediately aborted when agent is marked as busy (36733795)
* Fix issue with artfifact log rendering when there is no artifact type (36730128)
* Fix AgentWorkspaceInfo objects constructed from RPC workspace messages having an empty string for 'method' instead of null, causing agents to get stuck in a conform loop. (36718257)
* Fix issue with enrolling agent from installer goes to default server (36710606)
* Fixed a bug where the same error/warning message using different slash directions would not match with an existing issue when handled by the hashed issue handler (36698600)
* Add more logging and tracing for updating agents via REST API (36697817)
* Allow specifying arbitary key/value metadata on tools, and add product ids for MSI installers. Unreal Toolbox will automatically install/uninstall MSIs using the given product id. (36694744)
* Unreal Toolbox: Various improvements with installation process (36678430)
* Fix bug where not all fields were included in Equals() check for AgentWorkspaceInfo (36667931)
* Add scratch and min conform space to REST API agent response (36667252)
* Fix issue with mixed ungrouped and grouped tool downloads (36647304)
* Use the correct token for writing artifacts. Adds an IArtifactBuilder interface returned from IArtifactCollection.CreateAsync() which can be used to upload artifact data with the correct permissions. (36645064)
* Resurrect option to explicitly update build health issues in advanced parameters (36644246)
* Use same logic for shutting down on MacOS as Linux. (36578059)

## 2024-09-25

* Add filter for agent properties in REST API (36563629)
* Re-add logical cores as an agent property. UBA uses this property for determining number of agents to allocate. (36532400)
* Rename sample stream project to 5.5, and add a ugs-pcb artifact type. (36508675)
* Fix path for constructing Azure storage backends. (36508415)
* Move agent sandbox to be independent of agent settings, not in a hidden folder, and to avoid tripping path length warning by default (36505143)
* Fix issue with conform threshold for workspaces not propagating (36502791)

## 2024-09-19

* Expose .NET runtime version in agent properties (36427479)
* Add a separate build of the Horde Agent MSI for the toolbox, which includes metadata for running the installer. (36425974)
* Remove tray app and idle behavior setting from agent installer (36410206)
* Add support for group and platforms to bundled tools, adjust the build accordingly (36407524)
* Rename HordeToolbox again. Now: "UnrealToolbox!" (36401901)
* Add a dedicated installer for the tray app, and include it in the server installer. (36394095)
* Fix infinite loop issue when querying for jobs (36392029)
* Fix bug where physical cores are counted twice in capability detection (36387918)
* Replace OpenTracing with OpenTelemetry in JobDriver (36386025)
* Document the location of the server config file. (36384366)
* Ensure all streams register default artifact types.  We recently introduced the requirement that artifact types are explicitly configured (CL 35531333). The job driver expects certain artifact types to be present, and jobs will fail if these types are not registered.  This issue affects new installations and older installations upgrading to 5.5. (36383858)
* Remove OpenTelemetry.Instrumentation.AspNetCore from agent (36383393)
* Add an option for automatically updating tools in the tray app. (36356951)
* Fixed a bug where issues would have their FixCommitId set to 0 rather than cleared after re-assignment to a new owner.  Note we set fixChange to 0 in the onAssign function in IssueViewV2.tsx.  I now check whether fixChange is == 0 and if so set FixCommitId to be empty, this causes the FIX CL to be cleared. (36345723)
* Add information about processes with a file locked when it cannot be overwritten when extracting data from a bundle. (36341246)
* Add a dataflow graph with multiple deletion workers to improve GC performance. (36340210)
* Upgrade Horde Agent to NET 8. (36340066)
* Add an exponential pause between iterations over the GC tick queue. This should reduce the time that we spend idle during blob deletion. (36321827)
* Fix changes not being enumerated in descending order by PerforceService, causing the minimum changelist number for ICommitCollection.SubscribeAsync() not being updated. This prevents issue fixed tags from being processed. (36321198)
* Add missing file. (36318745)
* Replace OpenTracing with OpenTelemetry in agent (36307242)
* Move OpenTelemetry settings to EpicGames.Horde for sharing with agent (36303013)
* Added an additional sanitise case for the PerforceMetadataLogger when trying to get file annotations.  We could potentially handle this in LogEventParser AddLine but I wanted to limit the impact of these changes. (36299319)
* Add a proxy server to the Horde tray app, allowing users to connect to the Horde server via an unauthenticated connection bound to localhost. (36292016)
* Add a flag for showing a tool in the Horde tray app. (36284430)
* Add settings menu to tray app, as well as functionality for downloading tools from Horde. (36265467)
* Handle .horde.json file not existing when configuring server url on Mac/Linux. (36264830)
* Changed code traversing workspace on disk to use EnumerateFileSystemInfos instead of directories and files separately. This reduces number of kernel calls and save 20% time on a machine with attached ssd (36149765)
* Add timeouts for available port checking in tests (36117480)
* Add server-defined properties for an agent
  - Allows setting properties that will overwrite and merge with properties reported by agent itself
  - Refactor parameters for agent creation into an options object
  - When a user/agent can create new agents outside enrollment process, it's marked as trusted for the time being (grandfathering existing registration in JobRpcService) (36114930)
* Ignore server-defined properties when sent by agent (36108891)
* Adding Horde installer custom actions, data directory selector, and fix server to be able to bootstrap using custom data directory (36082966)
* Add tooltips for step times (36043646)
* Additional logging for terminating sessions. (36041396)
* Set the max thread count for managed workspace operations to one less than the number of reported CPUs on the machine. (36038061)
* Add better error for failing to start a process from the Horde Agent. (36026676)
* Advertise a new OSFamilyCompatibility property from agents, indicating OSes that the system can emulate (for Linux agents with WINE to indicate Windows compatibility). (36018324)
* Allow specifying a list of properties required for agents to execute compute leases. (36012503)
* Re-enable dedupe for the tools bundled with Horde. The permissions affecting access to this data are now waived for bundled tools. (36012474)
* Fix installed server not correctly identifying code changes correctly. Server was incorrectly setting a flag indicating that all files for a change had been enumerated, preventing it scanning the entire set. (35977472)
* Add a symbol store plugin. Symbol stores use aliases in the storage system to map symbol store paths onto content streams from existing artifacts, allowing reuse of data already available in artifacts.
  - Symbols can be tagged with the appropriate metadata by setting the Symbols=true attribute on the CreateArtifact task. Referencing the namespace that the symbols will be uploaded to from the symbol store config will allow accessing them through the api/v1/symbols route.
  - Hashing for symbols is compatible with symstore.exe, but is handled by a custom implementation in SymStore.cs. (35971584)
* Fix linq expression for generation of alias index. (35967893)
* Pause the addition of new blobs for GC once the queue is longer than 50,000 entries. (35967249)
* Allow graceful draining of leases when yielding to local user activity
  - The termination signal file used for spot interruptions is now written to let a workload know about the lease being drained.
  - This requires workload to scan and respect the file for this to be effective, which UBA does for example. (35962694)
* Add agent version to compute resource class, used for exposing version in compute resource API. (35954995)
* Log Horde server and agent version for a UBA session (35954941)
* Fix name of aliases over blobs collection. (35950154)
* Remove code to read imports from uploaded blobs. These should now be set at upload time. (35949781)
* Support loading artifacts out of log which are not in the step artifacts (35931864)
* Improve issue button rendering to not obscure the structured logging eyeball, also change where we render the structured log (35931043)
* Include artifacts created through the CreateArtifact BuildGraph task in the list of artifacts for the job. (35924757)
* Catch compute cancellation exceptions and avoid flagging them as errors (35923687)
* Guard against socket errors during closing of compute socket, also ensure CloseAsync can't be invoked twice. (35903197)
* Set the UE_HORDE_STREAMID environment variable containing the current stream id when running under Horde. (35900707)
* Document the look up of P4TRUST file (35892976)
* Change tray app to use Avalonia rather than Winforms. (35883986)
* Dashboard side of bundled tool management (35877843)
* Add clear conform to agent context menu (35877280)
* Adding bundled information to tool responses (35876724)
* Fix broken ServiceAccountAuth test (35876245)
* Add name field to service accounts and use that in audit logs, previously, name of service accounts resolved to "unknown". (35873276)
* Keep track of the agents which are currently able to execute sessions.  Each server now caches the state of a session based on the last update time, allowing it to perform most operations with minimal context fetches in the common case where an agent is always being updated on the same server. (35872405)
* Fixed issue in recent UHT changes that prevented errors begin generated from the header file object. (35863940)
* Add agent settings for CPU count and multiplier.  Only provided as hinting to workloads, which can chose to respect these. The initial use-case is letting UBA limit number of CPUs in use, much how maxcpu/mulcpu works for UBT.  Later on these values could configure job objects (on Windows) for proper OS-enforced CPU limiting. (35861968)
* Document use of ssl: prefix for connecting to Perforce servers (35859151)
* Replace NuGet package with more explicit one to reduce dependency size (35822082)
* Fix description and creation time not being deserialized as part of artifact responses correctly. (35811531)
* Store session state in Redis rather than MongoDB. (35780092)
* Add HordeHTTPClient GetUgsMetadataAsync method (35775820)
* Fixed symbol table lookup to properly include the header file when walking up the outer chain. (35773746)
* Agent enrollment improvements (35770720)
* Add REST API endpoint for listing ACL permissions for current user (35770032)
* Fix invalid dep injection of ServerSettings in JwtHandler (35769300)
* Show permissions for ACL scopes on account page, useful for debugging permissions in Horde. (35768929)
* Improve OIDC configuration experience
  - Add debug mode for better explaining why a JWT bearer token is rejected
  - Validate required settings are set for auth mode OIDC/Okta
  - Improve docs for OIDC settings (35768735)

## 2024-08-22

* Lock fluent to fix upstream regression (35716215)
* Add customizable landing pages (35713203)
* Surface when log data is missing in log event (35712179)
* Address additional artifact search feedback (35710162)
* Make it more clear when a step was canceled by Horde vs a user (35709294)
* Add lock around invoking nftables CLI tool (35690511)

## 2024-08-19

* Add ability to override agent's compute IP used for incoming connections (35621653)
* Generate issues for completed job steps asynchronously. (35601740)
* Randomize port assignment for relay port mappings. There's a worry of re-using the same port leads to potential races with how nftables/conntrack cleans up entries. In previous impl, lowest available port number was always used which led to higher contention. (35594027)
* Check for invalid AWS region names in config (35589406)
* Allow configuring the namespace for artifact types, tools, and replicated Perforce data. (35568444, 35568869, 35570127)
* Skip expiration of artifacts if no retention policy is specified. Previous behavior was to purge all that weren't explicitly kept. (35543239)
* Add ushell to Horde installer. (35541567)
* Fix registration of bundled tools in installed builds. (35538391)
* Require artifact types to be declared in config files. Also allow setting expiration policies per-project and per-stream, and handle expiration of artifact types in deleted streams or of a type which are no longer declared. (35531333)
* Improve lease termination handling during AWS spot interruptions. Only terminate session once a lease has finished. (35529116)
* Ensure resolved issues are excluded from queries when resolved by timeout. (35449290)
* Fix rendering of message templates containing width and format specifiers. (35446849)

## 2024-07-29

* Fix changelist number not being returned in artifact responses. (35140759)
* Change artifact paths to Type/Stream/Commit/Name/Id, to reflect permissions hierarchy. (35129994)
* Add extension methods to allow extracting IBlobRef<DirectoryNode> directly into directories. (35094979)
* Preliminary support for VCS commit ids that aren't an integer. All endpoints still support passing changelist numbers for now. (35091676)

## 2024-07-25

* Add a separate flag for marking an issue as fixed as a systemic issue, rather than having to pass a negative value as a fix changelist. Still supports passing/returning negative fix changelists as well for now, but will be removed in future. (35029295)
* Serialize config for pool size strategies and fleet managers as actual json objects, rather than json objects embedded in strings. Supports reading from json objects embedded in strings for backwards compatibility. (35026676)
* Set output assembly name for the command line tool to "Horde". (34984531)
* Separate out log event errors and critical failures into a separate fingerprint when we exceed the 25 event limit in the Hashed Issue Handler. (34930826)
* Add support for tagging log events with issue fingerprints. The $issue property on a log event can contain a serialized IssueFingerprint, which will be parsed by the ExternalIssueHandler handler inside Horde. This property can be added from C# code using the ILogger.BeginIssueScope() extension method. (34913930)
* ParentView is optional per perforce documentation. Fixes Unhandled Exception (Missing 'ParentView' tag when parsing 'StreamRecord') for streams that does not have it set (34890081)

## 2024-07-16

* Support for disabling conform tasks through the build config file. (34836886)
* Allow setting access permissions for different artifact types on a per-type, per-project and per-stream basis. (34836758)
* Allow specifying a secondary object store to read objects from, allowing a migration from one store in the background without causing downtime. (34828731)

## 2024-07-15

* Split server functionality into plugins. Plugins are still currently statically configured, which will be changed in future. (34620916, many others)
* Fix threading issue causing block cache to always attempt to access element zero, causing tests to get stuck in an infinite loop. (34759616)
* Add a debug endpoint for writing memory mapped file cache stats to the log. (34757974)
* Fix resource leaks for blob data reads. (34756957, 34757889)
* Fix tracking of allocated size in MemoryMappedFileCache. (34747643)
* Fix binding of plugin server config instances. (34745408)
* Move build functionality into a plugin. (34744727)
* Respect Forwarded-For header for client's IP during compute cluster ID resolving. Also return a better error message when no network range or cluster is found. (34743435)
* Add support for automatic computer cluster assignment in Horde, based on internal and external IP. (34734153)
* Add endpoint for resolving a suitable compute cluster ID (34706120)
* Set agent property if Wine executable is configured. Needed for scheduling Wine-compatible compute tasks. (34675604)
* Do not log cancellation exception as a warning when checking if blob exists. (34672928)
* Remove legacy artifacts and log system. (34663531)
* Resolve HTTP client IP for Datadog trace enricher (34644555)
* Fix user ID/username not getting set in Datadog enricher for OpenTelemetry traces. Must be accessed once response has been sent. (34636046)
* Support for artifact searches (34605400)
* Log each configuration source during agent start for better visibility where the agent is reading config from. Also log the actual logs dir. (34603382)
* Log where agent registration file is saved to/loaded from. When the file exists, the agent provides no hinting it reads this file which can be confusing when trying to reset the agent. (34600505)
* Add documentation for setting up a secret store. (34569155)
* Add support for compression of all responses using gzip, brotli, and zstd. (34565711)
* Add creation time to artifact responses. (34559619)
* Change config reader to operate on JsonNode instances rather than writing directly into target objects. (34546472)

## 2024-06-20

* Compute a digest for each block stored in the block cache, and validate it before returning values. (34515388)
* Remove requirement to specify a stream or key to enumerate artifacts. (34513227)
* Downgrade log event about duplicate build products to information, since we don't have the list of duplicate build products to ignore in Horde. (34481396)
* Exclude blocks for empty files from the Unsync manifest. (34464454)
* Add IPoolSizeStrategyFactory interface, which is used for creating IPoolSizeStrategy instances. This allows us to remove the graph/job/stream collection interfaces from FleetService, allowing it to exist without any job handling code in the solution. (34457658)
* Do not shutdown disabled agents by default. Prior to this change, this value defaulted to 8 hours which assumes you want this behavior in the first place. Making this nullable allow for opt-in instead. (34455677)
* Remove tracing span from shared tickers. For long-running callbacks, this can create data which is difficult for Otel/DD to handle. (34444738)
* Add a debug endpoint which streams a random block of data to the caller. Usage is: /api/v1/debug/randomdata?size=1mb (34426596)

## 2024-06-17

* Add support for auto-assigning cluster ID during compute allocation requests (34423910)
* Increase MongoDB client wait queue size from 300 -> 5000 (34423018)
* Allow specifying cache sizes using standard binary suffixes (eg. "4gb") (34382251)
* Add the shutdown timeout for Kestrel server (34379789, 34379660)
* Add an AWS parameter store secret provider (34379595)

## 2024-06-13

* Add a pipelined blob read class for storage blobs. (34322019)
* Add a disk-based cache for compressed Unsync blocks. Uses memory mapped files to read/write to underlying storage, LRU eviction via random sampling. (34288152, 34292867)
* Tweak MongoDB client's config defaults to curb wait queue full errors (34276157)
* Resolving of capabilities has been known to stall at times. Adding trace spans to these should help see which part is causing slowdowns. (34232453)
* Add zstd as a compression format for bundles. Also add some basic compression tests, and fix an isuse with gzip streams being truncated. (34211536)
* Include rolling hashes in bundle archives to support unsync manifests. DirectoryNode/ChunkedDataNodeRef now use HordeApiVersion for versioning purposes, rather than their own custom versioning scheme. These blob types will serialize the most relevant Horde API version number they support into archives. (34207581)
* Ignore cancellations in AWS instance lifecycle (34207087)
* Include modification times for file entries in bundles by default, but do not write them unless the configured API version allows it. (34205938)
* Add compute endpoint that automatically finds the best cluster. Currently uses IP of requester together with networks from global config. (34201862)
* Fix some artifacts not showing up in job responses, due to default cap of 100 artifacts returned. (34200332)
* Check agent's compute cluster membership on assignment. Previously it was only checked by the task source. (34199317)
* Set user ID in compute assignment request track usage by user (34195976)
* Disable config updates when running locally in Redis read-only mode, preventing an exception on startup. (34169346)
* Make horde chunk boundary calculation consistent with unsync, add unit tests to validate determinism (34168267)

## 2024-06-06

* Add a retry policy to the Epic telemetry sink, to stop 502 errors causing server log entries. (34141477)
* Add a GC verification mode, which sets a flag in the DB rather than deleting an object from the store. (34135143)
* Fix issue where files will not be uploaded to temp storage if they were previously tagged as outputs in another step. Now allows output files to be produced by multiple steps, as long as their attributes match. (34132683)
* Adding support for optional cancellation reasons for jobs and steps (34131480)
* Forward all claims for access tokens minted for Horde accounts The role claim was missing which is used for resolving many permissions. (34119849)
* Log reasons when a lease is terminated due to the cancellation token being set. (34111700)
* Add a link to the producing job to .uartifact files, as well as other metadata and keys on the artifact object. (34095063)
* Allow user configured backends and namespaces to override the defaults in defaults.global.json. (34092465)
* Print a message to the batch log whenever a lease is cancelled or fails due to an unhandled exception. (34084201)
* Add a setting ("ReportWarnings") to exclude issues which are only warnings from summary reports. (34078316)
* Fix P4 server health not being updated when HTTP health check endpoint returns degraded. (34077493)
* Re-encode json log events which are downgraded from warning to information level. (34066649)
* Add more trace attributes to compute resource allocation (34064620)
* Record endpoint address in MongoDB command tracer, jelps differentiate between primary and secondary use (read-only ops) (34063808)
* Use cached agent data for JobTaskSource, this ticks every 5 seconds refreshing agent and pool data. The cached agent data is refreshed at the same rate. (34062377)

## 2024-05-31

* Use cached agent data for fleet and pool size handling (34029489)
* Fix conform commands using the incorrect access token to communicate with the server. (34012793)
* Allow registering multiple workspace materializer factories. (34010953)
* Add an agent document property for the combined list of pools, and upgrade existing agent documents on read to include it. This allows indexed searches for dynamic pools. (34009396)
* Add a log enricher which adds the server version. (34003927, 34005245, 34004705)

## 2024-05-30

* Update test projects to NET 8. (34002443)
* Refactor WorkspaceInfo to avoid keeping a long-lived connection. Keeping idle connections to the Perforce server after the initial workspace setup is unnecessary and wastes server resources. (33949575)
* Log in to the server when querying for config files from Perforce. (33945114)
* Fix http auth handler not using correct configuration for server when run locally. (33940162)
* Skip cleaning files for AutoSDK workspace. (33858727)
* Only enable debug controllers if enabled in settings (33857362)
* Add a fingerprint description as part of issue response (33833567)
* EpicGames.Perforce: Fix parsing of array fields from Perforce records. (33807413)
* EpicGames.Perforce: Fix parsing of records which have duplicate field names in child records. (33804970)
* Allow overriding the targets to execute for a job. The optional 'Targets' property can be specified when creating a job, and will replace any -Target= arguments in the command line if set. (33799426)
* Pass -Target arguments to individual steps to ensure that BuildGraph can correctly filter out which blocks to embed in temp storage manifests. (33799034)
* Remove server OS version info from anonymous endpoint responses. (#11909) (33793366)
* Allow overriding the driver for jobs through the JobOptions object. (33791494)
* Prevent AWS status queries from causing session update failures. (33782757)
* Move job driver into a separate executable. (33775640, 33782224)

## 2024-05-20

* Add grouping keys and platforms to tools. Intent is for the dashboard to only show one tool for each grouping key by default (the one with the closest matching platform based on the browser's user-agent), but users can manually expand the list of tools to show other platforms if desired.
  Platforms should be NET RIDs, eg. win-x64, osx-arm64, etc... (https://learn.microsoft.com/en-us/dotnet/core/rid-catalog). (33698418)
* Use native GrpcChannel instances for agent connection management, rather than IRpcConnection. (33660725, 33638856)
* Add an AdditionalArguments property to jobs, which can be used to append arbitrary arguments to those derived from a job's parameters. This field is preserved - but not appended - to the arguments list if arguments are specified explicitly. Needs hooking up to the dashboard. (33630321)
* Prevent default parameters being appended to jobs when an explicit argument list is specified. (33628649)
* Move Perforce/BuildGraph functionality into a new Horde.Agent.Driver application. Intent is to separate this from the core Agent application over time, making it easier to iterate on Job/BuildGraph related functionality outside of the core agent deployment. (33606684)
* Move settings for different executors into their own files. (33605105)
* Trap exceptions when parsing invalid workflow ids from node annotations. (33602812)
* Enable HTTP compression for server-sent responses (33600392)
* Add support for agent queries via replica read (33575058)
* Optimize agent assignment for compute tasks by filtering by pool (33488689)
* Add command-based tracer for MongoDB. Old tracer operates at the collection level, this instead listens for events emitted by the Mongo client. (33479889)
* Keep a cached list of current agents in AgentService (33454500)
* Add an explicit error when the server URL does not have a valid scheme or host name. (33427628)
* Move expired and ephemeral agent clean up to shared ticker (33424708)

## 2024-05-01

* Explicitly check that the expiry time is set when searching for refs to expire, so that DocDB will use an indexed query. (33367777)
* Add CheckConnectionAsync to HordeHttpClient. (33353008)
* Add an index to the refs collection to prevent collection scans when finding refs to expire. (33338629)
* Reduce how long ephemeral and deleted agents are kept in database. Heavy use of auto-scaling can lead to excessive history of ephemeral agents being kept. In the case of AWS, each new instance has a unique agent ID. Many of the queries for agents (and in turn indices) are not optimized for a large collection. (33317167)
* Disable file modified errors in job executor. (33299280)
* Remove legacy job methods from horde_rpc interface. (33298879)
* Remove support for legacy artifact uploads. (33298589)
* Enable agents sending CPU/RAM usage metrics to the server. (33290135, 33289584)

## 2024-04-25

* Allow setting project-wide workspace types. Will automatically be inserted to the workspace type list of each stream belonging to the project. This also allows defining base types which can be inherited from. (33213109)
* EpicGames.Perforce: Fix errors parsing change views from labels. (33207649)
* Delete step logs when deleting a job. (33206294)
* Use the shared registry location to configure the Horde command line tool on Windows, and use a .horde.json in the user folder on Mac and Linux. (33205861)
* Rename step -> job step, report -> job report, batch -> job batch in job responses. (33191560)
* Include graph information in job responses. (33189353)
* Add a service to expire jobs after a period of time. Jobs are kept forever by default, though this may be modified through the ExpireAfterDays parameter in the JobOptions object in streams or projects. (33109364)
* Add auto conform for agents with workspaces below a certain free disk space threshold (33102671)
* Use a standard message for server subsystems that are operating normally. (33100047)
* Add min scratch space to workspace config (33098369)
* Recognize tags in Perforce changelists of the form '#horde 123' as indication that a change fixes a particular issue. The specific tag can be configured in the globals.json file to support multiple deployments with the same Perforce server. (33089498)
* Clear the default ASPNETCORE_HTTP_PORTS environment variable in Docker images to prevent warnings about port overrides on server startup. We manage HTTP port configuration through Horde settings, and don't want the NET runtime image defaults. (33072683)

## 2024-04-18

* Add dashboard's time since last user activity to HTTP log (33068679)
* Fix agent registration not being invalidated when server is reinstalled. Server returns "unauthenticated" (401) response, not "forbidden" (403). Forbidden is only returned when the server deliberately invalidates an agent. (33060431)
* Send DMs to individual users rather than the notification channel when config updates succeed. (33060054)
* Prevent hypens before a period in a sanitized string id. (33044611)
* EpicGames.Perforce: Fix parsing of records with multiple arrays. (33038480)
* Do not allow multiple agent services to run at once. (33037301)
* Handle zero byte reads due to socket shutdown when reading messages into a compute buffer. (33034396)

## 2024-04-12

* Ensure blobs uploaded to the tools endpoint have the tool id as a prefix to the blob locator (32907981)
* Allow specifying a unique id for each parameter in a job template (32906466)
* Send Slack notifications whenever config changes are applied (32897388)
* Make log output directory configurable in agent (32886152)
* Use strongly typed identifiers for artifact log messages in agent. (32861971)
* Fix memory leak and incorrect usage of native P4API library (32798937, 32794622)
* Add debug endpoint for capturing a dotMemory snapshot (32790300)
* Refactor API message names, project locations and reduce gRPC dependency (32785690, 32783652)
* Allow clients to upload the list of references for each blob, removing the need for the server to do it later. (32768299)

## 2024-04-05

* Handle socket shutdown errors gracefully rather than throwing exceptions. (32756972)
* Add log message whenever native P4 connection buffer increases in size above 16mb. (32751407)
* Prevent log appearing empty if tailing is enabled but no new data has been received before the existing tail data is expired. Rpc log sink was waiting forever for new log tail data, so server was discarding the data already received. (32749401)
* Fix default settings for the analytics dashboard, and update documentation to specify the correct telemetry store name. (32741193)
* Update derived data for issues when they are closed by timeout. (32733991)
* Explicitly set parent context for telemetry spans in Perforce updates, to work around async context mismatches. (32732888)
* Rework commit metadata replication to run a single task for each replicated cluster, for a simpler code flow and better tracing data. (32730286)
* Instrument JobTaskSource ticker. We see occasional slow downs in agent assignment for jobs. Adding some additional tracing should help see what's causing it. (32727003)
* Do not assign compute resource to same machine as requester (32723450)

## 2024-04-01

* Fix decoding of UTF8 characters as part of unescaping Json strings. (32638382)
* Increase Jira timeout to 30 seconds, and improve cancellation handling. (32636702)
* Add a defaults.json file for backwards compatibility with older server installers. Just includes default.global.json. (32619739)
* Move tool update packages onto a separate "Internal" tab on the dashboard. (32619239)
* Only export nodes to Horde that are referenced by the initial job parameters, unless -AllowTargetChanges is specified. Prevents data being copied to temp storage that will never be used unless the target list changes. (32616197)
* Add notes for setting up a self-signed cert for testing. (32613289)
* Doc updates. (32595767)
* Comment out the placeholder ticket value in the default config file. (32585713)
* Improve ordering of nodes when writing large file trees to storage, such that the nodes which are read after each other are adjacent in the storage blobs. Nodes are read in the reverse order to which they are written, depth-first. Arranging nodes in the blob in this order prevents thrashing of the cache and improves performance. (32585474)

## 2024-03-28 (UE 5.4 Release)

* Remove /userinfo call when authenticating via JWT. If an access token is passed, it's not guaranteed it has permission to access /userinfo from OIDC. ID tokens during normal web-based login does on the other hand. (32539012)
* Fix parsing of true/false literals in condition expressions. (32518640)
* Add a default pool to include interactive agents, and map it to the TestWin64 agent type. (32514202)
* Add logging to trace when blobs are scanned for imports, to help debug some logs being expired while still referenced. (32506875)
* Normalize keys for artifacts to allow case-insensitive searching. (32495123)
* Support setting arbitrary artifact metadata. This cannot be queried, but will be returned in artifact responses. (32464180)
* Run a background task to monitor for hangs when executing Perforce commands. (32461961)
* Allow configuring different telemetry stores for each project and stream. (32460547)
* Fix slow queries in storage service garbage collection. (32460451)
* Add a [TryAuthorize] attribute which attempts authorization, but which does not return a challenge or forbid result on failure. Should fix tool download requests using service account tokens. (32447487)
* Fix OIDC issues when using Horde auth with UBA. (32406994)

## 2024-03-21

* Lower log level of failed AWS instance start (32397273)
* Assume any exceptions while checking for config files being out of date as meaning they need to be read again. (32388229)
* Use the first specified credentials for server P4 commands if the serviceAccount property is not set. (32383940)
* Update docs for deploying server on Mac. (32370724)
* Log a warning whenever an obsolete endpoint is used. (32329528)
* Fix separate RAM slots being reported separately in agent properties. (32328916)
* Fix use of auth tokens from environment variables when the server URL is also derived from environment variables. (32305895)
* Fix StorageService not being registered as a hosted service, preventing GC from running. (32291615)
* Fix artifacts not being added to graphs. (32286255)
* Add output artifacts to the graph from the exported BuildGraph definition. (32281784)

## 2024-03-15

* Get the userinfo endpoint from the OIDC discovery document rather than assuming it's a fixed path. (32250458)
* Fix serialization issue with global config path on Linux (32240996)
* Improve error reporting when exceptions are thrown during leases. (32233035)
* Prevent DeadlineExceeded exceptions in log rpc task from being logged as errors. (32232976)
* Update EpicGames.Perforce.Native to use the 2023.2 Perforce libraries. (32232242)
* Enable notification service on all server types, otherwise we keep queuing up new tasks without ever removing them. (32220252)
* Invalidate the current session when deleting an agent from the farm. (32212590)
* Invalidate registrations with a server if creating a session fails with a permission denied error. (32212298)
* Fix job costs displaying for all users. (32154404)
* Expose the bundle page size as a configurable option. Still defaults to 1mb. (32153263)
* Use BitFaster.Caching for the LRU bundle cache. (32153088)
* Pool connections in ManagedWorkspace rather than creating a new instance each time. (32144653)
* Exclude appsettings.Local.json files from publish output folders. (32140083)
* Add a UGS config file to command line tool. (32120883)
* Add endpoints to check permissions for certain ACL actions in different scopes. (32118158)
* Immediately cancel running leases if agent status gets set to busy (paused) (32102044)
* Include a separate file containing default values for Horde configuration from the a file copy into C:\ProgramData. (32098391)
* Add OAuth/OIDC support for internal Horde accounts. (32097065)

## 2024-03-07

* Store the URL of the default Horde server in the registry on Windows (31994865)
* Download UGS deployment settings from Horde parameters endpoint (32001513)
* Allow using partitioned workspaces for P4 replication (32041630)
* Allow only keeping a fixed number of artifacts rather than expiring by time (32043455)
* Include dashboard web UI files in public Docker image for Horde server (32054568)
* Turn off native Perforce client on non-Windows agents (31991203)

## 2024-02-29

* Add a back-off delay when RPC client gets interrupted in JsonRpcLogSink (31881762)
* Add a manual approval process for agents joining the farm. New EnrollmentRpc endpoint allows agents to connect and wait for approval without being assigned an agent id or being able to take on work. (31869942)
* Fix bug with tray app not setting agent in paused state during idle (31837900)
* Copy the tray app to a folder under C:\Users\xxx\AppData\Local before running, so the agent can update without having to restart the tray app (which may be running under a different user account). (31815878)
* Use the agent's reported UpdateChannel property to choose which tool to use for updating the installation. ()
* Write the name of the channel to use for agent updates into the agent config file, and report it to the server through the agent properties. (31806993, 31810088)
* Separate implementations for regular user accounts from service accounts. (31802961)
* Added endpoints for querying entitlements of the current user (or any other account). /account/entitlements will return entitlements for the logged in user, and /api/v1/accounts/xxx/entitlements will return entitlements for a Horde account. (31777667)
* Accounts now use the user's full name for the standard OIDC name claim. (https://openid.net/specs/openid-connect-core-1_0.html#StandardClaims) (31773722)

## 2024-02-23

* Fix regression in UGS metadata filtering, where metadata entries with an empty project string should be returned for any project. (31741737)
* Artifact expiry times are now retroactively updated whenever the configured expiry time changes. (31736576)
* Allow specifying a description string for artifacts. (31729492)
* Disable internal Horde account login by default. (31724108)
* Fix bundled tools not being handled correctly in installed builds. (31721936)
* Read registry config first so files and env vars can override (31720705)

## 2024-02-22

* Fixed bundled tools not being handled correctly in installed builds. (31721936)
* Read registry config first so files and env vars can override (31720705)
* Fix leases not being cancelled when a batch moves into the NoLongerNeeded state. (31709570)
* Prevent Windows service startup from completing until all Horde services have started correctly. (31691578)
* Add firewall exception in installer (31678384, 31677487)
* Always return a mime type of text/plain for .log files, allowing them to be viewed in-browser (31648543)
* Configure HTTP/2 port in server installer (31647511)
* Enable new temp storage backend by default. (31637506)
* Support for defining ACL profiles which can be shared between different ACL entries. Intent is to allow configuring some standard permission sets which can be inherited and modified by users. Two default profiles exist - 'generic-read' and 'generic-run' - which match how we have roles configured internally. New profiles can be created in the same scopes that ACL entries can be defined. (31618249)
* Add a new batch error code for agents that fail during syncing. (31576140)
