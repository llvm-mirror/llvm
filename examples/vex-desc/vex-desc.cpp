//===- YAMLBench - Benchmark the YAMLParser implementation ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This program executes the YAMLParser on differntly sized YAML texts and
// outputs the run time.
//
//===----------------------------------------------------------------------===//


#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/system_error.h"

using namespace llvm;



static cl::opt<std::string>
 Buffer(cl::Positional, cl::desc("<input>"));

#include <iostream>

using namespace llvm;


#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/YAMLTraits.h" 

using llvm::yaml::Input;
using llvm::yaml::Output;
using llvm::yaml::IO;
using llvm::yaml::MappingTraits;
using llvm::yaml::MappingNormalization;
using llvm::yaml::ScalarTraits;
using llvm::yaml::Hex8;
using llvm::yaml::Hex16;
using llvm::yaml::Hex32;
using llvm::yaml::Hex64;

struct FooBar {
  int foo;
  int bar;
};
typedef std::vector<FooBar> FooBarSequence;



struct Config {
  //std::vector<FooBar> FooBars;
  int FooBars;
};
typedef std::vector<Config> ConfigSequence;


namespace llvm {
namespace yaml {
  template <>
  struct MappingTraits<FooBar> {
    static void mapping(IO &IO, FooBar &fb) {
      IO.mapRequired("foo", fb.foo);
      IO.mapRequired("bar", fb.bar);
    }
  };

  template <>
  struct MappingTraits<Config> {
    static void mapping(IO &IO, Config &Cfg) {
      IO.mapRequired("foobar", Cfg.FooBars);

    }
  };

  // template <>
  // struct MappingTraits<FooBar> {
  //   static void mapping(IO &io, FooBar& fb) {
  //     io.mapRequired("foo",    fb.foo);
  //     io.mapRequired("bar",    fb.bar);
  //   }
  // };
}
}



int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);
  if (Buffer.getNumOccurrences()) {
    OwningPtr<MemoryBuffer> Buf;
    if (MemoryBuffer::getFileOrSTDIN(Buffer, Buf))
      return 1;

    using llvm::yaml::Input;
    Config doc;
    Input yin(Buf->getBuffer());
    yin >> doc;
      
    if ( yin.error() )
      return 0;
  
  
    //std::cout << doc.foo << " " << doc.bar << "\n";    

  }



  return 0;
}
