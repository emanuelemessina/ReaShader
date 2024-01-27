/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

import { b64_to_utf16, utf16_to_b64 } from './strings.js'

// ------------------------

// we can register only the ids that we need, but keep it in sync with api.h!
export const DEFAULT_PARAM_IDS = {
    customShader: 3,
}

const SERVER_RESPONSE_TIMEOUT = 60000;

// -------------------------

/**
 * Provide a callback to the handlers that accepts a jsonObject
 */
export class MessageHandler {
    constructor(jsonString) {
        this.jsonObject = JSON.parse(jsonString);
    }

    handleVSTParamUpdate(callback) {
        this.#_reactTo("vstParamUpdate", callback);
        return this;
    }
    handleParamUpdate(callback) {
        this.#_reactTo("paramUpdate", callback);
        return this;
    }

    handleTrackInfo(callback) {
        this.#_reactTo("trackInfo", callback);
        return this;
    }

    handleParamGroupsList(callback) {
        this.#_reactTo("paramGroupsList", callback);
        return this;
    }

    handleParamTypesList(callback) {
        this.#_reactTo("paramTypesList", callback);
        return this;
    }

    handleParamsList(callback) {
        this.#_reactTo("paramsList", callback);
        return this;
    }

    handleServerShutdown(callback) {
        this.#_reactTo("serverShutdown", callback);
        return this;
    }

    handleRenderingDevicesList(callback) {
        this.#_reactTo("renderingDevicesList", callback);
        return this;
    }

    fallback(callback) {
        if (!this.reacted) {
            callback(this.jsonObject);
        }
    }

    fallbackWarning() {
        if (!this.reacted) {
            console.warn("Unexpected message type:", this.jsonObject);
        }
    }

    #_reactTo(type, callback) {
        if (this.jsonObject.type == type) {
            this.reacted = true;
            callback(this.jsonObject);
        }
    }
}

export class Messager {

    constructor(socket) {
        this.socket = socket;
    }

    // preferential path for vst params only
    sendVSTParamUpdate(paramId, sliderValue) {
        let msg = {
            type: "vstParamUpdate",
            paramId: paramId,
            value: sliderValue
        };

        this.#_send(msg);

        return this;
    }

    // data can be any json object depending on the param type
    sendParamUpdate(paramId, data) {
        let msg = {
            type: "paramUpdate",
            paramId: paramId,
            data: data
        };

        this.#_send(msg);

        return this;
    }

    sendParamAdd(title, groupId, typeId, derived) {
        let msg = {
            type: "paramAdd",
            param: {
                title: title,
                groupId: groupId,
                typeId: typeId,
                derived: derived
            }
        };

        this.#_send(msg);

        return this;
    }

    sendRequestTrackInfo() {
        let msg = {
            type: "request",
            what: "trackInfo"
        };

        this.#_send(msg);

        return this;
    }

    sendRequestParamGroupsList() {
        let msg = {
            type: "request",
            what: "paramGroupsList"
        };

        this.#_send(msg);

        return this;
    }

    sendRequestParamsList() {
        let msg = {
            type: "request",
            what: "paramsList"
        };

        this.#_send(msg);

        return this;
    }

    sendRequestParamTypesList() {
        let msg = {
            type: "request",
            what: "paramTypesList"
        };

        this.#_send(msg);

        return this;
    }

    sendRequestRenderingDevicesList() {
        let msg = {
            type: "request",
            what: "renderingDevicesList"
        };

        this.#_send(msg);

        return this;
    }

    sendRenderingDeviceChange(deviceId) {
        let msg = {
            type: "renderingDeviceChange",
            id: deviceId
        };

        this.#_send(msg);

        return this;
    }

    // Add more message builders as needed

    //--------------------------------------------------------

    #_send(json) {
        this.socket.send(JSON.stringify(json))
    }

    // metadata should be a SMALL json of extra file info
    async uploadFile(metadata, file, progressUpdate, onComplete, onRefuse, onError, onServerHanged) {

        const readerChunkSize = 1024 * 256; // 50KB chunks, after 64k of ws message we have ws fragmentation

        const uid32 = MurmurHash3(file.name).hash(file.size.toString()).hash(file.lastModified.toString()).result();
        const broadcastUid32 = 4294967295;

        const fileUploadHeader = {
            uid32: uid32,
            filename: file.name,
            mimetype: file.type,
            size: file.size,
            extension: file.name.split('.').pop(),
            numPackets: Math.ceil(file.size / readerChunkSize),
            metadata: metadata
        };
        
        const fileUploadHeaderArrayBuffer = new TextEncoder().encode(JSON.stringify(fileUploadHeader)).buffer;

        let aborted = false;

        // response to fileupload header
        const fileUploadHeaderResponse = async (event) => {
            
            // returns parsed response if it's for us, false if it's not
            function checkFileUploadChunkResponse(rawResponse) {
                try {
                    var response = JSON.parse(rawResponse);
                    if (parseInt(response.uid32 != broadcastUid32)) {
                        if (parseInt(response.uid32) != uid32) {
                            // not for us
                            return false;
                        }
                    }

                    // it's for us
                    return response;
                }
                catch (e) {
                    // not for us
                    return false;
                }
            }

            var response = checkFileUploadChunkResponse(event.data);
            
            if (response === false) { return; }
            
            const reader = new FileReader();

            function abortUpload(socket, eventHandlerToUnregister = null) {
                reader.abort();
                aborted = true;
                if (eventHandlerToUnregister != null) {
                    socket.removeEventListener('message', eventHandlerToUnregister);
                }
            }

            if (response.status === 'proceed') {
                // Server acknowledged the file upload, proceed with sending file data
                this.socket.removeEventListener('message', fileUploadHeaderResponse); // from now on this is not the handler anymore
                console.log(`Server acknowledged ${uid32} (${file.name}) upload.`);

                let readerOffset = 0;

                function readNextChunk() {
                    const blob = file.slice(readerOffset, readerOffset + readerChunkSize);
                    reader.readAsArrayBuffer(blob);
                }

                reader.onloadend = async () => {

                    if (aborted) { return; }

                    if (reader.readyState != FileReader.DONE) { return; }

                    const chunkBuffer = new Uint8Array(reader.result);

                    // Prepend uid32 to the chunk
                    const uid32ArrayBuffer = new Uint8Array(Uint32Array.of(uid32).buffer);
                    const sendChunkBuffer = new Uint8Array(uid32ArrayBuffer.length + chunkBuffer.length);
                    sendChunkBuffer.set(uid32ArrayBuffer);
                    sendChunkBuffer.set(chunkBuffer, uid32ArrayBuffer.length);

                    this.socket.send(sendChunkBuffer.buffer);

                    const responseTimeout = SERVER_RESPONSE_TIMEOUT; // (in milliseconds)

                    function gotError(message) {
                        console.error(`Server error for ${uid32} (${file.name}): ${message}`);
                        onError(message);
                    }

                    const chunkResponsePromise = new Promise((resolve, reject) => {

                        const chunkResponseHandler = (rawResp) => {
                            
                            var resp = checkFileUploadChunkResponse(rawResp.data);
                            if (resp === false) { return; }

                            clearTimeout(timer);
                            this.socket.removeEventListener('message', chunkResponseHandler);
                            resolve(resp);
                        };

                        const timer = setTimeout(() => {
                            clearTimeout(timer);
                            this.socket.removeEventListener('message', chunkResponseHandler);
                            reject(new Error('Server response timeout'));
                        }, responseTimeout);

                        this.socket.addEventListener('message', chunkResponseHandler);
                    });

                    try {
                        const chunkResponse = await chunkResponsePromise;

                        if (chunkResponse.status == "proceed") {

                            readerOffset += chunkBuffer.byteLength;

                            const percent = Math.min((readerOffset / file.size) * 100, 100);
                            progressUpdate(percent);

                            if (readerOffset < file.size) {
                                readNextChunk();
                            }
                            else {
                                // send ending chunk (uid32 only)
                                this.socket.send(uid32ArrayBuffer.buffer); // TODO: implement timeout for the proceed in frontend and still check for error messages

                                try {
                                    const lastChunkResponse = await chunkResponsePromise;

                                    if (lastChunkResponse.status == "proceed") {
                                        console.log(`File ${file.name} upload complete.`);
                                        onComplete();
                                        return;
                                    }
                                    else if (lastChunkResponse.status == "error") {
                                        // upload finished anyway, no need to call abort
                                        gotError(lastChunkResponse);
                                        return;
                                    }
                                    
                                }
                                catch (e) { throw e; }

                            }

                            return;
                        }
                        else if (chunkResponse.status == "error") {
                            abortUpload(this.socket);
                            gotError(chunkResponse.message);
                            return;
                        }

                    } catch (error) {
                        // timeout
                        console.error(error.message);
                        abortUpload(this.socket);
                        onServerHanged();
                    }

                };

                // Start the file upload reading the first chunk
                readNextChunk();
            }
            else if (response.status === 'busy') {
                abortUpload(this.socket, fileUploadHeaderResponse);
                console.log('Server is busy. Try again later.');
                onRefuse();
            }
            else if (response.status == "error") {
                abortUpload(this.socket, fileUploadHeaderResponse);
                console.error('Server error:', response.message);
                onError(response.message);
            }
        };
        
        this.socket.addEventListener('message', fileUploadHeaderResponse);

        // Send fileUploadHeader as a binary frame to start fileupload
        this.socket.send(fileUploadHeaderArrayBuffer);
    }

}