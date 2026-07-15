// Copyright Epic Games, Inc. All Rights Reserved.

import { SlackInteractivePayload } from './slack'
import { dispatchSlackInteractivePayload, extractHostFromSlackPayload, SlackDispatchContext } from './slack-actions'

const RECONNECT_DELAY_MS = 5000

export class SlackSocket {
	private ws: WebSocket | null = null
	private reconnectTimer: ReturnType<typeof setTimeout> | null = null
	private stopped = false

	constructor(
		private appToken: string,
		private externalUrl: string,
		private dispatchContext: SlackDispatchContext
	) {}

	async start() {
		this.stopped = false
		try {
			const wsUrl = await this.getWebSocketUrl()
			this.connect(wsUrl)
		}
		catch (err) {
			this.dispatchContext.logger.error(`Slack Socket Mode: failed to get WebSocket URL: ${err}`)
			this.scheduleReconnect()
		}
	}

	stop() {
		this.stopped = true
		if (this.reconnectTimer) {
			clearTimeout(this.reconnectTimer)
			this.reconnectTimer = null
		}
		this.ws?.close()
		this.ws = null
	}

	private async getWebSocketUrl(): Promise<string> {
		const resp = await fetch('https://slack.com/api/apps.connections.open', {
			method: 'POST',
			headers: {
				'Authorization': `Bearer ${this.appToken}`,
				'Content-Type': 'application/x-www-form-urlencoded'
			}
		})
		const json = await resp.json() as any
		if (!json.ok) {
			throw new Error(`apps.connections.open failed: ${json.error}`)
		}
		return json.url
	}

	private connect(url: string) {
		this.dispatchContext.logger.info('Slack Socket Mode: connecting...')
		const ws = new WebSocket(url)
		this.ws = ws

		ws.addEventListener('open', () => {
			this.dispatchContext.logger.info('Slack Socket Mode: connected')
		})

		ws.addEventListener('message', (event: MessageEvent) => {
			this.handleMessage(event.data as string)
		})

		ws.addEventListener('close', (event: Event) => {
			if (this.ws !== ws) return  // already replaced
			this.dispatchContext.logger.warn(`Slack Socket Mode: closed (code=${(event as any).code}), reconnecting...`)
			this.ws = null
			this.scheduleReconnect()
		})

		ws.addEventListener('error', () => {
			this.dispatchContext.logger.error('Slack Socket Mode: WebSocket error')
		})
	}

	private handleMessage(data: string) {
		let msg: any
		try {
			msg = JSON.parse(data)
		}
		catch {
			return
		}

		if (msg.type === 'hello') {
			this.dispatchContext.logger.verbose('Slack Socket Mode: hello received')
			return
		}

		if (msg.type === 'disconnect') {
			this.dispatchContext.logger.info('Slack Socket Mode: server requested disconnect, reconnecting...')
			this.ws?.close()
			return
		}

		if (msg.type === 'interactive') {
			const payload = msg.payload as SlackInteractivePayload
			if (payload?.type === 'view_submission') {
				// For view_submission, the response_action must be included in the envelope ACK.
				// Do NOT ACK immediately — wait for the dispatch result (within Slack's 3-second window).
				this.handleViewSubmission(msg.envelope_id, payload)
				return
			}
			// For block_actions, acknowledge immediately and handle asynchronously.
			if (msg.envelope_id) {
				this.ws?.send(JSON.stringify({ envelope_id: msg.envelope_id }))
			}
			this.handleInteractive(payload)
			return
		}

		// Acknowledge all other envelopes immediately
		if (msg.envelope_id) {
			this.ws?.send(JSON.stringify({ envelope_id: msg.envelope_id }))
		}
	}

	private handleInteractive(payload: SlackInteractivePayload) {
		if (!payload) return

		// With Socket Mode all connected instances receive every event.
		// Only handle actions targeted at this instance.
		const host = extractHostFromSlackPayload(payload)
		if (host && host !== this.externalUrl) {
			this.dispatchContext.logger.info(`Slack Socket Mode: ignoring block_action — host mismatch (payload='${host}', local='${this.externalUrl}')`)
			return
		}

		dispatchSlackInteractivePayload(payload, this.dispatchContext).catch(err => {
			this.dispatchContext.logger.error(`Slack Socket Mode: dispatch error: ${err}`)
		})
	}

	private async handleViewSubmission(envelopeId: string, payload: SlackInteractivePayload) {
		if (!payload) {
			if (envelopeId) this.ws?.send(JSON.stringify({ envelope_id: envelopeId }))
			return
		}

		// With Socket Mode all connected instances receive every event.
		// Only handle submissions targeted at this instance.
		const host = extractHostFromSlackPayload(payload)
		if (host && host !== this.externalUrl) {
			this.dispatchContext.logger.info(`Slack Socket Mode: ignoring view_submission — host mismatch (payload='${host}', local='${this.externalUrl}')`)
			if (envelopeId) this.ws?.send(JSON.stringify({ envelope_id: envelopeId }))
			return
		}

		let ackPayload: any = {}
		try {
			const result = await dispatchSlackInteractivePayload(payload, this.dispatchContext)
			// Extract the response_action JSON from the result message and include it in the ACK
			if (result?.message && typeof result.message === 'string') {
				try {
					const parsed = JSON.parse(result.message)
					if (parsed?.response_action) {
						ackPayload.payload = parsed
					}
				}
				catch { /* message isn't JSON — no response_action to send */ }
			}
		}
		catch (err) {
			this.dispatchContext.logger.error(`Slack Socket Mode: view submission dispatch error: ${err}`)
		}

		if (envelopeId) {
			this.ws?.send(JSON.stringify({ envelope_id: envelopeId, ...ackPayload }))
		}
	}

	private scheduleReconnect() {
		if (this.stopped || this.reconnectTimer) return
		this.reconnectTimer = setTimeout(() => {
			this.reconnectTimer = null
			this.start()
		}, RECONNECT_DELAY_MS)
	}
}
