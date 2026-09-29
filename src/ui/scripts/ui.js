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
    renderShaderPicker(snapshot.shaders, snapshot.shader.name);
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

// the compiled shaders (uploaded before) plus "none", which passes the video through
function renderShaderPicker(shaders, currentName) {
    const picker = document.querySelector('#shader .picker');
    picker.replaceChildren();

    const label = document.createElement('label');
    label.textContent = 'Shader';

    const select = document.createElement('select');
    const none = document.createElement('option');
    none.value = '';
    none.textContent = 'None (passthrough)';
    select.appendChild(none);
    for (const name of shaders) {
        const option = document.createElement('option');
        option.value = name;
        option.textContent = name;
        select.appendChild(option);
    }
    select.value = currentName;

    // shown only while no shader is selected
    const hint = document.createElement('small');
    hint.textContent = 'Upload a .frag shader to compile it and add it to this list.';
    hint.hidden = select.value !== '';

    select.addEventListener('change', () => {
        hint.hidden = select.value !== '';
        setShaderStatus(select.value ? `Loading ${select.value}...` : 'Unloading...', 'busy');
        native.shaderSelect(select.value);
    });

    picker.append(label, select, hint);
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
        setShaderStatus(`Compiling ${fileInput.files[0].name}...`, 'busy');
        native.shaderUpload(fileInput.files[0]).catch(error => setShaderStatus(String(error), 'error'));
    });

    selector.append(fileInput, submitButton);
    container.append(selector);
    uploader.appendChild(container);
}

// the last shader action's outcome, kept across snapshots; state: "busy" (spinner), "ok" or "error"
function setShaderStatus(text, state) {
    const status = document.getElementById('shaderStatus');
    status.querySelector('.text').textContent = text;
    status.className = state;
}
