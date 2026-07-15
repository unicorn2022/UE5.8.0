// Copyright Epic Games, Inc. All Rights Reserved.

import { Args } from '../common/args';
import { Vault } from './vault';

export class RoboArgs {

	private static _args: Args
	private static _vault: Vault

	static Init(args: Args) {
		this._args = args
		this._vault = new Vault(args.vault)
	}

	static get args() {
		return this._args
	}

	static get vault() {
		return this._vault
	}

	static get exclusiveLockOpenedsToRun() {
		return this._args.exclusiveLockOpenedsToRun
	}

	static get externalUrl() {
		return this._args.externalUrl
	}

	static get isRunningFunctionalTests() {
		return this._args.runningFunctionalTests
	}

	static get maxStompFileCount() {
		return this._args.maxStompFileCount
	}

	static get maxReconsiderFiles() {
		return this._args.maxReconsiderFiles
	}

	static get slackAlertChannel() {
		return this._args.slackAlertChannel
	}

	static get useMongo() {
		return this._args.mongoDB_URI.length > 0
	}

	static get persistSettingsToFile() {
		return !this.useMongo || this._args.alwaysPersistToFile
	}

	static get hasSocketMode() {
		return !!this._vault.slackAppToken
	}
}