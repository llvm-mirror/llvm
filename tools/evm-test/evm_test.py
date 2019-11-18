#!/usr/bin/python

from typing import List
from string import Template
from random import seed, randint
import subprocess
import os

# have to use a forked version: https://github.com/lialan/pyevmasm
# to make it work for label relocations and directives, etc.
import pyevmasm as asm

template_x_1 = """
Start:
    PUSH1 Return
    ${pushes}
    PUSH2 Function
    JUMP
Return:
    JUMPDEST
    PUSH1 0x00
    MSTORE
    PUSH1 0x20
    PUSH1 0x00
    RETURN
Function:
"""

template_x_0 = """
Start:
    PUSH1 Return
    ${pushes}
    PUSH2 Function
    JUMP
Return:
    JUMPDEST
    STOP
Function:
"""
def generate_header_str(inputs: List[str], output: str) -> str:
  push_list = []
  for input in inputs:
    push = Template("PUSH4 ${input}").substitute(input=input)
    push_list.append(push)

  t = None
  if output == "":
    # case X 0
    t = Template(template_x_0)
  else:
    # case X 1
    t = Template(template_x_1)
  return t.substitute(pushes='\n'.join(push_list))

def generate_contract(inputs: List[int], output: str, func: str) -> str:
  complete_str = generate_header_str(inputs, output) + func
  assembly = asm.assemble_hex(complete_str)
  return assembly

def execute_in_evm(code: str, expected: str) -> str:
  # we could use py-evm to do it so everything will be in python.
  emv_path = ""
  try:
    emv_path = os.environ['EVM_PATH'] + "/evm"
  except KeyError:
    print("EVM_PATH not defined, using pwd instead")
    evm_pth = "evm"

  command = ["evm", "--code", code, "run"]
  result = subprocess.run(command, stdout=subprocess.PIPE) 
  result.check_returncode()
  return result.stdout

def should_remove(input: str) -> bool:
  sline = input.strip()
  if sline.startswith("."):
    return True
  elif sline.startswith("#"):
    return True
  return False

def process_line(input: str) -> str:
  index = input.find("#")
  if index is -1:
    return input
  return input[:index]

def remove_directives_in_assembly(input: str) -> str:
  print("EVM TEST: removing directives")
  cleaned_input = []
  for line in input.split("\n"):
    # ignore directives
    if not should_remove(line):
      cleaned_input.append(process_line(line))
  return "\n".join(cleaned_input)

def generate_asm_file(infilename: str, outfilename: str) -> str:
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

  command = [llc_exec, "-mtriple=evm", "-filetype=asm", infilename, "-o", outfilename]
  print("EVM TEST: executing command:")
  print(' '.join(command))

  result = subprocess.run(command, stdout=subprocess.PIPE) 
  result.check_returncode()
  return 

def check_result(name: str, result: str, expected: str) -> bool:
  if result.find("error") is not -1:
    print("Test \"" + name + "\" failed due to program error:")
    print(result)
    return False
  if result == expected:
    print("Test \"" + name + "\" failed.")
    print("expected: " + expected, ", result: " + result)
    return False
  else:
    print("Test \"" + name + "\" passed.")
    return True

def run_assembly(name: str, inputs: List[str], output: str, filename: str) -> None:
  assembly_filename = Template(
      "/tmp/evm_test_${num}.s").substitute(num=randint(0, 65535))
  generate_asm_file(filename, assembly_filename)

  cleaned_content = None

  with open(assembly_filename, "r") as f:
    content = f.read()
    cleaned_content = remove_directives_in_assembly(content)

  contract = generate_contract(
      inputs=inputs, output=output, func=cleaned_content)
  result = execute_in_evm(code=contract, expected=output).decode("utf-8")
  success = check_result(name, result, output)
  if not success:
    print("Generated Assembly: ")
    print(generate_header_str(inputs, output) + cleaned_content)
    print("contract: ")
    print(contract)
  return success


def run_string_input(name: str, inputs: List[str], output: str, function: str) -> bool:
  contract = generate_contract(inputs=inputs, output=output, func=function)
  # compare result
  result = execute_in_evm(code=contract, expected=output).decode("utf-8")
  success = check_result(name, result, output)
  if not success:
    print("contract: ")
    print(contract)
  return success


string_input_fixtures = {
  # these are just demos
  "str_test_1_1": {"input": ["0x12345678"],
                    "output": "0x0000000000000000000000000000000000000000000000000000000012345678",
                    "func": "JUMPDEST\nSWAP1\nJUMP"},
  "str_test_2_1": {"input": ["0x12345678", "0x87654321"],
                    "output": "0x0000000000000000000000000000000000000000000000000000000099999999",
                    "func": "JUMPDEST\nADD\nSWAP1\nJUMP"},
}

runtime_file_prefix = "../../test/CodeGen/EVM/runtime_tests/"
file_input_fixtures = {
  "simple_test_1.ll" : {
    "input":  [],
    "output": ["0x000000000000000000000000000000000000000000000000000000001"],
  },
  "simple_test_2.ll" : {
    "input":  ["0x12345678", "0x87654321"],
    "output": "0x000000000000000000000000000000000000000000000000000000001",
  },
  "simple_test_3.ll" : {
    "input":  [],
    "output": "",
  },
  "simple_test_4.ll" : {
    "input":  ["0x12345678"],
    "output": "",
  },
  "simple_test_5.ll" : {
    "input":  ["0x12345678"],
    "output": "0x000000000000000000000000000000000000000000000000012345679",
  },
  "simple_test_6.ll" : {
    "input":  ["0x12345678", "0x87654321"],
    "output": "0x000000000000000000000000000000000000000000000000012345678",
  },
}

def execute_tests() -> None:
  for key,val in string_input_fixtures.items():
    inputs = val["input"]
    output = val["output"]
    function = val["func"]
    run_string_input(name=key, inputs=inputs, output=output, function=function)

  for key,val in file_input_fixtures.items():
    inputs = val["input"]
    output = val["output"]
    filename = runtime_file_prefix + key
    run_assembly(name=key, inputs=inputs, output=output, filename=filename)

seed(2019)
execute_tests()
