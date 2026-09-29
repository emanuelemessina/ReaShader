/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

// ReaShader's test application: doctest's runner, plus the shared GPU instance's teardown.
// Command line: doctest's (e.g. --test-suite=render, --test-case="*logo*", --success, --help).

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "support/support.h"

int main(int argc, char** argv)
{
	doctest::Context runner(argc, argv);
	int result = runner.run();
	test::shutdownGpu();
	return result;
}
