import { MessageHandler, Messager } from './api.js';
import { uiParamUpdate, uiCreateParamGroups, uiCreateParam, uiCreateDeviceSelector } from './rsui.js'

const socket = new WebSocket(`ws://localhost:${window.location.port}/ws`);
const messager = new Messager(socket);

// Connection opened
socket.addEventListener('open', (event) => {
    // request info
    messager
        .sendRequestTrackInfo()
        .sendRequestParamGroupsList()
        .sendRequestParamsList()
        .sendRequestRenderingDevicesList();
});

// Listen for messages
socket.addEventListener('message', (event) => {
    try {
        const handler = new MessageHandler(event.data);

        handler
            .handleServerShutdown(() => {
                window.close();
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
                for (let paramId in json.params) {
                    const param = json.params[paramId];
                    paramId = parseInt(paramId);

                    uiCreateParam(paramId, param);
                }
            })
            .handleRenderingDevicesList((json) => {
                uiCreateDeviceSelector(json.devices, json.selected);
            })
            ;
    } catch (error) {
        console.error('Error parsing incoming JSON:', error, event.data);
    }
});
