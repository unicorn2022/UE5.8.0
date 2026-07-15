// Copyright Epic Games, Inc. All Rights Reserved.

import { ContextualLogger } from 'common/logger'
import { PerforceContext } from '../../common/perforce'
import { ChangeInfo, MergeAction } from '../branch-interfaces'
import { EdgeOptions, EdgeProperties, NodeOptions } from '../branchdefs'
import { ChangelistFilter, ChangelistFilterResult, changelistFilters } from '../targets'

const GROUP_MEMBER_TYPES = ['owners', 'users', 'any'] as const
type GroupMemberType = typeof GROUP_MEMBER_TYPES[number]

function toGroupMemberType(value: string): GroupMemberType {
  if ((GROUP_MEMBER_TYPES as readonly string[]).includes(value)) {
    return value as GroupMemberType;
  }
  throw new Error(`Invalid group member type: ${value}`);
}

export class PerforceGroupFilter extends ChangelistFilter {

	static {
		changelistFilters.set("perforcegroup", PerforceGroupFilter)
	}

	static async shouldFilter(info: ChangeInfo, action: MergeAction, edgeProperties: EdgeOptions, logger: ContextualLogger) {

		const p4 = PerforceContext.getServerContext(logger, action.branch.config.streamServer)

        const authorizedUser = info.userRequest === 'edge-reconsider' ? info.owner || info.author : info.author
        const memberType: GroupMemberType = toGroupMemberType(edgeProperties.changelistFilter!.parameters[1])

        let result: ChangelistFilterResult = {result: 'allow'}

        let groups = [p4.getGroup(edgeProperties.changelistFilter!.parameters[0])]
        for (const groupPromise of groups) {
            const group = (await groupPromise)?.[0]
            if (group && group.entries) {
                switch(memberType) {
                    case 'owners':
                        if (group.entries.some((entry: any) => entry.Owners === authorizedUser)) {
                            return result
                        }
                        break
                    case 'users':
                        if (group.entries.some((entry: any) => entry.Users === authorizedUser)) {
                            return result
                        }
                        break
                    case 'any':
                        if (group.entries.some((entry: any) => entry.Owners === authorizedUser || entry.Users === authorizedUser)) {
                            return result
                        }
                        break
                }
                group.entries.forEach((entry: any) => { if (entry.Subgroups) { groups.push(p4.getGroup(entry.Subgroups)) }})
            }
        }

        const getMemberType = function() {
            switch(memberType) {
                case 'owners':
                    return 'an owner of'
                case 'users':
                    return 'a user of'
                case 'any':
                    return 'in'
            }
        }

        if (edgeProperties.changelistFilter?.mode === 'AllowOrBlock') {
            result.result = 'block'
            result.message = `${authorizedUser} not ${getMemberType()} group ${edgeProperties.changelistFilter!.parameters[0]}`
        }
        else {
            result.result = 'skip'
        }
		return result
	}

    static async validate(branches: NodeOptions[], edge: EdgeProperties, isPreviewing: boolean, logger: ContextualLogger): Promise<[string[],string[]]> {

        const errors: string[] = []
        const warnings: string[] = []

        const warnWhenPreviewing = (message: string) => {
            if (isPreviewing) {
                warnings.push(message)
            }
            else {
                errors.push(message)
            }
        }

        if (edge.changelistFilter!.parameters.length != 2) {
            errors.push("Expected exactly 2 parameters: groupName, groupMemberType")
        }

        if (edge.changelistFilter!.parameters.length > 1) {
            try {
                toGroupMemberType(edge.changelistFilter!.parameters[1])
            } 
            catch {
                errors.push("Parameter 2 is expected to be one of 'owners', 'users', or 'any'")
            }
        }

        if (edge.changelistFilter!.parameters.length > 0) {
            const edgeToUpper = edge.to.toUpperCase()
            const toServer = branches.filter(branch => branch.name?.toUpperCase() === edgeToUpper)[0].streamServer
            const p4 = PerforceContext.getServerContext(logger, toServer)
            
            if (!(await p4.getGroup(edge.changelistFilter!.parameters[0]))) {
                warnWhenPreviewing(`Group ${edge.changelistFilter!.parameters[0]} does not exist.`)
            }
        }

        return [errors, warnings]
    }

}