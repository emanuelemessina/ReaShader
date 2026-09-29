/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

// Messages to the plugin (see ReaShaderPlugin::handleWebUIMessage).
// window.postToNative is injected by the webview host before any script runs.

const native = {
    send(msg) {
        window.postToNative(JSON.stringify(msg));
    },

    // the page is loaded: the plugin answers with a snapshot
    ready() {
        this.send({ type: "ready" });
    },

    paramValue(id, value) {
        this.send({ type: "paramValue", id: id, value: value });
    },

    renderingDevice(index) {
        this.send({ type: "renderingDevice", index: index });
    },

    // ask for a fresh snapshot (rescans the built-in effects)
    refresh() {
        this.send({ type: "refresh" });
    },

    // one of the built-in effects, by file name
    shaderSelect(name) {
        this.send({ type: "shaderSelect", name: name });
    },

    // shaders are small GLSL text files: sent whole, as text
    async shaderUpload(file) {
        const source = await file.text();
        this.send({ type: "shaderUpload", name: file.name, source: source });
    },
};
