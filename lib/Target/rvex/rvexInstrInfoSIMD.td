//===- rvexInstrInfoSIMD.td - Target Description for rvex SIMD Instrs -===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
//                               rvex Backend
//
// Author: David Juhasz
// E-mail: juhda@caesar.elte.hu
// Institute: Dept. of Programming Languages and Compilers, ELTE IK, Hungary
//
// The research is supported by the European Union and co-financed by the
// European Social Fund (grant agreement no. TAMOP
// 4.2.1./B-09/1/KMR-2010-0003).
//
// This file describes the rvex SIMD instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

//
// SIMD Instructions (p.218.)
//

def ADDB : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "addb $Dest, $SrcA, $SrcB",
                      [(set (v4i8 T64GPRF:$Dest), (add T64GPRF:$SrcA, T64GPRF:$SrcB))]>;

//skip ADDBS_U because it's only for TilePro

def ADDH : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "addh $Dest, $SrcA, $SrcB",
                      [(set (v2i16 T64GPRF:$Dest), (add T64GPRF:$SrcA, T64GPRF:$SrcB))]>;

//skip ADDHS because it's only for TilePro

//TODO
/*
def ADDIB : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, i32imm:$Imm8),
                       "addib $Dest, $SrcA, $Imm8",
                       [(set (v4i8 T64GPRF:$Dest), (v4i8 (build_vector (i8 (add (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))),
                                                               (i8 imm8:$Imm8))),
                                                          (i8 (add (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))),
                                                               (i8 imm8:$Imm8))),
                                                          (i8 (add (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))),
                                                               (i8 imm8:$Imm8))),
                                                          (i8 (add (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))),
                                                               (i8 imm8:$Imm8)))) ))]>;
*/

//TODO
/*
def ADDIH : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, i32imm:$Imm8),
                       "addh $Dest, $SrcA, $Imm8",
                       [(set T64GPRF:$Dest, (build_vector (add (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                               (i16 imm16:$Imm8)),
                                                          (add (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                               (i16 imm16:$Imm8)) ))]>;
*/

def ADIFFB_U : InstBmLT64<(outs T64GPRF:$Dest),
                          (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                          "adiffb_u $Dest, $SrcA, $SrcB",
                          [(set (v4i8 T64GPRF:$Dest), (build_vector (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))),
                                                                              (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0))),
                                                                              (i8 (sub (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)),
                                                                                       (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0)))),
                                                                              (i8 (sub (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0)),
                                                                                       (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)))),
                                                                              SETUGE),
                                                                    (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))),
                                                                              (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1))),
                                                                              (i8 (sub (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)),
                                                                                       (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1)))),
                                                                              (i8 (sub (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1)),
                                                                                       (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)))),
                                                                              SETUGE),
                                                                    (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))),
                                                                              (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2))),
                                                                              (i8 (sub (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)),
                                                                                       (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2)))),
                                                                              (i8 (sub (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2)),
                                                                                       (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)))),
                                                                              SETUGE),
                                                                    (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))),
                                                                              (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3))),
                                                                              (i8 (sub (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)),
                                                                                       (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3)))),
                                                                              (i8 (sub (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3)),
                                                                                       (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)))),
                                                                              SETUGE) ))]>;

def ADIFFH : InstBmLT64<(outs T64GPRF:$Dest),
                        (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                        "adiffh $Dest, $SrcA, $SrcB",
                        [(set (v2i16 T64GPRF:$Dest), (build_vector (selectcc (sub (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                                  (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0))),
                                                                             (i16 0),
                                                                             (i16 (sub (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                                       (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0)))),
                                                                             (i16 (sub (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0)),
                                                                                       (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)))),
                                                                             SETGE),
                                                                   (selectcc (sub (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                                  (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1))),
                                                                             (i16 0),
                                                                             (i16 (sub (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                                       (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1)))),
                                                                             (i16 (sub (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1)),
                                                                                       (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)))),
                                                                             SETGE) ))]>;

def AVGB_U : InstBmLT64<(outs T64GPRF:$Dest),
                        (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                        "avgb_u $Dest, $SrcA, $SrcB",
                        [(set (v4i8 T64GPRF:$Dest), (build_vector (udiv (add (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)),
                                                                             (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0))),
                                                                        (i32 2)),
                                                                  (udiv (add (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)),
                                                                             (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1))),
                                                                        (i32 2)),
                                                                  (udiv (add (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)),
                                                                             (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2))),
                                                                        (i32 2)),
                                                                  (udiv (add (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)),
                                                                             (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3))),
                                                                        (i32 2)) ))]>;

def AVGH : InstBmLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "avgh $Dest, $SrcA, $SrcB",
                       [(set (v2i16 T64GPRF:$Dest), (build_vector (udiv (add (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                             (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0))),
                                                                        (i32 2)),
                                                                  (udiv (add (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                             (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1))),
                                                                        (i32 2)) ))]>;

def INTHB : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                       "inthb $Dest, $SrcA, $SrcB",
                       [(set (v4i8 T64GPRF:$Dest), (build_vector (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2))),
                                                                 (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))),
                                                                 (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3))),
                                                                 (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)))))]>;

def INTHH : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                       "inthh $Dest, $SrcA, $SrcB",
                       [(set (v2i16 T64GPRF:$Dest), (build_vector (i16 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1))),
                                                                   (i16 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)))))]>;

def INTLB : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                       "intlb $Dest, $SrcA, $SrcB",
                       [(set (v4i8 T64GPRF:$Dest), (build_vector (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0))),
                                                                 (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))),
                                                                 (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1))),
                                                                 (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)))))]>;

def INTLH : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                       "intlh $Dest, $SrcA, $SrcB",
                       [(set (v2i16 T64GPRF:$Dest), (build_vector (i16 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0))),
                                                                  (i16 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)))))]>;

def MAXB_U : InstArLT64<(outs T64GPRF:$Dest),
                        (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                        "maxb_u $Dest, $SrcA, $SrcB",
                        [(set (v4i8 T64GPRF:$Dest), (build_vector (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0))),
                                                                            SETUGE),
                                                                  (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1))),
                                                                            SETUGE),
                                                                  (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2))),
                                                                            SETUGE),
                                                                  (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3))),
                                                                            SETUGE) ))]>;

def MAXH : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "maxh $Dest, $SrcA, $SrcB",
                      [(set (v2i16 T64GPRF:$Dest), (build_vector (selectcc (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0))),
                                                                           (i16 (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0))),
                                                                           (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0))),
                                                                           (i16 (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0))),
                                                                           SETGE),
                                                                 (selectcc (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1))),
                                                                           (i16 (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1))),
                                                                           (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1))),
                                                                           (i16 (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1))),
                                                                           SETGE) ))]>;

//TODO!!!
/*
def MAXIB_U : InstArLT64<(outs T64GPRF:$Dest),
                         (ins T64GPRF:$SrcA, i32imm:$Imm8),
                         "maxib_u $Dest, $SrcA, $Imm8",
                         [(set (v4i8 T64GPRF:$Dest), (build_vector (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))),
                                                                       (i8 imm8:$Imm8),
                                                                       (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))),
                                                                       (i8 imm8:$Imm8),
                                                                       SETUGE),
                                                            (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))),
                                                                       (i8 imm8:$Imm8),
                                                                       (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))),
                                                                       (i8 imm8:$Imm8),
                                                                       SETUGE),
                                                            (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))),
                                                                       (i8 imm8:$Imm8),
                                                                       (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))),
                                                                       (i8 imm8:$Imm8),
                                                                       SETUGE),
                                                            (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))),
                                                                       (i8 imm8:$Imm8),
                                                                       (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))),
                                                                       (i8 imm8:$Imm8),
                                                                       SETUGE) ))]>;
def MAXIH : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, i32imm:$Imm8),
                       "maxih $Dest, $SrcA, $Imm8",
                       [(set T64GPRF:$Dest, (build_vector (selectcc (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                     (i16 imm8:$Imm8),
                                                                     (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                     (i16 imm8:$Imm8),
                                                                     SETGE),
                                                          (selectcc (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                     (i16 imm8:$Imm8),
                                                                     (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                     (i16 imm8:$Imm8),
                                                                     SETGE) ))]>;
*/
def MINB_U : InstArLT64<(outs T64GPRF:$Dest),
                        (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                        "minb_u $Dest, $SrcA, $SrcB",
                        [(set (v4i8 T64GPRF:$Dest), (build_vector (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0))),
                                                                            SETULE),
                                                                  (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1))),
                                                                            SETULE),
                                                                  (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2))),
                                                                            SETULE),
                                                                  (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))),
                                                                            (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3))),
                                                                            SETULE) ))]>;

def MINH : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "minh $Dest, $SrcA, $SrcB",
                      [(set (v2i16 T64GPRF:$Dest), (build_vector (selectcc (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0))),
                                                                           (i16 (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0))),
                                                                           (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0))),
                                                                           (i16 (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0))),
                                                                           SETLE),
                                                                 (selectcc (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1))),
                                                                           (i16 (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1))),
                                                                           (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1))),
                                                                           (i16 (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1))),
                                                                           SETLE) ))]>;

//TODO!!!
/*
def MINIB_U : InstArLT64<(outs T64GPRF:$Dest),
                         (ins T64GPRF:$SrcA, i32imm:$Imm8),
                         "minib_u $Dest, $SrcA, $Imm8",
                         [(set T64GPRF:$Dest, (build_vector (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)),
                                                                       imm8:$Imm8,
                                                                       (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)),
                                                                       imm8:$Imm8,
                                                                       SETULE),
                                                            (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)),
                                                                       imm8:$Imm8,
                                                                       (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)),
                                                                       imm8:$Imm8,
                                                                       SETULE),
                                                            (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)),
                                                                       imm8:$Imm8,
                                                                       (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)),
                                                                       imm8:$Imm8,
                                                                       SETULE),
                                                            (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)),
                                                                       imm8:$Imm8,
                                                                       (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)),
                                                                       imm8:$Imm8,
                                                                       SETULE) ))]>;
def MINIH : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, i32imm:$Imm8),
                       "minih $Dest, $SrcA, $Imm8",
                       [(set T64GPRF:$Dest, (build_vector (selectcc (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                     (i16 imm8:$Imm8),
                                                                     (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                     (i16 imm8:$Imm8),
                                                                     SETLE),
                                                          (selectcc (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                     (i16 imm8:$Imm8),
                                                                     (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                     (i16 imm8:$Imm8),
                                                                     SETLE) ))]>;
*/

def MNZB : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "mnzb $Dest, $SrcA, $SrcB",
                      [(set (v4i8 T64GPRF:$Dest), (build_vector (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)),
                                                                          (i8 0),
                                                                          (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0)),
                                                                          (i8 0),
                                                                          SETNE),
                                                                (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)),
                                                                          (i8 0),
                                                                          (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1)),
                                                                          (i8 0),
                                                                          SETNE), 
                                                                (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)),
                                                                          (i8 0),
                                                                          (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2)),
                                                                          (i8 0),
                                                                          SETNE), 
                                                                (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)),
                                                                          (i8 0),
                                                                          (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3)),
                                                                          (i8 0),
                                                                          SETNE) ))]>;

def MNZH : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "mnzh $Dest, $SrcA, $SrcB",
                      [(set (v2i16 T64GPRF:$Dest), (build_vector (selectcc (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                           (i16 0),
                                                                           (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0)),
                                                                           (i16 0),
                                                                           SETNE),
                                                                 (selectcc (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                           (i16 0),
                                                                           (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1)),
                                                                           (i16 0),
                                                                           SETNE) ))]>;

def MZB : InstArLT64<(outs T64GPRF:$Dest),
                     (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                     "mzb $Dest, $SrcA, $SrcB",
                     [(set (v4i8 T64GPRF:$Dest), (build_vector (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)),
                                                                         (i8 0),
                                                                         (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0)),
                                                                         (i8 0),
                                                                         SETEQ),
                                                               (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)),
                                                                         (i8 0),
                                                                         (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1)),
                                                                         (i8 0),
                                                                         SETEQ), 
                                                               (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)),
                                                                         (i8 0),
                                                                         (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2)),
                                                                         (i8 0),
                                                                         SETEQ), 
                                                               (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)),
                                                                         (i8 0),
                                                                         (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3)),
                                                                         (i8 0),
                                                                         SETEQ) ))]>;

def MZH : InstArLT64<(outs T64GPRF:$Dest),
                     (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                     "mzh $Dest, $SrcA, $SrcB",
                     [(set (v2i16 T64GPRF:$Dest), (build_vector (selectcc (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                          (i16 0),
                                                                          (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0)),
                                                                          (i16 0),
                                                                          SETEQ),
                                                                (selectcc (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                          (i16 0),
                                                                          (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1)),
                                                                          (i16 0),
                                                                          SETEQ) ))]>;

//skip PACKBS_U because it's only for TilePro

def PACKHB : InstArLT64<(outs T64GPRF:$Dest),
                        (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                        "packhb $Dest, $SrcA, $SrcB",
                        [(set (v4i8 T64GPRF:$Dest), (build_vector (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1))),
                                                                  (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3))),
                                                                  (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))),
                                                                  (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))) ))]>;

//skip PACKHS because it's only for TilePro

def PACKLB : InstArLT64<(outs T64GPRF:$Dest),
                        (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                        "packlb $Dest, $SrcA, $SrcB",
                        [(set (v4i8 T64GPRF:$Dest), (build_vector (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0))),
                                                           (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2))),
                                                           (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))),
                                                           (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))) ))]>;

//TODO!!!
/*
def SADAB_U : InstBmLT64<(outs T64GPRF:$Dest),
                         (ins T64GPRF:$SrcA, T64GPRF:$SrcB, T64GPRF:$src),
                         "sadab_u $Dest, $SrcA, $SrcB",
                         [(set (i32 T64GPRF:$Dest), (add (i32 (add (i32 (add (i32 (add (i32 (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))),
                                                                             (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0))),
                                                                             (sub (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))),
                                                                                  (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0)))),
                                                                             (sub (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0))),
                                                                                  (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)))),
                                                                             SETUGE)),
                                                                   (i32 (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))),
                                                                             (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1))),
                                                                             (sub (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))),
                                                                                  (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1)))),
                                                                             (sub (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1))),
                                                                                  (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)))),
                                                                             SETUGE)))),
                                                              (i32 (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))),
                                                                        (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2))),
                                                                        (sub (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))),
                                                                             (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2)))),
                                                                        (sub (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2))),
                                                                             (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)))),
                                                                        SETUGE)))),
                                                         (i32 (selectcc (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))),
                                                                   (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3))),
                                                                   (sub (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))),
                                                                        (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3)))),
                                                                   (sub (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3))),
                                                                        (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)))),
                                                                   SETUGE)))),
                                                    (i32 T64GPRF:$src) ))],
                         "$src = $Dest">;

def SADAH : InstBmLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, T64GPRF:$SrcB, T64GPRF:$src),
                       "sadah $Dest, $SrcA, $SrcB",
                       [(set T64GPRF:$Dest, (add (add (selectcc (sub (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                      (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0))),
                                                                 (i8 0),
                                                                 (sub (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                      (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0))),
                                                                 (sub (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0)),
                                                                      (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0))),
                                                                 SETGE),
                                                      (selectcc (sub (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                      (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1))),
                                                                 (i8 0),
                                                                 (sub (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                      (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1))),
                                                                 (sub (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1)),
                                                                      (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1))),
                                                                 SETGE)),
                                                 T64GPRF:$src ))],
                       "$src = $Dest">;

def SADAH_U : InstBmLT64<(outs T64GPRF:$Dest),
                         (ins T64GPRF:$SrcA, T64GPRF:$SrcB, T64GPRF:$src),
                         "sadah_u $Dest, $SrcA, $SrcB",
                         [(set T64GPRF:$Dest, (add (add (selectcc (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                   (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0)),
                                                                   (sub (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                        (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0))),
                                                                   (sub (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0)),
                                                                        (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0))),
                                                                   SETUGE),
                                                        (selectcc (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                   (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1)),
                                                                   (sub (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                        (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1))),
                                                                   (sub (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1)),
                                                                        (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1))),
                                                                   SETUGE)),
                                                   T64GPRF:$src ))],
                         "$src = $Dest">;
*/

//TODO!!!
/*
def SADB_U : InstBmLT64<(outs T64GPRF:$Dest),
                        (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                        "sadb_u $Dest, $SrcA, $SrcB",
                        [(set (i32 T64GPRF:$Dest), (add (add (add (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)),
                                                                       (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0)),
                                                                       (sub (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)),
                                                                            (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0))),
                                                                       (sub (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0)),
                                                                            (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))),
                                                                       SETUGE),
                                                            (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)),
                                                                       (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1)),
                                                                       (sub (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)),
                                                                            (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1))),
                                                                       (sub (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1)),
                                                                            (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))),
                                                                       SETUGE)),
                                                       (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)),
                                                                  (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2)),
                                                                  (sub (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)),
                                                                       (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2))),
                                                                  (sub (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2)),
                                                                       (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))),
                                                                  SETUGE)),
                                                  (selectcc (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)),
                                                             (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3)),
                                                             (sub (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)),
                                                                  (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3))),
                                                             (sub (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3)),
                                                                  (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))),
                                                             SETUGE) ))]>;

def SADH : InstBmLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "sadh $Dest, $SrcA, $SrcB",
                      [(set T64GPRF:$Dest, (add (selectcc (sub (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0))),
                                                           (i8 0),
                                                           (sub (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0))),
                                                           (sub (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0)),
                                                                (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0))),
                                                           SETGE),
                                                (selectcc (sub (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1))),
                                                           (i8 0),
                                                           (sub (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1))),
                                                           (sub (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1)),
                                                                (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1))),
                                                           SETGE) ))]>;

def SADH_U : InstBmLT64<(outs T64GPRF:$Dest),
                        (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                        "sadh_u $Dest, $SrcA, $SrcB",
                        [(set T64GPRF:$Dest, (add (selectcc (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                             (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0)),
                                                             (sub (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                  (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0))),
                                                             (sub (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0)),
                                                                  (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0))),
                                                             SETUGE),
                                                  (selectcc (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                             (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1)),
                                                             (sub (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                  (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1))),
                                                             (sub (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1)),
                                                                  (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1))),
                                                             SETUGE) ))]>;
*/

//TODO!!!
/*
def SEQB : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "seqb $Dest, $SrcA, $SrcB",
                      [(set (v4i8 T64GPRF:$Dest), (build_vector (i8 (seteq (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))),
                                                                    (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0))))),
                                                         (i8 (seteq (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))),
                                                                    (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1))))),
                                                         (i8 (seteq (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))),
                                                                    (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2))))),
                                                         (i8 (seteq (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))),
                                                                    (i8 (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3))))) ))]>;

def SEQH : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "seqh $Dest, $SrcA, $SrcB",
                      [(set T64GPRF:$Dest, (build_vector (i16 (seteq (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                     (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0)))),
                                                         (i16 (seteq (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                     (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1)))) ))]>;

def SEQIB : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, i32imm:$Imm8),
                       "seqib $Dest, $SrcA, $Imm8",
                       [(set T64GPRF:$Dest, (build_vector (i8 (seteq (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)),
                                                                     imm8:$Imm8)),
                                                          (i8 (seteq (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)),
                                                                     imm8:$Imm8)),
                                                          (i8 (seteq (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)),
                                                                     imm8:$Imm8)),
                                                          (i8 (seteq (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)),
                                                                     imm8:$Imm8)) ))]>;

def SEQIH : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, i32imm:$Imm8),
                       "seqih $Dest, $SrcA, $Imm8",
                       [(set T64GPRF:$Dest, (build_vector (i16 (seteq (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                      (i16 imm8:$Imm8))),
                                                          (i16 (seteq (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                      (i16 imm8:$Imm8))) ))]>;
*/

def SHLB : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "shlb $Dest, $SrcA, $SrcB",
                      [(set (v4i8 T64GPRF:$Dest), (build_vector (shl (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))), (i32 T64GPRF:$SrcB)),
                                                                (shl (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))), (i32 T64GPRF:$SrcB)),
                                                                (shl (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))), (i32 T64GPRF:$SrcB)),
                                                                (shl (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))), (i32 T64GPRF:$SrcB)) ))]>;

def SHLH : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "shlh $Dest, $SrcA, $SrcB",
                      [(set (v2i16 T64GPRF:$Dest), (build_vector (shl (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0))), (i32 T64GPRF:$SrcB)),
                                                                 (shl (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1))), (i32 T64GPRF:$SrcB)) ))]>;

def SHLIB : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, i32imm:$ShAmt),
                       "shlib $Dest, $SrcA, $ShAmt",
                       [(set (v4i8 T64GPRF:$Dest), (build_vector (shl (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))), (i32 regimm:$ShAmt)),
                                                                 (shl (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))), (i32 regimm:$ShAmt)),
                                                                 (shl (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))), (i32 regimm:$ShAmt)),
                                                                 (shl (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))), (i32 regimm:$ShAmt)) ))]>;

def SHLIH : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, i32imm:$ShAmt),
                       "shlih $Dest, $SrcA, $ShAmt",
                       [(set (v2i16 T64GPRF:$Dest), (build_vector (shl (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0))), (i32 regimm:$ShAmt)),
                                                                  (shl (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1))), (i32 regimm:$ShAmt)) ))]>;

def SHRB : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "shrb $Dest, $SrcA, $SrcB",
                      [(set (v4i8 T64GPRF:$Dest), (build_vector (srl (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))), (i32 T64GPRF:$SrcB)),
                                                                (srl (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))), (i32 T64GPRF:$SrcB)),
                                                                (srl (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))), (i32 T64GPRF:$SrcB)),
                                                                (srl (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))), (i32 T64GPRF:$SrcB)) ))]>;

def SHRH : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "shrh $Dest, $SrcA, $SrcB",
                      [(set (v2i16 T64GPRF:$Dest), (build_vector (srl (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0))), (i32 T64GPRF:$SrcB)),
                                                                 (srl (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1))), (i32 T64GPRF:$SrcB)) ))]>;

def SHRIB : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, i32imm:$ShAmt),
                       "shrib $Dest, $SrcA, $ShAmt",
                       [(set (v4i8 T64GPRF:$Dest), (build_vector (srl (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))), (i32 regimm:$ShAmt)),
                                                                 (srl (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))), (i32 regimm:$ShAmt)),
                                                                 (shl (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))), (i32 regimm:$ShAmt)),
                                                                 (shl (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))), (i32 regimm:$ShAmt)) ))]>;

def SHRIH : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, i32imm:$ShAmt),
                       "shrih $Dest, $SrcA, $ShAmt",
                       [(set (v2i16 T64GPRF:$Dest), (build_vector (srl (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0))), (i32 regimm:$ShAmt)),
                                                                  (srl (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1))), (i32 regimm:$ShAmt)) ))]>;

//TODO!!!
/*
def SLTB : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "sltb $Dest, $SrcA, $SrcB",
                      [(set (v4i8 T64GPRF:$Dest), (build_vector (i8 (setlt (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)),
                                                                    (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0)))),
                                                         (i8 (setlt (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)),
                                                                    (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1)))),
                                                         (i8 (setlt (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)),
                                                                    (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2)))),
                                                         (i8 (setlt (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)),
                                                                    (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3)))) ))]>;

def SLTB_U : InstArLT64<(outs T64GPRF:$Dest),
                        (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                        "sltb_u $Dest, $SrcA, $SrcB",
                        [(set T64GPRF:$Dest, (build_vector (i8 (setult (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)),
                                                                       (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0)))),
                                                           (i8 (setult (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)),
                                                                       (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1)))),
                                                           (i8 (setult (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)),
                                                                       (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2)))),
                                                           (i8 (setult (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)),
                                                                       (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3)))) ))]>;

def SLTEB : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                       "slteb $Dest, $SrcA, $SrcB",
                       [(set T64GPRF:$Dest, (build_vector (i8 (setle (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)),
                                                                     (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0)))),
                                                          (i8 (setle (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)),
                                                                     (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1)))),
                                                          (i8 (setle (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)),
                                                                     (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2)))),
                                                          (i8 (setle (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)),
                                                                     (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3)))) ))]>;

def SLTEB_U : InstArLT64<(outs T64GPRF:$Dest),
                         (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                         "slteb_u $Dest, $SrcA, $SrcB",
                         [(set T64GPRF:$Dest, (build_vector (i8 (setule (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)),
                                                                        (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0)))),
                                                            (i8 (setule (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)),
                                                                        (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1)))),
                                                            (i8 (setule (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)),
                                                                        (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2)))),
                                                            (i8 (setule (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)),
                                                                        (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3)))) ))]>;

def SLTEH : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                       "slteh $Dest, $SrcA, $SrcB",
                       [(set T64GPRF:$Dest, (build_vector (i16 (setle (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                      (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0)))),
                                                          (i16 (setle (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                      (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1)))) ))]>;

def SLTEH_U : InstArLT64<(outs T64GPRF:$Dest),
                         (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                         "slteh_u $Dest, $SrcA, $SrcB",
                         [(set T64GPRF:$Dest, (build_vector (i16 (setule (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                         (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0)))),
                                                            (i16 (setule (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                         (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1)))) ))]>;

def SLTH : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "slth $Dest, $SrcA, $SrcB",
                      [(set T64GPRF:$Dest, (build_vector (i16 (setlt (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                     (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0)))),
                                                         (i16 (setlt (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                     (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1)))) ))]>;

def SLTH_U : InstArLT64<(outs T64GPRF:$Dest),
                        (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                        "slth_u $Dest, $SrcA, $SrcB",
                        [(set T64GPRF:$Dest, (build_vector (i16 (setult (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                        (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0)))),
                                                           (i16 (setult (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                        (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1)))) ))]>;

def SLTIB : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, i32imm:$Imm8),
                       "sltib $Dest, $SrcA, $Imm8",
                       [(set T64GPRF:$Dest, (build_vector (i8 (setlt (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)),
                                                                     imm8:$Imm8)),
                                                          (i8 (setlt (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)),
                                                                     imm8:$imm8)),
                                                          (i8 (setlt (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)),
                                                                     imm8:$Imm8)),
                                                          (i8 (setlt (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)),
                                                                     imm8:$Imm8)) ))]>;

def SLTIB_U : InstArLT64<(outs T64GPRF:$Dest),
                         (ins T64GPRF:$SrcA, i32imm:$Imm8),
                         "sltib_u $Dest, $SrcA, $Imm8",
                         [(set T64GPRF:$Dest, (build_vector (i8 (setult (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)),
                                                                        imm8:$Imm8)),
                                                            (i8 (setult (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)),
                                                                        imm8:$Imm8)),
                                                            (i8 (setult (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)),
                                                                        imm8:$Imm8)),
                                                            (i8 (setult (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)),
                                                                        imm8:$Imm8)) ))]>;

def SLTIH : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, i32imm:$Imm8),
                       "sltih $Dest, $SrcA, $Imm8",
                       [(set T64GPRF:$Dest, (build_vector (i16 (setlt (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                      (i16 imm8:$Imm8))),
                                                          (i16 (setlt (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                      (i16 imm8:$Imm8))) ))]>;

def SLTIH_U : InstArLT64<(outs T64GPRF:$Dest),
                         (ins T64GPRF:$SrcA, i32imm:$Imm8),
                         "sltih_u $Dest, $SrcA, $Imm8",
                         [(set T64GPRF:$Dest, (build_vector (i16 (setult (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                         (i16 imm8:$Imm8))),
                                                            (i16 (setult (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                         (i16 imm8:$Imm8))) ))]>;

def SNEB : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "sneb $Dest, $SrcA, $SrcB",
                      [(set T64GPRF:$Dest, (build_vector (i8 (setne (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0)),
                                                                    (vector_extract (v4i8 T64GPRF:$SrcB), (i32 0)))),
                                                         (i8 (setne (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1)),
                                                                    (vector_extract (v4i8 T64GPRF:$SrcB), (i32 1)))),
                                                         (i8 (setne (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2)),
                                                                    (vector_extract (v4i8 T64GPRF:$SrcB), (i32 2)))),
                                                         (i8 (setne (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3)),
                                                                    (vector_extract (v4i8 T64GPRF:$SrcB), (i32 3)))) ))]>;

def SNEH : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "sneh $Dest, $SrcA, $SrcB",
                      [(set T64GPRF:$Dest, (build_vector (i16 (setne (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0)),
                                                                     (vector_extract (v2i16 T64GPRF:$SrcB), (i32 0)))),
                                                         (i16 (setne (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1)),
                                                                     (vector_extract (v2i16 T64GPRF:$SrcB), (i32 1)))) ))]>;
*/

def SRAB : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "srab $Dest, $SrcA, $SrcB",
                      [(set (v4i8 T64GPRF:$Dest), (build_vector (sra (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))), (i32 T64GPRF:$SrcB)),
                                                                (sra (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))), (i32 T64GPRF:$SrcB)),
                                                                (sra (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))), (i32 T64GPRF:$SrcB)),
                                                                (sra (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))), (i32 T64GPRF:$SrcB)) ))]>;

def SRAH : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "srah $Dest, $SrcA, $SrcB",
                      [(set (v2i16 T64GPRF:$Dest), (build_vector (sra (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0))), (i32 T64GPRF:$SrcB)),
                                                                 (sra (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1))), (i32 T64GPRF:$SrcB)) ))]>;

def SRAIB : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, i32imm:$ShAmt),
                       "sraib $Dest, $SrcA, $ShAmt",
                       [(set (v4i8 T64GPRF:$Dest), (build_vector (sra (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 0))), (i32 regimm:$ShAmt)),
                                                                 (sra (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 1))), (i32 regimm:$ShAmt)),
                                                                 (sra (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 2))), (i32 regimm:$ShAmt)),
                                                                 (sra (i8 (vector_extract (v4i8 T64GPRF:$SrcA), (i32 3))), (i32 regimm:$ShAmt)) ))]>;

def SRAIH : InstArLT64<(outs T64GPRF:$Dest),
                       (ins T64GPRF:$SrcA, i32imm:$ShAmt),
                       "sraih $Dest, $SrcA, $ShAmt",
                       [(set (v2i16 T64GPRF:$Dest), (build_vector (sra (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 0))), (i32 regimm:$ShAmt)),
                                                                  (sra (i16 (vector_extract (v2i16 T64GPRF:$SrcA), (i32 1))), (i32 regimm:$ShAmt)) ))]>;

def SUBB : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "subb $Dest, $SrcA, $SrcB",
                      [(set (v4i8 T64GPRF:$Dest), (sub T64GPRF:$SrcA, T64GPRF:$SrcB))]>;

//skip SUBBS_U because it's only for TilePro

def SUBH : InstArLT64<(outs T64GPRF:$Dest),
                      (ins T64GPRF:$SrcA, T64GPRF:$SrcB),
                      "subh $Dest, $SrcA, $SrcB",
                      [(set (v2i16 T64GPRF:$Dest), (sub T64GPRF:$SrcA, T64GPRF:$SrcB))]>;

//skip SUBHS because it's only for TilePro

