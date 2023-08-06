/**
 * Provide a callback to the handlers that accepts a jsonObject
 */
export class MessageHandler {
    constructor(jsonString) {
        this.jsonObject = JSON.parse(jsonString);
    }

    handleParamUpdate(callback) {
        this._reactTo("paramUpdate", callback);
        return this;
    }

    handleTrackInfo(callback) {
        this._reactTo("trackInfo", callback);
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

    _reactTo(type, callback) {
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
        msg = {
            type: "paramUpdate",
            paramId: paramId,
            value: value
        };

        _send(msg);

        return this;
    }

    sendRequestTrackInfo() {
        msg = {
            type: "request",
            what: "trackInfo"
        };

        _send(msg);

        return this;
    }

    sendRequestParamsList() {
        msg = {
            type: "request",
            what: "paramsList"
        };

        _send(msg);

        return this;
    }

    _send(json) {
        socket.send(JSON.stringify(msg))
    }

    // Add more message builders as needed
}
