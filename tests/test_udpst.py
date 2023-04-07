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
# This file provides the functional test cases and supporting functions
# for running test cases.  The entry point as test_updst(testCase) is
# called once for each test case.
#
# Author                  Date          Comments
# --------------------    ----------    ----------------------------------
# UNH-IOL Team            08/19/2022    Initial creation of the CI testing

import netem_parser as np
import subprocess
import json
import os

import traceback
import pytest_check as check
import pytest

import math


"""
Test Case Iterator

Is called once by PyTest for each test case defined within the yaml
configuration file.  This function handless the following actions.

1. Parse the configuration into two valid NetEm commands for Us/Ds.
2. Use docker compose to run the test case, and collects the client output
3. Calls check_all_in_case() to evaluate all test metrics for the case.

Note, any raised exception, due to parsing json output or failed metrics,
will cause the test case to fail and PyTest to move on to the next test
case in the yaml configuration.
"""
def test_udpst(testCase, udpst_containers):
    print('Test case was: ', testCase )

    # Strip the test cast label from the object
    testCase = testCase[list(testCase.keys())[0]]

    # Parse config to generate NetEm Command Line
    netemOpts = np.getNetemOpts(testCase) # 1: upstream, 2: downstream    
    (upstreamOpts, downstreamOpts) = (netemOpts[0], netemOpts[1])
    
    #results = gather_results(upstreamOpts, downstreamOpts, testCase["client-cli"], testCase["server-cli"])
    #def gather_results(upNetem, downNetem, clientArgs, serverArgs):
    
    # Setup the environment for docker compose, to pass in the arguments to the
    # containers for test.
    if upstreamOpts != '':
        os.environ["UP_NETEM_COMMAND"] = 'tc qdisc replace dev eth0 root ' + upstreamOpts
    else:
        os.environ["UP_NETEM_COMMAND"] = ''
    if downstreamOpts != '':
        os.environ["DOWN_NETEM_COMMAND"] = 'tc qdisc replace dev eth0 root ' + downstreamOpts
    else:
        os.environ["DOWN_NETEM_COMMAND"] = ''

    os.environ["SERVER_ARGS"] = testCase["server-cli"]
    os.environ["CLIENT_ARGS"] = testCase["client-cli"]

    # Clean up any hanging previous test runs (i.e. old containers)
    subprocess.Popen(["docker", "compose", "--project-name", "udpst-testing", "down"]).wait()
    subprocess.Popen(["docker", "compose", "--project-name", "udpst-testing", "rm", "-f"]).wait()

    # Run the test.
    subprocess.Popen(["docker", "compose", "--project-name", "udpst-testing", "up", "--abort-on-container-exit"]).wait()

    # Grab the logs from the client container that contain the json output
    results = subprocess.Popen(["docker", "logs", "udpst-testing-client-1"], stderr=subprocess.PIPE, stdout=subprocess.PIPE)
    results.wait()
    err = results.stderr.read().decode()
    out = results.stdout.read().decode()
    if err != '':
        raise Exception("Container failed to run with: " + err)
    elif out == '':
        raise Exception("Container provided no output!")
    else:
        try:
            results = json.loads(out)
        except Exception as e:
            raise e
    
    # Parse and check all metrics
    check_all_in_case(testCase, results)


"""
Iterate over all metrics within the test case, and evaluate their result.

test_case: dict following input case schema (plus metrics)
container_output: results json object from udpst clinet container
"""
def check_all_in_case(test_case, container_output):
    metrics = test_case.get('metrics', {})
    if metrics is None:
        return
    for (metric_name, metric) in metrics.items():
        try:
            result = eval(
                    metric, {}, {"test_case": test_case, "results": container_output, "within_range": within_range, "within_percent": within_percent, "math": math})

            check.is_true(result, metric_name + ' : ' + metric)

            # try turning result into a bool (expr of test_text should have a bool as eval result)
        except SyntaxError:
            tb = traceback.format_exc()
            check.is_true(
                False, f"test metric was syntactically malformed: {metric_name}, traceback:\n{tb}")
        except (ValueError, KeyError):
            tb = traceback.format_exc()
            check.is_true(
                False, f"test metric referred to a key or variable that did not exist: {metric_name}, traceback:\n{tb}")
        except Exception:
            tb = traceback.format_exc()
            check.is_true(
                False, f"test metric caused an unexpected exception: {metric_name}, traceback:\n{tb}")


"""
Check value is within the range least to most, inclusively.
"""
def within_range(value, least, most):
    return value >= least and value <= most


"""
Check if the actual value is within a plus/minus delta of a reference value.
"""
def within_percent(actual_value, reference_value, percent_delta):
    return math.fabs((actual_value - reference_value) / reference_value) <= percent_delta / 100.0

"""
Pytest fixture, which is used to so we can ensure we clean up containers and images
at the conclusion of the test session.  This ensures future sessions rebuild new
container images and we don't test stale code.
"""
@pytest.fixture(scope='session')
def udpst_containers():
    project_name = "udpst-testing"
    # Pre-build our containers, 
    subprocess.Popen(["docker", "compose", "--project-name", project_name, "build"]).wait()
    yield project_name
    print("\nTesting Finished, cleaning up after test run.\n")
    subprocess.Popen(["docker", "compose", "--project-name", project_name, "rm", "-f"]).wait()
    subprocess.Popen(["docker", "image", "rm", project_name+"_client"]).wait()
    subprocess.Popen(["docker", "image", "rm", project_name+"_server"]).wait()