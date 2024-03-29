#
# Copyright (c) 2022, Broadband Forum
# Copyright (c) 2022, UNH-IOL Communications
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from this
#    software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
#
# UDP Speed Test CI Testing - conftest.py
#
# This file provides the top level configuration and parameterization
# required by pytest, such as parsing the test-cases.yaml, and similar
# top level functionality.
#
# Author                  Date          Comments
# --------------------    ----------    ----------------------------------
# UNH-IOL Team            08/19/2022    Initial creation of the CI testing

import yaml

def pytest_addoption(parser):
    parser.addoption(
        "--udpstTestCasesYaml", action="store", default="test_cases.yaml", help="Filename of YAML test cases/"
    )

def parameterIdent(testCase):
    return next(iter(testCase))

def pytest_generate_tests(metafunc):
    if 'testCase' in metafunc.fixturenames:
        with open(metafunc.config.getoption('udpstTestCasesYaml'), 'r') as f:
            yamlTestCases =  yaml.load(f, Loader=yaml.FullLoader)
            metafunc.parametrize('testCase', yamlTestCases, ids=parameterIdent)
