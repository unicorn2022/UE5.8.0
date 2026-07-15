// Copyright Epic Games, Inc. All Rights Reserved.
"use strict"

// displayMessage, displaySuccessfulMessage, displayErrorMessage, promptForAsync
// and showReconsiderDialog are provided by ui-helpers.js, loaded before this script.

function doReconsider(bot, node, cl, edgeName, targetLabel) {
	const title = targetLabel ? `Reconsider CL#${cl} \u2192 ${targetLabel}` : `Reconsider CL#${cl}`
	showReconsiderDialog(title, String(cl))
		.then(data => {
			if (!data) return
			const base = `/api/op/bot/${encodeURIComponent(bot)}/node/${encodeURIComponent(node)}`
			const opPath = edgeName
				? `${base}/edge/${encodeURIComponent(edgeName)}/op/reconsider`
				: `${base}/op/reconsider`
			$.post(`${opPath}?${new URLSearchParams({cl: data.cl}).toString()}`)
				.then(() => displaySuccessfulMessage(`Reconsider of CL#${cl} queued successfully`))
				.catch(err => displayErrorMessage(`Error reconsidering CL#${cl}: ${err.responseText || err.message}`))
		})
}

function formatReconsiderButton(cl, reconsiderInfo) {
	if (!reconsiderInfo || reconsiderInfo.length === 0) {
		return '<td></td>'
	}
	let html = '<td><div class="btn-group">'
	html += `<button type="button" class="btn btn-sm btn-outline-secondary dropdown-toggle" data-toggle="dropdown">Reconsider</button>`
	html += `<div class="dropdown-menu">`
	for (let i = 0; i < reconsiderInfo.length; i++) {
		const entry = reconsiderInfo[i]
		if (i > 0) {
			html += `<div class="dropdown-divider"></div>`
		}
		html += `<h6 class="dropdown-header">${entry.bot}</h6>`
		for (const target of entry.targets) {
			const edgeLower = target.edgeName.toLowerCase()
			const label = edgeLower === 'main' || edgeLower.endsWith('-main')
				? getStreamGraphName(target.stream)
				: target.edgeName
			html += `<a class="dropdown-item" href="#" onclick="doReconsider('${entry.bot}','${entry.node}',${cl},'${target.edgeName}','${label.replace(/'/g, "\\'")}');return false;">${label}</a>`
		}
	}
	html += `</div></div></td>`
	return html
}

function getMergeMethodStyle(mergeMethod) {
	switch(mergeMethod) {
		case "automerge":
			return {color: "black", style: "solid"}
		case "initialSubmit":
			console.log("Unexpected mergeMethod of initialSubmit")
			return {color: "pink", style: "solid"}
		case "merge_with_conflict":
			return {color: "red", style: "dashed"}
		case "manual_merge":
			return {color: "darkgray", style: "dashed"}
		case "populate":
			return {color: "blue", style: "dashed"}
		case "transfer":
			return {color: "orange", style: "solid"}
	}
	console.log("UNKNOWN CASE: " + mergeMethod)
	return {color: "pink", style: "solid"}
}

function getStreamGraphName(streamDisplayName) {
	if (streamDisplayName.endsWith("/Main")) {
		return streamDisplayName
	}
	const match = streamDisplayName.match(/\/\/[^\/]+\/([^\/]+).*/)
	return match ? match[1] : streamDisplayName
}

function generateChangeList(dataObj) {

	let html = '<div style="margin: auto; width: 80%;"><table class="table"><tbody>'
	// By default the keys are going to be numerically ordered, but we want changes to appear after their parent
	// So go through and build them in a list order we prefer
	const orderedChanges = Object.keys(dataObj.changes).map(k => parseInt(k))
	const placedChanges = new Set()
	for (let i = 0; i < orderedChanges.length; i++) {
		const sourceCL = dataObj.changes[orderedChanges[i].toString()].sourceCL
		if (sourceCL && !placedChanges.has(sourceCL)) {
			let insertPoint = undefined
			for (let j = i+1; j < orderedChanges.length; j++) {
				if (orderedChanges[j] == sourceCL) {
					insertPoint = j
				}
				else if (insertPoint) {
					// Keep the children with lower CL# of a given stream in numerical order
					if (orderedChanges[j] < orderedChanges[i]) {
						insertPoint = j
					} else {
						break
					}
				}
			}
			if (insertPoint) {
				const removedKey = orderedChanges.splice(i, 1)[0]
				orderedChanges.splice(insertPoint, 0, removedKey)
				i--
				continue
			}
		}
		placedChanges.add(orderedChanges[i])
	}
	for (const cl of orderedChanges) {
		const clKey = cl.toString()
		const changeDetails = dataObj.changes[clKey]
		html += '<tr valign="middle">'
		html += `<td><b>${changeDetails.streamDisplayName}</b></td>`
		if (changeDetails.swarmLink) {
			html += `<td><a href="${changeDetails.swarmLink}" target="_blank">CL#${cl}</a></td>`
		} else {
			html += `<td>CL#${cl}</td>`
		}
		html += formatReconsiderButton(cl, changeDetails.reconsiderInfo)
		html += '</tr>'
	}
	html += "</tbody></table></div>"

	return html
}

function createGraph(changes) {
	let lines = [
		'digraph robomerge {',
		'fontname="sans-serif"; labelloc=top; fontsize=16;',
		'edge [penwidth=2]; nodesep=.7; ranksep=1.2;',
		'node [shape=box, style=filled, fontname="sans-serif", fillcolor=moccasin];'
	]
	for (let change in changes) {
		const changeDetails = changes[change]
		let attrs = [
			['label', `<<b>${getStreamGraphName(changeDetails.streamDisplayName)}</b><br/>${change}>`],
			['tooltip', `"${changeDetails.swarmLink}"` || `"CL# ${change}"`],
			['target', `"_blank"`],
			['margin', `"0.5,0.1"`],
			['fontsize', 15],
		]
		if (changeDetails.swarmLink) {
			attrs.push(['URL', `"${changeDetails.swarmLink}"`])
		}
		const attrStrs = attrs.map(([key, value]) => `${key}=${value}`)
		lines.push(`_${change} [${attrStrs.join(', ')}];`);	
	}
	for (let change in changes) {
		if (changes[change].sourceCL in changes) {
			if (changes[change].mergeMethod == "automerge") {
				lines.push(`_${changes[change].sourceCL} -> _${change};`)
			} else {
				const mergeMethodStyle = getMergeMethodStyle(changes[change].mergeMethod)
				lines.push(`_${changes[change].sourceCL} -> _${change} [color=${mergeMethodStyle.color}, style=${mergeMethodStyle.style}];`)
			}
		}
	}
	lines.push('}')

    const graphContainer = $('<div>').css({textAlign: 'center', width: '100%', overflowX: 'auto'});
    const flowGraph = $('<div class="flow-graph">').css({display: 'inline-block', float: 'none', verticalAlign: 'top', minWidth: '400px'}).appendTo(graphContainer);
    flowGraph.append($('<div>').css('text-align', 'center').text("Building graph..."));
    renderGraph(lines.join('\n'))
        .then(svg => {
        $('#graph-key-template')
            .clone()
            .removeAttr('id')
            .css({display: 'inline-block', verticalAlign: 'top', marginLeft: '40px'})
            .appendTo(graphContainer);
        const span = $('<div style="margin: auto; display: flex; justify-content: center;">').html(svg);
        const svgEl = $('svg', span).addClass('branch-graph');
        const naturalHeight = parseInt(svgEl.attr('height'));
        svgEl.removeAttr('width');
        const height = Math.max(Math.round(naturalHeight * 0.7), 60);
        svgEl.attr('height', height + 'px').css('vertical-align', 'top');
        flowGraph.empty();
        flowGraph.append(span);
    });
    return graphContainer;	
}

function buildResults(data) {
	if (Object.keys(data.data.changes).length > 0) {
		const $successPanel = $('#success-panel');
		$('#changes', $successPanel).html(generateChangeList(data.data));
		$successPanel.show();
		$('#graph').append(createGraph(data.data.changes))
	} else {
		const $errorPanel = $('#error-panel');
		$('pre', $errorPanel).html('No results found.')
		if (data.data.swarmURL) {
			$('.swarmURL').html(`<a href="${data.data.swarmURL}/changes/${data.data.originalCL.cl}" target="_blank">CL# ${data.data.originalCL.cl}</a>`)
		}
		$errorPanel.show();
	}
	receivedTrackingResults()
}

function doit(query) {
	$.get(query)
	.then(data => buildResults(data))
	.catch(error => {
		const $errorPanel = $('#error-panel');
		const errorMsg = error.responseText
			? error.responseText.replace(/\t/g, '    ')
			: 'Internal error: ' + error.message;
		$('pre', $errorPanel).text(errorMsg);
		$errorPanel.show();
		receivedTrackingResults()
	});
}

function receivedTrackingResults(message) {
	$('#loadingDiv').fadeOut("fast", "swing", function() { 
		$('#changes').fadeIn("fast") 
		$('#graph').fadeIn("fast")
	})
}

window.doTrackChange = doit;
