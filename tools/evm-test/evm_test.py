#!/usr/bin/python

from typing import List
from string import Template

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
Function:\n
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
Function:\n
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

def generate_contract(inputs: List[int], func: str) -> str:
  assert len(inputs) < 3
  complete_str = generate_header_str(inputs) + func
  return asm.assemble_hex(complete_str)

def execute_in_evm(code: str, expected: str) -> str:
  pass


print(generate_contract(
    inputs=["0x12345678", "0x87654321"], func="JUMPDEST\nADD\nSWAP1\nJUMP"))
