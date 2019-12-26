from collections import OrderedDict

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

simple_tests = OrderedDict({
    "simple_test_1": {
        "file": "simple_tests/simple_test_1.ll",
        "input":  [],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
    "simple_test_2": {
        "file": "simple_tests/simple_test_2.ll",
        "input":  ["0x12345678", "0x87654321"],
        "output": "0x0000000000000000000000000000000000000000000000000000000099999999",
    },
    #"simple_test_3": {
    #    "file": "simple_tests/simple_test_3.ll",
    #    "input":  [],
    #    "output": "0x",
    #},
    #"simple_test_4": {
    #    "file": "simple_tests/simple_test_4.ll",
    #    "input":  ["0x12345678"],
    #    "output": "0x",
    #},
    "simple_test_5.ll": {
        "file": "simple_tests/simple_test_5.ll",
        "input":  ["0x12345678"],
        "output": "0x0000000000000000000000000000000000000000000000000000000012345679",
    },
    "simple_test_6": {
        "file": "simple_tests/simple_test_6.ll",
        "input":  ["0x12345678", "0x87654321"],
        "output": "0x0000000000000000000000000000000000000000000000000000000012345678",
    },
    "simple_test_7": {
        "file": "simple_tests/simple_test_7.ll",
        "input":  ["0x12345678", "0x87654321"],
        "output": "0x0000000000000000000000000000000000000000000000000000000087654321",
    },
    "simple_test_8.ll": {
        "file": "simple_tests/simple_test_8.ll",
        "input":  ["0x12345678", "0x87654321"],
        "output": "0x0000000000000000000000000000000000000000000000000000000012345678",
    },
    "switch: 1": {
        "file": "simple_tests/switch.ll",
        "input":  ["0x00000001"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000064",
    },
    "switch: 2": {
        "file": "simple_tests/switch.ll",
        "input":  ["0x00000002"],
        "output": "0x00000000000000000000000000000000000000000000000000000000000000c8",
    },
    "switch: 3": {
        "file": "simple_tests/switch.ll",
        "input":  ["0x00000064"],
        "output": "0x000000000000000000000000000000000000000000000000000000000000012c",
    },
})

loop_tests = OrderedDict({
    "loop1": {
        "file": "loops/loop.ll",
        "input":  ["0x00001000", "0x00000001"],
        "output": "0x000000000000000000000000000000000000000000000000000000000000100a",
    },
    "loop2": {
        "file": "loops/loop2.ll",
        "input":  ["0x00001000", "0x0000000a"],
        "output": "0x000000000000000000000000000000000000000000000000000000000000100a"
    },
    "loop3": {
        "file": "loops/loop2.ll",
        "input":  ["0x0000000a", "0x00001000"],
        "output": "0x000000000000000000000000000000000000000000000000000000000000100a"
    },
    "adds: 100": {
        "file": "loops/adds.ll",
        "input": ["0x00000064"],
        "output": "0x00000000000000000000000000000000000000000000000000000000000013ba",
    },
    "number_of_digits: 10": {
        "file": "loops/num_digits.ll",
        "input": ["0x12345678"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000009",
    },
    "trailing zeros: 0x12345678": {
        "file": "loops/trailing_zeros.ll",
        "input": ["0x12345678"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000000",
    },
    "trailing zeros: 10000": {
        "file": "loops/trailing_zeros.ll",
        "input": ["0x00002710"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000004",
    },

})

setcc_tests = OrderedDict({
    "setcc_eq1": {
        "file": "setcc/setcc_eq.ll",
        "input":  ["0xff00ff00", "0x00ff00ff"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
    "setcc_ne1": {
        "file": "setcc/setcc_ne.ll",
        "input":  ["0xff00ff01", "0x00ff00ff"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
    "setcc_ule": {
        "file": "setcc/setcc_ule.ll",
        "input":  ["0xff00ff00", "0x00ff01ff"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000000",
    },
    "setcc_uge": {
        "file": "setcc/setcc_uge.ll",
        "input":  ["0xff00ff00", "0x00ff00ff"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
    "cmp1": {
        "file": "setcc/cmp.ll",
        "input":  ["0x00001234", "0x00004321"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000021908",
    },
    "cmp2": {
        "file": "setcc/cmp.ll",
        "input":  ["0x00004321", "0x00001234"],
        "output": "0x0000000000000000000000000000000000000000000000000000000004c5f4b4",
    },
})

fib_tests = OrderedDict({
    "fibonacci 1": {
        "file": "recursive_tests/fib.ll",
        "input": ["0x00000001"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
    "fibonacci 2": {
        "file": "recursive_tests/fib.ll",
        "input": ["0x00000002"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000002",
    },
    "fibonacci 3": {
        "file": "recursive_tests/fib.ll",
        "input": ["0x00000003"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000003",
    },
    "fibonacci 10": {
        "file": "recursive_tests/fib.ll",
        "input": ["0x0000000a"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000059",
    },
})

rec_tests = OrderedDict({
    "factorial: 0": {
        "file": "recursive_tests/factorial.ll",
        "input": ["0x00000000"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
    "factorial: 1": {
        "file": "recursive_tests/factorial.ll",
        "input": ["0x00000001"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
    "factorial: 5": {
        "file": "recursive_tests/factorial.ll",
        "input": ["0x00000005"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000078",
    },
    "ackermann: (0, 0)": {
        "file": "recursive_tests/ackermann.ll",
        "input": ["0x00000000", "0x00000000"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
    "ackermann: (3, 2)": {
        "file": "recursive_tests/ackermann.ll",
        "input": ["0x00000003", "0x00000002"],
        "output": "0x000000000000000000000000000000000000000000000000000000000000001d",
    },
})

call_tests = OrderedDict({
    "a -> b: 0": {
        "file": "call_tests/a_to_b.ll",
        "input": [],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
})

struct_tests = OrderedDict({
    "array load/stores": {
        "file": "struct_tests/array.ll",
        "input": ["0x12345678"],
        "output": "0x0000000000000000000000000000000000000000000000000000000012345678",
    },

})

safemath_tests = OrderedDict({
    "add 1": {
        "file": "safemath/add.ll",
        "input": ["0x12345678", "0x87654321"],
        "output": "0x0000000000000000000000000000000000000000000000000000000099999999",
    },
    "sub 1": {
        "file": "safemath/sub.ll",
        "input": ["0x12345678", "0x12345678"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000000",
    },
    "mul 1": {
        "file": "safemath/mul.ll",
        "input": ["0x00002710", "0x00002711"],
        "output": "0x0000000000000000000000000000000000000000000000000000000005f60810",
    },
    "div 1": {
        "file": "safemath/div.ll",
        "input": ["0x00002710", "0x00000063"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000065",
    },
    "mod 1": {
        "file": "safemath/mod.ll",
        "input": ["0x00002710", "0x00000063"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
})

math_tests = OrderedDict({
    "is prime number 0x12345678": {
        "file": "math/prime.ll",
        "input": ["0x12345678"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000000",
    },
    "is prime number 101": {
        "file": "math/prime.ll",
        "input": ["0x00000065"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
    "HCF: 24 36": {
        "file": "math/hcf.ll",
        "input": ["0x00000018", "0x0000024"],
        "output": "0x000000000000000000000000000000000000000000000000000000000000000c",
    }
})

pointer_tests = OrderedDict ({
    "swap": {
        "file": "ptr/swap.ll",
        "input": ["0x12345678", "0x87654321"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
})

sort_tests = OrderedDict({
    "insertion sort": {
        "file": "sorting/insertion.ll",
        "input": [],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
    "bubble sort": {
        "file": "sorting/bubble.ll",
        "input": [],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
    "quick sort": {
        "file": "sorting/quicksort.ll",
        "input": [],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
})

bitwise_tests = OrderedDict({
    "Round": {
        "file": "bitwise/round.ll",
        "input": ["0x12345678"],
        "output": "0x0000000000000000000000000000000000000000000000000000000020000000",
    },
    "Bits are all ones: true": {
        "file": "bitwise/bits_all_one.ll",
        "input": ["0xffffffff"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
    "Bits are all ones: false": {
        "file": "bitwise/bits_all_one.ll",
        "input": ["0xfff8ffff"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000000",
    },
    "Change bits": {
        "file": "bitwise/change_bits.ll",
        "input": ["0x11223344", "0x55667788", "0x00000000C", "0x00000013"],
        "output": "0x0000000000000000000000000000000000000000000000000000000011267344",
    },
    "Swap bits": {
        "file": "bitwise/swap_bits.ll",
        "input": ["0x00000001", "0x00000000", "0x00000001F"],
        "output": "0x0000000000000000000000000000000000000000000000000000000080000000",
    },
    "Check bits: true": {
        "file": "bitwise/check_bit.ll",
        "input": ["0x87654321", "0x0000000F"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000000",
    },
    "Check bits: false": {
        "file": "bitwise/check_bit.ll",
        "input": ["0x87654321", "0x0000000E"],
        "output": "0x0000000000000000000000000000000000000000000000000000000000000001",
    },
    "Num bits revert": {
        "file": "bitwise/num_bits_revert.ll",
        "input": ["0x12345678", "0x87654321"],
        "output": "0x000000000000000000000000000000000000000000000000000000000000000e",
    },
})

test_suite = [simple_tests, math_tests, bitwise_tests, safemath_tests, call_tests, loop_tests, fib_tests, pointer_tests, rec_tests,
              setcc_tests, struct_tests, sort_tests]