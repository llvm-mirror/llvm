# RUN: not llvm-mc %s -triple=mips -show-encoding -mcpu=mips32r6 -mattr=micromips 2>%t1
# RUN: FileCheck %s < %t1

  addiur1sp $7, 260        # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  addiur1sp $7, 241        # CHECK: :[[@LINE]]:{{[0-9]+}}: error: misaligned immediate operand value
  addiur1sp $8, 240        # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  addiur2 $9, $7, -1       # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  addiur2 $6, $7, 10       # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  addius5 $7, 9            # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  addiusp 1032             # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  align $4, $2, $3, -1     # CHECK: :[[@LINE]]:21: error: expected 2-bit unsigned immediate
  align $4, $2, $3, 4      # CHECK: :[[@LINE]]:21: error: expected 2-bit unsigned immediate
  beqzc16 $9, 20           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  beqzc16 $6, 31           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: branch to misaligned address
  beqzc16 $6, 130          # CHECK: :[[@LINE]]:{{[0-9]+}}: error: branch target out of range
  bnezc16 $9, 20           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  bnezc16 $6, 31           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: branch to misaligned address
  bnezc16 $6, 130          # CHECK: :[[@LINE]]:{{[0-9]+}}: error: branch target out of range
  break -1                 # CHECK: :[[@LINE]]:9: error: expected 10-bit unsigned immediate
  break 1024               # CHECK: :[[@LINE]]:9: error: expected 10-bit unsigned immediate
  break -1, 5              # CHECK: :[[@LINE]]:9: error: expected 10-bit unsigned immediate
  break 1024, 5            # CHECK: :[[@LINE]]:9: error: expected 10-bit unsigned immediate
  break 7, -1              # CHECK: :[[@LINE]]:12: error: expected 10-bit unsigned immediate
  break 7, 1024            # CHECK: :[[@LINE]]:12: error: expected 10-bit unsigned immediate
  break 1023, 1024         # CHECK: :[[@LINE]]:15: error: expected 10-bit unsigned immediate
  cache -1, 255($7)        # CHECK: :[[@LINE]]:9: error: expected 5-bit unsigned immediate
  cache 32, 255($7)        # CHECK: :[[@LINE]]:9: error: expected 5-bit unsigned immediate
  # FIXME: Check '0 < pos + size <= 32' constraint on ext
  ext $2, $3, -1, 31       # CHECK: :[[@LINE]]:15: error: expected 5-bit unsigned immediate
  ext $2, $3, 32, 31       # CHECK: :[[@LINE]]:15: error: expected 5-bit unsigned immediate
  ext $2, $3, 1, 0         # CHECK: :[[@LINE]]:18: error: expected immediate in range 1 .. 32
  ext $2, $3, 1, 33        # CHECK: :[[@LINE]]:18: error: expected immediate in range 1 .. 32
  ins $2, $3, -1, 31       # CHECK: :[[@LINE]]:15: error: expected 5-bit unsigned immediate
  ins $2, $3, 32, 31       # CHECK: :[[@LINE]]:15: error: expected 5-bit unsigned immediate
  ei $32                   # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  swe $33, 8($4)           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  swe $5, 8($34)           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  swe $5, 512($4)          # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  lbu16 $9, 8($16)         # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  lbu16 $3, -2($16)        # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  lbu16 $3, -2($16)        # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  lbu16 $16, 8($9)         # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  lhu16 $9, 4($16)         # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  lhu16 $3, 64($16)        # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  lhu16 $3, 64($16)        # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  lhu16 $16, 4($9)         # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  lsa   $4, $2, $3, 0      # CHECK: :[[@LINE]]:21: error: expected immediate in range 1 .. 4
  lsa   $4, $2, $3, 5      # CHECK: :[[@LINE]]:21: error: expected immediate in range 1 .. 4
  lw16  $9, 8($17)         # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  lw16  $4, 68($17)        # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  lw16  $4, 68($17)        # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  lw16  $17, 8($10)        # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  pref -1, 255($7)         # CHECK: :[[@LINE]]:8: error: expected 5-bit unsigned immediate
  pref 32, 255($7)         # CHECK: :[[@LINE]]:8: error: expected 5-bit unsigned immediate
  teq $34, $9, 5           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  teq $8, $35, 6           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  teq $8, $9, 16           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  tge $34, $9, 5           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  tge $8, $35, 6           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  tge $8, $9, 16           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  tgeu $34, $9, 5          # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  tgeu $8, $35, 6          # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  tgeu $8, $9, 16          # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  tlt $34, $9, 5           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  tlt $8, $35, 6           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  tlt $8, $9, 16           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  tltu $34, $9, 5          # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  tltu $8, $35, 6          # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  tltu $8, $9, 16          # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  tne $34, $9, 5           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  tne $8, $35, 6           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  tne $8, $9, 16           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  teq $8, $9, $2           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  tge $8, $9, $2           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  tgeu $8, $9, $2          # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  tlt $8, $9, $2           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  tltu $8, $9, $2          # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  tne $8, $9, $2           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  wait -1                  # CHECK: :[[@LINE]]:8: error: expected 10-bit unsigned immediate
  wait 1024                # CHECK: :[[@LINE]]:8: error: expected 10-bit unsigned immediate
  wrpgpr $34, $4           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  wrpgpr $3, $33           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  wsbh $34, $4             # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  wsbh $3, $33             # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  jrcaddiusp 1             # CHECK: :[[@LINE]]:{{[0-9]+}}: error: expected both 7-bit unsigned immediate and multiple of 4
  jrcaddiusp 2             # CHECK: :[[@LINE]]:{{[0-9]+}}: error: expected both 7-bit unsigned immediate and multiple of 4
  jrcaddiusp 3             # CHECK: :[[@LINE]]:{{[0-9]+}}: error: expected both 7-bit unsigned immediate and multiple of 4
  jrcaddiusp 10            # CHECK: :[[@LINE]]:{{[0-9]+}}: error: expected both 7-bit unsigned immediate and multiple of 4
  jrcaddiusp 18            # CHECK: :[[@LINE]]:{{[0-9]+}}: error: expected both 7-bit unsigned immediate and multiple of 4
  jrcaddiusp 31            # CHECK: :[[@LINE]]:{{[0-9]+}}: error: expected both 7-bit unsigned immediate and multiple of 4
  jrcaddiusp 33            # CHECK: :[[@LINE]]:{{[0-9]+}}: error: expected both 7-bit unsigned immediate and multiple of 4
  jrcaddiusp 125           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: expected both 7-bit unsigned immediate and multiple of 4
  jrcaddiusp 132           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: expected both 7-bit unsigned immediate and multiple of 4
  lwm16 $5, $6, $ra, 8($sp)   # CHECK: :[[@LINE]]:{{[0-9]+}}: error: $16 or $31 expected
  lwm16 $16, $19, $ra, 8($sp) # CHECK: :[[@LINE]]:{{[0-9]+}}: error: consecutive register numbers expected
  lwm16 $16-$25, $ra, 8($sp)  # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid register operand
  lwm16 $16, 8($sp)           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  lwm16 $16, $17, 8($sp)      # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  lwm16 $16-$20, 8($sp)       # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  lwm16 $16, $17, $ra, 8($fp)  # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  lwm16 $16, $17, $ra, 64($sp) # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  sb16 $9, 4($16)          # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  sb16 $3, 64($16)         # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  sb16 $16, 4($16)         # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  sb16 $7, 4($9)           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  sh16  $9, 8($17)         # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  sh16  $4, 68($17)        # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  sh16  $16, 8($17)        # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  sh16  $7, 8($9)          # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  sw16  $9, 4($17)         # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  sw16  $4, 64($17)        # CHECK: :[[@LINE]]:{{[0-9]+}}: error: immediate operand value out of range
  sw16  $16, 4($17)        # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  sw16  $7, 4($10)         # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  swm16 $5, $6, $ra, 8($sp)   # CHECK: :[[@LINE]]:{{[0-9]+}}: error: $16 or $31 expected
  swm16 $16, $19, $ra, 8($sp) # CHECK: :[[@LINE]]:{{[0-9]+}}: error: consecutive register numbers expected
  swm16 $16-$25, $ra, 8($sp)  # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid register operand
  swm16 $16, 8($sp)           # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  swm16 $16, $17, 8($sp)      # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  swm16 $16-$20, 8($sp)       # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  swm16 $16, $17, $ra, 8($fp)  # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
  swm16 $16, $17, $ra, 64($sp) # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
