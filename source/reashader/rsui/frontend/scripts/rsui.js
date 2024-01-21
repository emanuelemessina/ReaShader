/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

import { camelCaseToTitleCase } from './strings.js'

function scaleNormalizedValue(units, normalizedValue) {
    switch (units) {
        case "%":
            return (normalizedValue * 100).toFixed(3);
            break;
        default:
            return normalizedValue;
    }
}

function getParamDisplayValue(paramId, normalizedValue) {
    const unitsLabel = document.getElementById(`units_${paramId}`);
    const units = unitsLabel.innerHTML;

    return scaleNormalizedValue(units, normalizedValue);
}

export function uiParamUpdate(paramId, newValue) {
    newValue = parseFloat(newValue);
    const slider = document.getElementById(paramId);
    slider.value = newValue; // Convert to slider range
    const valueLabel = document.getElementById(`value_${paramId}`);
    valueLabel.textContent = getParamDisplayValue(paramId, newValue); // TODO scaling of values based on units
}

export function uiCreateParamGroups(groups) {
    // get param container
    const paramsContainer = document.getElementById('paramsContainer');
    paramsContainer.innerHTML = ''; // Clear previous content

    // create group containers
    for (let groupId in groups) {

        const group = groups[groupId];
        groupId = parseInt(groupId);

        const groupContainer = document.createElement('fieldset');
        groupContainer.classList.add('param-group-container');
        groupContainer.id = group.name;

        const legend = document.createElement('legend');
        legend.innerHTML = camelCaseToTitleCase(group.name);

        groupContainer.appendChild(legend);

        paramsContainer.appendChild(groupContainer);
    }
}

export function uiCreateParam(messager, paramId, param) {
    const groupContainer = document.getElementById(param.group);

    // create ui based on type
    switch (param.type) {
        case "vstParameter":
            const sliderContainer = document.createElement('div');
            sliderContainer.classList.add('slider-container');

            const titleLabel = document.createElement('label');
            titleLabel.textContent = param.title;
            titleLabel.setAttribute('for', `${paramId}`);

            const valueLabel = document.createElement('label');
            valueLabel.id = `value_${paramId}`;

            const unitsLabel = document.createElement('label');
            unitsLabel.id = `units_${paramId}`;
            unitsLabel.textContent = `${param.units}`;

            const slider = document.createElement('input');
            slider.id = paramId
            slider.type = 'range';
            slider.min = 0; // Set minimum value
            slider.max = 1; // Set maximum value
            slider.step = '0.001';

            // Add event listener to send paramUpdate message on slider change
            slider.addEventListener('input', (event) => {
                const newValue = parseFloat(event.target.value);

                // Update value label
                valueLabel.textContent = `${scaleNormalizedValue(param.units,newValue)}`;

                messager
                    .sendVSTParamUpdate(paramId, newValue);
            });

            // Append elements to the container
            sliderContainer.appendChild(titleLabel);
            sliderContainer.appendChild(slider);
            sliderContainer.appendChild(valueLabel);
            sliderContainer.appendChild(unitsLabel);

            groupContainer.appendChild(sliderContainer);

            // defer setting values after adding to container
            slider.value = param.value;
            valueLabel.textContent = `${scaleNormalizedValue(param.units, param.value)}`;

            break;
        case "int8u":

            break;
        case "string":

            break;
        default:
            console.warn(`Unexpected paramater type ${param.type}: `, paramId, param);
            break;
    }
}

export function uiCreateDeviceSelector(messager, devices, selected) {
    const groupContainer = document.getElementById('renderingDeviceSelect');

    for (let deviceId in devices) {

        const device = devices[deviceId];
        deviceId = parseInt(deviceId);

        const radioButtonContainer = document.createElement('div');
        radioButtonContainer.classList.add('radio-button-container');

        const radioButton = document.createElement('input');
        radioButton.type = 'radio';
        radioButton.name = `renderingDeviceSelect`;
        radioButton.value = deviceId;
        radioButton.id = `device_${deviceId}`

        // Auto-select the radio button if its index matches json.selected
        if (deviceId == parseInt(selected)) {
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
}

var paramTypesList;

export function setParamTypesList(list) { paramTypesList = list; }