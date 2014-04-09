//===-- SparcTargetInfo.cpp - Sparc Target Implementation -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Sparc.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

Target llvm::TheSparcTarget;
Target llvm::TheSparcV9Target;

// 这些函数什么时候被调用？
// 声明成extern "C"是因为什么？
extern "C" void LLVMInitializeSparcTargetInfo() {
  RegisterTarget<Triple::sparc, /*HasJIT=*/ true>
    X(TheSparcTarget, "sparc", "Sparc");
  RegisterTarget<Triple::sparcv9, /*HasJIT=*/ true>
    Y(TheSparcV9Target, "sparcv9", "Sparc V9");
}
