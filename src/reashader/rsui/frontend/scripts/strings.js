/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

export function camelCaseToTitleCase(input) {
    return input.replace(/([A-Z])/g, ' $1')
        .replace(/^./, function (str) { return str.toUpperCase(); });
}

export function utf8_to_utf16(utf8) {
    return decodeURIComponent(escape(utf8))
}
export function utf16_to_utf8(utf16) {
    return unescape(encodeURIComponent(utf16))
}

export function utf16_to_b64(utf16) {
    return btoa(utf16_to_utf8(utf16))
}
export function b64_to_utf16(b64) {
    return utf8_to_utf16(atob(b64))
}