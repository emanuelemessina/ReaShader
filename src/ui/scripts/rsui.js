/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

// Builds the UI from the plugin's snapshot. Rebuilding is idempotent: every snapshot replaces
// what the previous one created.

function displayValue(units, value) {
    return units === "%" ? (value * 100).toFixed(1) : value.toFixed(3);
}

function renderSnapshot(snapshot) {
    document.title = `${snapshot.track.number} | ${snapshot.track.name}`;
    renderParams(snapshot.params);
    renderDevices(snapshot.devices.names, snapshot.devices.selected);
    renderShaderPicker(snapshot.shaders, snapshot.shader.name, snapshot.shadersDir);
    renderShaderUploader();
}

// -------- params --------

// only the shader's params have sliders (the plugin's own, like Audio Gain, are host-only)
function renderParams(params) {
    const container = document.querySelector('#shader .params');
    container.replaceChildren();
    for (const param of params) {
        if (param.group === "shader")
            container.appendChild(createSlider(param));
    }
}

function createSlider(param) {
    const container = document.createElement('div');
    container.classList.add('slider-container');

    const title = document.createElement('label');
    title.textContent = param.label;
    title.htmlFor = `param_${param.id}`;

    const slider = document.createElement('input');
    slider.id = `param_${param.id}`;
    slider.type = 'range';
    slider.min = param.minValue;
    slider.max = param.maxValue;
    slider.step = (param.maxValue - param.minValue) / 1000;
    slider.value = param.value;
    slider.dataset.units = param.units;

    const value = document.createElement('label');
    value.id = `value_${param.id}`;
    value.textContent = displayValue(param.units, param.value);

    const units = document.createElement('label');
    units.textContent = param.units;

    slider.addEventListener('input', () => {
        const newValue = parseFloat(slider.value);
        value.textContent = displayValue(param.units, newValue);
        native.paramValue(param.id, newValue);
    });

    container.append(title, slider, value, units);
    return container;
}

// host automation changed a value
function setParamValue(id, newValue) {
    const slider = document.getElementById(`param_${id}`);
    if (!slider)
        return;
    slider.value = newValue;
    document.getElementById(`value_${id}`).textContent = displayValue(slider.dataset.units, newValue);
}

// -------- rendering device --------

function renderDevices(names, selected) {
    const group = document.getElementById('renderingDevice');
    group.querySelectorAll(':scope > :not(legend)').forEach(element => element.remove());

    names.forEach((name, index) => {
        const container = document.createElement('div');
        container.classList.add('radio-button-container');

        const radio = document.createElement('input');
        radio.type = 'radio';
        radio.name = 'renderingDevice';
        radio.id = `device_${index}`;
        radio.value = index;
        radio.checked = index === selected;

        const label = document.createElement('label');
        label.htmlFor = radio.id;
        label.textContent = name;

        container.append(radio, label);
        group.appendChild(container);
    });

    const switchButton = document.createElement('button');
    switchButton.type = 'button';
    switchButton.textContent = 'Switch Device';
    switchButton.addEventListener('click', () => {
        const checked = group.querySelector('input[name="renderingDevice"]:checked');
        if (checked)
            native.renderingDevice(parseInt(checked.value));
    });
    group.appendChild(switchButton);
}

// -------- shader --------

// the built-in effects: .frag files in the plugin's effects folder (plus the current shader, if uploaded)
function renderShaderPicker(builtinShaders, currentName, shadersDir) {
    const picker = document.querySelector('#shader .picker');
    picker.replaceChildren();

    const label = document.createElement('label');
    label.textContent = 'Built-in effects';
    label.title = `Add your own .frag files to ${shadersDir}, then refresh`;

    const select = document.createElement('select');
    const names = builtinShaders.includes(currentName) ? builtinShaders : [...builtinShaders, currentName];
    for (const name of names) {
        const option = document.createElement('option');
        option.value = name;
        option.textContent = builtinShaders.includes(name) ? name : `${name} (uploaded)`;
        option.selected = name === currentName;
        select.appendChild(option);
    }
    select.addEventListener('change', () => {
        if (builtinShaders.includes(select.value)) {
            setShaderStatus(`Loading ${select.value}...`, false);
            native.shaderSelect(select.value);
        }
    });

    const refreshButton = document.createElement('button');
    refreshButton.type = 'button';
    refreshButton.textContent = '↻';
    refreshButton.title = 'Rescan the effects folder';
    refreshButton.addEventListener('click', () => native.refresh());

    const folder = document.createElement('small');
    folder.textContent = shadersDir;

    picker.append(label, select, refreshButton, folder);
}

// uploads a shader file from anywhere
function renderShaderUploader() {
    const uploader = document.querySelector('#shader .uploader');
    uploader.replaceChildren();

    const container = document.createElement('div');
    container.classList.add('input-file-container');

    const selector = document.createElement('div');
    selector.classList.add('selector-container');

    const fileInput = document.createElement('input');
    fileInput.type = 'file';
    fileInput.accept = '.glsl,.frag';

    const submitButton = document.createElement('button');
    submitButton.type = 'button';
    submitButton.textContent = 'Upload';
    submitButton.disabled = true;

    fileInput.addEventListener('change', () => {
        submitButton.disabled = fileInput.files.length === 0;
    });
    submitButton.addEventListener('click', () => {
        submitButton.disabled = true;
        setShaderStatus(`Loading ${fileInput.files[0].name}...`, false);
        native.shaderUpload(fileInput.files[0]).catch(error => setShaderStatus(String(error), true));
    });

    selector.append(fileInput, submitButton);
    container.append(selector);
    uploader.appendChild(container);
}

// the last shader action's outcome; kept across snapshots
function setShaderStatus(text, isError) {
    const status = document.getElementById('shaderStatus');
    status.textContent = text;
    status.classList.toggle('error', isError);
}
