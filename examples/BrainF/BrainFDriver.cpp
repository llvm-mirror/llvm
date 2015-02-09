//===-- BrainFDriver.cpp - BrainF compiler driver -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===--------------------------------------------------------------------===//
//
// This program converts the BrainF language into LLVM assembly,
// which it can then run using the JIT or output as BitCode.
//
// This implementation has a tape of 65536 bytes,
// with the head starting in the middle.
// Range checking is off by default, so be careful.
// It can be enabled with -abc.
//
// Use:
// ./BrainF -jit      prog.bf          #Run program now
// ./BrainF -jit -abc prog.bf          #Run program now safely
// ./BrainF           prog.bf          #Write as BitCode
//
// lli prog.bf.bc                      #Run generated BitCode
//
//===--------------------------------------------------------------------===//

#include "BrainF.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>
#include <iostream>
using namespace llvm;

//Command line options

static cl::opt<std::string>
InputFilename(cl::Positional, cl::desc("<input brainf>"));

static cl::opt<std::string>
OutputFilename("o", cl::desc("Output filename"), cl::value_desc("filename"));

static cl::opt<bool>
ArrayBoundsChecking("abc", cl::desc("Enable array bounds checking"));

static cl::opt<bool>
JIT("jit", cl::desc("Run program Just-In-Time"));


//Add main function so can be fully compiled
void addMainFunction(Module *mod) {
  //define i32 @main(i32 %argc, i8 **%argv)
  Function *main_func = cast<Function>(mod->
    getOrInsertFunction("main", IntegerType::getInt32Ty(mod->getContext()),
                        IntegerType::getInt32Ty(mod->getContext()),
                        PointerType::getUnqual(PointerType::getUnqual(
                          IntegerType::getInt8Ty(mod->getContext()))), NULL));
  {
    Function::arg_iterator args = main_func->arg_begin();
    Value *arg_0 = args++;
    arg_0->setName("argc");
    Value *arg_1 = args++;
    arg_1->setName("argv");
  }

  //main.0:
  BasicBlock *bb = BasicBlock::Create(mod->getContext(), "main.0", main_func);

  //call void @brainf()
  {
    CallInst *brainf_call = CallInst::Create(mod->getFunction("brainf"),
                                             "", bb);
    brainf_call->setTailCall(false);
  }

  //ret i32 0
  ReturnInst::Create(mod->getContext(),
                     ConstantInt::get(mod->getContext(), APInt(32, 0)), bb);
}

int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(argc, argv, " BrainF compiler\n");

  LLVMContext &Context = getGlobalContext();

  if (InputFilename == "") {
    errs() << "Error: You must specify the filename of the program to "
    "be compiled.  Use --help to see the options.\n";
    abort();
  }

  //Get the output stream
  raw_ostream *out = &outs();
  if (!JIT) {
    if (OutputFilename == "") {
      std::string base = InputFilename;
      if (InputFilename == "-") { base = "a"; }

      // Use default filename.
      OutputFilename = base+".bc";
    }
    if (OutputFilename != "-") {
      std::error_code EC;
      out = new raw_fd_ostream(OutputFilename, EC, sys::fs::F_None);
    }
  }

  //Get the input stream
  std::istream *in = &std::cin;
  if (InputFilename != "-")
    in = new std::ifstream(InputFilename.c_str());

  //Gather the compile flags
  BrainF::CompileFlags cf = BrainF::flag_off;
  if (ArrayBoundsChecking)
    cf = BrainF::CompileFlags(cf | BrainF::flag_arraybounds);

  //Read the BrainF program
  BrainF bf;
  std::unique_ptr<Module> Mod(bf.parse(in, 65536, cf, Context)); // 64 KiB
  if (in != &std::cin)
    delete in;
  addMainFunction(Mod.get());

  //Verify generated code
  if (verifyModule(*Mod)) {
    errs() << "Error: module failed verification.  This shouldn't happen.\n";
    abort();
  }

  //Write it out
  if (JIT) {
    InitializeNativeTarget();

    outs() << "------- Running JIT -------\n";
    Module &M = *Mod;
    ExecutionEngine *ee = EngineBuilder(std::move(Mod)).create();
    std::vector<GenericValue> args;
    Function *brainf_func = M.getFunction("brainf");
    GenericValue gv = ee->runFunction(brainf_func, args);
  } else {
    WriteBitcodeToFile(Mod.get(), *out);
  }

  //Clean up
  if (out != &outs())
    delete out;

  llvm_shutdown();

  return 0;
}
