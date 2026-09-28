/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

// ------------------------

// we can register only the ids that we need, but keep it in sync with api.h!
const DEFAULT_PARAM_IDS = {
    renderingDevice: 2,
    customShader: 3,
}

// -------------------------

/**
 * Provide a callback to the handlers that accepts a jsonObject
 */
class MessageHandler {
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

    handleParamAdd(callback) {
        this.#_reactTo("paramAdd", callback);
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

class Messager {

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

    // metadata should be a SMALL json of extra file info.
    // Sent as a single base64-encoded JSON message (custom shaders are small GLSL text files, and
    // the postToNative/eval transport is JSON/string-based, not raw binary frames) -- replaces the
    // old chunked-binary-with-uid32-correlation protocol the WebSocket transport needed.
    // onRefuse/onServerHanged are kept in the signature for compatibility with rsui.js's call site
    // but no longer apply (there's no server round-trip/timeout to refuse or hang on anymore).
    async uploadFile(metadata, file, progressUpdate, onComplete, onRefuse, onError, onServerHanged) {
        try {
            const dataUrl = await new Promise((resolve, reject) => {
                const reader = new FileReader();
                reader.onload = () => resolve(reader.result);
                reader.onerror = () => reject(reader.error || new Error('Failed to read file'));
                reader.readAsDataURL(file);
            });

            const base64Data = dataUrl.substring(dataUrl.indexOf(',') + 1);

            this.#_send({
                type: "fileUpload",
                name: file.name,
                extension: file.name.split('.').pop(),
                size: file.size,
                metadata: metadata,
                data: base64Data
            });

            progressUpdate(100);
            onComplete();
        } catch (error) {
            console.error(`Upload failed for ${file.name}:`, error);
            onError(error.message || String(error));
        }
    }

}