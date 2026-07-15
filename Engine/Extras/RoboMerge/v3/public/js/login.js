// Copyright Epic Games, Inc. All Rights Reserved.
"use strict"

const COOKIE_REGEX = /\s*(.*?)=(.*)/;
function getCookie(name) {
	for (const cookieKV of decodeURIComponent(document.cookie).split(';')) {
		const match = cookieKV.match(COOKIE_REGEX);
		if (match && match[1].toLowerCase() === name.toLowerCase()) {
			return match[2];
		}
	}
	return null;
}

var authClient;

$(async () => {

	const $ldapForm = $('.login-form .ldap-form');

	const clearError = () => $('#login-error').hide();

	const $nameInput = $('input[name=user]', $ldapForm).on('input', clearError);
	const $passwordInput = $('input[name=password]', $ldapForm).on('input', clearError);

	const $oktaForm = $('.login-form .okta-form');
	const $oktaError = $('#okta-login-error');

	// Preserve any hash from URL on login page load
	if (window.location.hash) {
		try {
			sessionStorage.setItem('robomerge_auth_hash', window.location.hash);
		} catch (e) {
			console.warn('Failed to preserve hash on login page:', e);
		}
	}

	const signInMethod = await $.get('/signInMethod');

	if (signInMethod.okta) {
		$oktaForm.show();

		try {
			const config = await $.get('/oktaConfig');
			authClient = new OktaAuth(JSON.parse(config));
		}
		catch(error) {
			console.error(error);
			$('html').show();
			$oktaError.text("OktaAuth is not configured").show();
		}

		// If this is a login redirect from Okta we can parse the tokens returned from Okta
		if (authClient && authClient.isLoginRedirect()) {

			try {
				const { tokens } = await authClient.token.parseFromUrl();
				authClient.tokenManager.setTokens(tokens);

				const { accessToken, idToken } = await authClient.tokenManager.getTokens();
				const user = await authClient.token.getUserInfo(accessToken, idToken);

				const postData = {
					user: user.preferred_username,
					displayName: user.name,
					groups: JSON.stringify(user.groups)
				};

				$.post('/oktaLogin', postData)
				.done((token) => {
					// Check to see if we have a redirect request from the website
					var urlParams = new URLSearchParams(window.location.search);
					var redirectString = "/"
					if (urlParams.has("redirect")) {
						redirectString = decodeURIComponent(urlParams.get("redirect"))
					}
					else {
						const redirectCookie = getCookie('redirect_to')
						if (redirectCookie) {
							redirectString = redirectCookie
						}
					}

					// Retrieve preserved hash from sessionStorage
					let preservedHash = '';
					try {
						const hash = sessionStorage.getItem('robomerge_auth_hash');
						if (hash) {
							preservedHash = hash;
							sessionStorage.removeItem('robomerge_auth_hash');
						}
					} catch (e) {
						console.warn('Failed to retrieve preserved hash:', e);
					}

					document.cookie = `auth=${token}; secure=true; path=/;`;
					document.cookie = 'redirect_to=;';
					window.location = window.origin + redirectString + preservedHash;
				})
				.fail((xhr, status, error) => {
					$('html').show();
					$oktaError.text(error + ": " + xhr.responseText).show();
				});

			}
			catch(error) {
				$('html').show();
				$oktaError.text(error).show();
			}

		}
		else {
			// Check to see if user just signed out
			if (getCookie('signedOut')) {
				$('html').show();
				document.cookie = 'signedOut=;';
			}
			else if (authClient) {
				oktaSignIn();
			}
		}
	}

	if (signInMethod.ldap) {
		if (!signInMethod.okta) {
			$('html').show();
		}
		$ldapForm.show();
	}

	$ldapForm.on('submit', () => {
		const $submitButton = $('button', $ldapForm).prop('disabled', true).text('Signing in ...');

		// clear auth on attempt to log in
		document.cookie = 'auth=;';

		const postData = {
			user: $nameInput.val(),
			password: $passwordInput.val()
		};

		$.post('/dologin', postData)
		.then((token) => {
			// win!
			clearError();

			// Check to see if we have a redirect request from the website
			var urlParams = new URLSearchParams(window.location.search);
			var redirectString = "/"
			if (urlParams.has("redirect")) {
				redirectString = decodeURIComponent(urlParams.get("redirect"))
			}

			// Retrieve preserved hash from sessionStorage
			let preservedHash = '';
			try {
				const hash = sessionStorage.getItem('robomerge_auth_hash');
				if (hash) {
					preservedHash = hash;
					sessionStorage.removeItem('robomerge_auth_hash');
				}
			} catch (e) {
				console.warn('Failed to retrieve preserved hash:', e);
			}

			document.cookie = `auth=${token}; secure=true`;
			document.cookie = 'redirect_to=;';

			// @todo remove redirect_to cookie on server!
			window.location = window.origin + redirectString + preservedHash

		}, (xhr, status, error) => {
			// bad luck
			$submitButton.prop('disabled', false).text('Sign in');
			$passwordInput.focus();

			$('#login-error')
			.text(error === 'Unauthorized' ? 'Invalid username or password!' : 'Failed to authenticate, please try later')
			.show();
		});

		return false;
	});

	
});

async function oktaSignIn() {
	// Clear auth cookie on attempt to sign in
	document.cookie = 'auth=;';

	var urlParams = new URLSearchParams(window.location.search);
	if (urlParams.has("redirect")) {
		document.cookie = `redirect_to=${decodeURIComponent(urlParams.get("redirect"))}`
	}

	// Start full-page redirect to Okta
	if (authClient) {
		authClient.token.getWithRedirect();
	}
}
