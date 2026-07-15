// Copyright Epic Games, Inc. All Rights Reserved.

import { DateTimeFormatOptions } from 'intl';
import { Badge } from '../common/badge';
import { Random, setDefault } from '../common/helper';
import { ContextualLogger } from '../common/logger';
import { ExclusiveFileDetails } from '../common/perforce';
import { Blockage, Branch, BranchArg, EditableBranch, ExclusiveLockInfo, ForcedCl, MergeAction, NagBlockageEvent, NodeOpUrlGenerator, PauseEvent, resolveBranchArg, getDisplayNameFromBranchArg } from './branch-interfaces';
import { PersistentConflict, Resolution } from './conflict-interfaces';
import { BlockageStatusChangedEvent, BotEventHandler, BotEvents } from './events';
import { NodeBot } from './nodebot';
import { PersistedSlackMessage, PersistedSlackMessages } from './persistedmessages';
import { RoboArgs } from './roboargs';
import { BlockageNodeOpParams, BlockageNodeOpUrls } from './roboserver';
import { Slack, SlackActionsBlock, SlackAttachment, SlackFile, SlackInteractiveButton, SlackLinkButtonsAttachment, SlackMessageField, SlackMessage, SlackMessageStyles } from './slack';
import { WebServer } from '../common/webserver'
import { DummySlackApp } from '../common/dummyslackserver'

const CHANGELIST_FIELD_TITLE = 'Change'
const ACKNOWLEDGED_FIELD_TITLE = 'Acknowledged'
const EPIC_TIME_OPTIONS: DateTimeFormatOptions = {timeZone: 'EST5EDT', timeZoneName: 'short'}

const KNOWN_BOT_NAMES = ['buildmachine', 'robomerge'];
const KNOWN_BOT_EMAILS = ['bot.email@companyname.com'];

let SLACK_TOKENS: {[name: string]: string} = {}
const SLACK_DEV_DUMMY_TOKEN = 'dev'

export function notificationsInit() {
	if (RoboArgs.args.devMode) {
		Badge.setDevMode()
	}

	if (RoboArgs.args.slackDomain.indexOf('localhost') >= 0) { // todo should parse out port
		const slackServer = new WebServer(new ContextualLogger('dummy Slack'))
		slackServer.addApp(DummySlackApp)
		slackServer.open(8811, 'http')
		return
	}

	const tokensObj = RoboArgs.vault.slackTokens
	if (tokensObj) {
		SLACK_TOKENS = tokensObj
	}
}

export function isUserAKnownBot(user: string) {
	return KNOWN_BOT_NAMES.indexOf(user) !== -1 || KNOWN_BOT_EMAILS.indexOf(user) !== -1
}

export async function postToRobomergeAlerts(message: string, replies?: (string|SlackFile|SlackMessage)[]) {
	if (!RoboArgs.args.devMode) {
		return postMessageToChannel(`${message}\n\n${RoboArgs.externalUrl}`, RoboArgs.args.slackAlertChannel, SlackMessageStyles.DANGER, replies)
	}
}

export async function postMessageToChannel(message: string, channel: string, style: SlackMessageStyles = SlackMessageStyles.GOOD, replies?: (string|SlackFile|SlackMessage)[]) {
	if (SLACK_TOKENS.bot) {
		const slack = new Slack({id: channel, botToken: SLACK_TOKENS.bot, userToken: SLACK_TOKENS.user}, RoboArgs.args.slackDomain, new ContextualLogger(`Post Message to ${channel}`))	
		const thread_ts = await slack.postMessage({text: message, style, channel, mrkdwn: true })
		for (const reply of (replies || [])) {
			if (typeof reply === 'string') {
				await slack.reply(thread_ts, {text: message, channel, mrkdwn: true})
			}
			else if ('content' in reply) {
				reply.thread_ts = thread_ts
				await slack.uploadFile(reply)
			}
			else {
				await slack.reply(thread_ts, reply)
			}
		}
	}
}

//////////
// Utils

// make a Slack notification link for a user
function atifyUser(user: string) {
	// don't @ names of bots (currently making them tt style for Slack)
	return isUserAKnownBot(user) ? `\`${user}\`` : isUserSlackGroup(user) ? user : '@' + user
}

export function isUserSlackGroup(user: string) {
	return user.startsWith('@') || user.startsWith('<')
}

function formatDuration(durationSeconds: number) {
	const underSixHours = durationSeconds < 6 * 3600
	const durationBits: string[] = []
	if (durationSeconds > 3600) {
		const hoursFloat = durationSeconds / 3600
		const hours = underSixHours ? Math.floor(hoursFloat) : Math.round(hoursFloat)
		durationBits.push(`${hours} hour` + (hours === 1 ? '' : 's'))
	}
	// don't bother with minutes if over six hours
	if (underSixHours) {
		if (durationSeconds < 90) {
			if (durationSeconds > 10) {
				durationSeconds = Math.round(durationSeconds)
			}
			durationBits.push(`${durationSeconds} seconds`)
		}
		else {
			const minutes = Math.round((durationSeconds / 60) % 60)
			durationBits.push(`${minutes} minutes`)
		}
	}
	return durationBits.join(', ')
}

function formatResolution(info: PersistentConflict) {
	// potentially three people involved:
	//	a: original author
	//	b: owner of conflict (when branch resolver overridden)
	//	c: instigator of skip

	// Format message as "a's change was skipped [by c][ on behalf of b][ after N minutes]
	// combinations of sameness:
	// (treat null c as different value, but omit [by c])
	//		all same: 'a skipped own change'
	//		a:	skipped by owner, @a, write 'by owner (b)' instead of b, c
	//		b:	'a skipped own change' @b
	//		c:	resolver not overridden, @a, omit b
	//		all distinct: @a, @b
	// @ a and/or c if they're not the same as b

	const overriddenOwner = info.owner !== info.author

	// display info.resolvingAuthor where possible, because it has correct case
	const resolver = info.resolvingAuthor && info.resolvingAuthor.toLowerCase()
	const bits: string[] = []
	if (resolver === info.author) {
		bits.push(info.resolvingAuthor!, info.resolution!, 'own change')
		if (overriddenOwner) {
			bits.push('on behalf of', info.owner)
		}
	}
	else {
		bits.push(info.author + "'s", 'change was', info.resolution!)
		if (!resolver) {
			// don't know who skipped (shouldn't happen) - notify owner
			if (overriddenOwner) {
				bits.push(`(owner: ${info.owner})`)
			}
		}
		else if (info.owner === resolver) {
			bits.push(`by owner (${info.resolvingAuthor})`)
		}
		else {
			bits.push('by', info.resolvingAuthor!)
			if (overriddenOwner) {
				bits.push('on behalf of', atifyUser(info.owner))
			}
			else {
				// only case we @ author - change has been resolved by another known person, named in message
				bits[0] = atifyUser(info.author) + "'s"
			}
		}
	}

	if (info.timeTakenToResolveSeconds) {
		bits.push('after', formatDuration(info.timeTakenToResolveSeconds))
	}

	if (info.resolvingReason) {
		bits.push(`(Reason: ${info.resolvingReason})`)
	}

	let message = bits.join(' ') + '.'

	if (info.timeTakenToResolveSeconds) {
		// add time emojis!
		if (info.timeTakenToResolveSeconds < 2*60) {
			const emote = Random.choose(['eevee_run', 'espeon_run', 'sylveon_run', 'flareon_run',
				'jolteon_run', 'leafeon_run', 'glaceon_run', 'umbreon_run', 'vaporeon_run', 'sonic_run', 'fallguy_run'])
			message += ` :${emote}:`
		}
		else if (info.timeTakenToResolveSeconds < 10*60) {
			message += ' :+1:'
		}
		// else if (info.timeTakenToResolveSeconds > 30*60) {
		// 	message += ' :sadpanda:'
		// }
	}
	return message
}

/** Wrapper around Slack, keeping track of messages keyed on target branch and source CL */
export class SlackMessages {
	private readonly smLogger: ContextualLogger
	constructor(private slack: Slack, private persistedMessages: PersistedSlackMessages, parentLogger: ContextualLogger) {
		this.smLogger = parentLogger.createChild('SlackMsgs')
	}

	async postOrUpdate(cl: number, branchArg: BranchArg, message: SlackMessage, persistMessage = true) {
		const findResult = await this.find(cl, branchArg, message.channel)

		// If we find a message, simply update the contents
		if (findResult) {
			// keep ack field if present and not in new message

			if (findResult.messageOpts.fields && (
				!message.fields || !message.fields.some(field =>
				field.title === ACKNOWLEDGED_FIELD_TITLE))) {

				for (const field of findResult.messageOpts.fields) {
					if (field.title === ACKNOWLEDGED_FIELD_TITLE) {
						message.fields = [...(message.fields || []), field]
						break
					}
				}
			}
			findResult.messageOpts = message
			await this.update(findResult)

			return findResult.permalink
		}
		// Otherwise, we will need to create a new one
		else {
			message.target = resolveBranchArg(branchArg, true)
			message.cl = cl

			let timestamp: string
			let permalink: string
			try {
				timestamp = await this.slack.postMessage(message)
				permalink = await this.slack.getPermalink(timestamp, message.channel)
			}
			catch (err) {
				this.smLogger.printException(err, 'Error talking to Slack')
				return
			}

			// Used for messages we don't care to keep, currently the /api/test/directmessage endpoint
			if (persistMessage) {
				this.persistedMessages.add(message, timestamp, permalink)
			}

			return permalink
		}
	}

	async postReply(cl: number, branchArg: BranchArg, message: SlackMessage): Promise<string | undefined> {
		const findResult = await this.find(cl, branchArg, message.channel)

		if (findResult) {
			try {
				return await this.slack.reply(findResult.timestamp, message)
			}
			catch (err) {
				console.error('Error updating message in Slack! ' + err.toString())
				return undefined
			}
		}
		else {
			this.smLogger.error(`Failed to find message record for ${cl}:${branchArg}:${message.channel}`)
			return undefined
		}
	}

	async postButtonReply(cl: number, branchArg: BranchArg, message: SlackMessage) {
		const persistedMsg = await this.find(cl, branchArg, message.channel)
		if (!persistedMsg) {
			this.smLogger.error(`Failed to find message record for ${cl}:${branchArg}:${message.channel}`)
				return
			}
		try {
			const ts = await this.slack.reply(persistedMsg.timestamp, message)
			persistedMsg.buttonReplyTimestamp = ts
			persistedMsg.update()
		}
		catch (err) {
			this.smLogger.error(`Error posting button reply to Slack: ${err}`)
		}
	}

	async updateButtonReply(cl: number, branchArg: BranchArg, message: SlackMessage) {
		const persistedMsg = await this.find(cl, branchArg, message.channel)
		if (!persistedMsg) {
			this.smLogger.error(`Failed to find message record for ${cl}:${branchArg}:${message.channel}`)
			return
		}
		if (persistedMsg.buttonReplyTimestamp) {
			try {
				await this.slack.update(persistedMsg.buttonReplyTimestamp, message)
			}
			catch (err) {
				this.smLogger.error(`Error updating button reply in Slack: ${err}`)
			}
		}
		else {
			await this.postButtonReply(cl, branchArg, message)
		}
	}

	async clearButtonReplies(cl: number, branchArg: BranchArg) {
		const messages = await this.persistedMessages.findAll(cl, branchArg)
		for (const msg of messages) {
			if (msg.buttonReplyTimestamp) {
				try {
					await this.slack.delete(msg.buttonReplyTimestamp, msg.messageOpts.channel)
				}
				catch (err) {
					this.smLogger.error(`Error deleting button reply in Slack: ${err}`)
				}
				msg.buttonReplyTimestamp = undefined
				msg.update()
			}
			// Also clear buttons embedded directly in DM message bodies
			if (msg.messageOpts.blocks && msg.messageOpts.blocks.length > 0) {
				try {
					await this.slack.update(msg.timestamp, { ...msg.messageOpts, blocks: [] })
				}
				catch (err) {
					this.smLogger.error(`Error clearing blocks from message: ${err}`)
				}
				msg.messageOpts.blocks = []
				msg.update()
			}
		}
	}

	async postFile(cl: number, branchArg: BranchArg, file: SlackFile) {
		const findResult = await this.find(cl, branchArg, file.channels)

		if (findResult) {
			try {
				file.thread_ts = findResult.timestamp
				await this.slack.uploadFile(file)
			}
			catch (err) {
				console.error('Error uploading file to Slack! ' + err.toString())
				return
			}
		}
		else {
			this.smLogger.error(`Failed to find message record for ${cl}:${branchArg}:${file.channels}`)
		}
	}

	private async getDMChannelId(emailAddress: string|Promise<string|null>|null, cl: number) {
		// The Slack API requires a user ID to open a direct message with users.
		// The most consistent way to do this is getting their email address out of P4.
		if (emailAddress && typeof(emailAddress) !== 'string') {
			emailAddress = await emailAddress
		}
		if (!emailAddress) {
			this.smLogger.error("Failed to get email address during notifications for CL " + cl)
			return
		}

		let userId : string
		try {
			userId = (await this.getSlackUser(emailAddress))
		} catch (err) {
			this.smLogger.printException(err, `Failed to get user ID for Slack DM, given email address "${emailAddress}" for CL ${cl}`)
			return
		}

		// Open up a new conversation with the user now that we have their ID
		try {
			return this.slack.openDMConversation(userId)
		} catch (err) {
			this.smLogger.printException(err, `Failed to get Slack conversation ID for user ID "${userId}" given email address "${emailAddress}" for CL ${cl}`)
		}

		return
	}

	async postDM(emailAddress: string|Promise<string|null>|null, cl: number, branchArg: BranchArg, dm: SlackMessage, persistMessage = true) {

		const channelId = await this.getDMChannelId(emailAddress, cl)
		if (channelId) {
			dm.channel = channelId
			// Add the channel/conversation ID to the messageOpts and proceed normally.
			this.smLogger.info(`Creating direct message for ${emailAddress} (key: ${PersistedSlackMessages.generateKey(cl, branchArg, channelId)})`)
			await this.postOrUpdate(cl, branchArg, dm, persistMessage)
		}
	}

	async postFileToDM(emailAddress: string|Promise<string|null>|null, cl: number, branchArg: BranchArg, file: SlackFile) {

		const channelId = await this.getDMChannelId(emailAddress, cl)
		if (!channelId) {
			return
		}
		file.channels = channelId
		await this.postFile(cl, branchArg, file)
	}

	findAll(cl: number, branchArg: BranchArg) {
		return this.persistedMessages.findAll(cl, branchArg)
	}

	find(cl: number, branchArg: BranchArg, channel: string) {
		return this.persistedMessages.find(cl, branchArg, channel)
	}

	async update(persistedMessage: PersistedSlackMessage) {
		try {
			await this.slack.update(persistedMessage.timestamp, persistedMessage.messageOpts)
		}
		catch (err) {
			console.error('Error updating message in Slack! ' + err.toString())
			return
		}

		persistedMessage.update()
	}

	/** Deliberately ugly function name to avoid accidentally posting duplicate messages about conflicts! */
	postNonConflictMessage(msg: SlackMessage) {
		this.slack.postMessage(msg)
		.catch(err => console.error('Error posting non-conflict to Slack! ' + err.toString()))
	}

	async addUserToChannel(emailAddress: string, channel: string, externalUser? :boolean)
	{
		if (!isUserAKnownBot(emailAddress)) {
			const user = await this.getSlackUser(emailAddress)
			if (user) {
				const result = await this.slack.addUserToChannel(user, channel, externalUser)
				if (!result.ok) {
					if (result.error === "already_in_channel") { /* this is expected and fine */ }
					else if (result.error === "failed_for_some_users") { 
						if (result.failed_user_ids[user] === "unable_to_add_user_to_public_channel") {
							/* this is expected and fine */ 
						}
						else {
							const errorMsg = `Error inviting ${emailAddress} (${user}) to channel <#${channel}>: ${result.error}\n${JSON.stringify(result.failed_user_ids)}`
							this.smLogger.error(errorMsg)
							postToRobomergeAlerts(errorMsg)
						}
					}
					else if (result.error === "user_is_restricted") {
						// This an external user so we need to use the admin invite and validate that the channel ends in -ext
						const channelInfo = await this.slack.getChannelInfo(channel)
						if (!channelInfo.channel) {
							const errorMsg = `Failed to get name of channel ${channel} when inviting user ${emailAddress} (${user})`
							this.smLogger.error(errorMsg)
							postToRobomergeAlerts(errorMsg)
						}
						else if (channelInfo.channel.name.endsWith('-ext')) {
							await this.addUserToChannel(emailAddress, channel, true)
						}
						else {
							this.smLogger.error(`Cannot invite external user ${emailAddress} (${user}) to restricted channel ${channel}`)
						}
					}
					else {
						const errorMsg = `Error inviting ${emailAddress} (${user}) to channel <#${channel}>: ${result.error}`
						this.smLogger.error(errorMsg)
						postToRobomergeAlerts(errorMsg)
					}
				}
			}
			else {
				const errorMsg = `Unable to add ${emailAddress} to channel <#${channel}>`
				this.smLogger.error(errorMsg)
				postToRobomergeAlerts(errorMsg)
			}
		}
	}

	async getPingTarget(userEmailAddress: Promise<string|null>, owner: string) {
		const userEmail = await userEmailAddress
		const slackUser = userEmail ? await this.getSlackUser(userEmail) : null
		return slackUser 
		? `<@${slackUser}>` 
		: isUserSlackGroup(owner)
			? owner 
			: isUserAKnownBot(owner)
				? ""
				: `@${owner}`
	}

	// With their email address, we can get their user ID via Slack API
	getSlackUser(emailAddress: string) {
		return this.slack.lookupUserIdByEmail(emailAddress.toLowerCase())
	}

	async getSlackUserEmail(slackUserId: string): Promise<string | null> {
		const result = await this.slack.getUserInfo(slackUserId)
		return (result && result.ok && result.user && result.user.profile && result.user.profile.email)
			? result.user.profile.email as string
			: null
	}
}

export function makeClLink(cl: number, alias?: string) {
	return `<https://p4-swarm.companyname.net/changes/${cl}|${alias ? alias : cl}>`
}

export class BotNotifications implements BotEventHandler {
	private readonly externalRobomergeUrl : string;
	private readonly blockageUrlGenerator : NodeOpUrlGenerator
	private readonly botNotificationsLogger : ContextualLogger
	slackChannel: string;

	constructor(private botname: string, slackChannel: string, externalUrl: string, 
		blockageUrlGenerator: NodeOpUrlGenerator, parentLogger: ContextualLogger,
		slackMessages?: SlackMessages, slackChannelOverrides?: [Branch, Branch, string, boolean][]) {
		this.botNotificationsLogger = parentLogger.createChild('Notifications')
		// Hacky way to dynamically change the URL for notifications
		this.externalRobomergeUrl = externalUrl
		this.slackChannel = slackChannel
		this.slackMessages = slackMessages
		this.blockageUrlGenerator = blockageUrlGenerator

		if (slackMessages && slackChannelOverrides) {
			this.additionalBlockChannelIds = new Map(slackChannelOverrides
				.filter(
					([_1, _2, channel, _3]) => channel !== slackChannel
				)
				.map(
					([source, target, channel, postOnlyToChannel]) => [`${source.upperName}|${target.upperName}`, [channel, postOnlyToChannel]]
				)
			)
		}
	}

	getChannelsToPostTo(sourceBranch: Readonly<EditableBranch>|string, targetBranchName?: string) {
		let channelsToPostTo = []
		if (targetBranchName) {
			const sourceBranchName = (typeof(sourceBranch) === 'string' ? sourceBranch : sourceBranch.upperName)
			const additionalChannelInfo = this.additionalBlockChannelIds.get(sourceBranchName + '|' + targetBranchName)
			if (additionalChannelInfo) {
				const [sideChannel, postOnlyToSideChannel] = additionalChannelInfo
				if (!postOnlyToSideChannel) {
					channelsToPostTo.push(this.slackChannel)
				}
				channelsToPostTo.push(sideChannel)
			}
			else {
				channelsToPostTo.push(this.slackChannel)
			}
		}
		else if (typeof(sourceBranch) !== 'string') {
			if (sourceBranch.config.additionalSlackChannelForBlockages) {
				if (!sourceBranch.config.postMessagesToAdditionalChannelOnly) {
					channelsToPostTo.push(this.slackChannel)
				}
				channelsToPostTo.push(sourceBranch.config.additionalSlackChannelForBlockages)
			}
			else {
				channelsToPostTo.push(this.slackChannel)
			}
		}
		return channelsToPostTo
	}

	async getTriager(triager: string, triagerEmail?: Promise<string|null>) {
		if (triager && !isUserSlackGroup(triager))
		{
			const emailAddress = await triagerEmail
			if (emailAddress)
			{
				const user = await this.slackMessages!.getSlackUser(emailAddress)
				if (user) {
					triager = `<@${user}>`
				}
				else {
					triager = `@${triager}`
					this.botNotificationsLogger.error(`Unable to look up triager from ${triager} (${emailAddress})`)
				}
			}
			else {
				triager = `@${triager}`
				this.botNotificationsLogger.error(`Unable to look up email address for triager ${triager}`)
			}
		}
		return triager
	}

	/** Conflict */
	// considered if no Slack set up, should continue to add people to notify, but too complicated:
	//		let's go all in on Slack. Fine for this to be async (but fire and forget)
	async onBlockage(blockage: Blockage, isNew: boolean) {
		const changeInfo = blockage.change

		if (changeInfo.userRequest) {
			// maybe DM?
			return
		}

		// TODO: Support DMs if we don't have a channel configured
		if (!this.slackMessages) {
			// doing nothing at the moment - don't want to complicate things with fallbacks
			// probably worth having fallback channel, so don't necessarily have to always set up specific channel
			// (in that case would have to show bot as well as branch)
			return
		}

		// or integration failure (better wording? exclusive check-out?)
		const sourceBranch = changeInfo.branch
		let targetBranch
		if (blockage.action) {
			targetBranch = blockage.action.branch
		}
		const issue = blockage.failure.kind.toLowerCase()

		const channelPing = await this.slackMessages.getPingTarget(blockage.ownerEmail, blockage.owner)

		const isBotUser = isUserAKnownBot(blockage.owner)
		let text = ""
		if (blockage.approval) {
			text = `${channelPing}'s change needs to be approved\n\n${blockage.approval.settings.description}`
		}
		else if (isBotUser) {
			if (blockage.triager) {
				const triagerPing = await this.getTriager(blockage.triager, blockage.triagerEmail)
				if (triagerPing) {
					text = `${triagerPing} - `
				}
			}
			text += `Blockage caused by \`${blockage.owner}\` commit!`
		}
		else {
			text = `${channelPing}, please resolve the following ${issue}:`
		}														

		// Look up blockage URLs up front — needed for both channel message buttons and DM
		const blockageUrls = this.blockageUrlGenerator(blockage)

		let message = this.makeSlackChannelMessage(
			`${sourceBranch.displayName} blocked! (${issue})`,
			text,
			SlackMessageStyles.DANGER, 
			makeClLink(changeInfo.cl), 
			sourceBranch, 
			targetBranch, 
			changeInfo.author
		);

		if (blockage.approval) {
			message.fields?.push({title: 'Shelved Change', short: true, value: makeClLink(blockage.approval.shelfCl)})
		}

		let messagesToPost: { message: SlackMessage, reply?: boolean }[] = []
		const channelsToPostTo = this.getChannelsToPostTo(sourceBranch, targetBranch?.upperName)
		let usersToInvite: Set<string> = new Set()

		const userEmail = await blockage.ownerEmail
		if (userEmail) {
			usersToInvite.add(userEmail)
		}

		messagesToPost.push({ message })

		let threadLinks = []

		const branchArg = targetBranch ? targetBranch.name : blockage.failure.kind
		for (const channel of channelsToPostTo) {
			for (const user of usersToInvite) {
				this.slackMessages.addUserToChannel(user, channel)
			}
			for (const messageToPost of messagesToPost) {
				if (messageToPost.reply) {
					await this.slackMessages.postReply(changeInfo.cl, branchArg, { ...messageToPost.message, channel })
				}
				else {
					threadLinks.push(await this.slackMessages.postOrUpdate(changeInfo.cl, branchArg, { ...messageToPost.message, channel }))
				}
			}
		}

		if (targetBranch) {
			// For first-time blockages, post supplementary details (conflict files, locked file pings)
			if (isNew) {
				await this._postBlockageDetails(blockage, changeInfo.cl, branchArg, channelsToPostTo)
			}

			// Post action buttons as a thread reply (new blockage) or update existing (re-trigger).
			// updateButtonReply falls back to posting if no existing ts is tracked.
			if (blockageUrls?.params) {
				const actionsBlock = this._makeActionsBlock(blockageUrls, targetBranch.displayName)
				if (actionsBlock.elements.length > 0) {
					for (const channel of channelsToPostTo) {
						if (isNew) {
							await this.slackMessages.postButtonReply(changeInfo.cl, branchArg, {
								text: '',
								channel,
								mrkdwn: false,
								blocks: [actionsBlock]
							})
						} 
						else {
							await this.slackMessages.updateButtonReply(changeInfo.cl, branchArg, {
								text: '',
								channel,
								mrkdwn: false,
								blocks: [actionsBlock]
							})
						}
					}
				}
			}

			// Post message to owner in DM
			if (!isBotUser && userEmail) {
				let dm: SlackMessage
				if (blockage.approval) {
					dm = {
						title: 'Approval needed to commit to ' + targetBranch.displayName,
						text: `Your change has been shelved in ${makeClLink(blockage.approval.shelfCl)} and sent to <#${blockage.approval.settings.channelId}> for approval\n\n` +
								blockage.approval.settings.description,
						channel: "",
						mrkdwn: true
					}
				}
				else {
					if (!blockageUrls) {
						const error = `Could not get blockage URLs for blockage -- CL ${blockage.change.cl}`
						this.botNotificationsLogger.printException(error)
						throw error
					}

					const dmText = `Your change (${makeClLink(changeInfo.source_cl)}) ` +
						`hit '${issue}' while merging from *${sourceBranch.displayName}* to *${targetBranch.displayName}*.\n\n` +
						'`' + blockage.change.description.substring(0, 80) + '`\n\n' +
						"*_Resolving this blockage is time sensitive._*"
				
					dm = this.makeSlackDirectMessage(dmText, changeInfo.cl, changeInfo.cl, targetBranch.displayName, blockageUrls, threadLinks.filter((l): l is string => !!l))
				}

				this.slackMessages.postDM(userEmail, changeInfo.cl, targetBranch, dm)
			}
		}
		else {
			// Syntax error — button first (tracked, stays pinned), then description below it
			if (blockageUrls?.params) {
				const actionsBlock = this._makeActionsBlock(blockageUrls, '')
				if (actionsBlock.elements.length > 0) {
					for (const channel of channelsToPostTo) {
						if (isNew) {
							await this.slackMessages.postButtonReply(changeInfo.cl, branchArg, {
								text: '', channel, mrkdwn: false, blocks: [actionsBlock]
							})
						}
						else {
							await this.slackMessages.updateButtonReply(changeInfo.cl, branchArg, {
								text: '', channel, mrkdwn: false, blocks: [actionsBlock]
							})
						}
					}
				}
			}

			// Post syntax error description after the button so new status replies accumulate below
			if (isNew) {
				for (const channel of channelsToPostTo) {
					await this.slackMessages.postReply(changeInfo.cl, branchArg, {
						text: blockage.failure.description,
						style: SlackMessageStyles.DANGER,
						channel,
						mrkdwn: false
					})
				}
			}

			// DM the owner with the error and an Acknowledge button
			if (!isBotUser && userEmail) {
				const dmText = `Your change (${makeClLink(changeInfo.source_cl)}) in *${sourceBranch.displayName}* ` +
					`has a RoboMerge syntax error:\n\n` +
					'`' + blockage.failure.description + '`\n\n' +
					'*Please fix the syntax error and re-submit to unblock RoboMerge.*'
				const dm: SlackMessage = {
					title: `Syntax error in ${sourceBranch.displayName}`,
					text: dmText,
					channel: '',
					mrkdwn: true
				}
				if (blockageUrls?.params) {
					dm.blocks = [this._makeActionsBlock(blockageUrls, '')]
				}
				this.slackMessages.postDM(userEmail, changeInfo.cl, branchArg, dm)
			}
		}
	}

	private _makeActionsBlock(conflictUrls: BlockageNodeOpUrls, targetBranch: string): SlackActionsBlock {
		const params = conflictUrls.params!
		const buttons: SlackInteractiveButton[] = []

		buttons.push(this.makeInteractiveButton('Acknowledge Conflict', 'acknowledge', params, 'primary'))

		if (conflictUrls.createShelfUrl) {
			buttons.push(this.makeInteractiveButton(`Create Shelf in ${targetBranch}`, 'create_shelf', params, 'primary'))
		}
		if (conflictUrls.skipUrl) {
			buttons.push(this.makeInteractiveButton(`Skip Merge to ${targetBranch}`, 'skip', params))
		}
		if (conflictUrls.stompUrl) {
			buttons.push(this.makeInteractiveButton(`Stomp Changes in ${targetBranch}`, 'stompchanges', params, 'danger'))
		}
		if (conflictUrls.unlockUrl) {
			buttons.push(this.makeInteractiveButton(`Unlock ${targetBranch}`, 'unlockchanges', params, 'danger'))
		}

		return { type: 'actions', elements: buttons }
	}

	/** Post supplementary details for a blockage: conflict file list and/or lock holder pings.
	 *  Called for new blockages (isNew=true) and when circumstances change (onBlockageStatusChanged). */
	private async _postBlockageDetails(
		blockage: Blockage,
		cl: number,
		branchArg: string,
		channels: string[]
	) {
		if (!this.slackMessages) return

		// Upload conflict details for merge conflicts
		if (blockage.failure.details) {
			const file: SlackFile = {
				content: blockage.failure.details,
				channels: '',
				filename: 'conflictdetails.txt'
			}
			for (const channel of channels) {
				await this.slackMessages.postFile(cl, branchArg, { ...file, channels: channel })
			}
		}

		// Upload locked files per holder for exclusive check-outs
		if (blockage.failure.kind === 'Exclusive check-out') {
			const exclusiveLockInfo = blockage.failure.additionalInfo as ExclusiveLockInfo
			const authorDict = new Map<string, ExclusiveFileDetails[]>()
			for (const f of exclusiveLockInfo.exclusiveFiles) {
				setDefault(authorDict, f.user.toLowerCase(), []).push(f)
			}
			for (const exclusiveLockUser of exclusiveLockInfo.exclusiveLockUsers) {
				let file: SlackFile = {
					content: authorDict.get(exclusiveLockUser.user)!.map(ef => ef.depotPath).join('\n'),
					channels: '',
					filename: 'exclusivelockedfiles.txt'
				}
				let exclusiveLockSlackUser: string | undefined
				if (exclusiveLockUser.user.length > 0) {
					const exclusiveLockUserEmail = await exclusiveLockUser.userEmail
					if (exclusiveLockUserEmail) {
						const slackId = await this.slackMessages.getSlackUser(exclusiveLockUserEmail)
						if (slackId) {
							exclusiveLockSlackUser = `<@${slackId}> `
						}
						for (const channel of channels) {
							this.slackMessages.addUserToChannel(exclusiveLockUserEmail, channel)
						}
					}
					if (!exclusiveLockSlackUser) {
						exclusiveLockSlackUser = `@${exclusiveLockUser.user} `
					}
					file.initial_comment = `${exclusiveLockSlackUser} please unlock the files blocking robomerge or work with ${blockage.owner} to resolve the conflict. Hit retry on the blocked stream once the files are unlocked.`
				} else {
					file.initial_comment = 'The following locked files did not have their owner determined and as such those owners may not have been notified'
				}
				for (const channel of channels) {
					await this.slackMessages.postFile(cl, branchArg, { ...file, channels: channel })
				}
			}
		}
	}

	/** Fired when the failure kind or file set changes on a retry of the same blocked CL.
	 *  Posts a thread reply noting the transition, then re-posts the full details for the
	 *  new failure kind as if it were first occurrence. */
	async onBlockageStatusChanged(evt: BlockageStatusChangedEvent) {
		if (!this.slackMessages) return
		const { previousConflict, newBlockage } = evt
		const changeInfo = newBlockage.change
		if (changeInfo.userRequest) return

		const sourceBranch = changeInfo.branch
		const targetBranch = newBlockage.action?.branch
		const branchArg = targetBranch ? targetBranch.name : newBlockage.failure.kind
		const channels = this.getChannelsToPostTo(sourceBranch, targetBranch?.upperName)
		const cl = changeInfo.cl

		// Build context line for the thread reply
		const kindChanged = previousConflict.kind !== newBlockage.failure.kind
		let contextLine: string
		if (kindChanged) {
			contextLine = `*Status update:* blockage type changed from *${previousConflict.kind}* to *${newBlockage.failure.kind}*`
		} else {
			contextLine = `*Status update:* ${newBlockage.failure.kind} circumstances have changed`
		}

		// For non-target-branch failures (syntax errors etc.), append the description to the reply
		if (!targetBranch && newBlockage.failure.description) {
			contextLine += `\n\n${newBlockage.failure.description}`
		}

		// Post a brief thread reply noting the change
		const replyMessage: SlackMessage = {
			text: contextLine,
			style: SlackMessageStyles.WARNING,
			channel: '',
			mrkdwn: true
		}
		for (const channel of channels) {
			await this.slackMessages.postReply(cl, branchArg, { ...replyMessage, channel })
		}

		// Post the full supplementary details for the new failure kind
		await this._postBlockageDetails(newBlockage, cl, branchArg, channels)

		// Update the action buttons to reflect the new blockage state
		const blockageUrls = targetBranch ? this.blockageUrlGenerator(newBlockage) : null
		if (targetBranch && blockageUrls?.params) {
			const actionsBlock = this._makeActionsBlock(blockageUrls, targetBranch.displayName)
			if (actionsBlock.elements.length > 0) {
				for (const channel of channels) {
					await this.slackMessages.updateButtonReply(cl, branchArg, {
						text: '',
						channel,
						mrkdwn: false,
						blocks: [actionsBlock]
					})
				}
			}
		}
	}

	onBlockageAcknowledged(info: PersistentConflict) {
		if (this.slackMessages) {
			const targetKey = info.targetBranchName || info.kind
			const title = ACKNOWLEDGED_FIELD_TITLE
			if (info.acknowledger) {
				// hard code to Epic (!) Standard/Daylight Time
				const suffix = info.acknowledgedAt ? ' at ' + info.acknowledgedAt.toLocaleTimeString('en-US', EPIC_TIME_OPTIONS) : ''
				this.tryAddFieldToChannelMessages(info.cl, targetKey, {title, value: info.acknowledger + suffix, short: true})
			}
			else {
				this.tryRemoveFieldFromChannelMessages(info.cl, targetKey, title)
			}
		}
	}

	async onNagBlockage(evt: NagBlockageEvent) {
		if (this.slackMessages) {
			const conflict = evt.conflict
			const ageMinutes = ((Date.now() - conflict.time.getTime()) / 1000) / 60
			let timeDesc
			if (ageMinutes < 60) {
				timeDesc = `${Math.floor(ageMinutes)} minutes`
			}
			else {
				const hours = Math.floor(ageMinutes / 60)
				if (hours == 1) {
					timeDesc = "1 hour"
				}
				else {
					timeDesc = `${hours} hours`
				}
			}

			const triager = await this.getTriager(evt.triager, evt.triagerEmail)
			const channelPing = await this.slackMessages.getPingTarget(evt.ownerEmail, conflict.owner)

			let messageTargets = []
			let loggerTargets = []
			if (triager && triager != channelPing) {
				messageTargets.push(triager)
				loggerTargets.push(evt.triager)
			}
			if (channelPing != "") {
				messageTargets.push(channelPing)
				loggerTargets.push(conflict.owner)
			}
			messageTargets.push("Robomerge")

			const nagMessage: SlackMessage = {
				text: `${messageTargets.join(' ')} blocked for more than ${timeDesc}`,
				style: SlackMessageStyles.DANGER,
				channel: this.slackChannel,
				mrkdwn: true
			}

			const channelsToPostTo = this.getChannelsToPostTo(evt.sourceBranch, conflict.targetBranchName)

			this.botNotificationsLogger.info(`Sending nag notification${loggerTargets.length > 0 ? ' to ' + loggerTargets.join(' and ') : ''} after ${Math.floor(ageMinutes)} minutes`)
			const branchName = (conflict.kind === "Syntax error" ? conflict.kind : conflict.targetBranchName || conflict.blockedBranchName)
			for (let slackChannel of channelsToPostTo) {
				nagMessage.channel = slackChannel
				await this.slackMessages.postReply(conflict.cl, branchName, nagMessage)
			}
		}
	}

	////////////////////////
	// Conflict resolution
	//
	// For every non-conflicting merge merge operation, no matter whether we committed anything, look for Slack conflict
	// messages that can be set as resolved. This covers the following cases:
	//
	// Normal case (A): user commits CL with resolved unshelved changes.
	//	- RM reparses the change that conflicted and sees nothing to do
	//
	// Corner case (B): user could commit just those files that were conflicted.
	//	- RM will merge the rest of the files and we'll see a non-conflicted commit

	/** On change (case A above) - update message if we see that a conflict has been resolved */
	onBranchUnblocked(info: PersistentConflict) {
		if (this.slackMessages) {
			let newClDesc: string | undefined, messageStyle: SlackMessageStyles
			if (info.resolution === Resolution.RESOLVED) {
				if (info.resolvingCl) {
					newClDesc = `${makeClLink(info.cl)} -> ${makeClLink(info.resolvingCl)}`
				}
				messageStyle = SlackMessageStyles.GOOD
			}
			else {
				messageStyle = SlackMessageStyles.WARNING
			}

			const messageText = formatResolution(info)
			const targetKey = info.targetBranchName || info.kind
			this.updateMessagesAfterUnblock(info.cl, targetKey, '', messageStyle, messageText, newClDesc)
			.then(success => {
				if (!success) {
					this.botNotificationsLogger.warn(`Conflict message not found to update (${info.blockedBranchName} -> ${targetKey} CL#${info.sourceCl})`)
					const message = this.makeSlackChannelMessage('', messageText, messageStyle, makeClLink(info.cl), info.blockedBranchName, info.targetBranchName, info.author)
					this.slackMessages!.postOrUpdate(info.cl, targetKey, message)
				}
			})
			this.slackMessages.clearButtonReplies(info.cl, targetKey)
		}
	}

	onNonSkipLastClChange(details: ForcedCl) {
		if (this.slackMessages) {
			const channelsToPostTo = this.getChannelsToPostTo(details.sourceBranchUpperName, details.targetBranchUpperName)
			for (let slackChannel of channelsToPostTo) {
				this.slackMessages.postNonConflictMessage({
					title: details.nodeOrEdgeName + ' forced to new CL',
					text: details.reason,
					style: SlackMessageStyles.WARNING,
					fields: [{
						title: 'By', short: true, value: details.culprit
					}, {
						title: 'Changelists', short: true, value: `${makeClLink(details.previousCl)} -> ${makeClLink(details.forcedCl)}`
					}],
					title_link: this.externalRobomergeUrl + '#' + this.botname,
					mrkdwn: true,
					channel: slackChannel // Default to the configured channel
				}) 
			}
		}
	}

	onPause(details: PauseEvent) {
		if (this.slackMessages) {
			const channelsToPostTo = this.getChannelsToPostTo(details.sourceBranchUpperName, details.targetBranchUpperName)
			for (let slackChannel of channelsToPostTo) {
				this.slackMessages.postNonConflictMessage({
					title: details.nodeOrEdgeName + ' paused',
					text: details.message || "",
					style: SlackMessageStyles.WARNING,
					fields: [{
						title: 'By', short: true, value: details.owner
					}],
					title_link: this.externalRobomergeUrl + '#' + this.botname,
					mrkdwn: true,
					channel: slackChannel // Default to the configured channel
				}) 
			}
		}
	}

	onUnpause(details: PauseEvent) {
		if (this.slackMessages) {
			const channelsToPostTo = this.getChannelsToPostTo(details.sourceBranchUpperName, details.targetBranchUpperName)
			for (let slackChannel of channelsToPostTo) {
				this.slackMessages.postNonConflictMessage({
					title: details.nodeOrEdgeName + ' unpaused',
					text: details.message || "",
					style: SlackMessageStyles.WARNING,
					fields: [{
						title: 'By', short: true, value: details.owner
					}],
					title_link: this.externalRobomergeUrl + '#' + this.botname,
					mrkdwn: true,
					channel: slackChannel // Default to the configured channel
				}) 
			}
		}
	}

	sendTestMessage(username : string) {

		if (this.slackMessages) {
			let text = `${username}, please resolve the following test message:\n(source CL: 0, conflict CL: 1, shelf CL: 2)`

			// This doesn't need to be a full Blockage -- just enough to generate required info for message
			const testBlockage: Blockage = {
				action: {
					branch: {
						name: "TARGET_BRANCH",
					} as Branch,
				} as MergeAction,
				failure: {
					kind: "Merge conflict",
					description: ""
				}
			} as Blockage

			const messageOpts = this.makeSlackDirectMessage(text, 0, 1, "TARGETBRANCH",
				NodeBot.getBlockageUrls(
					testBlockage,
					this.externalRobomergeUrl,
					"TEST",
					"SOURCE_BRANCH",
					"0",
					false
				)
			)

			this.slackMessages.postDM(`${username}@companyname.com`, 0, "TARGETBRANCH", messageOpts, false)
		}
		else {
			this.botNotificationsLogger.error(`Slack not enabled. Unable to send test message to ${username}`)
		}
	}

	sendGenericNonConflictMessage(message: string) {
		if (this.slackMessages) {
			this.slackMessages.postNonConflictMessage({
				text: message,
				style: SlackMessageStyles.WARNING,
				mrkdwn: true,
				channel: this.slackChannel
			})
		}
	}

	private makeSlackChannelMessage(title: string, text: string, style: SlackMessageStyles, clDesc: string, sourceBranch: BranchArg,
											targetBranch?: BranchArg, author?: string, buttons?: SlackLinkButtonsAttachment[]) {
		const integrationText = [getDisplayNameFromBranchArg(sourceBranch)]
		if (targetBranch) {
			integrationText.push(getDisplayNameFromBranchArg(targetBranch))
		}
		const fields: SlackMessageField[] = [
			{title: 'Integration', short: true, value: integrationText.join(' -> ')},
			{title: CHANGELIST_FIELD_TITLE, short: true, value: clDesc},
		]

		if (author) {
			fields.push({title: 'Author', short: true, value: author})
		}

		const opts: SlackMessage = {title, text, style, fields,
			title_link: this.externalRobomergeUrl + '#' + this.botname,
			mrkdwn: true,
			channel: this.slackChannel // Default to the configured channel
		}

		if (buttons) {
			opts.attachments = buttons
		}

		return opts
	}

	// This is an extremely opinionated function to send a stylized direct message to the end user.
	private makeSlackDirectMessage(messageText: string, sourceCl: number, conflictCl: number, targetBranch: string, conflictUrls: BlockageNodeOpUrls, threadLinks?: string[]) : SlackMessage {
		// Start collecting our attachments
		let attachCollection : SlackAttachment[] = []
		
		const helpURL = RoboArgs.args.helpPageURL != '/help' ? RoboArgs.args.helpPageURL : `${this.externalRobomergeUrl}/help`

		// General information
		attachCollection.push({
			text: `To learn more about robomerge and how to resolve blockages, please review the <${helpURL}|robomerge help page>.`,
			mrkdwn_in: ["text"]
		})

		threadLinks = (threadLinks || []).filter(threadLink => threadLink)
		if (threadLinks.length > 0) {
			let text = `Discussion for this issue can be found in the following thread${threadLinks.length > 1 ? 's' : ''}:`
			for (const threadLink of threadLinks) {
				text += `\n${threadLink}`
			}
			attachCollection.push({
				text,
				mrkdwn_in: ["text"]
			})
		}

		attachCollection.push({
			text: "You can also get help via the robomerge Slack channel: <#C9321FLTU>",
			mrkdwn_in: ["text"]
		})
		attachCollection.push({
			text: "If you cannot login to robomerge or access the 'robomerge-help' slack channel, please contact the IT helpdesk.",
			mrkdwn_in: ["text"]
		})

		const conflictClLink = makeClLink(conflictCl, 'conflict CL #' + conflictCl)
		const params = conflictUrls.params
		if (params) {
			// Block Kit interactive buttons — actions performed directly in Slack
			const buttons: SlackInteractiveButton[] = []

			// Acknowledge
			buttons.push(this.makeInteractiveButton('Acknowledge Conflict', 'acknowledge', params, 'primary'))

			// Create Shelf (workspace selected via modal)
			if (conflictUrls.createShelfUrl) {
				buttons.push(this.makeInteractiveButton(`Create Shelf in ${targetBranch}`, 'create_shelf', params, 'primary'))
			}

			// Skip
			if (conflictUrls.skipUrl) {
				buttons.push(this.makeInteractiveButton(`Skip Merge to ${targetBranch}`, 'skip', params))
			}

			// Stomp
			if (conflictUrls.stompUrl) {
				buttons.push(this.makeInteractiveButton(`Stomp Changes in ${targetBranch}`, 'stompchanges', params, 'danger'))
			}

			// Unlock (exclusive check-out)
			if (conflictUrls.unlockUrl) {
				buttons.push(this.makeInteractiveButton(`Unlock ${targetBranch}`, 'unlockchanges', params, 'danger'))
			}

			const actionsBlock: SlackActionsBlock = { type: 'actions', elements: buttons }

			attachCollection.push({
				text: `"I will merge ${conflictClLink} to *${targetBranch}* myself."`,
				mrkdwn_in: ["text"]
			})

			return {
				text: messageText,
				mrkdwn: true,
				channel: "",
				attachments: attachCollection,
				blocks: [actionsBlock]
			}
		}
		else {
			// Fallback: legacy link buttons (no params means no interactive support)
			attachCollection.push(<SlackLinkButtonsAttachment>{
				text: `"I will merge ${conflictClLink} to *${targetBranch}* myself."`,
				fallback: `Please acknowledge blockages at ${this.externalRobomergeUrl}`,
				mrkdwn_in: ["text"],
				actions: [{ type: "button", text: "Acknowledge Conflict", url: conflictUrls.acknowledgeUrl, style: "primary" }]
			})

			if (conflictUrls.createShelfUrl) {
				attachCollection.push(<SlackLinkButtonsAttachment>{
					text: `"Please create a shelf with the conflicts encountered while merging ${makeClLink(sourceCl)} into *${targetBranch}*"`,
					fallback: `You can create a shelf at ${this.externalRobomergeUrl}`,
					mrkdwn_in: ["text"],
					actions: [{ type: "button", text: `Create Shelf in ${targetBranch}`, url: conflictUrls.createShelfUrl, style: "primary" }]
				})
			}

			if (conflictUrls.skipUrl) {
				attachCollection.push(<SlackLinkButtonsAttachment>{
					text: `"${makeClLink(sourceCl)} should not be automatically merged to *${targetBranch}*."`,
					fallback: `You can skip work at ${this.externalRobomergeUrl}`,
					mrkdwn_in: ["text"],
					actions: [{ type: "button", text: `Skip Merge to ${targetBranch}`, url: conflictUrls.skipUrl, style: "default" }]
				})
			}

			if (conflictUrls.stompUrl) {
				attachCollection.push(<SlackLinkButtonsAttachment>{
					text: `"The changes in ${makeClLink(sourceCl)} should stomp the work in *${targetBranch}*."`,
					fallback: `You can stomp work at ${this.externalRobomergeUrl}`,
					mrkdwn_in: ["text"],
					actions: [{ type: "button", text: `Stomp Changes in ${targetBranch}`, url: conflictUrls.stompUrl, style: "danger" }]
				})
			}

			return {
				text: messageText,
				mrkdwn: true,
				channel: "",
				attachments: attachCollection
			}
		}
	}

	private makeInteractiveButton(buttonText: string, actionId: string, params: BlockageNodeOpParams, style?: 'primary' | 'danger'): SlackInteractiveButton {
		const btn: SlackInteractiveButton = {
			type: 'button',
			text: { type: 'plain_text', text: buttonText },
			action_id: actionId,
			value: JSON.stringify(params)
		}
		if (style) {
			btn.style = style
		}
		return btn
	}

	

	/** Pre-condition: this.slackMessages must be valid */
	private async updateMessagesAfterUnblock(cl: number, targetBranch: BranchArg, newTitle: string,
										newStyle: SlackMessageStyles, newText: string, newClDesc?: string) {
		// Find all messages relating to CL and branch
		const messages = await this.slackMessages!.findAll(cl, targetBranch)

		if (messages.length == 0) {
			return false
		}

		for (const messageRecord of messages) {
			const message = messageRecord.messageOpts
			if (newTitle) {
				message.title = newTitle
			}
			else {
				delete message.title
			}

			// e.g. change colour from red to orange
			message.style = newStyle

			// e.g. change source CL to 'source -> dest'
			if (message.fields) {
				const newFields: SlackMessageField[] = []
				for (const field of message.fields) {
					switch (field.title) {
						case CHANGELIST_FIELD_TITLE:
							if (newClDesc) {
								field.value = newClDesc
							}
							break

						case ACKNOWLEDGED_FIELD_TITLE:
							// skip add
							continue
					}
					newFields.push(field)
				}
				message.fields = newFields
			}

			// Delete button attachments and blocks sent via Robomerge Slack App
			if (message.attachments) {
				delete message.attachments
				message.blocks = []

				// Hacky: If we remove attachments, we'll no longer have a link to the CL in the message.
				// UE-72320 - Add in a link to the original changelist
				message.text = newText.replace('change', `change (${makeClLink(cl)})`)
			}
			else {
				message.text = newText
			}

			// optionally remove second row of entries
			if (message.fields) {
				// remove shelf entry
				message.fields = message.fields.filter(field => field.title !== 'Shelf' && field.title !== 'Author')
				delete message.footer
			}

			this.slackMessages!.update(messageRecord)
		}
		
		return true
	}

	/** Pre-condition: this.slackMessages must be valid */
	private tryAddFieldToChannelMessages(cl: number, targetBranch: BranchArg, newField: SlackMessageField) {
		// Find all messages relating to CL and branch
		this.slackMessages!.findAll(cl, targetBranch)
		.then(messages => {
			for (const messageRecord of messages) {
				const message = messageRecord.messageOpts
				if (message.attachments) {
					// skip DMs
					continue
				}

				if (message.fields && message.fields.find(field => field.title === newField.title)) {
					// do not add same field twice (shouldn't happen, but hey)
					continue
				}

				message.fields = [...(message.fields || []), newField]
				this.slackMessages!.update(messageRecord)
			}
		})
	}

	/** Pre-condition: this.slackMessages must be valid */
	private tryRemoveFieldFromChannelMessages(cl: number, targetBranch: BranchArg, fieldTitle: string) {
		// Find all messages relating to CL and branch
		this.slackMessages!.findAll(cl, targetBranch)
		.then(messages => {
			for (const messageRecord of messages) {
				const message = messageRecord.messageOpts
				if (message.attachments || !message.fields) {
					// skip DMs (expecting there to be fields usually, but skipping if not)
					continue
				}

				const replacementFields = message.fields.filter(field => field.title !== fieldTitle)
				if (message.fields.length > replacementFields.length) {
					message.fields = replacementFields
					this.slackMessages!.update(messageRecord)
				}
			}
		})
	}

	private readonly slackMessages?: SlackMessages
	private readonly additionalBlockChannelIds = new Map<string, [string, boolean]>()
}

export function bindBotNotifications(events: BotEvents, slackChannelOverrides: [Branch, Branch, string, boolean][], persistence: PersistedSlackMessages, blockageUrlGenerator: NodeOpUrlGenerator, 
	externalUrl: string, logger: ContextualLogger) {

	let slackMessages

	const botToken = (!RoboArgs.args.devMode || RoboArgs.args.useSlackInDev) && SLACK_TOKENS.bot || RoboArgs.args.devMode && RoboArgs.args.useSlackInDev && SLACK_DEV_DUMMY_TOKEN 
	const userToken = (!RoboArgs.args.devMode || RoboArgs.args.useSlackInDev) && SLACK_TOKENS.user || RoboArgs.args.devMode && RoboArgs.args.useSlackInDev && SLACK_DEV_DUMMY_TOKEN
	if (botToken && events.botConfig.slackChannel) {
		logger.info('Enabling Slack messages for ' +  events.botname)
		slackMessages = new SlackMessages(new Slack({id: events.botConfig.slackChannel, botToken, userToken}, RoboArgs.args.slackDomain, logger), persistence, logger)
	}
		
	events.registerHandler(new BotNotifications(events.botname, events.botConfig.slackChannel, externalUrl, blockageUrlGenerator, logger, slackMessages, slackChannelOverrides))

	return slackMessages
}

export function runTests(parentLogger: ContextualLogger) {
	const unitTestLogger = parentLogger.createChild('Notifications')
	const conf: PersistentConflict = {
		blockedBranchName: 'from',
		targetBranchName: 'to',

		cl: 101,
		sourceCl: 1,
		author: 'x',
		owner: 'x',
		kind: 'Unit Test error',

		time: new Date,
		nagCount: 0,
		ugsIssue: -1,

		resolution: 'pickled' as Resolution
	}

	let nextCl = 101

	const tests = [
		["x's change was pickled.",							'x'],
		["x's change was pickled (owner: y).",				'y'],
		["x pickled own change.",							'x', 'x'],
		["x pickled own change on behalf of y.",			'y', 'x'],
		["@x's change was pickled by y.",					'x', 'y'],
		["x's change was pickled by owner (y).",			'y', 'y'],
		["x's change was pickled by y on behalf of @z.",	'z', 'y'],
	]

	let passed = 0
	for (const test of tests) {
		conf.owner = test[1]
		conf.resolvingAuthor = test[2]
		conf.cl = nextCl++

		const formatted = formatResolution(conf)
		if (test[0] === formatted) {
			++passed
		}
		else {
			unitTestLogger.error('Mismatch!\n' +
				`\tExpected:   ${test[0]}\n` + 
				`\tResult:     ${formatted}\n\n`)
		}
	}

	unitTestLogger.info(`Resolution format: ${passed} out of ${tests.length} correct`)
	return tests.length - passed
}
