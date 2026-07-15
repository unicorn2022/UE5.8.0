// Copyright Epic Games, Inc. All Rights Reserved.
// ui-helpers.js -- Shared UI utility functions (modals, alerts) used across multiple pages.
//                  Must be loaded before boilerplate.js and any page-specific scripts.

// Displays a Bootstrap modal prompting the user for a single text value.
// map:       { <key>: <promptText> | { prompt: <string>, default: <string> } }
// helpText:  optional HTML shown below the input
// modalSize: optional Bootstrap modal size class e.g. 'modal-lg'
// okLabel:   optional label for the confirm button (default 'OK')
// Returns a Promise resolving to { <key>: <value> } on confirm, or null on cancel.
function promptForAsync(map, helpText, modalSize = '', okLabel = 'OK') {
	return new Promise((resolve, reject) => {
		const keys = Object.keys(map);
		if (keys.length !== 1) {
			reject(new Error('promptForAsync only supports single-field prompts'));
			return;
		}

		const key = keys[0];
		const p = map[key];
		let promptText = key;
		let defaultValue = "";

		if (typeof(p) === "object") {
			defaultValue = p.default || "";
			promptText = p.prompt || key;
		} else {
			promptText = p;
		}

		const modalId = 'promptModal' + Date.now();
		const modalHtml = `
			<div class="modal fade" id="${modalId}" tabindex="-1" role="dialog" data-backdrop="static" data-keyboard="false">
				<div class="modal-dialog ${modalSize}" role="document">
					<div class="modal-content">
						<div class="modal-header">
							<h5 class="modal-title">${promptText}</h5>
							<button type="button" class="close" data-dismiss="modal" aria-label="Close">
								<span aria-hidden="true">&times;</span>
							</button>
						</div>
						<div class="modal-body">
							<input type="text" class="form-control" id="${modalId}_input" value="${defaultValue}">
							${helpText ? `<small class="form-text text-muted" style="white-space: pre-line; margin-top: 10px; font-family: monospace;">${helpText}</small>` : ''}
						</div>
						<div class="modal-footer">
							<button type="button" class="btn btn-secondary" id="${modalId}_cancel">Cancel</button>
							<button type="button" class="btn btn-primary" id="${modalId}_ok">${okLabel}</button>
						</div>
					</div>
				</div>
			</div>
		`;

		$('body').append(modalHtml);
		const $modal = $('#' + modalId);
		const $input = $('#' + modalId + '_input');

		$modal.modal('show');

		$modal.on('shown.bs.modal', function() {
			$input.focus().select();
		});

		$('#' + modalId + '_ok').on('click', function() {
			const value = $input.val();
			const result = {};
			result[key] = value;
			$modal.modal('hide');
			resolve(result);
		});

		$('#' + modalId + '_cancel').on('click', function() {
			$modal.modal('hide');
			resolve(null);
		});

		$input.on('keypress', function(e) {
			if (e.which === 13) {
				e.preventDefault();
				$('#' + modalId + '_ok').click();
			}
		});

		$modal.on('keydown', function(e) {
			if (e.which === 27) {
				$('#' + modalId + '_cancel').click();
			}
		});

		$modal.on('hidden.bs.modal', function() {
			$modal.remove();
		});
	});
}

// Opens a Bootstrap modal prompting the user to reconsider a CL.
// promptText: title shown in the modal header
// defaultCL:  pre-populated value for the CL input (pass '' for blank)
// okLabel:    optional label for the confirm button (default 'Reconsider')
// Returns a Promise resolving to the entered CL string, or null on cancel.
const RECONSIDER_HELP_TEXT =
	'<div style="display:flex">Usage:&nbsp;<span>&lt;cl&gt; [#stomp] [#robomerge &lt;stream&gt; | #robomerge[&lt;bot&gt;] &lt;stream&gt; | ...]<span></div><br>'
	+ 'Examples:<br>'
	+ '<div style="padding-left:2ch">12345678</div>'
	+ '<div style="padding-left:2ch">12345678 #robomerge targetStream</div>'
	+ '<div style="display:flex">&nbsp;&nbsp;12345678&nbsp;<span>#robomerge[botName] targetStream | #robomerge[additionalBotName] additionalTargetStream</span></div>'
	+ '<br>'
	+ 'Flags:<br>'
	+ '<div style="display:flex">&nbsp;&nbsp;#stomp&nbsp;&mdash;&nbsp;<span>If a merge conflict occurs, stomp binary file conflicts instead of creating a shelf for manual resolution. Stomp applies only to the initial merge and is not propagated to subsequent merges in the flow.</span></div>'

function showReconsiderDialog(promptText, defaultCL, okLabel = 'Reconsider') {
	return promptForAsync({cl: {prompt: promptText, default: defaultCL}}, RECONSIDER_HELP_TEXT, 'modal-lg', okLabel)
}

// Adds a Bootstrap alert message to #status_message.
// closable: if true (default), adds a dismiss button and auto-fades after 10s.
function displayMessage(message, alertStyle, closable = true) {
	window.scrollTo(0, 0);

	let messageDiv = $(`<div class="alert ${alertStyle} fade in show" role="alert">`)

	if (closable) {
		messageDiv.addClass("alert-dismissible")
		let button = $('<button type="button" class="close" data-dismiss="alert" aria-label="Close">')
		button.html('&times;')
		messageDiv.append(button)
		setTimeout(function() {
			messageDiv.slideUp().alert('close')
		}, 10000)
	}

	messageDiv.append(message)
	$('#status_message').append(messageDiv)
}

function displaySuccessfulMessage(message, closable = true) {
	displayMessage(`<strong>Success!</strong> ${message}`, "alert-success", closable)
}
function displayInfoMessage(message, closable = true) {
	displayMessage(`${message}`, "alert-info", closable)
}
function displayWarningMessage(message, closable = true) {
	displayMessage(`<strong>Warning:</strong> ${message}`, "alert-warning", closable)
}
function displayErrorMessage(message, closable = true) {
	displayMessage(`<strong>Error:</strong> ${message}`, "alert-danger", closable)
}
