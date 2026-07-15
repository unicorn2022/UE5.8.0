// Copyright Epic Games, Inc. All Rights Reserved.

import * as http from 'http';
import * as https from 'https';
import * as net from 'net';
import * as _url from 'url';
import { ContextualLogger } from './logger';

interface GetArgs {
	url: string,
	headers?: {[header: string]: any}
}

export interface PostArgs extends GetArgs {
	body: string,
	contentType?: string
}

export interface ResponseWithHeaders {
	body: string
	headers: http.IncomingHttpHeaders
}

export function reqWithHeaders(args: GetArgs, method: string) {
	const IS_POST = method === 'POST' || method === 'PUT'

	return new Promise<ResponseWithHeaders>((done, fail) => {
		const url = new _url.URL(args.url);

		let response = '';

		const postArgs = args as PostArgs
		const headers: {[header: string]: any} = IS_POST
			? {'Content-Length': Buffer.byteLength(postArgs.body)}
			: {}

		if (args.headers) {
			for (const header in args.headers) {
				headers[header] = args.headers[header]
			}
		}

		if (postArgs.contentType) {
			headers['Content-Type'] = postArgs.contentType
		}

		const callback = (res: http.IncomingMessage) => {
			res.on('data', chunk => response += chunk)
			res.on('end', () => done({body: response, headers: res.headers}))
		}

		let req: http.ClientRequest = url.protocol === 'http:'
			? http.request({host: url.hostname, port: url.port || '80', path: url.pathname + url.search, method, headers}, callback)
			: https.request({host: url.hostname, port: url.port || '443', path: url.pathname + url.search, method, headers}, callback)

		req.on('error', err => fail(err))
		if (IS_POST) {
			req.write(postArgs.body)
		}
		req.end()
	});
}

export async function req(args: GetArgs, method: string) {
	const response = await reqWithHeaders(args, method)
	return response.body
}

export function get(args: GetArgs) {
	return req(args, 'GET')
}

export function post(args: PostArgs) {
	return req(args, 'POST')
}

export function postWithHeaders(args: PostArgs) {
	return reqWithHeaders(args, 'POST')
}

export function put(args: PostArgs) {
	return req(args, 'PUT')
}

export function sendTcp(urlStr: string, data: string, logger: ContextualLogger) {
	const url = new _url.URL(urlStr);

	const client = new net.Socket;
	client.connect(parseInt(url.port), url.hostname, () => client.write(data))
	return new Promise<void>((done, fail) => {

		client.on('data', (returned: string) => {
			logger.info('Analytics response: ' + returned)
			client.destroy()
			done()
		})

		client.on('error', err => fail(err))
	})
}
