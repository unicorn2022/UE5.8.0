// Copyright Epic Games, Inc. All Rights Reserved.
import { FunctionalTest, P4Client, P4Util, Stream } from '../framework'

// Tests that ~<branchName> in a #robomerge command shelves at the correct pointinstead of auto-merging
// Also tests that multiple ~<branchName> in a #robomerge command shelves for each
const streams: Stream[] = [
	{name: 'Main', streamType: 'mainline'},
	{name: 'Dev1', streamType: 'development', parent: 'Main'},
	{name: 'Dev2', streamType: 'development', parent: 'Main'},
	{name: 'Dev3', streamType: 'development', parent: 'Main'},
	{name: 'Dev1_Plus', streamType: 'development', parent: 'Main'},
]

export class ManualEdgeMerges extends FunctionalTest {
	async setup() {
		await this.p4.depot('stream', this.depotSpec())
		await this.createStreamsAndWorkspaces(streams)

		this.mainClient = this.getClient('Main')
		await P4Util.addFileAndSubmit(this.mainClient, 'test.txt', 'Initial content')

		await this.populateStreams(streams.slice(1))
	}

	async run() {
		const manualMerges = streams.slice(2).map(s => `~${this.fullBranchName(s.name)}`).join(" ")
		await P4Util.editFileAndSubmit(this.mainClient, 'test.txt', 'Main edit',
			`${manualMerges}`)

		await this.waitForRobomergeIdle()

		// Confirm a pending/shelved CL exists in the Release workspace
		await Promise.all(
			streams.slice(2).map(async s => {
			const releaseClient = this.getClient(s.name)
			const pendingChanges = await releaseClient.changes(1, true)
			if (pendingChanges.length === 0) {
				throw new Error(`Expected a shelved CL in ${s.name} workspace but found none`)
			}
		}))
	}

	async verify() {
		// Dev should have been auto-merged (head rev 2)
		// Release should NOT have been submitted (still head rev 1)
		await Promise.all([
			this.checkHeadRevision('Main', 'test.txt', 2),
			this.checkHeadRevision('Dev1', 'test.txt', 2),
			this.checkHeadRevision('Dev2', 'test.txt', 1),
			this.checkHeadRevision('Dev3', 'test.txt', 1),
			this.checkHeadRevision('Dev1_Plus', 'test.txt', 1),
		])
	}

	getBranches() {
		return [
			this.makeForceAllBranchDef('Main', ['Dev1', 'Dev2', 'Dev3']),
			this.makeForceAllBranchDef('Dev1', ['Dev1_Plus']),
			this.makeForceAllBranchDef('Dev2', []),
			this.makeForceAllBranchDef('Dev3', []),
			this.makeForceAllBranchDef('Dev1_Plus', [])
		]
	}

	private mainClient: P4Client
}
