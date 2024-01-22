/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

import { b64_to_utf16, utf16_to_b64 } from './strings.js'

/**
 * Provide a callback to the handlers that accepts a jsonObject
 */
export class MessageHandler {
    constructor(jsonString) {
        this.jsonObject = JSON.parse(jsonString);
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

    // value can be any json object depending on the param type
    sendParamUpdate(paramId, value) {
        let msg = {
            type: "vstParamUpdate",
            paramId: paramId,
            value: value
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

    #_send(json) {
        this.socket.send(JSON.stringify(json))
    }

    // Add more message builders as needed
}
