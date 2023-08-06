const socket = new WebSocket(`ws://localhost:${window.location.port}/ws`);

// Connection opened
socket.addEventListener('open', (event) => {
    //socket.send('Hello Server!');
});

const slider = document.getElementById('slider');
const sliderValueElement = document.getElementById('sliderValue');

// Listen for messages
socket.addEventListener('message', (event) => {
    try {
        const jsonData = JSON.parse(event.data);
        console.log('Parsed JSON:', jsonData);

        switch (jsonData.type) {
            case 'paramUpdate':
                console.log('Param update:', jsonData.paramId, jsonData.value);

                const receivedValue = parseFloat(jsonData.value);
                slider.value = receivedValue; // Convert to slider range
                sliderValueElement.textContent = receivedValue.toFixed(3);

                break;
            case 'trackInfo':
                console.log('Track info:', jsonData.trackNumber, jsonData.trackName);
                document.title = `${jsonData.trackNumber} | ${jsonData.trackName}`;
                break;

            default:
                console.log('Unknown type:', jsonData.type);
                break;
        }
    } catch (error) {
        console.error('Error parsing JSON:', error);
    }
});

// Listen for slider value changes

slider.addEventListener('input', (event) => {
    const sliderValue = parseFloat(event.target.value);
    sliderValueElement.textContent = sliderValue;

    const jsonMessage = {
        type: 'paramUpdate',
        paramId: 0,
        value: sliderValue
    };

    console.log("Sending param update:", 0, sliderValue);

    socket.send(JSON.stringify(jsonMessage));
});