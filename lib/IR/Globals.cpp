//===-- Globals.cpp - Implement the GlobalValue & GlobalVariable class ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the GlobalValue & GlobalVariable classes for the IR
// library.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/GlobalValue.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/ErrorHandling.h"
using namespace llvm;

//===----------------------------------------------------------------------===//
//                            GlobalValue Class
//===----------------------------------------------------------------------===//

bool GlobalValue::isMaterializable() const {
  if (const Function *F = dyn_cast<Function>(this))
    return F->isMaterializable();
  return false;
}
bool GlobalValue::isDematerializable() const {
  return getParent() && getParent()->isDematerializable(this);
}
std::error_code GlobalValue::materialize() {
  return getParent()->materialize(this);
}
void GlobalValue::Dematerialize() {
  getParent()->Dematerialize(this);
}

const DataLayout *GlobalValue::getDataLayout() const {
  return getParent()->getDataLayout();
}

/// Override destroyConstant to make sure it doesn't get called on
/// GlobalValue's because they shouldn't be treated like other constants.
void GlobalValue::destroyConstant() {
  llvm_unreachable("You can't GV->destroyConstant()!");
}

/// copyAttributesFrom - copy all additional attributes (those not needed to
/// create a GlobalValue) from the GlobalValue Src to this one.
void GlobalValue::copyAttributesFrom(const GlobalValue *Src) {
  setVisibility(Src->getVisibility());
  setUnnamedAddr(Src->hasUnnamedAddr());
  setDLLStorageClass(Src->getDLLStorageClass());
}

unsigned GlobalValue::getAlignment() const {
  if (auto *GA = dyn_cast<GlobalAlias>(this)) {
    // In general we cannot compute this at the IR level, but we try.
    if (const GlobalObject *GO = GA->getBaseObject())
      return GO->getAlignment();

    // FIXME: we should also be able to handle:
    // Alias = Global + Offset
    // Alias = Absolute
    return 0;
  }
  return cast<GlobalObject>(this)->getAlignment();
}

void GlobalObject::setAlignment(unsigned Align) {
  assert((Align & (Align-1)) == 0 && "Alignment is not a power of 2!");
  assert(Align <= MaximumAlignment &&
         "Alignment is greater than MaximumAlignment!");
  unsigned AlignmentData = Log2_32(Align) + 1;
  unsigned OldData = getGlobalValueSubClassData();
  setGlobalValueSubClassData((OldData & ~AlignmentMask) | AlignmentData);
  assert(getAlignment() == Align && "Alignment representation error!");
}

unsigned GlobalObject::getGlobalObjectSubClassData() const {
  unsigned ValueData = getGlobalValueSubClassData();
  return ValueData >> AlignmentBits;
}

void GlobalObject::setGlobalObjectSubClassData(unsigned Val) {
  unsigned OldData = getGlobalValueSubClassData();
  setGlobalValueSubClassData((OldData & AlignmentMask) |
                             (Val << AlignmentBits));
  assert(getGlobalObjectSubClassData() == Val && "representation error");
}

void GlobalObject::copyAttributesFrom(const GlobalValue *Src) {
  const auto *GV = cast<GlobalObject>(Src);
  GlobalValue::copyAttributesFrom(GV);
  setAlignment(GV->getAlignment());
  setSection(GV->getSection());
}

const char *GlobalValue::getSection() const {
  if (auto *GA = dyn_cast<GlobalAlias>(this)) {
    // In general we cannot compute this at the IR level, but we try.
    if (const GlobalObject *GO = GA->getBaseObject())
      return GO->getSection();
    return "";
  }
  return cast<GlobalObject>(this)->getSection();
}

Comdat *GlobalValue::getComdat() {
  if (auto *GA = dyn_cast<GlobalAlias>(this)) {
    // In general we cannot compute this at the IR level, but we try.
    if (const GlobalObject *GO = GA->getBaseObject())
      return const_cast<GlobalObject *>(GO)->getComdat();
    return nullptr;
  }
  return cast<GlobalObject>(this)->getComdat();
}

void GlobalObject::setSection(StringRef S) { Section = S; }

bool GlobalValue::isDeclaration() const {
  // Globals are definitions if they have an initializer.
  if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(this))
    return GV->getNumOperands() == 0;

  // Functions are definitions if they have a body.
  if (const Function *F = dyn_cast<Function>(this))
    return F->empty() && !F->isMaterializable();

  // Aliases are always definitions.
  assert(isa<GlobalAlias>(this));
  return false;
}

//===----------------------------------------------------------------------===//
// GlobalVariable Implementation
//===----------------------------------------------------------------------===//

GlobalVariable::GlobalVariable(Type *Ty, bool constant, LinkageTypes Link,
                               Constant *InitVal, const Twine &Name,
                               ThreadLocalMode TLMode, unsigned AddressSpace,
                               bool isExternallyInitialized)
    : GlobalObject(PointerType::get(Ty, AddressSpace), Value::GlobalVariableVal,
                   OperandTraits<GlobalVariable>::op_begin(this),
                   InitVal != nullptr, Link, Name),
      isConstantGlobal(constant),
      isExternallyInitializedConstant(isExternallyInitialized) {
  setThreadLocalMode(TLMode);
  if (InitVal) {
    assert(InitVal->getType() == Ty &&
           "Initializer should be the same type as the GlobalVariable!");
    Op<0>() = InitVal;
  }
}

GlobalVariable::GlobalVariable(Module &M, Type *Ty, bool constant,
                               LinkageTypes Link, Constant *InitVal,
                               const Twine &Name, GlobalVariable *Before,
                               ThreadLocalMode TLMode, unsigned AddressSpace,
                               bool isExternallyInitialized)
    : GlobalObject(PointerType::get(Ty, AddressSpace), Value::GlobalVariableVal,
                   OperandTraits<GlobalVariable>::op_begin(this),
                   InitVal != nullptr, Link, Name),
      isConstantGlobal(constant),
      isExternallyInitializedConstant(isExternallyInitialized) {
  setThreadLocalMode(TLMode);
  if (InitVal) {
    assert(InitVal->getType() == Ty &&
           "Initializer should be the same type as the GlobalVariable!");
    Op<0>() = InitVal;
  }

  if (Before)
    Before->getParent()->getGlobalList().insert(Before, this);
  else
    M.getGlobalList().push_back(this);
}

void GlobalVariable::setParent(Module *parent) {
  Parent = parent;
}

void GlobalVariable::removeFromParent() {
  getParent()->getGlobalList().remove(this);
}

void GlobalVariable::eraseFromParent() {
  getParent()->getGlobalList().erase(this);
}

void GlobalVariable::replaceUsesOfWithOnConstant(Value *From, Value *To,
                                                 Use *U) {
  // If you call this, then you better know this GVar has a constant
  // initializer worth replacing. Enforce that here.
  assert(getNumOperands() == 1 &&
         "Attempt to replace uses of Constants on a GVar with no initializer");

  // And, since you know it has an initializer, the From value better be
  // the initializer :)
  assert(getOperand(0) == From &&
         "Attempt to replace wrong constant initializer in GVar");

  // And, you better have a constant for the replacement value
  assert(isa<Constant>(To) &&
         "Attempt to replace GVar initializer with non-constant");

  // Okay, preconditions out of the way, replace the constant initializer.
  this->setOperand(0, cast<Constant>(To));
}

void GlobalVariable::setInitializer(Constant *InitVal) {
  if (!InitVal) {
    if (hasInitializer()) {
      Op<0>().set(nullptr);
      NumOperands = 0;
    }
  } else {
    assert(InitVal->getType() == getType()->getElementType() &&
           "Initializer type must match GlobalVariable type");
    if (!hasInitializer())
      NumOperands = 1;
    Op<0>().set(InitVal);
  }
}

/// copyAttributesFrom - copy all additional attributes (those not needed to
/// create a GlobalVariable) from the GlobalVariable Src to this one.
void GlobalVariable::copyAttributesFrom(const GlobalValue *Src) {
  assert(isa<GlobalVariable>(Src) && "Expected a GlobalVariable!");
  GlobalObject::copyAttributesFrom(Src);
  const GlobalVariable *SrcVar = cast<GlobalVariable>(Src);
  setThreadLocalMode(SrcVar->getThreadLocalMode());
  setExternallyInitialized(SrcVar->isExternallyInitialized());
}


//===----------------------------------------------------------------------===//
// GlobalAlias Implementation
//===----------------------------------------------------------------------===//

GlobalAlias::GlobalAlias(Type *Ty, unsigned AddressSpace, LinkageTypes Link,
                         const Twine &Name, Constant *Aliasee,
                         Module *ParentModule)
    : GlobalValue(PointerType::get(Ty, AddressSpace), Value::GlobalAliasVal,
                  &Op<0>(), 1, Link, Name) {
  Op<0>() = Aliasee;

  if (ParentModule)
    ParentModule->getAliasList().push_back(this);
}

GlobalAlias *GlobalAlias::create(Type *Ty, unsigned AddressSpace,
                                 LinkageTypes Link, const Twine &Name,
                                 Constant *Aliasee, Module *ParentModule) {
  return new GlobalAlias(Ty, AddressSpace, Link, Name, Aliasee, ParentModule);
}

GlobalAlias *GlobalAlias::create(Type *Ty, unsigned AddressSpace,
                                 LinkageTypes Linkage, const Twine &Name,
                                 Module *Parent) {
  return create(Ty, AddressSpace, Linkage, Name, nullptr, Parent);
}

GlobalAlias *GlobalAlias::create(Type *Ty, unsigned AddressSpace,
                                 LinkageTypes Linkage, const Twine &Name,
                                 GlobalValue *Aliasee) {
  return create(Ty, AddressSpace, Linkage, Name, Aliasee, Aliasee->getParent());
}

GlobalAlias *GlobalAlias::create(LinkageTypes Link, const Twine &Name,
                                 GlobalValue *Aliasee) {
  PointerType *PTy = Aliasee->getType();
  return create(PTy->getElementType(), PTy->getAddressSpace(), Link, Name,
                Aliasee);
}

GlobalAlias *GlobalAlias::create(const Twine &Name, GlobalValue *Aliasee) {
  return create(Aliasee->getLinkage(), Name, Aliasee);
}

void GlobalAlias::setParent(Module *parent) {
  Parent = parent;
}

void GlobalAlias::removeFromParent() {
  getParent()->getAliasList().remove(this);
}

void GlobalAlias::eraseFromParent() {
  getParent()->getAliasList().erase(this);
}

void GlobalAlias::setAliasee(Constant *Aliasee) {
  assert((!Aliasee || Aliasee->getType() == getType()) &&
         "Alias and aliasee types should match!");
  setOperand(0, Aliasee);
}
