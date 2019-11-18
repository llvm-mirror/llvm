#!/usr/bin/python

from typing import List
from string import Template
import subprocess
import os

# have to use a forked version: https://github.com/lialan/pyevmasm
# to make it work for label relocations and directives, etc.
import pyevmasm as asm

template_0_1 = """
Start:
    PUSH1 Return
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

template_1_1 = """
Start:
    PUSH1 Return
    PUSH4 ${input_value}
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

template_2_1 = """
Start:
    PUSH1 Return
    PUSH4 ${input_value_1}
    PUSH4 ${input_value_2}
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

def generate_header_str(inputs: List[int]) -> str:
  assert len(inputs) < 3
  t = ""
  if len(inputs) is 1:
    t = Template(template_1_1).substitute(input_value = inputs[0])
  elif len(inputs) is 2:
    t = Template(template_2_1).substitute(
        input_value_1 = inputs[0], input_value_2 = inputs[1])
  return t

def generate_header_binary(inputs: List[int]) -> str:
  asm.assemble_hex(generate_header_str(inputs))

def generate_function_binary(filename: str, offset: int) -> str:
  assert offset > 0
  # being lazy
  pass

def generate_contract(inputs: List[int], func: str) -> str:
  assert len(inputs) < 3
  complete_str = generate_header_str(inputs) + func
  f=open("./generated_contract.s", "w")
  f.write(complete_str)
  f.close()
  return asm.assemble_hex(complete_str)

def execute_in_evm(code: str, expected: str) -> str:
  # we could use py-evm to do it so everything will be in python.
  emv_path = ""
  try:
    emv_path = os.environ['EVM_PATH']
  except KeyError(key):
    print("\"" + key + "\" not defined, using pwd instead")

  command = [emv_path + "/evm", "--code", code, "run"]
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
    print("LLC_PATH not defined, using $PATH instead")
  
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


def run_assembly(name: str, inputs: List[str], output: str, filename: str) -> None:
  assembly_filename = filename + ".s"
  generate_asm_file(filename, assembly_filename)

  cleaned_content = None
  with open(assembly_filename, "r") as f:
    content = f.read()
    cleaned_content = remove_directives_in_assembly(content)
  os.remove(assembly_filename)

  contract = generate_contract(
      inputs=inputs, func=cleaned_content)

  try:
    execute_in_evm(code=contract, expected=output)
  except:
    raise Error("Running test error: " + name)

def run_string_input(name: str, inputs: List[str], output: str, function: str) -> bool:
  contract = generate_contract(inputs=inputs, func=function)
  # compare result
  result = execute_in_evm(code=contract, expected=output).decode("utf-8")
  if result == output:
    print("Test \"" + name + "\" failed.")
    print("expected: " + output, ", result: " + result)
    return False
  else:
    print("Test \"" + name + "\" passed.")
    return True


string_input_fixtures = {
  # these are just demos
  "str_test_2_1": {"input": ["0x12345678", "0x87654321"],
                    "output": "0x0000000000000000000000000000000000000000000000000000000099999999",
                    "func": "JUMPDEST\nADD\nSWAP1\nJUMP"},
  "str_test_1_1": {"input": ["0x12345678"],
                    "output": "0x0000000000000000000000000000000000000000000000000000000012345678",
                    "func": "JUMPDEST\nJUMP"},
}


# this shows how to specify input filename.
#run_assembly(name="test", inputs=[
#             "0x12345678", "0x87654321"], output=None, filename="./test.ll")

# this shows how to test using input strings.
run_string_input(name="string_test", inputs=[
    "0x12345678", "0x87654321"], output="0x0000000000000000000000000000000000000000000000000000000099999999", function="JUMPDEST\nADD\nSWAP1\nJUMP")


for key,val in string_input_fixtures.items():
  inputs = val["input"]
  output = val["output"]
  function = val["func"]
  run_string_input(name=key, inputs=inputs, output=output, function=function)