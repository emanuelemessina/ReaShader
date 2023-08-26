/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

export function utf8_to_utf16(utf8) {
    return decodeURIComponent(escape(utf8))
}
export function utf16_to_utf8(utf16) {
    return unescape(encodeURIComponent(value))
}

export function camelCaseToTitleCase(input) {
    return input.replace(/([A-Z])/g, ' $1')
        .replace(/^./, function (str) { return str.toUpperCase(); });
}

function decodeJSONFromUTF8(json) {
    function decodeValue(value) {
        if (typeof value === 'string') {
            return utf8_to_utf16(value);
        } else if (Array.isArray(value)) {
            return value.map(decodeValue);
        } else if (typeof value === 'object' && value !== null) {
            const decodedObject = {};
            for (const key in value) {
                if (value.hasOwnProperty(key)) {
                    decodedObject[key] = decodeValue(value[key]);
                }
            }
            return decodedObject;
        } else {
            return value;
        }
    }

    return decodeValue(json);
}
function encodeJSONToUTF8(json) {
    function encodeValue(value) {
        if (typeof value === 'string') {
            return utf8_to_utf16(value);
        } else if (Array.isArray(value)) {
            return value.map(encodeValue);
        } else if (typeof value === 'object' && value !== null) {
            const encodedObject = {};
            for (const key in value) {
                if (value.hasOwnProperty(key)) {
                    encodedObject[key] = encodeValue(value[key]);
                }
            }
            return encodedObject;
        } else {
            return value;
        }
    }

    return encodeValue(json);
}


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

    sendParamUpdate(paramId, value) {
        let msg = {
            type: "paramUpdate",
            paramId: paramId,
            value: value
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
