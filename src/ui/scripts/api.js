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

    // GLSL to compile, store and append to the chain; shaders are small text files, sent whole
    async shaderUpload(file) {
        const source = await file.text();
        this.send({ type: "shaderUpload", name: file.name, source: source });
    },

    // a .cube file to parse, store and append to the chain, sent whole as text (a few MB at most)
    async lutUpload(file) {
        const source = await file.text();
        this.send({ type: "lutUpload", name: file.name, source: source });
    },

    // -------- the chain: nodes by uid, stored shaders and LUTs by name --------

    // kind: "shader" or "lut"; no index: last
    nodeAdd(kind, name, index) {
        this.send({ type: "nodeAdd", kind: kind, name: name, index: index });
    },

    nodeRemove(uid) {
        this.send({ type: "nodeRemove", uid: uid });
    },

    nodeMove(uid, index) {
        this.send({ type: "nodeMove", uid: uid, index: index });
    },

    nodeBypass(uid, bypass) {
        this.send({ type: "nodeBypass", uid: uid, bypass: bypass });
    },

    // another stored shader or LUT, of the node's kind
    nodeSet(uid, name) {
        this.send({ type: "nodeSet", uid: uid, name: name });
    },

    // a shader node's LUT (iChannel1), "" = none
    nodeLut(uid, name) {
        this.send({ type: "nodeLut", uid: uid, name: name });
    },
};
