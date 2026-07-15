// Copyright Epic Games, Inc. All Rights Reserved.
import { FailureKind } from "./status-types";

export enum Resolution {
	RESOLVED = 'resolved',
	SKIPPED = 'skipped',
	CANCELLED = 'cancelled',
	DUNNO = 'fixed(?)'
}

// made to be minimal/serialisable
export interface PersistentConflict {
	// upper case branch names
	blockedBranchName: string
	targetBranchName?: string
	targetStream?: string

	cl: number
	sourceCl: number
	author: string
	owner: string

	kind: FailureKind
	disableStompReason?: string
	time: Date

	nagCount: number
	lastNagTime?: Date

	ugsIssue: number

	resolution?: Resolution
	resolvingCl?: number
	resolvingAuthor?: string
	timeTakenToResolveSeconds?: number
	resolvingReason?: string

	// Set when the Acknowledge button is clicked
	acknowledger?: string
	acknowledgedAt?: Date

	// Compact fingerprint of failure details for circumstance-change detection on retry.
	// Content varies by kind — see buildDetailsSnapshot() in conflicts.ts.
	// May be absent after a service restart if the persisted copy was shrunk for BSON limits.
	detailsSnapshot?: string[]
	detailsSnapshotTruncated?: boolean   // true if list exceeded MAX_SNAPSHOT_ENTRIES
}

export function PersistentConflictToString(conflict: PersistentConflict) {
	return `${conflict.blockedBranchName}->${conflict.targetBranchName}(${conflict.kind}) @ CL#${conflict.cl} (sourceCL: ${conflict.sourceCl})`
}