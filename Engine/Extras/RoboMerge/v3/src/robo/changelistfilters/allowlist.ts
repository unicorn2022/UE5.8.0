// Copyright Epic Games, Inc. All Rights Reserved.

import { ContextualLogger } from 'common/logger'
import { PerforceContext } from '../../common/perforce'
import { ChangeInfo, MergeAction } from '../branch-interfaces'
import { EdgeOptions, EdgeProperties, NodeOptions } from '../branchdefs'
import { ChangelistFilter, ChangelistFilterResult, changelistFilters } from '../targets'
const ini = require('ini')

export class AllowListFilter extends ChangelistFilter {

	static {
		changelistFilters.set("allowlist", AllowListFilter)
	}

	static async shouldFilter(info: ChangeInfo, action: MergeAction, edgeProperties: EdgeOptions, logger: ContextualLogger) {

        const authorizedUser = info.userRequest === 'edge-reconsider' ? info.owner || info.author : info.author
        const authorizedUserLower = authorizedUser.toLowerCase()
		const p4 = PerforceContext.getServerContext(logger, action.branch.config.streamServer)
        const configFile = await p4.print(edgeProperties.changelistFilter!.parameters[0])
        const config = ini.parse(configFile);
        let result: ChangelistFilterResult = {result: 'allow'}

        if (!config[edgeProperties.changelistFilter!.parameters[1]].allowlist?.split(',').some((user: string) => user.trim().toLowerCase() === authorizedUserLower))
        {
            if (edgeProperties.changelistFilter?.mode === 'AllowOrBlock') {
                result.result = 'block'
                result.message = `${authorizedUser} not in allowlist of [${edgeProperties.changelistFilter!.parameters[1]}]`
            }
            else {
                result.result = 'skip'
            }
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
            errors.push("Expected exactly 2 parameters: perforcePath, sectionName")
        }

        const edgeToUpper = edge.to.toUpperCase()
        const toServer = branches.filter(branch => branch.name?.toUpperCase() === edgeToUpper)[0].streamServer
        const p4 = PerforceContext.getServerContext(logger, toServer)
        
        if (edge.changelistFilter!.parameters.length > 0) {            
            const configFile = await p4.print(edge.changelistFilter!.parameters[0])
            if (configFile.length > 0) {
                try {
                    const config = ini.parse(configFile);
                    if (edge.changelistFilter!.parameters.length > 1) {
                        const section = config[edge.changelistFilter!.parameters[1]]
                        if (section) {
                            if (!section.allowlist) {
                                warnings.push(`[${edge.changelistFilter!.parameters[1]}] does not have allowlist in ${edge.changelistFilter!.parameters[0]}`)
                            }
                        }
                        else {
                            warnWhenPreviewing(`[${edge.changelistFilter!.parameters[1]}] not found in ${edge.changelistFilter!.parameters[0]}`)
                        }
                    }
                }
                catch {
                    errors.push(`Failed to parse ${edge.changelistFilter!.parameters[0]}`)
                }
            }
            else {
                warnWhenPreviewing(`${edge.changelistFilter!.parameters[0]} does not exist.`)
            }
        }

        return [errors, warnings]
    }

}