// Copyright Epic Games, Inc. All Rights Reserved.

import { AlreadyIntegrated, Blockage, ChangeInfo, ForcedCl, PauseEvent, PendingChange } from './branch-interfaces';
import { BeginIntegratingToGateEvent, EndIntegratingToGateEvent, NagBlockageEvent } from './branch-interfaces';
import { BotConfig } from './branchdefs';
import { PersistentConflict } from './conflict-interfaces';

type OnChangeParsed = (arg: ChangeInfo) => void
type OnBlockage = (arg: Blockage, isNew: boolean) => void
type OnBlockageAcknowledged = (arg: PersistentConflict) => void
type OnNagBlockage = (arg: NagBlockageEvent) => void
type OnCommit = (arg: PendingChange) => void
type OnAlreadyIntegrated = (arg: AlreadyIntegrated) => void
type OnForcedLastCl = (arg: ForcedCl) => void
type OnNonSkipLastClChange = (arg: ForcedCl) => void
type OnPause = (arg: PauseEvent) => void
type OnUnpause = (arg: PauseEvent) => void
type OnBranchUnblocked = (arg: PersistentConflict) => void
type OnConflictStatus = (arg: boolean) => void
type OnBeginIntegratingToGate = (arg: BeginIntegratingToGateEvent) => void
type OnEndIntegratingToGate = (arg: EndIntegratingToGateEvent) => void

export interface BlockageStatusChangedEvent {
	previousConflict: PersistentConflict
	newBlockage: Blockage
}
type OnBlockageStatusChanged = (arg: BlockageStatusChangedEvent) => void

export interface BotEventHandler {
	onChangeParsed?: OnChangeParsed
	onBlockage?: OnBlockage
	onBlockageAcknowledged?: OnBlockageAcknowledged
	onCommit?: OnCommit
	onAlreadyIntegrated?: OnAlreadyIntegrated
	onForcedLastCl?: OnForcedLastCl
	onNonSkipLastClChange?: OnNonSkipLastClChange
	onPause?: OnPause
	onUnpause?: OnUnpause
	onBranchUnblocked?: OnBranchUnblocked
	onConflictStatus?: OnConflictStatus

	onBeginIntegratingToGate?: OnBeginIntegratingToGate
	onEndIntegratingToGate?: OnEndIntegratingToGate
	onBlockageStatusChanged?: OnBlockageStatusChanged
}

/** Bindings for bot events */
export class BotEvents {
	constructor(public botname: string, public botConfig: BotConfig) {
	}

	// Fired for every change parsed with no syntax errors
	onChangeParsed(listener: OnChangeParsed) {
		this.changeParsedListeners.push(listener)
	}

	// Fired when a conflict or syntax error has occurred
	onBlockage(listener: OnBlockage) {
		this.blockageListeners.push(listener)
	}

	// Fired when a user has acknowledged (or unacknowledged) a blockage
	onBlockageAcknowledged(listener: OnBlockageAcknowledged) {
		this.blockageAcknowledgedListeners.push(listener)
	}

	onNagBlockage(listener: OnNagBlockage) {
		this.nagBlockageListeners.push(listener)
	}

	// Fired when a merge has been committed
	onCommit(listener: OnCommit) {
		this.commitListeners.push(listener)
	}

	// Fired when an integration is found not to be necessary
	onAlreadyIntegrated(listener: OnAlreadyIntegrated) {
		this.alreadyIntegratedListeners.push(listener)
	}

	// Fired when the botʼs last processed changelist has been manually changed
	onForcedLastCl(listener: OnForcedLastCl) {
		this.forcedLastClListeners.push(listener)
	}

	// Fired in addition to forceLastCl for those changes that don't just skip a conflict
	onNonSkipLastClChange(listener: OnNonSkipLastClChange) {
		this.nonSkipLastClChangeListeners.push(listener)
	}

	onPause(listener: OnPause) {
		this.pauseListeners.push(listener)
	}

	onUnpause(listener: OnUnpause) {
		this.unpauseListeners.push(listener)
	}

	// Fired when a previously blocked bot has been unblocked
	onBranchUnblocked(listener: OnBranchUnblocked) {
		this.branchUnblockedListeners.push(listener)
	}

	// Fired if a bot becomes conflict free or loses that status
	onConflictStatus(listener: OnConflictStatus) {
		this.conflictStatusListeners.push(listener)
	}

	onBeginIntegratingToGate(listener: OnBeginIntegratingToGate) {
		this.beginIntegratingToGateListeners.push(listener)
	}

	onEndIntegratingToGate(listener: OnEndIntegratingToGate) {
		this.endIntegratingToGateListeners.push(listener)
	}

	onBlockageStatusChanged(listener: OnBlockageStatusChanged) {
		this.blockageStatusChangedListeners.push(listener)
	}

	// register an object to handle some or all of the above
	registerHandler(handler: BotEventHandler) {
		const proto = handler.constructor.prototype
		for (const propName of Object.getOwnPropertyNames(proto)) {
			const prop = (proto as any)[propName]
			switch (propName) {
				case 'onChangeParsed': this.changeParsedListeners.push(arg => prop.call(handler, arg)); break
				case 'onBlockage': this.blockageListeners.push((arg, isNew) => prop.call(handler, arg, isNew)); break
				case 'onBlockageAcknowledged': this.blockageAcknowledgedListeners.push((arg) => prop.call(handler, arg)); break
				case 'onNagBlockage': this.nagBlockageListeners.push((arg) => prop.call(handler, arg)); break
				case 'onCommit': this.commitListeners.push(arg => prop.call(handler, arg)); break
				case 'onAlreadyIntegrated': this.alreadyIntegratedListeners.push(arg => prop.call(handler, arg)); break
				case 'onForcedLastCl': this.forcedLastClListeners.push(arg => prop.call(handler, arg)); break
				case 'onNonSkipLastClChange': this.nonSkipLastClChangeListeners.push(arg => prop.call(handler, arg)); break
				case 'onPause': this.pauseListeners.push(arg => prop.call(handler, arg)); break
				case 'onUnpause': this.unpauseListeners.push(arg => prop.call(handler, arg)); break
				case 'onBranchUnblocked': this.branchUnblockedListeners.push(arg => prop.call(handler, arg)); break
				case 'onConflictStatus': this.conflictStatusListeners.push(arg => prop.call(handler, arg)); break
				case 'onBeginIntegratingToGate': this.beginIntegratingToGateListeners.push(arg => prop.call(handler, arg)); break
				case 'onEndIntegratingToGate': this.endIntegratingToGateListeners.push(arg => prop.call(handler, arg)); break
			case 'onBlockageStatusChanged': this.blockageStatusChangedListeners.push(arg => prop.call(handler, arg)); break
			}
		}
	}

	protected readonly changeParsedListeners: OnChangeParsed[] = []
	protected readonly blockageListeners: OnBlockage[] = []
	protected readonly blockageAcknowledgedListeners: OnBlockageAcknowledged[] = []
	protected readonly nagBlockageListeners: OnNagBlockage[] = []
	protected readonly commitListeners: OnCommit[] = []
	protected readonly alreadyIntegratedListeners: OnAlreadyIntegrated[] = []
	protected readonly forcedLastClListeners: OnForcedLastCl[] = []
	protected readonly nonSkipLastClChangeListeners: OnNonSkipLastClChange[] = []
	protected readonly pauseListeners: OnPause[] = []
	protected readonly unpauseListeners: OnUnpause[] = []
	protected readonly branchUnblockedListeners: OnBranchUnblocked[] = []
	protected readonly conflictStatusListeners: OnConflictStatus[] = []
	protected readonly beginIntegratingToGateListeners: OnBeginIntegratingToGate[] = []
	protected readonly endIntegratingToGateListeners: OnEndIntegratingToGate[] = []
	protected readonly blockageStatusChangedListeners: OnBlockageStatusChanged[] = []
}

/** API for firing bot events */
export class BotEventTriggers extends BotEvents {
	reportChangeParsed(arg: ChangeInfo) {
		for (const listener of this.changeParsedListeners) {
			listener(arg)
		}
	}

	reportBlockage(arg: Blockage, isNew: boolean) {
		for (const listener of this.blockageListeners) {
			listener(arg, isNew)
		}
	}

	reportBlockageAcknowledged(arg: PersistentConflict) {
		for (const listener of this.blockageAcknowledgedListeners) {
			listener(arg)
		}
	}

	nagBlockage(arg: NagBlockageEvent) {
		for (const listener of this.nagBlockageListeners) {
			listener(arg)
		}
	}

	reportCommit(arg: PendingChange) {
		for (const listener of this.commitListeners) {
			listener(arg)
		}
	}

	reportAlreadyIntegrated(arg: AlreadyIntegrated) {
		for (const listener of this.alreadyIntegratedListeners) {
			listener(arg)
		}
	}

	reportForcedLastCl(arg: ForcedCl) {
		for (const listener of this.forcedLastClListeners) {
			listener(arg)
		}
	}

	reportNonSkipLastClChange(arg: ForcedCl) {
		for (const listener of this.nonSkipLastClChangeListeners) {
			listener(arg)
		}
	}

	reportPause(arg: PauseEvent) {
		for (const listener of this.pauseListeners) {
			listener(arg)
		}
	}

	reportUnpause(arg: PauseEvent) {
		for (const listener of this.unpauseListeners) {
			listener(arg)
		}
	}

	reportBranchUnblocked(arg: PersistentConflict) {
		for (const listener of this.branchUnblockedListeners) {
			listener(arg)
		}
	}

	reportConflictStatus(arg: boolean) {
		for (const listener of this.conflictStatusListeners) {
			listener(arg)
		}
	}

	reportBeginIntegratingToGate(arg: BeginIntegratingToGateEvent) {
		for (const listener of this.beginIntegratingToGateListeners) {
			listener(arg)
		}
	}

	reportEndIntegratingToGate(arg: EndIntegratingToGateEvent) {
		for (const listener of this.endIntegratingToGateListeners) {
			listener(arg)
		}
	}

	reportBlockageStatusChanged(arg: BlockageStatusChangedEvent) {
		for (const listener of this.blockageStatusChangedListeners) {
			listener(arg)
		}
	}
}
