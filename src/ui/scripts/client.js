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
                setShaderStatus(msg.status, msg.state);
                break;
            default:
                console.warn("Unexpected message from the plugin:", msg);
        }
    } catch (error) {
        console.error("Failed to handle message from the plugin:", error, msg);
    }
};

// about box: opened by the logo; closed by its button, a click outside it, or Escape
const about = document.getElementById('about');
document.getElementById('logo').addEventListener('click', () => setAboutOpen(true));
about.querySelector('.close').addEventListener('click', () => setAboutOpen(false));
about.addEventListener('click', (event) => { if (event.target === about) setAboutOpen(false); });
document.addEventListener('keydown', (event) => { if (event.key === 'Escape' && !about.hidden) setAboutOpen(false); });
about.querySelectorAll('a[data-url]').forEach(link => link.addEventListener('click', (event) => {
    event.preventDefault();
    native.openUrl(link.dataset.url);
}));

native.ready();
