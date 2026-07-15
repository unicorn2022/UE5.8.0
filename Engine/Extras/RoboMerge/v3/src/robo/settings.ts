// Copyright Epic Games, Inc. All Rights Reserved.

import * as fs from 'fs';
import { ContextualLogger } from '../common/logger';
import { PerforceContext, Workspace } from '../common/perforce';
import { VersionReader } from '../common/version';
import { isA_MergeConflictAdditionalInformation, MergeConflictAdditionalInformation } from './branch-interfaces';
import { postToRobomergeAlerts } from './notifications';
import { migrateMessagesToDB } from './persistedmessages';
import { RoboArgs } from './roboargs';
import semver = require('semver');

const BotModel = require('./models/bot');

/**
 * Descriptions of major versions in settings layout
 * Original / Unversioned / 0.0.0 - The original Robomerge settings, completely unversioned. Pre-Nov 2018
 * 1.0.0 - Slack message storage overhaul Nov 2018
 * 2.2.2 - No change to settings structure -- Going to match Robomerge versions from now on -- May 2019
 * 3.0.0 - Branchbot.ts is split into NodeBot and EdgeBot classes, adding edges settings.
 *       - Reworked pause object into seperate 'manual pause' and 'blockage' objects, need to migrate old data - Sept. '19
 * 3.0.2 - Fix for pause state serialisation.
 * 3.1.0 - Remove backwards compatibility before 3.0.2
 */
const VERSION = VersionReader.getPackageVersion()

export function settingsInit() {
}
	
interface SaveObjectOpts {
	name?: string
	ensureInP4?: boolean
}

const jsonlint: any = require('jsonlint-mod')

function readFileToString(filename: string) {
	try {
		return fs.readFileSync(filename, 'utf8');
	}
	catch (e) {
		return null;
	}
}

export class Context {
	readonly path: string[];
	protected object: { [key: string]: any };

	constructor(settings: Settings, topLevelObjectName: string);
	constructor(settings: Settings, pathToElement: string[]);
	constructor(private settings: Settings, pathToElement: string | string[]) {
		if (typeof(pathToElement) === "string") {
			this.path = [ pathToElement ]
		} else {
			this.path = pathToElement
		}
		
		const settingsObject: any = settings.object;

		// Start on highest level and proceed down the tree
		this.object = settingsObject
		for (let index = 0; index < this.path.length; index++) {
			const elementName = this.path[index]
			if (this.object[elementName] === undefined) {
				this.object[elementName] = {};
			} else if (typeof(this.object[elementName]) !== "object") {
				throw new Error(`Error finding object element for ${this.path.slice(0, index).join('->')}`);
			}
			
			this.object = this.object[elementName]
		}
	}

	getInt(name: string, dflt?: number) {
		let val = parseInt(this.get(name));
		return isNaN(val) ? (dflt === undefined ? 0 : dflt) : val;
	}
	get(name: string) {
		return this.object[name];
	}
	getSubContext(name: string | string[]): Context {
		const path = typeof(name) === "string" ? [...this.path, name] : [...this.path, ...name]
		return new Context(this.settings, path)
	}
	set(name: string, value: any) {
		this.object[name] = value;
		this.settings._saveObject({name: this.path[0]});
	}
}

type FilePurpose = 
	'PERSISTENCE' |
	'BACKUP' |
	'TEMP'

const FilePurposeExtensionMap = {
	'PERSISTENCE' : '.json',
	'BACKUP' : '.bak',
	'TEMP' : '.save'
}

function getPersistenceFilepath(botname: string) {
	return RoboArgs.args.persistenceDir + `/${botname}.settings`
}

type SavingState = 'Saving' | 'NeedsSave'

export class Settings {
	botname: string
	enableSave: boolean
	object: { [key: string]: any }
	private savingState: Map<string, SavingState> = new Map()
	private	lastPersistBackupTime: Date
	private readonly settingsLogger: ContextualLogger
	private readonly p4backupWorkspace?: Workspace

	private constructor(botname: string, parentLogger: ContextualLogger, private p4: PerforceContext) {
		this.settingsLogger = parentLogger.createChild('Settings')

		this.lastPersistBackupTime = new Date(Date.now())
		this.botname = botname.toLowerCase() 
		if (RoboArgs.args.persistenceBackupFrequency > 0) {
			this.p4backupWorkspace = {directory: fs.realpathSync(RoboArgs.args.persistenceDir), name: RoboArgs.args.persistenceBackupWorkspace}
		}

		// see if we should enable saves
		this.enableSave = !process.env["NOSAVE"];
		if (!this.enableSave) {
			this.settingsLogger.info("Saving config has been disabled by NOSAVE environment variable");
		}
	}

	static async CreateAsync(botname: string, parentLogger: ContextualLogger, p4: PerforceContext) {

		const settings = new Settings(botname, parentLogger, p4)

		if (RoboArgs.useMongo) {
			// first try to get the bot data from the DB
			const botRecords = await BotModel.find({botname: botname})
			settings.settingsLogger.info(`Loaded ${botRecords.length} node records`)
			if (botRecords.length > 1) {
				// starting with 3.3.0 the individual node records themselves are being persisted/versioned, 
				// not the top level object itself, so we can set the object to that version to indicate there
				// should only be versioning on the individual nodes
				settings.object = {version: '3.3.0'} 
				
				for (const botRecord of botRecords) {
					settings.object[botRecord.nodename] = botRecord.details
				}
			}			
			else if (botRecords.length == 1) {
				const objVersion = semver.coerce(String(botRecords[0].details.version))!
				if (semver.lt(objVersion, '3.3.0')) {
					settings.settingsLogger.info(`Migrated from per bot to per node records`)
					settings.object = botRecords[0].details
					BotModel.deleteOne({_id: botRecords[0]._id})
				}
				else {
					settings.object[botRecords[0].nodename] = botRecords[0].details
				}
			}
		}

		let objVersion : semver.SemVer
		let loadedFromFile = false
		if (!settings.object)
		{
			// load the object from disk
			let filebits = readFileToString(settings.getFilename('PERSISTENCE'));
			if (filebits) {
				settings.object = jsonlint.parse(filebits);
				loadedFromFile = true
			}
			else {
				// Create "empty" settings object, but include latest version so we don't needless enter migration code
				settings.object = {
					version: VERSION.raw
				};
				objVersion = VERSION
			}
		}

		// Originally we did not version configuration.
		// If we have no version, assume it needs all migrations
		if (!settings.object.version) {
			settings.settingsLogger.warn("No version found in settings data.")
			objVersion = new semver.SemVer('0.0.0')
		} 
		// Ensure we have a semantic version 
		else {
			const fileSemVer = semver.coerce(String(settings.object.version))
			if (fileSemVer) {
				objVersion = fileSemVer
			} else {
				throw new Error(`Found version field in settings file, but it does not appear to be a semantic version: "${settings.object.version}"`)
			}
		}

		if (semver.lt(objVersion, '3.0.2')) {
			throw new Error(`Settings files prior to 3.0.2 are unsupported`)
		}

		// Clean up oversized describe records bloating files
		if (semver.lt(objVersion, '3.3.0')) {
			settings._shrink(true)
		}

		/**
		 * MIGRATION CODE SECTION START
		 */
		// For comparison later
		const previousObjVersion = new semver.SemVer(objVersion)

		// Transition slack messages out of the settings object and into the database
		if (loadedFromFile && RoboArgs.useMongo) {
			settings.settingsLogger.info("Migrated settings from json to MongoDB")
			await migrateMessagesToDB(settings)
		}

		// We're up to date!
		if (previousObjVersion.compare(VERSION) !== 0) {
			settings.settingsLogger.info(`Changing settings version from '${previousObjVersion.raw}' to '${VERSION.raw}'`)
		}

		/**
		 * MIGRATION CODE SECTION FINISH
		 */
		
		settings.object.version = VERSION.raw
		settings._saveObject({ensureInP4: true});

		return settings
	}

	getContext(name: string) {
		return new Context(this, name);
	}

	private getFilename(purpose: FilePurpose) {
		return getPersistenceFilepath(this.botname) + FilePurposeExtensionMap[purpose]
	}

	_saveObject(opts?: SaveObjectOpts) {
		if (this.enableSave) {
			if (RoboArgs.useMongo) {
				if (opts?.name) {
					const saveToDB = (saveObject: any) => {
						this.savingState.set(opts.name!, 'Saving')
						saveObject.version = this.object.version
						BotModel.collection.updateOne({botname:this.botname,nodename:opts.name},{$set:{botname:this.botname,nodename:opts.name,details:saveObject}},{upsert:true})
						.then(() => {
							if (this.savingState.get(opts.name!) == 'NeedsSave') {
								saveToDB(this.object[opts.name!])
							}
							else {
								this.savingState.delete(opts.name!)
							}
						})
						.catch(() => {
							if (this.savingState.get(opts.name!) === 'NeedsSave') {
								this.settingsLogger.warn(`Unable to save ${this.botname}:${opts.name}. Attempting resave before applying mitigation as settings have changed.`)
								saveToDB(this.object[opts.name!])
							}
							else {
								// Deep-clone the live node data so _shrinkNodeObject does NOT
								// modify the in-memory state — only the persisted copy is trimmed.
								// Compare against saveObject size: if shrinking produces the same size as what we just tried, there is no point retrying.
								const shrunkCopy = JSON.parse(JSON.stringify(this.object[opts.name!]))
								const size = Buffer.byteLength(JSON.stringify(saveObject))
								this._shrinkNodeObject(shrunkCopy)
								const shrunkSize = Buffer.byteLength(JSON.stringify(shrunkCopy))
								if (shrunkSize < size) {
									this.settingsLogger.warn(`Unable to save ${this.botname}:${opts.name}. Size: ${size}. Attempting resave with shrunk size ${shrunkSize}.`)
									saveToDB(shrunkCopy)
								}
								else {
									const errorMsg = `Unable to save ${this.botname}:${opts.name}. Size: ${size}.`
									this.settingsLogger.error(errorMsg)
									postToRobomergeAlerts(errorMsg)
									this.savingState.delete(opts.name!)
								}
							}
						})
					}
					if (!this.savingState.has(opts.name)) {
						saveToDB(this.object[opts.name!])
					}
					else {
						this.savingState.set(opts.name, 'NeedsSave')
					}
				}
				else {
					Object.keys(this.object).forEach(key => {
						if (key !== 'version') {
							this._saveObject({name: key})
						}
					})
				}
			}
			if (RoboArgs.persistSettingsToFile) {
				const persistentFile = this.getFilename('PERSISTENCE')
				const tempFile = this.getFilename('TEMP')

				if (fs.existsSync(persistentFile)) {
					fs.copyFileSync(persistentFile,this.getFilename('BACKUP'))
				}

				let filebits = JSON.stringify(this.object, null, '  ')
				fs.writeFileSync(tempFile, filebits, "utf8")

				fs.copyFileSync(tempFile, persistentFile)

				if (this.p4backupWorkspace) {
					if (opts?.ensureInP4) {
						this.p4.fstat(this.p4backupWorkspace, persistentFile)
						.then((fstat) => {
							if (fstat.length == 0 || fstat[0].headAction == 'delete') {
								this.p4.new_cl(this.p4backupWorkspace!, "Adding persistent backup", [])
								.then((cl: number) => {
									this.p4.add(this.p4backupWorkspace!, cl, persistentFile, "text+wS64")
									.then(() => this.p4.submit(this.p4backupWorkspace!, cl))
								})
							}
						})
					}
					else if ((Date.now() - this.lastPersistBackupTime.getTime()) / 60000 >= RoboArgs.args.persistenceBackupFrequency) {
						this.lastPersistBackupTime = new Date(Date.now())

						this.p4.revertFile(this.p4backupWorkspace, persistentFile, ['-k'])
						.then(() => this.p4.sync(this.p4backupWorkspace, persistentFile, {opts: ['-k']}))
						.then(() => this.p4.new_cl(this.p4backupWorkspace!, "Updating persistent backup"))
						.then((cl: number) => {
							this.p4.edit(this.p4backupWorkspace!, cl, persistentFile, ['-k'])
							.then(() => this.p4.submit(this.p4backupWorkspace!, cl))
						})
						
					}
				}
			}
		}
	}

	_shrink(trim?: boolean) {
			Object.keys(this.object).forEach(key => {
				if (key !== 'version') {
					this._shrinkNode(key, trim)
				}
			})
	}

	/** Shrink a node object in place. Used for migration and as the implementation
	 *  base for the deep-clone save path. Operates on any plain object — callers
	 *  are responsible for passing either the live node or a disposable clone. */
	_shrinkNodeObject(nodeObj: any, trim?: boolean) {
		// Clear describeResult from edge pause states (large file list)
		const edges = nodeObj?.edges || {}
		for (const key of Object.keys(edges)) {
			const edge = edges[key]
			const additionalInfo: MergeConflictAdditionalInformation = edge.pause?.blockage?.additionalInfo
			if (isA_MergeConflictAdditionalInformation(additionalInfo)) {
				if (additionalInfo?.describeResult) {
					if (!trim || additionalInfo.describeResult.entries.length > RoboArgs.maxStompFileCount) {
						additionalInfo.limitedInfoMode = true
						additionalInfo.describeResult = undefined
					}
				}
			}
		}
		// Clear detailsSnapshot from persisted conflicts (can be large for big CLs)
		for (const conflict of (nodeObj?.conflicts ?? [])) {
			delete conflict.detailsSnapshot
			delete conflict.detailsSnapshotTruncated
		}
	}

	/** Shrink the live in-memory node object. Used for migration paths that
	 *  intentionally want to permanently discard large fields. The MongoDB
	 *  error-recovery path should use the deep-clone approach instead to
	 *  preserve in-memory data. */
	_shrinkNode(nodeName: string, trim?: boolean) {
		this._shrinkNodeObject(this.object[nodeName], trim)
	}
}

