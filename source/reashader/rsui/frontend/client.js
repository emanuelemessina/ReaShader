import { MessageHandler, Messager } from './api';

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
            .handleParamUpdate((json) => {
                const receivedValue = parseFloat(jsonData.value);
                slider.value = receivedValue; // Convert to slider range
                sliderValueElement.textContent = receivedValue.toFixed(3);
            })
            .handleTrackInfo((json) => {
                document.title = `${jsonData.trackNumber} | ${jsonData.trackName}`;
            })
            .handleParamsList((jsonData) => {
                // Handle params list
                const paramsContainer = document.getElementById('paramsContainer');
                paramsContainer.innerHTML = ''; // Clear previous content

                for (const paramId in jsonData.params) {
                    const param = jsonData.params[paramId];

                    const sliderContainer = document.createElement('div');
                    sliderContainer.classList.add('slider-container');

                    const titleLabel = document.createElement('label');
                    titleLabel.textContent = param.title;

                    const valueLabel = document.createElement('label');
                    valueLabel.textContent = `${param.value} ${param.units}`;

                    const slider = document.createElement('input');
                    slider.type = 'range';
                    slider.min = 0; // Set your minimum value
                    slider.max = 1; // Set your maximum value
                    slider.value = param.value;
                    slider.step = '0.001';

                    // Add event listener to send paramUpdate message on slider change
                    slider.addEventListener('input', (event) => {
                        const newValue = parseFloat(event.target.value);

                        // Update value label
                        valueLabel.textContent = `${newValue} ${param.units}`;

                        messager
                            .sendParamUpdate(paramId, newValue);
                    });

                    // Append elements to the container
                    sliderContainer.appendChild(titleLabel);
                    sliderContainer.appendChild(slider);
                    sliderContainer.appendChild(valueLabel);
                    paramsContainer.appendChild(sliderContainer);
                }
            } catch (error) {
                console.error('Error parsing incoming JSON:', error, event.data);
            }
    });