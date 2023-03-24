"""
Takes in a dictionary and produces a netem command string for both the upstream (client to server)
and downstream (server to client) configurations.

inputs: 
    yamlDict: a dictionary that was created from a udpst test yaml file
outputs:
    upstreamNetemStr: a string representing a netem command for the upstream configuration
    downstreamNetemStr: a string representing a netem command for the downstream configuration

NOTE: There is no syntax checking to ensure the netem command string created is a valid command
string. It is the responsibility of the individual creating the test cases to ensure the yaml
follows the right schema. Future work might employ schema validation prior to this function
being called.
"""
from types import NoneType


def getNetemOpts(case):
    if 'netem' in case.keys():
        netemOpts = case['netem']
    else:
        return ['', '']
    if netemOpts is None:
        return ['', '']

    if 'upstream' in netemOpts.keys():
        upstream = netemOpts['upstream']
        upstreamNetemStr = netemEntry(upstream)
    else:
        upstreamNetemStr = ''

    if 'downstream' in netemOpts.keys():
        downstream = netemOpts['downstream']
        downstreamNetemStr = netemEntry(downstream)
    else:
        downstreamNetemStr = ''

    return [upstreamNetemStr, downstreamNetemStr]

"""
Starts the netem command string concatenation

inputs: 
    val: a dictionary that was created from a udpst test yaml file that contains either
    upstream or downstream configurations
output: a string representing a netem command for the configuration
"""
def netemEntry(val):
    return 'netem' + parse(val)


"""
Concatenates strings to create the remainder of the netem command string

inputs: 
    val: a dictionary, list, or primitive type used to create the netem command string
output: a string representing a netem command for the configuration
"""
def parse(val):
    if type(val) is dict:
        return parseDict(val)
    elif type(val) is list:
        return parseList(val)
    else:
        return ' ' + str(val)

"""
Concatenates the entries from a dictionary to create the remainder of the netem command string

inputs: 
    val: a dictionary used to create a portion of the netem command string
output: a string representing a portion of a netem command for the configuration
"""
def parseDict(value):
    silent_keys = ['params']
    output = ''
    for (key, val) in value.items():
        if key in silent_keys:
            output += parse(val)
        else:
            output += ' ' + key + parse(val)
    return output

"""
Concatenates the entries from a list to create the remainder of the netem command string

inputs: 
    val: a list used to create a portion of the netem command string
output: a string representing a portion of a netem command for the configuration
"""
def parseList(value):
    output = ''
    for val in value:
        output += parse(val)
    return output
