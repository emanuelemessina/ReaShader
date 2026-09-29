/**
 * @file
 * @brief Builds the UI from the plugin's snapshot.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

// Rebuilding is idempotent: every snapshot replaces what the previous one created.

function displayValue(units, value) {
    return units === "%" ? (value * 100).toFixed(1) : value.toFixed(3);
}

function renderSnapshot(snapshot) {
    document.title = `${snapshot.track.number} | ${snapshot.track.name}`;
    renderParams(snapshot.params);
    renderDevices(snapshot.devices.names, snapshot.devices.selected);
    renderShaderPicker(snapshot.shaders, snapshot.shader.name);
    renderAbout(snapshot.version, snapshot.logo);
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

// the compiled shaders (uploaded before) plus "none", which passes the video through,
// next to a button that uploads a new one
function renderShaderPicker(shaders, currentName) {
    const picker = document.querySelector('#shader .picker');
    picker.replaceChildren();

    // upload: starts as soon as a file is picked (a cancelled dialog picks none)
    const fileInput = document.createElement('input');
    fileInput.type = 'file';
    fileInput.accept = '.glsl,.frag';
    fileInput.hidden = true;
    fileInput.addEventListener('change', () => {
        const file = fileInput.files[0];
        if (!file)
            return;
        setShaderStatus(`Compiling ${file.name}...`, 'busy');
        native.shaderUpload(file).catch(error => setShaderStatus(String(error), 'error'));
    });

    const uploadButton = document.createElement('button');
    uploadButton.type = 'button';
    uploadButton.classList.add('icon-button');
    uploadButton.title = 'Upload a .frag shader';
    uploadButton.innerHTML = FOLDER_ICON;
    uploadButton.addEventListener('click', () => {
        fileInput.value = ''; // picking the same file again still uploads it
        fileInput.click();
    });

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
    hint.textContent = 'Upload a .frag shader with the folder button to compile it and add it to this list.';
    hint.hidden = select.value !== '';

    select.addEventListener('change', () => {
        hint.hidden = select.value !== '';
        setShaderStatus(select.value ? `Loading ${select.value}...` : 'Unloading...', 'busy');
        native.shaderSelect(select.value);
    });

    picker.append(fileInput, uploadButton, select, hint);
}

const FOLDER_ICON = '<svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true">' +
    '<path fill="currentColor" d="M10 4H4a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-8l-2-2z"/></svg>';

// -------- about box --------

// open while the 3D logo is on: opening it turns the logo on, closing turns it off
function renderAbout(version, open) {
    document.querySelector('#about .version').textContent = version;
    document.getElementById('about').hidden = !open;
}

function setAboutOpen(open) {
    document.getElementById('about').hidden = !open;
    native.logo(open);
}

// the last shader action's outcome, kept across snapshots; state: "busy" (spinner), "ok" or "error"
function setShaderStatus(text, state) {
    const status = document.getElementById('shaderStatus');
    status.querySelector('.text').textContent = text;
    status.className = state;
}
