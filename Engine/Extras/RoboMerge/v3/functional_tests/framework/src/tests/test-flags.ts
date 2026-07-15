// Copyright Epic Games, Inc. All Rights Reserved.
import { P4Util } from '../framework'
import { MultipleDevAndReleaseTestBase } from '../MultipleDevAndReleaseTestBase'

const initialContent = 'Initial content'
const contentAfterFirstEdit = 'Initial content\n\nFirst addition'

export class TestFlags extends MultipleDevAndReleaseTestBase {
	async setup() {
		await this.p4.depot('stream', this.depotSpec())
		await this.createStreamsAndWorkspaces()

		const client = this.getClient('Main', 'testuser1')
		await Promise.all([
			P4Util.addFile(client, 'test.txt', initialContent),
			P4Util.addFile(client, 'test_delete.txt', initialContent)
		])
		await client.submit('Adding initial files')
		await this.initialPopulate()
	}

	run() {
		const r1client = this.getClient('Release-1.0')
		const nullMergePerkin = '!' + this.fullBranchName('Dev-Perkin')
		const skipPootle = '-' + this.fullBranchName('Dev-Pootle')
		return r1client.sync()
		.then(() => P4Util.editFileAndSubmit(r1client, 'test.txt', contentAfterFirstEdit, skipPootle))
		.then(() => P4Util.editFile(r1client, 'test.txt', 
				'Initial content\n\nFirst addition\n\nSecond addition'))
		.then(() => P4Util.addFile(r1client, 'test_add.txt', initialContent))
		.then(() => r1client.delete('test_delete.txt'))
		.then(() => P4Util.submit(r1client, '#robomerge ' + nullMergePerkin + ' ' + skipPootle))
	}

	async checkNumForwardedCommands(stream: string, expected: number) {
		const change = (await this.getClient(stream).changes(1))![0]
		const description = change.description
		
		let numTags = 0

		const tag = `#ROBOMERGE[${this.botName}]`
		for (const line of description.split('\n')) {
			if (line.startsWith(tag)) {
				++numTags
			}
		}
		if (numTags !== expected) {
			throw new Error(`Unexpected number of ${tag} tags, expected: ${expected}, got ${numTags}`)
		}
	}

	verify() {
		return Promise.all([
			this.checkNumForwardedCommands('Release-2.0', 1),
			this.checkNumForwardedCommands('Main', 1),
			this.checkNumForwardedCommands('Dev-Perkin', 0),

			this.checkHeadRevision('Main', 'test.txt', 3),
			this.checkHeadRevision('Release-2.0', 'test.txt', 3),
			this.checkHeadRevision('Dev-Perkin', 'test.txt', 3),
			this.checkHeadRevision('Dev-Pootle', 'test.txt', 1),
			this.getClient('Dev-Perkin').print('test.txt').then(contents => {
				if (contents !== contentAfterFirstEdit) {
					throw new Error('null merge not so null: ' + contents)
				}
			}),

			this.checkHeadRevision('Main', 'test_add.txt', 1),
			this.checkHeadRevision('Release-2.0', 'test_add.txt', 1),
			this.checkHeadRevision('Dev-Perkin', 'test_add.txt', 1),
			this.checkHeadRevision('Dev-Pootle', 'test_add.txt', 0),
			this.getClient('Dev-Perkin').print('test_add.txt').then(contents => {
				if (contents !== '') {
					throw new Error('null merge of add not so null: ' + contents)
				}
			}),

			this.checkHeadRevision('Main', 'test_delete.txt', 2),
			this.checkHeadRevision('Release-2.0', 'test_delete.txt', 2),
			this.checkHeadRevision('Dev-Perkin', 'test_delete.txt', 2),
			this.checkHeadRevision('Dev-Pootle', 'test_delete.txt', 1),
			this.getClient('Dev-Perkin').print('test_delete.txt').then(contents => {
				if (contents !== initialContent) {
					throw new Error('null merge of delete not so null: ' + contents)
				}
			})
		])
	}
}
