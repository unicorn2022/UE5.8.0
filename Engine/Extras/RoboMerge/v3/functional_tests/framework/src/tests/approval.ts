import { EdgeProperties, P4Util } from '../framework'
import { SimpleMainAndReleaseTestBase, streams } from '../SimpleMainAndReleaseTestBase'

export class Approval extends SimpleMainAndReleaseTestBase {

	async setup() {
		await this.p4.depot('stream', this.depotSpec())
		await this.createStreamsAndWorkspaces(streams)
		await P4Util.addFileAndSubmit(this.getClient('Main'), 'test.txt', 'Initial content')
		await this.initialPopulate()
	}

	async run() {
		const releaseClient = this.getClient('Release')
		await releaseClient.sync()

		// await this.p4.lock(this.getClient('Main').workspace, this.getStreamPath('Main'))
		await P4Util.editFileAndSubmit(releaseClient, 'test.txt', 'Initial content\n\nmergeable')
	}

	verify() {
		return Promise.all([
				this.ensureNotBlocked('Main'),
				this.ensureBlocked('Release', 'Main'),
				this.ensureConflictMessagePostedToSlack('Release', 'Main')
			])
	}

	getEdges() : EdgeProperties[] {
		return [
		  { from: this.fullBranchName('Release'), to: this.fullBranchName('Main')
		  , approval: {
		      description: 'hey there!',
		      channelId: 'abc123',
		      block: true
		    }
		  }
		]
	}
}

export class ApprovalNonBlocking extends SimpleMainAndReleaseTestBase {
	cl = -1

	async setup() {
		await this.p4.depot('stream', this.depotSpec())
		await this.createStreamsAndWorkspaces(streams)
		await P4Util.addFileAndSubmit(this.getClient('Main'), 'test.txt', 'Initial content')
		await this.initialPopulate()
	}

	async run() {
		const releaseClient = this.getClient('Release')
		await releaseClient.sync()
		this.cl = await P4Util.editFileAndSubmit(releaseClient, 'test.txt', 'Initial content\n\nmergeable')
	}

	async verify() {
		const approvalMessageSent = await this.wasMessagePostedToSlack('abc123', this.cl, 'Main')
		if (!approvalMessageSent) {
			throw new Error(`Approval message not sent to channel 'abc123' for CL ${this.cl}`)
		}
		return Promise.all([
			this.ensureNotBlocked('Main'),
			this.ensureNotBlocked('Release', 'Main'),
			// CL should be shelved for approval, not committed to Main
			this.checkHeadRevision('Main', 'test.txt', 1)
		])
	}

	getEdges() : EdgeProperties[] {
		return [
		  { from: this.fullBranchName('Release'), to: this.fullBranchName('Main')
		  , approval: {
		      description: 'hey there!',
		      channelId: 'abc123',
		      block: false
		    }
		  }
		]
	}
}