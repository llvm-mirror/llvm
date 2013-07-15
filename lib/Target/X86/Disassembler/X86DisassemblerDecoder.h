/*===-- X86DisassemblerDecoderInternal.h - Disassembler decoder ---*- C -*-===*
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *
 *===----------------------------------------------------------------------===*
 *
 * This file is part of the X86 Disassembler.
 * It contains the public interface of the instruction decoder.
 * Documentation for the disassembler can be found in X86Disassembler.h.
 *
 *===----------------------------------------------------------------------===*/

#ifndef X86DISASSEMBLERDECODER_H
#define X86DISASSEMBLERDECODER_H

#ifdef __cplusplus
extern "C" {
#endif

#define INSTRUCTION_SPECIFIER_FIELDS \
  uint16_t operands;

#define INSTRUCTION_IDS     \
  uint16_t instructionIDs;

#include "X86DisassemblerDecoderCommon.h"

#undef INSTRUCTION_SPECIFIER_FIELDS
#undef INSTRUCTION_IDS

/*
 * Accessor functions for various fields of an Intel instruction
 */
#define modFromModRM(modRM)  (((modRM) & 0xc0) >> 6)
#define regFromModRM(modRM)  (((modRM) & 0x38) >> 3)
#define rmFromModRM(modRM)   ((modRM) & 0x7)
#define scaleFromSIB(sib)    (((sib) & 0xc0) >> 6)
#define indexFromSIB(sib)    (((sib) & 0x38) >> 3)
#define baseFromSIB(sib)     ((sib) & 0x7)
#define wFromREX(rex)        (((rex) & 0x8) >> 3)
#define rFromREX(rex)        (((rex) & 0x4) >> 2)
#define xFromREX(rex)        (((rex) & 0x2) >> 1)
#define bFromREX(rex)        ((rex) & 0x1)

#define rFromVEX2of3(vex)       (((~(vex)) & 0x80) >> 7)
#define xFromVEX2of3(vex)       (((~(vex)) & 0x40) >> 6)
#define bFromVEX2of3(vex)       (((~(vex)) & 0x20) >> 5)
#define mmmmmFromVEX2of3(vex)   ((vex) & 0x1f)
#define wFromVEX3of3(vex)       (((vex) & 0x80) >> 7)
#define vvvvFromVEX3of3(vex)    (((~(vex)) & 0x78) >> 3)
#define lFromVEX3of3(vex)       (((vex) & 0x4) >> 2)
#define ppFromVEX3of3(vex)      ((vex) & 0x3)

#define rFromVEX2of2(vex)       (((~(vex)) & 0x80) >> 7)
#define vvvvFromVEX2of2(vex)    (((~(vex)) & 0x78) >> 3)
#define lFromVEX2of2(vex)       (((vex) & 0x4) >> 2)
#define ppFromVEX2of2(vex)      ((vex) & 0x3)

/*
 * These enums represent Intel registers for use by the decoder.
 */

#define REGS_8BIT     \
  ENTRY(AL)           \
  ENTRY(CL)           \
  ENTRY(DL)           \
  ENTRY(BL)           \
  ENTRY(AH)           \
  ENTRY(CH)           \
  ENTRY(DH)           \
  ENTRY(BH)           \
  ENTRY(R8B)          \
  ENTRY(R9B)          \
  ENTRY(R10B)         \
  ENTRY(R11B)         \
  ENTRY(R12B)         \
  ENTRY(R13B)         \
  ENTRY(R14B)         \
  ENTRY(R15B)         \
  ENTRY(SPL)          \
  ENTRY(BPL)          \
  ENTRY(SIL)          \
  ENTRY(DIL)

#define EA_BASES_16BIT  \
  ENTRY(BX_SI)          \
  ENTRY(BX_DI)          \
  ENTRY(BP_SI)          \
  ENTRY(BP_DI)          \
  ENTRY(SI)             \
  ENTRY(DI)             \
  ENTRY(BP)             \
  ENTRY(BX)             \
  ENTRY(R8W)            \
  ENTRY(R9W)            \
  ENTRY(R10W)           \
  ENTRY(R11W)           \
  ENTRY(R12W)           \
  ENTRY(R13W)           \
  ENTRY(R14W)           \
  ENTRY(R15W)

#define REGS_16BIT    \
  ENTRY(AX)           \
  ENTRY(CX)           \
  ENTRY(DX)           \
  ENTRY(BX)           \
  ENTRY(SP)           \
  ENTRY(BP)           \
  ENTRY(SI)           \
  ENTRY(DI)           \
  ENTRY(R8W)          \
  ENTRY(R9W)          \
  ENTRY(R10W)         \
  ENTRY(R11W)         \
  ENTRY(R12W)         \
  ENTRY(R13W)         \
  ENTRY(R14W)         \
  ENTRY(R15W)

#define EA_BASES_32BIT  \
  ENTRY(EAX)            \
  ENTRY(ECX)            \
  ENTRY(EDX)            \
  ENTRY(EBX)            \
  ENTRY(sib)            \
  ENTRY(EBP)            \
  ENTRY(ESI)            \
  ENTRY(EDI)            \
  ENTRY(R8D)            \
  ENTRY(R9D)            \
  ENTRY(R10D)           \
  ENTRY(R11D)           \
  ENTRY(R12D)           \
  ENTRY(R13D)           \
  ENTRY(R14D)           \
  ENTRY(R15D)

#define REGS_32BIT  \
  ENTRY(EAX)        \
  ENTRY(ECX)        \
  ENTRY(EDX)        \
  ENTRY(EBX)        \
  ENTRY(ESP)        \
  ENTRY(EBP)        \
  ENTRY(ESI)        \
  ENTRY(EDI)        \
  ENTRY(R8D)        \
  ENTRY(R9D)        \
  ENTRY(R10D)       \
  ENTRY(R11D)       \
  ENTRY(R12D)       \
  ENTRY(R13D)       \
  ENTRY(R14D)       \
  ENTRY(R15D)

#define EA_BASES_64BIT  \
  ENTRY(RAX)            \
  ENTRY(RCX)            \
  ENTRY(RDX)            \
  ENTRY(RBX)            \
  ENTRY(sib64)          \
  ENTRY(RBP)            \
  ENTRY(RSI)            \
  ENTRY(RDI)            \
  ENTRY(R8)             \
  ENTRY(R9)             \
  ENTRY(R10)            \
  ENTRY(R11)            \
  ENTRY(R12)            \
  ENTRY(R13)            \
  ENTRY(R14)            \
  ENTRY(R15)

#define REGS_64BIT  \
  ENTRY(RAX)        \
  ENTRY(RCX)        \
  ENTRY(RDX)        \
  ENTRY(RBX)        \
  ENTRY(RSP)        \
  ENTRY(RBP)        \
  ENTRY(RSI)        \
  ENTRY(RDI)        \
  ENTRY(R8)         \
  ENTRY(R9)         \
  ENTRY(R10)        \
  ENTRY(R11)        \
  ENTRY(R12)        \
  ENTRY(R13)        \
  ENTRY(R14)        \
  ENTRY(R15)

#define REGS_MMX  \
  ENTRY(MM0)      \
  ENTRY(MM1)      \
  ENTRY(MM2)      \
  ENTRY(MM3)      \
  ENTRY(MM4)      \
  ENTRY(MM5)      \
  ENTRY(MM6)      \
  ENTRY(MM7)

#define REGS_XMM  \
  ENTRY(XMM0)     \
  ENTRY(XMM1)     \
  ENTRY(XMM2)     \
  ENTRY(XMM3)     \
  ENTRY(XMM4)     \
  ENTRY(XMM5)     \
  ENTRY(XMM6)     \
  ENTRY(XMM7)     \
  ENTRY(XMM8)     \
  ENTRY(XMM9)     \
  ENTRY(XMM10)    \
  ENTRY(XMM11)    \
  ENTRY(XMM12)    \
  ENTRY(XMM13)    \
  ENTRY(XMM14)    \
  ENTRY(XMM15)

#define REGS_YMM  \
  ENTRY(YMM0)     \
  ENTRY(YMM1)     \
  ENTRY(YMM2)     \
  ENTRY(YMM3)     \
  ENTRY(YMM4)     \
  ENTRY(YMM5)     \
  ENTRY(YMM6)     \
  ENTRY(YMM7)     \
  ENTRY(YMM8)     \
  ENTRY(YMM9)     \
  ENTRY(YMM10)    \
  ENTRY(YMM11)    \
  ENTRY(YMM12)    \
  ENTRY(YMM13)    \
  ENTRY(YMM14)    \
  ENTRY(YMM15)

#define REGS_SEGMENT \
  ENTRY(ES)          \
  ENTRY(CS)          \
  ENTRY(SS)          \
  ENTRY(DS)          \
  ENTRY(FS)          \
  ENTRY(GS)

#define REGS_DEBUG  \
  ENTRY(DR0)        \
  ENTRY(DR1)        \
  ENTRY(DR2)        \
  ENTRY(DR3)        \
  ENTRY(DR4)        \
  ENTRY(DR5)        \
  ENTRY(DR6)        \
  ENTRY(DR7)

#define REGS_CONTROL  \
  ENTRY(CR0)          \
  ENTRY(CR1)          \
  ENTRY(CR2)          \
  ENTRY(CR3)          \
  ENTRY(CR4)          \
  ENTRY(CR5)          \
  ENTRY(CR6)          \
  ENTRY(CR7)          \
  ENTRY(CR8)

#define ALL_EA_BASES  \
  EA_BASES_16BIT      \
  EA_BASES_32BIT      \
  EA_BASES_64BIT

#define ALL_SIB_BASES \
  REGS_32BIT          \
  REGS_64BIT

#define ALL_REGS      \
  REGS_8BIT           \
  REGS_16BIT          \
  REGS_32BIT          \
  REGS_64BIT          \
  REGS_MMX            \
  REGS_XMM            \
  REGS_YMM            \
  REGS_SEGMENT        \
  REGS_DEBUG          \
  REGS_CONTROL        \
  ENTRY(RIP)

/*
 * EABase - All possible values of the base field for effective-address
 *   computations, a.k.a. the Mod and R/M fields of the ModR/M byte.  We
 *   distinguish between bases (EA_BASE_*) and registers that just happen to be
 *   referred to when Mod == 0b11 (EA_REG_*).
 */
typedef enum {
  EA_BASE_NONE,
#define ENTRY(x) EA_BASE_##x,
  ALL_EA_BASES
#undef ENTRY
#define ENTRY(x) EA_REG_##x,
  ALL_REGS
#undef ENTRY
  EA_max
} EABase;

/*
 * SIBIndex - All possible values of the SIB index field.
 *   Borrows entries from ALL_EA_BASES with the special case that
 *   sib is synonymous with NONE.
 * Vector SIB: index can be XMM or YMM.
 */
typedef enum {
  SIB_INDEX_NONE,
#define ENTRY(x) SIB_INDEX_##x,
  ALL_EA_BASES
  REGS_XMM
  REGS_YMM
#undef ENTRY
  SIB_INDEX_max
} SIBIndex;

/*
 * SIBBase - All possible values of the SIB base field.
 */
typedef enum {
  SIB_BASE_NONE,
#define ENTRY(x) SIB_BASE_##x,
  ALL_SIB_BASES
#undef ENTRY
  SIB_BASE_max
} SIBBase;

/*
 * EADisplacement - Possible displacement types for effective-address
 *   computations.
 */
typedef enum {
  EA_DISP_NONE,
  EA_DISP_8,
  EA_DISP_16,
  EA_DISP_32
} EADisplacement;

/*
 * Reg - All possible values of the reg field in the ModR/M byte.
 */
typedef enum {
#define ENTRY(x) MODRM_REG_##x,
  ALL_REGS
#undef ENTRY
  MODRM_REG_max
} Reg;

/*
 * SegmentOverride - All possible segment overrides.
 */
typedef enum {
  SEG_OVERRIDE_NONE,
  SEG_OVERRIDE_CS,
  SEG_OVERRIDE_SS,
  SEG_OVERRIDE_DS,
  SEG_OVERRIDE_ES,
  SEG_OVERRIDE_FS,
  SEG_OVERRIDE_GS,
  SEG_OVERRIDE_max
} SegmentOverride;

/*
 * VEXLeadingOpcodeByte - Possible values for the VEX.m-mmmm field
 */

typedef enum {
  VEX_LOB_0F = 0x1,
  VEX_LOB_0F38 = 0x2,
  VEX_LOB_0F3A = 0x3
} VEXLeadingOpcodeByte;

/*
 * VEXPrefixCode - Possible values for the VEX.pp field
 */

typedef enum {
  VEX_PREFIX_NONE = 0x0,
  VEX_PREFIX_66 = 0x1,
  VEX_PREFIX_F3 = 0x2,
  VEX_PREFIX_F2 = 0x3
} VEXPrefixCode;

typedef uint8_t BOOL;

/*
 * byteReader_t - Type for the byte reader that the consumer must provide to
 *   the decoder.  Reads a single byte from the instruction's address space.
 * @param arg     - A baton that the consumer can associate with any internal
 *                  state that it needs.
 * @param byte    - A pointer to a single byte in memory that should be set to
 *                  contain the value at address.
 * @param address - The address in the instruction's address space that should
 *                  be read from.
 * @return        - -1 if the byte cannot be read for any reason; 0 otherwise.
 */
typedef int (*byteReader_t)(const void* arg, uint8_t* byte, uint64_t address);

/*
 * dlog_t - Type for the logging function that the consumer can provide to
 *   get debugging output from the decoder.
 * @param arg     - A baton that the consumer can associate with any internal
 *                  state that it needs.
 * @param log     - A string that contains the message.  Will be reused after
 *                  the logger returns.
 */
typedef void (*dlog_t)(void* arg, const char *log);

/*
 * The x86 internal instruction, which is produced by the decoder.
 */
struct InternalInstruction {
  /* Reader interface (C) */
  byteReader_t reader;
  /* Opaque value passed to the reader */
  const void* readerArg;
  /* The address of the next byte to read via the reader */
  uint64_t readerCursor;

  /* Logger interface (C) */
  dlog_t dlog;
  /* Opaque value passed to the logger */
  void* dlogArg;

  /* General instruction information */

  /* The mode to disassemble for (64-bit, protected, real) */
  DisassemblerMode mode;
  /* The start of the instruction, usable with the reader */
  uint64_t startLocation;
  /* The length of the instruction, in bytes */
  size_t length;

  /* Prefix state */

  /* 1 if the prefix byte corresponding to the entry is present; 0 if not */
  uint8_t prefixPresent[0x100];
  /* contains the location (for use with the reader) of the prefix byte */
  uint64_t prefixLocations[0x100];
  /* The value of the VEX prefix, if present */
  uint8_t vexPrefix[3];
  /* The length of the VEX prefix (0 if not present) */
  uint8_t vexSize;
  /* The value of the REX prefix, if present */
  uint8_t rexPrefix;
  /* The location where a mandatory prefix would have to be (i.e., right before
     the opcode, or right before the REX prefix if one is present) */
  uint64_t necessaryPrefixLocation;
  /* The segment override type */
  SegmentOverride segmentOverride;
  /* 1 if the prefix byte, 0xf2 or 0xf3 is xacquire or xrelease */
  BOOL xAcquireRelease;

  /* Sizes of various critical pieces of data, in bytes */
  uint8_t registerSize;
  uint8_t addressSize;
  uint8_t displacementSize;
  uint8_t immediateSize;

  /* Offsets from the start of the instruction to the pieces of data, which is
     needed to find relocation entries for adding symbolic operands */
  uint8_t displacementOffset;
  uint8_t immediateOffset;

  /* opcode state */

  /* The value of the two-byte escape prefix (usually 0x0f) */
  uint8_t twoByteEscape;
  /* The value of the three-byte escape prefix (usually 0x38 or 0x3a) */
  uint8_t threeByteEscape;
  /* The last byte of the opcode, not counting any ModR/M extension */
  uint8_t opcode;
  /* The ModR/M byte of the instruction, if it is an opcode extension */
  uint8_t modRMExtension;

  /* decode state */

  /* The type of opcode, used for indexing into the array of decode tables */
  OpcodeType opcodeType;
  /* The instruction ID, extracted from the decode table */
  uint16_t instructionID;
  /* The specifier for the instruction, from the instruction info table */
  const struct InstructionSpecifier *spec;

  /* state for additional bytes, consumed during operand decode.  Pattern:
     consumed___ indicates that the byte was already consumed and does not
     need to be consumed again */

  /* The VEX.vvvv field, which contains a third register operand for some AVX
     instructions */
  Reg                           vvvv;

  /* The ModR/M byte, which contains most register operands and some portion of
     all memory operands */
  BOOL                          consumedModRM;
  uint8_t                       modRM;

  /* The SIB byte, used for more complex 32- or 64-bit memory operands */
  BOOL                          consumedSIB;
  uint8_t                       sib;

  /* The displacement, used for memory operands */
  BOOL                          consumedDisplacement;
  int32_t                       displacement;

  /* Immediates.  There can be two in some cases */
  uint8_t                       numImmediatesConsumed;
  uint8_t                       numImmediatesTranslated;
  uint64_t                      immediates[2];

  /* A register or immediate operand encoded into the opcode */
  BOOL                          consumedOpcodeModifier;
  uint8_t                       opcodeModifier;
  Reg                           opcodeRegister;

  /* Portions of the ModR/M byte */

  /* These fields determine the allowable values for the ModR/M fields, which
     depend on operand and address widths */
  EABase                        eaBaseBase;
  EABase                        eaRegBase;
  Reg                           regBase;

  /* The Mod and R/M fields can encode a base for an effective address, or a
     register.  These are separated into two fields here */
  EABase                        eaBase;
  EADisplacement                eaDisplacement;
  /* The reg field always encodes a register */
  Reg                           reg;

  /* SIB state */
  SIBIndex                      sibIndex;
  uint8_t                       sibScale;
  SIBBase                       sibBase;

  const struct OperandSpecifier *operands;
};

/* decodeInstruction - Decode one instruction and store the decoding results in
 *   a buffer provided by the consumer.
 * @param insn      - The buffer to store the instruction in.  Allocated by the
 *                    consumer.
 * @param reader    - The byteReader_t for the bytes to be read.
 * @param readerArg - An argument to pass to the reader for storing context
 *                    specific to the consumer.  May be NULL.
 * @param logger    - The dlog_t to be used in printing status messages from the
 *                    disassembler.  May be NULL.
 * @param loggerArg - An argument to pass to the logger for storing context
 *                    specific to the logger.  May be NULL.
 * @param startLoc  - The address (in the reader's address space) of the first
 *                    byte in the instruction.
 * @param mode      - The mode (16-bit, 32-bit, 64-bit) to decode in.
 * @return          - Nonzero if there was an error during decode, 0 otherwise.
 */
int decodeInstruction(struct InternalInstruction* insn,
                      byteReader_t reader,
                      const void* readerArg,
                      dlog_t logger,
                      void* loggerArg,
                      const void* miiArg,
                      uint64_t startLoc,
                      DisassemblerMode mode);

/* x86DisassemblerDebug - C-accessible function for printing a message to
 *   debugs()
 * @param file  - The name of the file printing the debug message.
 * @param line  - The line number that printed the debug message.
 * @param s     - The message to print.
 */

void x86DisassemblerDebug(const char *file,
                          unsigned line,
                          const char *s);

const char *x86DisassemblerGetInstrName(unsigned Opcode, const void *mii);

#ifdef __cplusplus
}
#endif

#endif
