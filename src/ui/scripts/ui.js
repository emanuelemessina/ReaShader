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
    renderPicker('shader', snapshot.shaders, snapshot.shader.name);
    renderPicker('lut', snapshot.luts, snapshot.lut.name);
    renderLutModes(snapshot.lut.mode);
    renderAbout(snapshot.version, snapshot.logo);
}

// -------- params --------

// sliders in the fieldset of their group: the shader's params, and the LUT's (LUT Mix);
// the "main" group (Audio Gain) is host-only
function renderParams(params) {
    const containers = {
        shader: document.querySelector('#shader .params'),
        lut: document.querySelector('#lut .params'),
    };
    Object.values(containers).forEach(container => container.replaceChildren());
    for (const param of params) {
        const container = containers[param.group];
        if (container)
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

// -------- shader and LUT pickers --------

// What differs between the pickers of uploaded items: shaders (compiled on upload) and LUTs (parsed on upload)
const PICKERS = {
    shader: {
        accept: '.glsl,.frag',
        uploadTitle: 'Upload a .frag shader',
        none: 'None (passthrough)',
        hint: 'Upload a .frag shader with the folder button to compile it and add it to this list.',
        uploading: name => `Compiling ${name}...`,
        upload: file => native.shaderUpload(file),
        select: name => native.shaderSelect(name),
    },
    lut: {
        accept: '.cube',
        uploadTitle: 'Upload a .cube LUT',
        none: 'None',
        hint: 'Upload a .cube LUT with the folder button to add it to this list.',
        uploading: name => `Reading ${name}...`,
        upload: file => native.lutUpload(file),
        select: name => native.lutSelect(name),
    },
};

// the stored items (uploaded before) plus "none", next to a button that uploads a new one
function renderPicker(kind, names, currentName) {
    const config = PICKERS[kind];
    const picker = document.querySelector(`#${kind} .picker`);
    picker.replaceChildren();

    // upload: starts as soon as a file is picked (a cancelled dialog picks none)
    const fileInput = document.createElement('input');
    fileInput.type = 'file';
    fileInput.accept = config.accept;
    fileInput.hidden = true;
    fileInput.addEventListener('change', () => {
        const file = fileInput.files[0];
        if (!file)
            return;
        setStatus(kind, config.uploading(file.name), 'busy');
        config.upload(file).catch(error => setStatus(kind, String(error), 'error'));
    });

    const uploadButton = document.createElement('button');
    uploadButton.type = 'button';
    uploadButton.classList.add('icon-button');
    uploadButton.title = config.uploadTitle;
    uploadButton.innerHTML = FOLDER_ICON;
    uploadButton.addEventListener('click', () => {
        fileInput.value = ''; // picking the same file again still uploads it
        fileInput.click();
    });

    const select = document.createElement('select');
    const none = document.createElement('option');
    none.value = '';
    none.textContent = config.none;
    select.appendChild(none);
    for (const name of names) {
        const option = document.createElement('option');
        option.value = name;
        option.textContent = name;
        select.appendChild(option);
    }
    select.value = currentName;

    // shown only while nothing is selected
    const hint = document.createElement('small');
    hint.textContent = config.hint;
    hint.hidden = select.value !== '';

    select.addEventListener('change', () => {
        hint.hidden = select.value !== '';
        setStatus(kind, select.value ? `Loading ${select.value}...` : 'Unloading...', 'busy');
        config.select(select.value);
    });

    picker.append(fileInput, uploadButton, select, hint);
}

// where the LUT applies, relative to the shader
const LUT_MODES = [
    ['before', 'Before the shader'],
    ['after', 'After the shader'],
    ['shader', 'In the shader (iLut)'],
];

function renderLutModes(current) {
    const container = document.querySelector('#lut .modes');
    container.replaceChildren();
    for (const [mode, text] of LUT_MODES) {
        const option = document.createElement('div');
        option.classList.add('radio-button-container');

        const radio = document.createElement('input');
        radio.type = 'radio';
        radio.name = 'lutMode';
        radio.id = `lutMode_${mode}`;
        radio.checked = mode === current;
        radio.addEventListener('change', () => native.lutMode(mode));

        const label = document.createElement('label');
        label.htmlFor = radio.id;
        label.textContent = text;

        option.append(radio, label);
        container.appendChild(option);
    }
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

// the last shader or LUT action's outcome (kind: "shader" or "lut"), kept across snapshots;
// state: "busy" (spinner), "ok" or "error"
function setStatus(kind, text, state) {
    const status = document.querySelector(`#${kind} .status`);
    status.querySelector('.text').textContent = text;
    status.className = `status ${state}`;
}
