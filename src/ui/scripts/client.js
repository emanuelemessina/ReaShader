/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

// Messages from the plugin (see ReaShaderPlugin's web UI section).
// The webview host calls window.__reashaderOnMessage(msg) with the message object.

window.__reashaderOnMessage = (msg) => {
    try {
        switch (msg.type) {
            case "snapshot":
                renderSnapshot(msg);
                break;
            case "paramValue":
                setParamValue(msg.id, msg.value);
                break;
            case "shaderStatus":
                setShaderStatus(msg.status, msg.error);
                break;
            default:
                console.warn("Unexpected message from the plugin:", msg);
        }
    } catch (error) {
        console.error("Failed to handle message from the plugin:", error, msg);
    }
};

native.ready();
