import { MessageHandler, Messager } from './api.js';
import { utf8_to_utf16, utf16_to_utf8 } from './api.js';

const socket = new WebSocket(`ws://localhost:${window.location.port}/ws`);
const messager = new Messager(socket);

// Connection opened
socket.addEventListener('open', (event) => {
    // request info
    messager
        .sendRequestTrackInfo()
        .sendRequestParamsList();
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
            .handleParamsList((json) => {
                // Handle params list
                const paramsContainer = document.getElementById('paramsContainer');
                paramsContainer.innerHTML = ''; // Clear previous content

                for (let paramId in json.params) {
                    const param = json.params[paramId];
                    paramId = parseInt(paramId);

                    const sliderContainer = document.createElement('div');
                    sliderContainer.classList.add('slider-container');

                    const titleLabel = document.createElement('label');
                    titleLabel.textContent = utf8_to_utf16(param.title);
                    titleLabel.setAttribute('for', `${paramId}`);

                    const valueLabel = document.createElement('label');
                    valueLabel.textContent = `${param.value}`;
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

                    paramsContainer.appendChild(sliderContainer);
                }
            });
    } catch (error) {
        console.error('Error parsing incoming JSON:', error, event.data);
    }
});
