// Copyright Epic Games, Inc. All Rights Reserved.

import { ContextualLogger } from '../common/logger';
import * as request from '../common/request';
import { OperationReturnType } from './ipc';
import { Slack, SlackInteractivePayload } from './slack';
import { BlockageNodeOpParams } from './roboserver';

export interface SlackDispatchContext {
	sendMessage: (msg: string, args?: any[]) => any
	botToken: string | undefined
	slackDomain: string | undefined
	logger: ContextualLogger
}

export function extractHostFromSlackPayload(payload: SlackInteractivePayload): string | undefined {
	try {
		if (payload.type === 'block_actions' && payload.actions && payload.actions[0]) {
			return JSON.parse(payload.actions[0].value).host
		}
		if (payload.type === 'view_submission' && payload.view) {
			return JSON.parse(payload.view.private_metadata).host
		}
	}
	catch { /* ignore parse errors */ }
	return undefined
}

export async function dispatchSlackInteractivePayload(payload: SlackInteractivePayload, dispatchContext: SlackDispatchContext) {
	if (payload.type === 'block_actions') {
		return _handleSlackBlockAction(payload, dispatchContext)
	}
	if (payload.type === 'view_submission') {
		return _handleSlackViewSubmission(payload, dispatchContext)
	}
	return { statusCode: 400, message: `Unsupported payload type: ${payload.type}` }
}

async function _handleSlackBlockAction(payload: SlackInteractivePayload, dispatchContext: SlackDispatchContext) {
	const action = payload.actions && payload.actions[0]
	if (!action) {
		return { statusCode: 400, message: 'No actions in payload' }
	}

	let params: any
	try {
		params = JSON.parse(action.value)
	}
	catch {
		return { statusCode: 400, message: 'Invalid action value JSON' }
	}

	const { bot, branch, cl, edge, target, targetStream } = params
	const who = payload.user.name

	if (!bot || !branch || !cl) {
		dispatchContext.logger.warn(`_handleSlackBlockAction: missing required params — bot=${bot}, branch=${branch}, cl=${cl}`)
		return { statusCode: 400, message: 'Missing required params (bot, branch, cl)' }
	}

	const clStr = String(cl)
	dispatchContext.logger.info(`_handleSlackBlockAction: action='${action.action_id}' bot='${bot}' branch='${branch}' cl=${cl} edge='${edge ?? 'none'}' user='${who}'`)

	switch (action.action_id) {
		case 'acknowledge': {
			const result = edge
				? await dispatchContext.sendMessage('doEdgeOp', [bot, branch, edge, 'acknowledge', { who, cl: clStr }])
				: await dispatchContext.sendMessage('doNodeOp', [bot, branch, 'acknowledge', { who, cl: clStr }])
			return _slackActionResult(result, payload, `Acknowledged by ${who}`, dispatchContext)
		}

		case 'skip': {
			if (!edge) {
				return { statusCode: 400, message: 'skip requires edge param' }
			}
			if (!payload.trigger_id) {
				return { statusCode: 400, message: 'Missing trigger_id for modal' }
			}
			return _openSkipReasonModal(payload.trigger_id, params, dispatchContext)
		}

		case 'stompchanges': {
			if (!target) {
				return { statusCode: 400, message: 'stompchanges requires target param' }
			}
			if (!payload.trigger_id) {
				return { statusCode: 400, message: 'Missing trigger_id for modal' }
			}
			return _openStompConfirmModal(payload.trigger_id, params, dispatchContext)
		}

		case 'unlockchanges': {
			if (!target) {
				return { statusCode: 400, message: 'unlockchanges requires target param' }
			}
			const result = await dispatchContext.sendMessage('doNodeOp', [bot, branch, 'unlockchanges', { who, cl: clStr, target }])
			return _slackActionResult(result, payload, `Unlock started by ${who}`, dispatchContext)
		}

		case 'create_shelf': {
			if (!payload.trigger_id) {
				return { statusCode: 400, message: 'Missing trigger_id for modal' }
			}
			return _openCreateShelfModal(payload.trigger_id, params, who, targetStream, dispatchContext, payload.response_url)
		}

		default:
			return { statusCode: 400, message: `Unknown action_id: ${action.action_id}` }
	}
}

async function _handleSlackViewSubmission(payload: SlackInteractivePayload, dispatchContext: SlackDispatchContext) {
	const view = payload.view
	if (!view) {
		return { statusCode: 400, message: 'No view in submission payload' }
	}

	dispatchContext.logger.info(`_handleSlackViewSubmission: callback_id='${view.callback_id}' user='${payload.user?.name ?? 'unknown'}'`)
	switch (view.callback_id) {
		case 'skip_reason_modal':   return _handleSkipReasonSubmit(payload, view, dispatchContext)
		case 'stomp_confirm_modal': return _handleStompConfirmSubmit(payload, view, dispatchContext)
		default:                    return _handleCreateShelfSubmit(payload, view, dispatchContext)
	}
}

async function _handleCreateShelfSubmit(payload: SlackInteractivePayload, view: NonNullable<SlackInteractivePayload['view']>, dispatchContext: SlackDispatchContext) {
	let params: any
	try {
		params = JSON.parse(view.private_metadata)
	}
	catch {
		return { statusCode: 400, message: 'Invalid private_metadata JSON' }
	}

	const { bot, branch, cl, target, edge } = params
	const who = payload.user.name

	const values = view.state.values
	let workspace: string | undefined
	for (const blockId in values) {
		for (const actionId in values[blockId]) {
			const selected = values[blockId][actionId].selected_option
			if (selected) {
				workspace = selected.value
				break
			}
		}
		if (workspace) break
	}

	if (!workspace) {
		return {
			statusCode: 200,
			message: JSON.stringify({ response_action: 'errors', errors: { workspace_block: 'Please select a workspace' } }),
			headers: [['Content-Type', 'application/json']]
		}
	}

	const acknowledgeSelected = ((values['acknowledge_block'] as any)?.['acknowledge_checkbox']?.selected_options as any[] | undefined)
		?.some((o: any) => o.value === 'acknowledge') ?? false

	const result = await dispatchContext.sendMessage('doNodeOp', [bot, branch, 'create_shelf', { who, cl: String(cl), target, workspace }])
	if (result && result.statusCode === 200) {
		if (acknowledgeSelected) {
			// Mirror the web UI: acknowledge via edge op when an edge is present
			if (edge) {
				await dispatchContext.sendMessage('doEdgeOp', [bot, branch, edge, 'acknowledge', { who, cl: String(cl) }])
			} else {
				await dispatchContext.sendMessage('doNodeOp', [bot, branch, 'acknowledge', { who, cl: String(cl) }])
			}
		}
		return { statusCode: 200, message: JSON.stringify({ response_action: 'clear' }), headers: [['Content-Type', 'application/json']] }
	}
	return { statusCode: 200, message: JSON.stringify({ response_action: 'errors', errors: { workspace_block: result.message || 'Error creating shelf' } }), headers: [['Content-Type', 'application/json']] }
}

async function _handleSkipReasonSubmit(payload: SlackInteractivePayload, view: NonNullable<SlackInteractivePayload['view']>, dispatchContext: SlackDispatchContext) {
	let params: any
	try {
		params = JSON.parse(view.private_metadata)
	}
	catch {
		return { statusCode: 400, message: 'Invalid private_metadata JSON' }
	}

	const { bot, branch, cl, edge } = params
	const who = payload.user.name

	const values = view.state.values
	let selectedReason: string | undefined
	for (const blockId in values) {
		for (const actionId in values[blockId]) {
			const sel = values[blockId][actionId].selected_option
			if (sel) { selectedReason = sel.value; break }
		}
		if (selectedReason) break
	}

	if (!selectedReason) {
		return {
			statusCode: 200,
			message: JSON.stringify({ response_action: 'errors', errors: { reason_block: 'Please select a reason' } }),
			headers: [['Content-Type', 'application/json']]
		}
	}

	const reasonText = selectedReason === 'notrelevant'
		? 'Work is not relevant to other branches'
		: 'User will redo work in the merge target'

	const result = await dispatchContext.sendMessage('doEdgeOp', [bot, branch, edge, 'set_last_cl', {
		who, cl: String(cl), reason: reasonText, unblock: 'true', isSkip: 'true'
	}])

	if (result && result.statusCode === 200) {
		if (payload.response_url && dispatchContext.botToken) {
			request.post({
				url: payload.response_url,
				body: JSON.stringify({ replace_original: false, text: `✓ Skipped by ${who} (${reasonText})` }),
				contentType: 'application/json'
			}).catch(() => { /* best-effort */ })
		}
		return { statusCode: 200, message: JSON.stringify({ response_action: 'clear' }), headers: [['Content-Type', 'application/json']] }
	}
	return { statusCode: 200, message: JSON.stringify({ response_action: 'errors', errors: { reason_block: result?.message || 'Error performing skip' } }), headers: [['Content-Type', 'application/json']] }
}

async function _handleStompConfirmSubmit(payload: SlackInteractivePayload, view: NonNullable<SlackInteractivePayload['view']>, dispatchContext: SlackDispatchContext) {
	let params: any
	try {
		params = JSON.parse(view.private_metadata)
	}
	catch {
		return { statusCode: 400, message: 'Invalid private_metadata JSON' }
	}

	const { bot, branch, cl, target } = params
	const who = payload.user.name

	const result = await dispatchContext.sendMessage('doNodeOp', [bot, branch, 'stompchanges', { who, cl: String(cl), target }])

	if (result && result.statusCode === 200) {
		return { statusCode: 200, message: JSON.stringify({ response_action: 'clear' }), headers: [['Content-Type', 'application/json']] }
	}

	// Stomp failed — update the modal to show the error so the user sees it
	const errorText = result?.message || 'Unknown error performing stomp'
	const errorModal = {
		type: 'modal',
		callback_id: 'stomp_confirm_modal',
		title: { type: 'plain_text', text: 'Stomp Failed' },
		close: { type: 'plain_text', text: 'Close' },
		private_metadata: view.private_metadata,
		blocks: [
			{
				type: 'section',
				text: { type: 'mrkdwn', text: `:x: *Stomp failed*\n\n${errorText}` }
			}
		]
	}
	return {
		statusCode: 200,
		message: JSON.stringify({ response_action: 'update', view: errorModal }),
		headers: [['Content-Type', 'application/json']]
	}
}

async function _openSkipReasonModal(triggerId: string, params: BlockageNodeOpParams, dispatchContext: SlackDispatchContext) {
	if (!dispatchContext.botToken || !dispatchContext.slackDomain) {
		return { statusCode: 500, message: 'Slack not configured for modal' }
	}
	const modal = {
		type: 'modal',
		callback_id: 'skip_reason_modal',
		title: { type: 'plain_text', text: 'Skip Merge' },
		submit: { type: 'plain_text', text: 'Skip' },
		close: { type: 'plain_text', text: 'Cancel' },
		private_metadata: JSON.stringify(params),
		blocks: [
			{
				type: 'section',
				text: { type: 'mrkdwn', text: `*Skip merging CL ${params.cl} to ${params.edge}?*\nThis will skip the merge and allow Robomerge to continue.` }
			},
			{
				type: 'input',
				block_id: 'reason_block',
				label: { type: 'plain_text', text: 'Reason for skipping' },
				element: {
					type: 'static_select',
					action_id: 'reason_select',
					placeholder: { type: 'plain_text', text: 'Select a reason' },
					options: [
						{ text: { type: 'plain_text', text: 'Work is not relevant to the target branch' }, value: 'notrelevant' },
						{ text: { type: 'plain_text', text: 'I will manually redo work in the merge targets' }, value: 'willredo' }
					]
				}
			}
		]
	}
	const slack = new Slack({ id: '', botToken: dispatchContext.botToken, userToken: '' }, dispatchContext.slackDomain, dispatchContext.logger)
	const result = await slack.openModal(triggerId, modal)
	return result?.ok ? { statusCode: 200, message: '' } : { statusCode: 500, message: `Failed to open modal: ${result?.error}` }
}

async function _openStompConfirmModal(triggerId: string, params: BlockageNodeOpParams, dispatchContext: SlackDispatchContext) {
	if (!dispatchContext.botToken || !dispatchContext.slackDomain) {
		return { statusCode: 500, message: 'Slack not configured for modal' }
	}
	const modal = {
		type: 'modal',
		callback_id: 'stomp_confirm_modal',
		title: { type: 'plain_text', text: 'Stomp Changes' },
		submit: { type: 'plain_text', text: 'Confirm Stomp' },
		close: { type: 'plain_text', text: 'Cancel' },
		private_metadata: JSON.stringify(params),
		blocks: [
			{
				type: 'section',
				text: {
					type: 'mrkdwn',
					text: `:warning: *Stomp changes in ${params.target} with CL ${params.cl}?*\n\nRobomerge will verify the stomp before proceeding. Non-binary file conflicts will block the stomp.`
				}
			}
		]
	}
	const slack = new Slack({ id: '', botToken: dispatchContext.botToken, userToken: '' }, dispatchContext.slackDomain, dispatchContext.logger)
	const result = await slack.openModal(triggerId, modal)
	return result?.ok ? { statusCode: 200, message: '' } : { statusCode: 500, message: `Failed to open modal: ${result?.error}` }
}

async function _openCreateShelfModal(triggerId: string, params: any, who: string, targetStream: string | undefined, dispatchContext: SlackDispatchContext, responseUrl?: string) {
	if (!dispatchContext.botToken || !dispatchContext.slackDomain) {
		return { statusCode: 500, message: 'Slack not configured for modal' }
	}

	const { cl, target } = params

	dispatchContext.logger.info(`_openCreateShelfModal: user='${who}', CL=${cl}, target='${target}', targetStream='${targetStream}'`)

	const workspacesResult = await dispatchContext.sendMessage('getWorkspaces', [who, ''])
	const allWorkspaces: Array<{client: string, Stream?: string}> = workspacesResult?.data ?? []
	dispatchContext.logger.info(`_openCreateShelfModal: getWorkspaces returned ${allWorkspaces.length} total workspaces for user='${who}'`)

	// Slack option text must be ≤ 75 chars; option value must be ≤ 150 chars.
	const truncateLabel = (label: string) =>
		label.length <= 75 ? label : label.substring(0, 72) + '...'

	const makeClientOption = (ws: {client: string}) => ({
		text: { type: 'plain_text', text: truncateLabel(ws.client) },
		value: ws.client.length > 150 ? ws.client.substring(0, 150) : ws.client
	})

	// For inexact matches, always show "client (stream-local)" where stream-local is the stream
	// name with the shared depot prefix stripped — the depot is shown in the group label instead.
	const makeInexactOption = (ws: {client: string, Stream?: string}, depotPath: string) => {
		const streamLocal = ws.Stream ? ws.Stream.replace(depotPath, '') : ws.Stream ?? ''
		const label = streamLocal ? `${ws.client} (${streamLocal})` : ws.client
		return {
			text: { type: 'plain_text', text: truncateLabel(label) },
			value: ws.client.length > 150 ? ws.client.substring(0, 150) : ws.client
		}
	}

	// Sort alphabetically within each category (mirrors the web UI)
	const sorted = [...allWorkspaces].sort((a, b) => a.client.localeCompare(b.client))

	// Categorise workspaces the same way the web UI does:
	//   exact   — Stream matches targetStream exactly  (shown first, bold on web)
	//   inexact — Stream shares the same depot path    (shown second)
	//   plain   — no Stream field                      (shown when no targetStream)
	let exactOptions: ReturnType<typeof makeClientOption>[] = []
	let inexactOptions: ReturnType<typeof makeInexactOption>[] = []
	let plainOptions: ReturnType<typeof makeClientOption>[] = []
	let depotPath = ''

	if (targetStream) {
		const depotPathMatch = targetStream.match(/(\/\/[\w\-]+\/)/)
		depotPath = depotPathMatch ? depotPathMatch[0] : ''
		for (const ws of sorted) {
			if (!ws.Stream) continue
			if (ws.Stream === targetStream) {
				// Exact match — stream shown in group label, omit it from individual options
				exactOptions.push(makeClientOption(ws))
			} else if (depotPath && ws.Stream.startsWith(depotPath)) {
				inexactOptions.push(makeInexactOption(ws, depotPath))
			}
		}
	} else {
		for (const ws of sorted) {
			if (!ws.Stream) {
				plainOptions.push(makeClientOption(ws))
			}
		}
	}

	// Build the select element — use option_groups when there are multiple categories
	// so the grouping is visible in the dropdown (mirrors web UI radio button sections).
	const totalOptions = exactOptions.length + inexactOptions.length + plainOptions.length
	if (totalOptions === 0) {
		const msg = allWorkspaces.length === 0
			? `No workspaces found for user '${who}'. Please use the web UI to create a shelf.`
			: `No workspaces matching stream '${targetStream}' found for user '${who}'. Please use the web UI to create a shelf.`
		dispatchContext.logger.warn(`_openCreateShelfModal: ${msg}`)
		if (responseUrl) {
			request.post({ url: responseUrl, body: JSON.stringify({ replace_original: false, response_type: 'ephemeral', text: `:warning: ${msg}` }), contentType: 'application/json' }).catch(() => { /* best-effort */ })
		}
		return { statusCode: 200, message: msg }
	}

	// Group label for exact matches — include the stream name since individual options omit it.
	// Slack group labels are also capped at 75 chars.
	const exactGroupLabel = truncateLabel(targetStream ? `Matching workspaces (${targetStream})` : 'Matching workspaces')

	let selectElement: any
	if (exactOptions.length > 0 && inexactOptions.length > 0) {
		// Two distinct groups — use option_groups so the headers are visible
		selectElement = {
			type: 'static_select',
			action_id: 'workspace_select',
			placeholder: { type: 'plain_text', text: 'Select a workspace' },
			option_groups: [
				{ label: { type: 'plain_text', text: exactGroupLabel }, options: exactOptions.slice(0, 50) },
				{ label: { type: 'plain_text', text: truncateLabel(`Other workspaces in this depot (${depotPath})`) }, options: inexactOptions.slice(0, 50) }
			]
		}
	} else if (exactOptions.length > 0) {
		// Only exact matches — single group with stream name in header, options are client-name only
		selectElement = {
			type: 'static_select',
			action_id: 'workspace_select',
			placeholder: { type: 'plain_text', text: 'Select a workspace' },
			option_groups: [
				{ label: { type: 'plain_text', text: exactGroupLabel }, options: exactOptions.slice(0, 100) }
			]
		}
	} else {
		// Only inexact or plain workspaces — flat options list
		const flat = [...inexactOptions, ...plainOptions].slice(0, 100)
		selectElement = {
			type: 'static_select',
			action_id: 'workspace_select',
			placeholder: { type: 'plain_text', text: 'Select a workspace' },
			options: flat
		}
	}

	const modal = {
		type: 'modal',
		callback_id: 'create_shelf_modal',
		title: { type: 'plain_text', text: 'Create Shelf' },
		submit: { type: 'plain_text', text: 'Create Shelf' },
		close: { type: 'plain_text', text: 'Cancel' },
		private_metadata: JSON.stringify(params),
		blocks: [{
			type: 'section',
			text: { type: 'mrkdwn', text: `Select workspace to create shelf for CL ${cl} → *${target}*` }
		}, {
			type: 'input',
			block_id: 'workspace_block',
			label: { type: 'plain_text', text: 'Workspace' },
			element: selectElement
		}, {
			type: 'input',
			block_id: 'acknowledge_block',
			optional: true,
			label: { type: 'plain_text', text: ' ' },
			element: {
				type: 'checkboxes',
				action_id: 'acknowledge_checkbox',
				initial_options: [{
					text: { type: 'plain_text', text: 'Acknowledge conflict on successful shelf creation' },
					value: 'acknowledge'
				}],
				options: [{
					text: { type: 'plain_text', text: 'Acknowledge conflict on successful shelf creation' },
					value: 'acknowledge'
				}]
			}
		}]
	}

	const slack = new Slack(
		{ id: '', botToken: dispatchContext.botToken, userToken: '' },
		dispatchContext.slackDomain,
		dispatchContext.logger
	)
	const modalResult = await slack.openModal(triggerId, modal)
	if (modalResult && modalResult.ok) {
		dispatchContext.logger.info(`_openCreateShelfModal: modal opened successfully for user='${who}', CL=${cl}`)
		return { statusCode: 200, message: '' }
	}
	const errDetail = modalResult?.error ?? 'unknown error'
	const errMessages = modalResult?.response_metadata?.messages
	dispatchContext.logger.error(`_openCreateShelfModal: views.open failed for user='${who}', CL=${cl}: ${errDetail}${errMessages ? ' — ' + JSON.stringify(errMessages) : ''}`)
	if (responseUrl) {
		request.post({ url: responseUrl, body: JSON.stringify({ replace_original: false, response_type: 'ephemeral', text: `:x: Failed to open Create Shelf dialog: ${errDetail}` }), contentType: 'application/json' }).catch(() => { /* best-effort */ })
	}
	return { statusCode: 500, message: `Failed to open modal: ${errDetail}` }
}

async function _slackActionResult(result: OperationReturnType, payload: SlackInteractivePayload, successText: string, dispatchContext: SlackDispatchContext) {
	if (result && result.statusCode === 200) {
		if (payload.response_url && dispatchContext.botToken) {
			request.post({
				url: payload.response_url,
				body: JSON.stringify({ replace_original: false, text: successText }),
				contentType: 'application/json'
			}).catch(() => { /* best-effort */ })
		}
		return { statusCode: 200, message: '' }
	}
	return { statusCode: 200, message: JSON.stringify({ text: `Error: ${result && result.message}` }), headers: [['Content-Type', 'application/json']] }
}
