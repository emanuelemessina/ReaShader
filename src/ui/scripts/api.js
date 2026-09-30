/**
 * @file
 * @brief Messages to the plugin (see ReaShaderPlugin::handleWebUIMessage).
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

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

    logo(enabled) {
        this.send({ type: "logo", enabled: enabled });
    },

    // https links open in the system browser (the webview must stay on the UI)
    openUrl(url) {
        this.send({ type: "openUrl", url: url });
    },

    // a compiled shader by name, "" = none
    shaderSelect(name) {
        this.send({ type: "shaderSelect", name: name });
    },

    // GLSL to compile and store; shaders are small text files, sent whole
    async shaderUpload(file) {
        const source = await file.text();
        this.send({ type: "shaderUpload", name: file.name, source: source });
    },

    // a stored LUT by name, "" = none
    lutSelect(name) {
        this.send({ type: "lutSelect", name: name });
    },

    // a .cube file to parse and store, sent whole as text (a few MB at most)
    async lutUpload(file) {
        const source = await file.text();
        this.send({ type: "lutUpload", name: file.name, source: source });
    },

    // "before" / "after" the shader, or "shader" (the shader samples the LUT itself)
    lutMode(mode) {
        this.send({ type: "lutMode", mode: mode });
    },
};
