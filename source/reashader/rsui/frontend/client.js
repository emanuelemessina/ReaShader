import { MessageHandler, Messager } from './api.js';
import { utf8_to_utf16, utf16_to_utf8, camelCaseToTitleCase } from './api.js';

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
                const newValue = parseFloat(json.value);
                const slider = document.getElementById(json.paramId);
                slider.value = newValue; // Convert to slider range
                const valueLabel = document.getElementById(`value_${json.paramId}`);
                valueLabel.textContent = newValue.toFixed(3);
            })
            .handleTrackInfo((json) => {
                document.title = `${json.trackNumber} | ${json.trackName}`;
            })
            .handleParamGroupsList((json) => {
                // get param container
                const paramsContainer = document.getElementById('paramsContainer');
                paramsContainer.innerHTML = ''; // Clear previous content

                // create group containers
                for (let groupId in json.groups) {

                    const group = json.groups[groupId];
                    groupId = parseInt(groupId);

                    const groupContainer = document.createElement('fieldset');
                    groupContainer.classList.add('param-group-container');
                    groupContainer.id = group.name;

                    const legend = document.createElement('legend');
                    legend.innerHTML = camelCaseToTitleCase(group.name);

                    groupContainer.appendChild(legend);

                    paramsContainer.appendChild(groupContainer);
                }
            })
            .handleParamsList((json) => {

                // iterate over params
                for (let paramId in json.params) {
                    const param = json.params[paramId];
                    paramId = parseInt(paramId);

                    const groupContainer = document.getElementById(param.group);

                    // create ui based on type
                    switch (param.type) {
                        case "slider":
                            const sliderContainer = document.createElement('div');
                            sliderContainer.classList.add('slider-container');

                            const titleLabel = document.createElement('label');
                            titleLabel.textContent = utf8_to_utf16(param.title);
                            titleLabel.setAttribute('for', `${paramId}`);

                            const valueLabel = document.createElement('label');
                            valueLabel.textContent = `${(param.value).toFixed(3)}`;
                            valueLabel.id = `value_${paramId}`;

                            const unitsLabel = document.createElement('label');
                            unitsLabel.textContent = `${utf8_to_utf16(param.units)}`;

                            const slider = document.createElement('input');
                            slider.id = paramId
                            slider.type = 'range';
                            slider.min = 0; // Set your minimum value
                            slider.max = 1; // Set your maximum value
                            slider.value = param.value;
                            slider.step = '0.001';

                            // Add event listener to send paramUpdate message on slider change
                            slider.addEventListener('input', (event) => {
                                const newValue = parseFloat(event.target.value);

                                // Update value label
                                valueLabel.textContent = `${newValue}`;

                                messager
                                    .sendParamUpdate(paramId, newValue);
                            });

                            // Append elements to the container
                            sliderContainer.appendChild(titleLabel);
                            sliderContainer.appendChild(slider);
                            sliderContainer.appendChild(valueLabel);
                            sliderContainer.appendChild(unitsLabel);

                            groupContainer.appendChild(sliderContainer);

                            break;
                        case "hidden":
                            break;
                        default:
                            console.warn(`Unexpected paramater type ${param.type}: `, paramId, param);
                            break;
                    }
                }
            })
            .handleRenderingDevicesList((json) => {

                const groupContainer = document.getElementById('renderingDeviceSelect');

                for (let deviceId in json.devices) {

                    const device = json.devices[deviceId];
                    deviceId = parseInt(deviceId);

                    const radioButtonContainer = document.createElement('div');
                    radioButtonContainer.classList.add('radio-button-container');

                    const radioButton = document.createElement('input');
                    radioButton.type = 'radio';
                    radioButton.name = `renderingDeviceSelect`;
                    radioButton.value = deviceId;
                    radioButton.id = `device_${deviceId}`

                    // Auto-select the radio button if its index matches json.selected
                    if (deviceId == parseInt(json.selected)) {
                        radioButton.checked = true;
                    }

                    radioButtonContainer.appendChild(radioButton);

                    const label = document.createElement('label');
                    label.innerHTML = device.name;
                    label.setAttribute('for', `device_${deviceId}`);

                    radioButtonContainer.appendChild(label);

                    groupContainer.appendChild(radioButtonContainer);
                }

                // add switch device button

                const submitButton = document.createElement('button');
                submitButton.type = 'button';
                submitButton.textContent = 'Switch Device';

                submitButton.addEventListener('click', () => {
                    // Find the selected radio button value
                    let selectedDeviceId = document.querySelector('input[name="renderingDeviceSelect"]:checked').value;
                    selectedDeviceId = parseInt(selectedDeviceId);

                    // Send the selected device ID to the server
                    messager.sendRenderingDeviceChange(selectedDeviceId);
                });

                // Append submit button to the group container
                groupContainer.appendChild(submitButton);

            })
            ;
    } catch (error) {
        console.error('Error parsing incoming JSON:', error, event.data);
    }
});
