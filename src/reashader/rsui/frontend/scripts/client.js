/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

// Transport adapter over webview/webview's postToNative()/eval() bridge (see
// webui_host_win32.cpp), duck-typed against the same { send(), addEventListener('message', cb) }
// shape the old WebSocket object had -- postToNative is injected automatically by the C++ side's
// webview.bind("postToNative", ...) call, before this script ever runs.
const socket = {
    send: (msg) => window.postToNative(msg),
    addEventListener: (type, cb) => {
        if (type === 'message')
            window.__reashaderOnMessage = cb;
    }
};
const messager = new Messager(socket);

// postToNative is already available by the time this (deferred) script runs, so request the
// initial data straight away instead of waiting for a connection-opened event.
messager
    .sendRequestTrackInfo()
    .sendRequestParamGroupsList()
    .sendRequestParamsList()
    .sendRequestRenderingDevicesList()
    .sendRequestParamTypesList()
    ;

// Listen for messages
socket.addEventListener('message', (event) => {
    try {
        const handler = new MessageHandler(event.data);

        handler
            .handleServerShutdown(() => {
                window.close();
            })
            .handleVSTParamUpdate((json) => {
                uiVSTParamUpdate(json.paramId, json.value);
            })
            .handleParamUpdate((json) => {
                uiParamUpdate(json.paramId, json.value);
            })
            .handleTrackInfo((json) => {
                document.title = `${json.trackNumber} | ${json.trackName}`;
            })
            .handleParamGroupsList((json) => {
                uiCreateParamGroups(json.groups);
            })
            .handleParamsList((json) => {

                // iterate over params
                for (let paramIndex in json.params) {
                    const param = json.params[paramIndex];
                    const paramId = param.id;

                    uiCreateParam(messager, paramId, param);
                }
            })
            .handleParamAdd((json) => {
                let param = json["param"];
                uiCreateParam(messager, param.id, param);
            })
            .handleRenderingDevicesList((json) => {
                uiCreateDeviceSelector(messager, json.devices, json.selected);
            })
            .handleParamTypesList((json) => {
                setParamTypesList(json.types);
            })
            ;
    } catch (error) {
        // leave it, might be for other handlers
        //console.error('Error parsing incoming JSON:', error, event.data);
    }
});
