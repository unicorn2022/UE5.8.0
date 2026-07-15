// Copyright Epic Games, Inc. All Rights Reserved.

import * as request from '../common/request';
import { ContextualLogger } from '../common/logger';
import { URLSearchParams } from "url"

export interface SlackChannel {
	id: string
	botToken: string
	userToken: string
}

export interface SlackMessageField {
	title: string
	value: string | number | boolean 
	short: boolean
}

// https://api.slack.com/docs/message-attachments#link_buttons
export interface SlackLinkButtonAction {
	type: "button"
	text: string
	url: string
	style?: "default" | "primary" | "danger" //black/white, (green), (red)
	value?: string
}

// https://api.slack.com/reference/block-kit/block-elements#button
export interface SlackInteractiveButton {
	type: 'button'
	text: { type: 'plain_text'; text: string }
	action_id: string       // operation name, e.g. 'acknowledge'
	value: string           // JSON-encoded params: {bot, branch, cl, edge?, target?}
	style?: 'primary' | 'danger'
}

// https://api.slack.com/reference/block-kit/blocks#actions
export interface SlackActionsBlock {
	type: 'actions'
	elements: SlackInteractiveButton[]
}

// Inbound payload from Slack when a user clicks an interactive button
export interface SlackInteractiveAction {
	action_id: string
	value: string
}

export interface SlackInteractivePayload {
	type: 'block_actions' | 'view_submission'
	user: { id: string; name: string }
	channel?: { id: string }
	message?: { ts: string }
	response_url?: string
	// block_actions
	actions?: SlackInteractiveAction[]
	// view_submission
	view?: {
		callback_id: string
		private_metadata: string
		state: { values: { [blockId: string]: { [actionId: string]: { selected_option?: { value: string } } } } }
	}
	// triggers view.open for modal
	trigger_id?: string
}

// https://api.slack.com/docs/interactive-message-field-guide#attachment_fields
export interface SlackAttachment {
	text?: string
	color?: string
	pretext?: string
	mrkdwn_in: string[] // ["pretext", "text", "fields"]
}

export interface SlackLinkButtonsAttachment extends SlackAttachment {
	text: string
	fallback: string
	actions: SlackLinkButtonAction[] // Up to 5 buttons can exist in one attachment before Slack gets mad
}

export enum SlackMessageStyles {
	GOOD = 'good',
	WARNING = 'warning',
	DANGER = 'danger'
}

export interface SlackMessage {
	text: string

	username?: string
	style?: SlackMessageStyles
	fields?: SlackMessageField[]
	title?: string
	title_link?: string
	icon_emoji?: string
	pretext?: string
	footer?: string
	thread_ts?: string

	// An array because the Slack API expects it to be
	attachments?: SlackAttachment[]
	// Block Kit blocks (interactive buttons etc.)
	blocks?: SlackActionsBlock[]
	// Direct Message support
	channel: string
	// Allows Markdown formatting in messages
	mrkdwn: boolean

	// For the dummy server implementation we store additional information
	cl?: number
	target?: string
}

export interface SlackFile {
	content: string
	channels: string
	filename?: string
	filetype?: string
	initial_comment?: string
	thread_ts?: string
	title?: string
}

enum PostFormat {
	JSON,
	URL
}

const MAIN_MESSAGE_FIELDS = new Set(['username', 'icon_emoji', 'channel', 'target', 'cl', 'thread_ts']);

export class Slack {
	constructor(private channel: SlackChannel, private domain: string, private readonly logger: ContextualLogger) {
	}

	async addUserToChannel(user: string, channel: string, externalUser?: boolean) {
		if (externalUser) {		
			return this.post_user(this.channel.userToken || this.channel.botToken, 'admin.conversations.invite', {channel_id:channel, user_ids:user}, true)
		}
		return this.post('conversations.invite', {channel, users:user}, true)
	}

	async getChannelInfo(channel:string) {
		return this.get('conversations.info', {channel})
	}

	async getPermalink(thread_ts: string, channel: string) {
		return (await this.get('chat.getPermalink', {channel, message_ts: thread_ts})).permalink
	}

	async postMessage(message: SlackMessage, retryCount: number = 0): Promise<string> {
		const MAX_RETRIES = 3
		const result = await this.post('chat.postMessage', this.makeArgs(message), true)
		if (result.ok) {
			return result.ts
		}
		else if (result.error == "rate_limited" ||
				 result.error == "request_timeout" ||
				 result.error == "service_unavailable") {

			if (retryCount < MAX_RETRIES) {
				// Exponential backoff: 1s, 2s, 4s
				const retrySeconds = Math.pow(2, retryCount)

				this.logger.warn(`chat.postMessage failed with ${result.error}. Retrying after ${retrySeconds} seconds (attempt ${retryCount + 1}/${MAX_RETRIES})`)

				await new Promise(resolve => setTimeout(resolve, retrySeconds * 1000))

				// Retry the request
				return this.postMessage(message, retryCount + 1)
			}

			// Max retries exceeded
			const errMsg = `Slack chat.postMessage failed after ${MAX_RETRIES} retries with error: ${result.error}`
			this.logger.error(errMsg)
			throw new Error(errMsg)
		}
		else {
			throw new Error(`Slack chat.postMessage error ${result.error}`)
		}
	}

	reply(thread_ts: string, message: SlackMessage) {
		message.thread_ts = thread_ts
		return this.postMessage(message)
	}

	update(ts: string, message: SlackMessage) {
		const args = this.makeArgs(message)
		args.ts = ts
		return this.post('chat.update', args)
	}

	delete(ts: string, channel: string) {
		return this.post('chat.delete', { ts, channel }, true)
	}

	uploadFile(file: SlackFile) {
		let getURLargs: any = {
			filename: file.filename,
			length: file.content.length
		}
		if (file.filetype) {
			getURLargs.snippet_type = file.filetype // slack API gets snippy if you pass in snippet_type = undefined
		}
		return this.post('files.getUploadURLExternal', getURLargs, true, PostFormat.URL)
			.then( getURLresult => {
				return (getURLresult.ok ?
					request.post({
						url: getURLresult.upload_url,
						body: file.content,
						contentType: 'application/x-www-form-urlencoded'
					}).then(
						() => {
							const completeArgs = {
								files:[{id: getURLresult.file_id, title: file.title}],
								channel_id: file.channels,
								initial_comment: file.initial_comment,
								thread_ts: file.thread_ts
							}
							return this.post('files.completeUploadExternal', completeArgs)
						}
					)
					: undefined)
			}
		)
	}

	listMessages(count?: number) {
		const args: any = count ? {count} : count
		// use channels.history if public?
		return this.get('groups.history', args)
	}

	async lookupUserIdByEmail(email: string) {
		const userLookupResult = await this.get('users.lookupByEmail', {email}, true)
		return userLookupResult.ok ? userLookupResult.user.id : null
	}

	async openDMConversation(users: string | string[]) : Promise<string|null> {
		if (users instanceof Array) {
			users = users.join(',')
		}
		const result = await this.post('conversations.open', {users})
		return result.channel ? result.channel.id : null
	}

	/*private*/ async post_user(userToken: string, command: string, args: any, canFail? : boolean, format?: PostFormat, retryCount: number = 0): Promise<any> {
		const MAX_RETRIES = 3
		const response = await request.postWithHeaders({
			url: this.domain + '/api/' + command,
			body: format == PostFormat.URL ? new URLSearchParams(Object.entries(args)).toString() : JSON.stringify(args),
			headers: {Authorization: 'Bearer ' + userToken},
			contentType: format == PostFormat.URL ? 'application/x-www-form-urlencoded' : 'application/json; charset=utf-8'
		})
		try {
			const result = JSON.parse(response.body)
			if (result.ok || canFail) {
				return result
			}

			if (result.error === 'ratelimited' && retryCount < MAX_RETRIES) {
				const retryAfter = response.headers['retry-after']
				const retrySeconds = retryAfter ? parseInt(retryAfter as string, 10) : 1

				this.logger.warn(`${command} rate limited. Retrying after ${retrySeconds} seconds (attempt ${retryCount + 1}/${MAX_RETRIES})`)

				await new Promise(resolve => setTimeout(resolve, retrySeconds * 1000))

				// Retry the request
				return this.post_user(userToken, command, args, canFail, format, retryCount + 1)
			}
		}
		catch {
		}

		this.logger.error(`${command} generated:\n\t${response.body}`)
		return {ok: false}
	}

	/*private*/ async post(command: string, args: any, canFail? : boolean, format?: PostFormat) {
		return this.post_user(this.channel.botToken, command, args, canFail, format)
	}

	/*private*/ async get(command: string, args: any, canFail? : boolean) {

		// erg: why am I always passing a channel?
		if (this.channel.id && !args.channel) {
			args.channel = this.channel.id
		}

		const qsBits: string[] = []
		for (const arg in args) {
			qsBits.push(`${encodeURIComponent(arg)}=${encodeURIComponent(args[arg])}`)
		}

		const url = this.domain + `/api/${command}?${qsBits.join('&')}`

		const rawResult = await request.get({url,
			headers: {Authorization: 'Bearer ' + this.channel.botToken}
		})

		try {
			const result = JSON.parse(rawResult)
			if (result.ok || canFail) {
				return result
			}
		}
		catch {
		}
		
		this.logger.error(`url: '${url}' error: '${rawResult}'`)
		return {ok: false}
	}

// behaviour seems to be: put in attachment for green margin if any opts sent at all (e.g. {} different to nothing passed)


// text in messageOpts was different to text argument:
//	former goes in attachment

//	should now overload so that string only puts text in args

// look out for makeArgs with just string - support if necessary
	async getUserInfo(userId: string) {
		return this.get('users.info', {user: userId}, true)
	}

	async openModal(triggerId: string, view: any) {
		return this.post('views.open', {trigger_id: triggerId, view: JSON.stringify(view)}, true)
	}

	private makeArgs(message: SlackMessage) {
		const args: {[arg: string]: any} = {}

		// markdown disabled to allow custom links
		// (seems can't have both a link and a user @ without looking up user ids)
		const attch: any = {color: message.style || 'good', text: message.text}
		args.attachments = [attch]

		// Add any explicit attachments
		if (message.attachments) {
			args.attachments = args.attachments.concat(message.attachments)
		}

		// Block Kit blocks are passed at the top level, not inside attachments.
		// Pass explicitly even when empty so chat.update clears existing blocks.
		if (message.blocks !== undefined) {
			args.blocks = message.blocks
		}

		const SKIP_ATTACHMENT_KEYS = new Set(['style', 'attachments', 'blocks'])
		const opts = message as unknown as {[key: string]: any}
		for (const key in opts) {
			if (MAIN_MESSAGE_FIELDS.has(key)) {
				args[key] = opts[key]
			}
			else if (!SKIP_ATTACHMENT_KEYS.has(key)) {
				attch[key] = opts[key]
			}
		}

		// parse=client doesn't allow custom links (can't seem to specify it just for main message)
		// args.parse = 'client'
		// args.parse = 'full'
		args.link_names = 1
		return args
	}
}
