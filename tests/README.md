# OB-UDPST Functional Testing

The following framework is provided for running automated functional tests on OB-UDPST.
The basic premise of the automated testing is through the use of containers in
combination with NetEm.  The server container runs udpst as the server process, after a
NetEm configuration is applied to the container virtual Ethernet interface, thus setting
the downstream network emulation parameters.  This process is repeated on the client
container for upstream.  The udpst client then runs the speed-test over the connection
between the two containers, measuring the performance of the network, including any
emulated WAN settings (i.e. rate, loss, jitter, etc.).

This framework allows developers to define test cases through the `test_cases.yaml`
file.  Each test case definition provides three key components:

* A set of UDPST command line flags for the service and client (Note 1)
* A set of NetEm configuration values for upstream and downstream
* A set of metrics to evaluate the measurements from UDPST

## Requirements

The server and client commands **MUST** include the appropriate flags to run as the
expected mode. For example, the `client-cli` command flags **MUST** include `"-d"` or
`"-d server"` to run in the client mode. Additionally the `client-cli` value **MUST**
include `"-f -json"` if metrics are defined for a specific test case within `test_cases.yaml`

The system running the test must provide a set of requirements, including:

* docker
* docker compose v2
* python
* python modules from requirements.txt

An example bash script `setup.sh` to create the python virtual environment and install
the requirements.

## Defining the Tests

**NOTE:** At this time there is no schema validation enforced for the `test_cases.yaml`
file prior to parsing the `key: value` pairs.

The information model for a test case:
```yml
---
- case-1: 
    client-cli: "client cli command options"
    server-cli: "server cli command options"
    netem: # full set of netem options, will not be a valid netem command
      downstream:
        limit: 1 # in packets
        delay: 
          params:
            - 2 # in ms
            - 3 # in ms, jitter (optional)
            - 4 # in percent, correlation (optional)
          distribution: normal # { uniform | normal | pareto |  paretonormal } (optional)
          reorder: 
            params:
              - 5 # in percent (optional)
              - 6 # in percent, correlation (optional)
            gap: 7  # as an integer (optional) 
        loss:
          params: ecn # flag to use Explicit Congestion Notification
          random: 8 # in percent 
          state: 
            - 9 # in percent, p13
            - 10 # in percent, p31 (optional)
            - 11 # in percent, p32 (optional)
            - 12 # in percent, p23 (optional)
            - 13 # in percent, p14 (optional)
          gemodel:
            - 14 # in percent, p
            - 15 # in percent, r (optional)
            - 16 # in percent, 1-h (optional)
            - 17 # in percent, 1-k (optional)
        corrupt: 
          - 18 # in percent
          - 19 # in percent, correlation (optional)
        duplicate: 
          - 20 # in percent
          - 21 # in percent, correlation (optional)
        rate:
          - 22 # in bits per second
          - 23 # in bytes (can be negative), packetoverhead (optional)
          - 24 # as an unsigned integer, cellsize (optional)
          - 25 # as an integer (can be negative), celloverhead (optional)
        slot:
          params:
            - 26 # assume default is in us, min_delay
            - 27 # assume default is in us, max_delay, (optional)
          distribution: 
            - normal # { uniform | normal | pareto |  paretonormal | custom} (optional)
            - 28 # in ms, delay
            - 29 # in ms, jitter
          packets: 30 # in packets, (optional)
          bytes: 31 # in bytes, (optional)
      upstream: 
        limit: 10 # in packets
        delay:
          params:
            - 100 # in ms
            - 10 # in ms, jitter (optional)
            - 1 # in percent, correlation (optional)
          distribution: normal # { uniform | normal | pareto |  paretonormal } (optional)
          reorder:
            params: 
              - 25 # in percent
              - 75 # in percent, correlation (optional)
            gap: 4  # as an integer (optional) 
        loss:
          params: ecn # flag to use Explicit Congestion Notification
          random: 0.1 # in percent 
          state: 
            - 1 # in percent, p13
            - 5 # in percent, p31 (optional)
            - 10 # in percent, p32 (optional)
            - 50 # in percent, p23 (optional)
            - 2 # in percent, p14 (optional)
          gemodel:
            - 1 # in percent, p
            - 5 # in percent, r (optional)
            - 10 # in percent, 1-h (optional)
            - 10 # in percent, 1-k (optional)
        corrupt: 
          - 1 # in percent
          - 3 # in percent, correlation (optional)
        duplicate: 
          - 1 # in percent
          - 3 # in percent, correlation (optional)
        rate:
          - 1 # in bits per second
          - 2 # in bytes (can be negative), packetoverhead (optional)
          - 3 # as an unsigned integer, cellsize (optional)
          - 5 # as an integer (can be negative), celloverhead (optional)
        slot:
          params:
            - 800 # assume default is in us, min_delay
            - 1000 # assume default is in us, max_delay, (optional)
          distribution: 
            - normal # { uniform | normal | pareto |  paretonormal | custom} (optional)
            - 100 # in ms, delay
            - 10 # in ms, jitter
          packets: 10 # in packets, (optional)
          bytes: 3 # in bytes, (optional)
    metrics:
      loss_lessthan_10: results["Output"]["Summary"]["DeliveredPercent"] > 90.0
      rtt_range_consistent: results["Output"]["Summary"]["RTTMax"] - results["Output"]["Summary"]["RTTMin"] < 1.0
...
```

### Test Cases

All test case keys should be unique and provide some context as to what the test case is
testing for readability after all tests are completed.

### CLI Options

Strings defining the client and server CLI commands, per UDPST.

### NetEm Options

For a detailed description of NetEm options see [tc-netem(8) — Linux manual page](https://man7.org/linux/man-pages/man8/tc-netem.8.html "NetEm man page") and [networking:netem [Wiki]](https://wiki.linuxfoundation.org/networking/netem "Linux Foundation NetEm Wiki"). 

It should be noted that while all portions of the `netem` key can be filled in, there are
some combinations of `key: value` pairs that will result in invalid NetEm command
strings. i.e. specifying a `reorder` value without specifying any `delay` values. See the
**NOTE** in **Defining the Tests** above.

If any of the `netem` keys inside of the `test_cases.yaml` are listed but empty this will
result in an invalid NetEm command and the test will fail.

#### Assumptions

In order to simplify parsing the YAML certain assumptions were made with regard to the
default units of NetEm commands. If these assumptions are incorrect an update may be
necessary. These assumptions are as follows:

* The `min_delay` and `max_delay` parameters for `slot` are assumed to be in *&mu;s* 

There is the option to include the units in the yaml (i.e. `slot: -800us`), but it would
making referencing that configuration value from a metric almost impossible.

#### Options Not Supported

NetEm options that require loading files are **not supported** at this time.

### Metrics

The `metrics` test case key contains a dictionary of named tests to apply to a given case.
Each entry is expected to be a Python expression that evaluates to a boolean value. If
the metric evaluates to `True`, that metric is considered to be met and will be
interpreted by PyTest as a PASS. An exception or falsely value is considered a failure. A
logical AND operation is applied across multiple metrics defined within a single test
case.

The context that a metric expression is evaluated in contains a `results` dictionary that
is the loaded result of the JSON output from UDP-ST and a `test_case` dictionary that is
the loaded test case from `test_cases.yaml`

An example of the `results` dictionary is shown here:

```python
results = 
{
    'IPLayerMaxConnections': 1,
    'IPLayerMaxIncrementalResult': 3600,
    'IPLayerCapSupported': {'SoftwareVersion': '7.5.0',
                            'ControlProtocolVersion': 9,
                            'Metrics': 'IPLR,Sampled_RTT,IPDV,IPRR,RIPR'},
    'Input': {
        'Interface': '',
        'Role': 'Receiver',
        'Host': 'server',
        'Port': 50639,
        'HostIPAddress': '172.23.0.2',
        'ClientIPAddress': '172.23.0.3',
        'ClientPort': 43557,
        'JumboFramesPermitted': 1,
        'NumberOfConnections': 1,
        'DSCP': 0,
        'ProtocolVersion': 'Any',
        'UDPPayloadMin': 48,
        'UDPPayloadMax': 8972,
        'UDPPayloadDefault': 1222,
        'UDPPayloadContent': 'zeroes',
        'TestType': 'Search',
        'IPDVEnable': 0,
        'IPRREnable': 1,
        'RIPREnable': 1,
        'PreambleDuration': 0,
        'StartSendingRateIndex': 0,
        'SendingRateIndex': -1,
        'NumberTestSubIntervals': 10,
        'NumberFirstModeTestSubIntervals': 0,
        'TestSubInterval': 1000,
        'StatusFeedbackInterval': 50,
        'TimeoutNoTestTraffic': 1000,
        'TimeoutNoStatusMessage': 1000,
        'Tmax': 1000,
        'TmaxRTT': 3000,
        'TimestampResolution': 1,
        'SeqErrThresh': 10,
        'ReordDupIgnoreEnable': 0,
        'LowerThresh': 30,
        'UpperThresh': 90,
        'HighSpeedDelta': 10,
        'SlowAdjThresh': 3,
        'HSpeedThresh': 1000000000,
        'RateAdjAlgorithm': 'B',
        },
    'Output': {
        'BOMTime': '2022-08-16T20:39:40.410973Z',
        'TmaxUsed': 1000,
        'TestInterval': 10,
        'TmaxRTTUsed': 3000,
        'TimestampResolutionUsed': 1,
        'Summary': {
            'DeliveredPercent': 100,
            'LossRatioSummary': 0,
            'ReorderedRatioSummary': 0,
            'ReplicatedRatioSummary': 0,
            'LossCount': 0,
            'ReorderedCount': 0,
            'ReplicatedCount': 0,
            'PDVMin': 0,
            'PDVAvg': 0,
            'PDVMax': 0.002,
            'PDVRangeSummary': 0.002,
            'RTTMin': 0,
            'RTTMax': 0.001,
            'RTTRangeSummary': 0.001,
            'IPLayerCapacitySummary': 1943.34,
            'InterfaceEthMbps': 0,
            'MinOnewayDelaySummary': 0,
            'MinRTTSummary': 0,
            },
        'AtMax': {
            'Mode': 1,
            'Intervals': 10,
            'TimeOfMax': '2022-08-16T20:39:50.472678Z',
            'DeliveredPercent': 100,
            'LossRatioAtMax': 0,
            'ReorderedRatioAtMax': 0,
            'ReplicatedRatioAtMax': 0,
            'LossCount': 0,
            'ReorderedCount': 0,
            'ReplicatedCount': 0,
            'PDVMin': 0,
            'PDVAvg': 0,
            'PDVMax': 0.002,
            'PDVRangeAtMax': 0.002,
            'RTTMin': 0,
            'RTTMax': 0.001,
            'RTTRangeAtMax': 0.001,
            'MaxIPLayerCapacity': 4114.99,
            'InterfaceEthMbps': 0,
            'MaxETHCapacityNoFCS': 4121.4,
            'MaxETHCapacityWithFCS': 4132.37,
            'MaxETHCapacityWithFCSVLAN': 4134.2,
            'MinOnewayDelayAtMax': 0,
            },
        'ModalResult': [],
        'EOMTime': '2022-08-16T20:39:50.923480Z',
        'Status': 'Complete',
        },
    'ErrorStatus': 0,
    'ErrorMessage': '',
    }
```

An example of the `test_case` dictionary is shown here:

```python
test_case = 
{'case-2': {
    'client-cli': '-s -f json -d server',
    'server-cli': '-s -v',
    'netem': {
      'downstream': {
        'delay': {
          'params': [100]}, 
          'rate': [1]},
      'upstream': {
        'delay': {
          'params': [100]}, 
          'rate': [1]}},
    'metrics': {
      'reordered_lessthan_delivered': 'results["Output"]["Summary"]["DeliveredPercent"] / 100 > results["Output"]["Summary"]["ReorderedRatioSummary"]'},
    }}
```

The context that a metric expression is evaluated in provides access to any vanilla
Python functions, the Python [`math`](https://docs.python.org/3/library/math.html "Python math library doc page")
library, as well as two additional functions to evaluate if a result is within a
certain range or percent.

These two additional functions have the following signatures:

```python
def within_range(actual_value, min_val, max_val)

def within_percent(actual_value, reference_value, percent_delta)
```

Some example metric expressions (which may not be specifically useful, but are valid) are
given here:

```yaml
metrics:
  within_good_pct: within_percent(results["Output"]["Summary"]["RTTMax"], 0.05, 10)
  within_good_rng: within_range(results["Output"]["Summary"]["LossCount"], test_case[list(test_case.keys()[0])]["netem"]["downstream"]["corrupt"][0] - 10, test_case[list(test_case.keys()[0])]["netem"]["downstream"]["corrupt"][0] + 10)
  loss_lessthan_10: results["Output"]["Summary"]["DeliveredPercent"] > 90.0
  range_not_nan: not math.isnan(results["Output"]["Summary"]["PDVRangeSummary"])
```

## Running the Tests

Run `pytest` from the `udpst\tests` directory.

To generate the junit XML output required for bamboo and other task runners, the
command line flag `--junitxml=output.xml` can be used.
