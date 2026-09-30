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
    renderChain(snapshot.chain, snapshot.params, snapshot.shaders, snapshot.luts);
    renderAddRow('shader', snapshot.shaders);
    renderAddRow('lut', snapshot.luts);
    renderDevices(snapshot.devices.names, snapshot.devices.selected);
    renderAbout(snapshot.version, snapshot.logo);
}

// -------- params --------

// `label` is shown without the node's name, which its card already shows
function createSlider(param, label) {
    const container = document.createElement('div');
    container.classList.add('slider-container');

    const title = document.createElement('label');
    title.textContent = label;
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

// -------- the chain --------

// What differs between the kinds of node: shaders (compiled on upload) and LUTs (parsed on upload)
const KINDS = {
    shader: {
        title: 'Shader',
        accept: '.glsl,.frag',
        add: '+ Add a shader...',
        uploadTitle: 'Upload a .frag shader: it is compiled, stored, and added to the chain',
        hint: 'Upload a .frag shader with the folder button: it is compiled, stored, and added to the chain.',
        uploading: name => `Compiling ${name}...`,
        upload: file => native.shaderUpload(file),
    },
    lut: {
        title: 'LUT',
        accept: '.cube',
        add: '+ Add a LUT...',
        uploadTitle: 'Upload a .cube LUT: it is stored and added to the chain',
        hint: 'Upload a .cube LUT with the folder button: it is stored and added to the chain.',
        uploading: name => `Reading ${name}...`,
        upload: file => native.lutUpload(file),
    },
};

// one card per node, in order, each with its sliders (params whose node is the card's)
function renderChain(chain, params, shaders, luts) {
    const container = document.querySelector('#chain .nodes');
    container.replaceChildren();

    if (chain.length === 0) {
        const empty = document.createElement('p');
        empty.classList.add('empty');
        empty.textContent = 'No shaders or LUTs: video passes through. Add one below.';
        container.appendChild(empty);
        return;
    }

    chain.forEach((node, index) => {
        const nodeParams = params.filter(param => param.node === node.uid);
        container.appendChild(createNodeCard(node, index, chain.length, nodeParams,
            node.kind === 'shader' ? shaders : luts, luts));
    });
}

function createNodeCard(node, index, count, params, names, luts) {
    const card = document.createElement('div');
    card.classList.add('node');
    card.classList.toggle('bypassed', node.bypass);

    // header: position, kind, the stored shader or LUT (swappable), move, bypass, remove
    const header = document.createElement('div');
    header.classList.add('node-header');

    const position = document.createElement('span');
    position.classList.add('position');
    position.textContent = index + 1;

    const kind = document.createElement('span');
    kind.classList.add('kind');
    kind.textContent = KINDS[node.kind].title;

    const content = createSelect(names, node.name);
    content.title = `Another ${KINDS[node.kind].title.toLowerCase()} for this node`;
    content.addEventListener('change', () => {
        setStatus(`Loading ${content.value}...`, 'busy');
        native.nodeSet(node.uid, content.value);
    });

    const up = createIconButton('↑', 'Move up', () => native.nodeMove(node.uid, index - 1));
    up.disabled = index === 0;
    const down = createIconButton('↓', 'Move down', () => native.nodeMove(node.uid, index + 1));
    down.disabled = index === count - 1;

    const bypass = document.createElement('span');
    bypass.classList.add('bypass');
    const checkbox = document.createElement('input');
    checkbox.type = 'checkbox';
    checkbox.id = `bypass_${node.uid}`;
    checkbox.checked = node.bypass;
    checkbox.addEventListener('change', () => native.nodeBypass(node.uid, checkbox.checked));
    const checkboxLabel = document.createElement('label');
    checkboxLabel.htmlFor = checkbox.id;
    checkboxLabel.textContent = 'Bypass';
    bypass.append(checkbox, checkboxLabel);

    const remove = createIconButton('×', 'Remove from the chain', () => native.nodeRemove(node.uid));
    remove.classList.add('remove');

    header.append(position, kind, content, up, down, bypass, remove);

    // body: a shader's LUT, then the sliders
    const body = document.createElement('div');
    body.classList.add('node-body');

    if (node.kind === 'shader') {
        const row = document.createElement('div');
        row.classList.add('node-lut');
        const label = document.createElement('label');
        label.textContent = 'LUT (iLut)';
        const select = createSelect(luts, node.lut, 'None');
        label.htmlFor = select.id = `lut_${node.uid}`;
        select.addEventListener('change', () => native.nodeLut(node.uid, select.value));
        row.append(label, select);
        body.appendChild(row);
    }

    const sliders = document.createElement('div');
    sliders.classList.add('params');
    const prefix = `${node.name}: `;
    for (const param of params)
        sliders.appendChild(createSlider(param, param.label.startsWith(prefix) ? param.label.slice(prefix.length) : param.label));
    body.appendChild(sliders);

    card.append(header, body);
    return card;
}

// a stored shader or LUT to add to the chain, and a button that uploads a new one (and adds it)
function renderAddRow(kind, names) {
    const config = KINDS[kind];
    const row = document.querySelector(`#chain .add.${kind}`);
    row.replaceChildren();

    const select = createSelect(names, '', config.add);
    select.addEventListener('change', () => {
        if (!select.value)
            return;
        setStatus(`Adding ${select.value}...`, 'busy');
        native.nodeAdd(kind, select.value);
        select.value = '';
    });

    // upload: starts as soon as a file is picked (a cancelled dialog picks none)
    const fileInput = document.createElement('input');
    fileInput.type = 'file';
    fileInput.accept = config.accept;
    fileInput.hidden = true;
    fileInput.addEventListener('change', () => {
        const file = fileInput.files[0];
        if (!file)
            return;
        setStatus(config.uploading(file.name), 'busy');
        config.upload(file).catch(error => setStatus(String(error), 'error'));
    });

    const uploadButton = createIconButton('', config.uploadTitle, () => {
        fileInput.value = ''; // picking the same file again still uploads it
        fileInput.click();
    });
    uploadButton.innerHTML = FOLDER_ICON;

    row.append(fileInput, select, uploadButton);

    // shown only while nothing is stored
    if (names.length === 0) {
        const hint = document.createElement('small');
        hint.textContent = config.hint;
        row.appendChild(hint);
    }
}

// `names` as options, `current` selected (listed even if it's not stored here, e.g. from another
// machine's project), and an optional first option with value ""
function createSelect(names, current, first) {
    const select = document.createElement('select');
    const options = first !== undefined ? [['', first]] : [];
    for (const name of names)
        options.push([name, name]);
    if (current && !names.includes(current))
        options.push([current, current]);
    for (const [value, text] of options) {
        const option = document.createElement('option');
        option.value = value;
        option.textContent = text;
        select.appendChild(option);
    }
    select.value = current;
    return select;
}

function createIconButton(text, title, onClick) {
    const button = document.createElement('button');
    button.type = 'button';
    button.classList.add('icon-button');
    button.title = title;
    button.textContent = text;
    button.addEventListener('click', onClick);
    return button;
}

const FOLDER_ICON = '<svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true">' +
    '<path fill="currentColor" d="M10 4H4a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-8l-2-2z"/></svg>';

// the last chain action's outcome, kept across snapshots; state: "busy" (spinner), "ok" or "error"
function setStatus(text, state) {
    const status = document.querySelector('#chain .status');
    status.querySelector('.text').textContent = text;
    status.className = `status ${state}`;
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
