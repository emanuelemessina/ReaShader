/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

/*

// disable for this piece

#pragma warning(push)
#pragma warning(disable : CODE)

...

#pragma pop

---------------------

// disable for this line

#pragma warning(suppress : CODE)
	...

*/

/* COMPILER WARNING CODES*/

#define W_PTR_MIGHT_BE_NULL 6011

// ex: C4996: '_vsnprintf': This function or variable may be unsafe. Consider using _vsnprintf_s instead. To disable
// deprecation, use _CRT_SECURE_NO_WARNINGS.
#define W_FUNC_MAY_BE_UNSAFE 4996

// ex: Warning C26451 Arithmetic overflow: Using operator '-' on a 4 byte value and then casting the result to a 8 byte
// value. Cast the value to the wider type before calling operator '-' to avoid overflow (io.2).
#define W_RESULT_OF_ARITHMETIC_OPERATION_CAST_TO_LARGER_SIZE 26451

// ex:  warning C4996: '_vsnprintf': This function or variable may be unsafe. Consider using _vsnprintf_s instead. To
// disable deprecation, use _CRT_SECURE_NO_WARNINGS. See online help for details.
#define W_FUNC_UNSAFE 4996