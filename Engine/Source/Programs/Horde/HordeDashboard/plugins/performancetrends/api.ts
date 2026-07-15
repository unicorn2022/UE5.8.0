import backend from "horde/backend";
import { REASON_REENTRANT_REQUEST, RequestCancellationManager } from "horde/backend/RequestCancellationManager";
import { MetricConstraint, ISummaryMetricProvider, MetricTypeRegistry } from "./metrictypes/PerformanceTrendsTypes";
import { GetTestProjectResponse, GetTestProjectPlatformResponse, GetTestProjectCommitResponse } from "./responses/GetTestProjectResponse";

// #region -- Budget Types --

export interface MetricThresholdRequest {
    testType: string;
    metricName: string;
    thresholdValue: number;
    largerIsWorse: boolean;
}

export interface PerformanceBudgetAddRequest {
    name: string;
    description?: string;
    computedStream: string;
    testProject: string;
    platforms?: string[];
    thresholds: MetricThresholdRequest[];
}

export interface PerformanceBudgetUpdateRequest {
    name?: string;
    description?: string;
    platforms?: string[];
    thresholds?: MetricThresholdRequest[];
}

export interface MetricThresholdResponse {
    testType: string;
    metricName: string;
    thresholdValue: number;
    largerIsWorse: boolean;
}

export interface PerformanceBudgetResponse {
    id: string;
    name: string;
    description?: string;
    owner?: {
        id: string;
        name: string;
        login: string;
    };
    computedStream: string;
    testProject: string;
    platforms?: string[];
    thresholds: MetricThresholdResponse[];
    updateTimeUtc: string;
}

// #endregion -- Budget Types --

let performanceTrendsCancellationManager: RequestCancellationManager = new RequestCancellationManager();

export const DEFAULT_METRIC_COUNT: number = 2000;

/**
 * Gets a metric of a specific @see ISummaryMetricProvider<T> type.
 * @param metricProvider The metric provider to use for request, and construction of resulting data.
 * @param testProject The test project to obtain data for.
 * @param testIdentity The test identity of the metric to filter for.
 * @param testType The test type of the metric to filter for.
 * @param streams The streams to filter for.
 * @param platforms The platforms to filter for.
 * @returns An array of constructed metrics.
 */
export async function getMetrics<T extends MetricConstraint>(metricProvider: ISummaryMetricProvider<T>, testProject: string, testIdentity: string, testTypes: string[], streams?: string[], platforms?: string[], startCommit: number | null = null, endCommit: number | null = null, count: number = DEFAULT_METRIC_COUNT): Promise<T[]> {
    let params = { type: metricProvider.metricType };

    params["testProject"] = testProject;
    params["testIdentity"] = testIdentity;
    params["testTypes"] = testTypes;
    params["count"] = count;

    if (streams !== undefined) {
        params["streams"] = streams;
    }

    if (platforms !== undefined) {
        params["platforms"] = platforms;
    }

    if (startCommit !== null) {
        params["startCommitIdOrdered"] = startCommit;
    }

    if (endCommit !== null) {
        params["endCommitIdOrdered"] = endCommit;
    }

    const response = await backend.fetch.get(`/api/v1/performancetrends/metrics`,
        {
            params,
            signal: performanceTrendsCancellationManager.getSignal("getMetrics")
        }
    );

    return (response.data as any[]).map(x => MetricTypeRegistry.create<T>(metricProvider.metricType, x)).filter((x): x is T => x != null);
}

/**
 * Gets all test projects, across all known metric types.
 * @returns A list of test projects across all known metric types.
 */
export async function getTestProjects(): Promise<GetTestProjectResponse[]> {
    return await backend.fetch.get(`/api/v1/performancetrends/testprojects`,
        {
            signal: performanceTrendsCancellationManager.getSignal("getTestProjects")
        }
    ).then((response) => {
        let data = response.data as any;
        const results: GetTestProjectResponse[] = data.map((x) => ({
            summaryType: x["summaryType"],
            testName: x["testName"],
            testIdentity: x["testIdentity"],
            testType: x["testType"],
            computedStream: x["computedStream"]
        }));

        return results;
    }).catch(reason => {
        if (String(reason).includes(REASON_REENTRANT_REQUEST)) {
            return [];
        }
        throw reason;
    });
}

/**
 * Gets all platforms given the test project, test identity, and summary type.
 * @returns A list of platforms for the given test project, test identity and summary type.
 */
export async function getPlatforms(testProject: string, testIdentity: string, metricSummaryType: string, streams?: string[]): Promise<GetTestProjectPlatformResponse[]> {
    let params = {};

    params["metricSummaryType"] = metricSummaryType;
    params["testProject"] = testProject;
    params["testIdentity"] = testIdentity;

    if (streams) {
        params["streams"] = streams;
    }

    return await backend.fetch.get(`/api/v1/performancetrends/platforms`,
        {
            params: { ...params },
            signal: performanceTrendsCancellationManager.getSignal("getPlatforms")
        }
    ).then(response => response.data as GetTestProjectPlatformResponse[])
        .catch(reason => {
            if (String(reason).includes(REASON_REENTRANT_REQUEST)) {
                return [];
            }
            throw reason;
        });
}

/**
 * Gets all commits given the test project, test identity, summary type, and platform.
 * @returns A list of commits for the given test project, test identity, summary type, and platform.
 */
export async function getCommits(testProject: string, testIdentity: string, metricSummaryType: string, streams: string[], platforms: string[]): Promise<GetTestProjectCommitResponse[]> {
    let params = {};

    params["metricSummaryType"] = metricSummaryType;
    params["testProject"] = testProject;
    params["testIdentity"] = testIdentity;

    if (streams.length > 0) {
        params["streams"] = streams;
    }

    if (platforms.length > 0) {
        params["platforms"] = platforms;
    }

    return backend.fetch.get(
        `/api/v1/performancetrends/commits`,
        {
            params,
            signal: performanceTrendsCancellationManager.getSignal("getCommits")
        }
    ).then(response => response.data as GetTestProjectCommitResponse[])
        .catch(reason => {
            if (String(reason).includes(REASON_REENTRANT_REQUEST)) {
                return [];
            }
            throw reason;
        });
}

// #region -- Budget API --

/**
 * Creates a new performance budget.
 * @param request The budget creation request.
 * @returns The created budget response.
 */
export async function createBudget(request: PerformanceBudgetAddRequest): Promise<PerformanceBudgetResponse> {
    const response = await backend.fetch.post(`/api/v1/performancetrends/budgets`, request);
    return response.data as PerformanceBudgetResponse;
}

/**
 * Gets performance budget groups for a stream with optional filters.
 * @param computedStream The computed stream to get budgets for (e.g., "++Fortnite+Main").
 * @param testProject Optional test project filter.
 * @param platform Optional platform filter. Returns budgets that include this platform or have no platform restriction.
 * @returns List of matching budget groups.
 */
export async function getBudgets(computedStream: string, testProject?: string, platform?: string): Promise<PerformanceBudgetResponse[]> {
    const params: Record<string, string> = { computedStream };

    if (testProject) params.testProject = testProject;
    if (platform) params.platform = platform;

    const response = await backend.fetch.get(`/api/v1/performancetrends/budgets`, { params });
    return response.data as PerformanceBudgetResponse[];
}

/**
 * Gets a single performance budget by ID.
 * @param budgetId The budget ID to get.
 * @returns The budget response.
 */
export async function getBudget(budgetId: string): Promise<PerformanceBudgetResponse> {
    const response = await backend.fetch.get(`/api/v1/performancetrends/budgets/${budgetId}`);
    return response.data as PerformanceBudgetResponse;
}

/**
 * Updates an existing performance budget.
 * @param budgetId The budget ID to update.
 * @param request The update request containing fields to modify.
 * @returns The updated budget response.
 */
export async function updateBudget(budgetId: string, request: PerformanceBudgetUpdateRequest): Promise<PerformanceBudgetResponse> {
    const response = await backend.fetch.put(`/api/v1/performancetrends/budgets/${budgetId}`, request);
    return response.data as PerformanceBudgetResponse;
}

/**
 * Deletes a performance budget.
 * @param budgetId The budget ID to delete.
 */
export async function deleteBudget(budgetId: string): Promise<void> {
    await backend.fetch.delete(`/api/v1/performancetrends/budgets/${budgetId}`);
}

// #endregion -- Budget API --