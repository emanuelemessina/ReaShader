/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

import { MessageHandler, Messager } from './api.js';
import { uiVSTParamUpdate ,uiParamUpdate, uiCreateParamGroups, uiCreateParam, uiCreateDeviceSelector, setParamTypesList } from './rsui.js'

const socket = new WebSocket(`ws://localhost:${window.location.port}/ws`);
const messager = new Messager(socket);

// Connection opened
socket.addEventListener('open', (event) => {
    // request info
    messager
        .sendRequestTrackInfo()
        .sendRequestParamGroupsList()
        .sendRequestParamsList()
        .sendRequestRenderingDevicesList()
        .sendRequestParamTypesList()
        ;
});

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
