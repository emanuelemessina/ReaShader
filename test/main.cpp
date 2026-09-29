/**
 * @file
 * @brief ReaShader's test application: doctest's runner, plus the shared GPU instance's teardown.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

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
