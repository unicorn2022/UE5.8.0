// Copyright Epic Games, Inc. All Rights Reserved.

import { FunctionalTest, P4Client, P4Util, RobomergeBranchSpec, Stream } from '../framework'
import { Perforce } from '../test-perforce'

const DEPOT_NAME = 'SyntaxErrDefer'
const TEXT_FILENAME = 'test.txt'

const streams: Stream[] = [
	{name: 'Main', streamType: 'mainline', depotName: DEPOT_NAME},
	{name: 'Dev',  streamType: 'development', parent: 'Main', depotName: DEPOT_NAME}
]

let streamsPromise: Promise<void> | null = null
function createStreams(p4: Perforce, test: FunctionalTest) {
	if (streamsPromise) {
		return streamsPromise.then(() =>
			Promise.all(['Main', 'Dev'].map(s => {
				const client = test.p4Client('testuser1', s, DEPOT_NAME)
				return client.create(P4Util.specForClient(client))
			}))
		)
	}

	streamsPromise = (async () => {
		await p4.depot('stream', test.depotSpec(DEPOT_NAME))
		await test.createStreamsAndWorkspaces(streams, DEPOT_NAME)

		const mainClient = test.getClient('Main', 'testuser1', DEPOT_NAME)
		await P4Util.addFileAndSubmit(mainClient, TEXT_FILENAME, 'Initial content')

		await p4.populate(test.getStreamPath('Dev', DEPOT_NAME), 'Initial branch of files from Main')
	})()

	return streamsPromise
}

// Default bot: default bot #ROBOMERGE directives are processed here.
// Submitting '#robomerge unknownNode' triggers a syntax error and blocks Main.
export class SyntaxErrDeferDefaultBot extends FunctionalTest {
	private mainClient: P4Client

	async setup() {
		await createStreams(this.p4, this)
		this.mainClient = this.getClient('Main', 'testuser1', DEPOT_NAME)
	}

	async run() {
		// Bare #robomerge unknownNode — unknown to this bot, triggers a syntax error
		await P4Util.editFileAndSubmit(this.mainClient, TEXT_FILENAME, 'Change', 'unknownNode')
	}

	verify() {
		return this.ensureBlocked('Main')
	}

	getBranches(): RobomergeBranchSpec[] {
		return [{
			streamDepot: DEPOT_NAME,
			name: this.fullBranchName('Main'),
			streamName: 'Main',
			flowsTo: [],
			isDefaultBot: true
		}]
	}

	allowSyntaxErrors() { return true }
}

// Non-default bot: default bot #ROBOMERGE directives are ignored, but should DEFER
// while the default bot has a syntax error on the same CL.
export class SyntaxErrDeferNonDefaultBot extends FunctionalTest {
	async setup() {
		await createStreams(this.p4, this)
	}

	async run() {
		// CL was already submitted by SyntaxErrDeferDefaultBot.run()
	}

	async verify() {
		return Promise.all([
			this.ensureDeferred('Main'),                                         // deferred_cl must be set
			this.checkHeadRevision('Dev', TEXT_FILENAME, 1, DEPOT_NAME)          // Dev must NOT be integrated
		])
	}

	getBranches(): RobomergeBranchSpec[] {
		return [
			{
				streamDepot: DEPOT_NAME,
				name: this.fullBranchName('Main'),
				streamName: 'Main',
				flowsTo: [this.fullBranchName('Dev')],
				isDefaultBot: false	// must be explicit: bot-level config has isDefaultBot:true
			},
			{
				streamDepot: DEPOT_NAME,
				name: this.fullBranchName('Dev'),
				streamName: 'Dev',
				flowsTo: []
			}
		]
	}
}
