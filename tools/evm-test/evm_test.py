#!/usr/bin/python

from typing import List
from string import Template
from random import seed, randint
import subprocess
import os
import json

import evm_testsuite

def execute_with_input_in_evm(code: str, input: str, expected: str) -> bool:
    def check_result(command: str, result: str, exp: str) -> bool:
        if result.find("error") is not -1:
            print("Failed.")
            print(result)
            print(command)
            return False

        result_int = int(result, base=16)
        exp_int = int(exp, base=16)
        if result_int == exp_int:
            print("Passed.")
            return True
        else:
            print("Failed.", end="")
            print(" Expected: " + exp + ", result: " + result)
            return False

    # we could use py-evm to do it so everything will be in python.
    emv_path = ""
    try:
        emv_path = os.environ['EVM_PATH'] + "/evm"
    except KeyError:
        #print("EVM_PATH not defined, using pwd instead")
        evm_path = "evm"

    command = []

    if (len(input)) == 0:
        command = [evm_path, "--code", code, "run"]
    else:
        command = [evm_path, "--input", input, "--code", code, "run"]

    result = subprocess.run(command, stdout=subprocess.PIPE)
    success = check_result(' '.join(command), result.stdout.decode("utf-8"), exp = expected)
    return success

def generate_obj_file(infilename: str, outfilename: str) -> None:
    defined_llc = False
    llc_path = None
    key = 'LLC_PATH'
    try:
        llc_path = os.environ[key]
    except KeyError:
        #print("LLC_PATH not defined, using $PATH instead")
        pass

    llc_exec = None
    if defined_llc:
        llc_exec = llc_path + "/llc"
    else:
        llc_exec = "llc"

    command = [llc_exec, "-mtriple=evm",
               "-filetype=obj", infilename, "-o", outfilename]

    result = subprocess.run(command, stdout=subprocess.PIPE)
    result.check_returncode()
    return

def run_binary(name: str, inputs: List[str], output: str, filename: str) -> bool:
    def concat_inputs(inputs: List[int]) -> str:
        # pad to 32bytes.
        input_strs = [] 
        for input in inputs:
            input_str = "{:064x}".format(int(input, 16))
            input_strs.append(input_str)
        return ''.join(input_strs)

    def get_contract(inputfile: str) -> str:
        import binascii
        output = []
        with open(inputfile, 'rb') as file:
            byte = file.read()
            output.append(binascii.hexlify(byte).decode("utf-8"))
        output_str = ''.join(output)
        return output_str

    print("Executing test: \"" + name + "\": ", end="")
    object_filename = Template(
        "/tmp/evm_test_${num}.o").substitute(num=randint(0, 65535))
    generate_obj_file(filename, object_filename)

    contract_code = get_contract(object_filename)
    input = concat_inputs(inputs)

    success = execute_with_input_in_evm(code=contract_code, input=input, expected=output)
    return success

def run_testset(testset) -> List[str]:
    failed_tests = []
    for key, val in testset.items():
        file = val["file"]
        inputs = val["input"]
        output = val["output"]
        filename = evm_testsuite.runtime_file_prefix + file
        result = run_binary(name=key, inputs=inputs,
                            output=output, filename=filename)
        if not result:
            failed_tests.append(key)
    return failed_tests

def binary_tests() -> List[str]:
    failed_tests = []
    for tests in evm_testsuite.test_suite:
        failed_tests += run_testset(tests)
    return failed_tests

def print_failed(tests: List[str]) -> None:
    print("The following test cases are failing:")
    for t in tests:
        print("    " + t)

def execute_tests() -> None:
    for key, val in evm_testsuite.string_input_fixtures.items():
        inputs = val["input"]
        output = val["output"]
        function = val["func"]
    failed_tests = binary_tests()
    print_failed(failed_tests)


seed(2019)
execute_tests()
