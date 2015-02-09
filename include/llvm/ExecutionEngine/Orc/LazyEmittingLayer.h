//===- LazyEmittingLayer.h - Lazily emit IR to lower JIT layers -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Contains the definition for a lazy-emitting layer for the JIT.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_LAZYEMITTINGLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_LAZYEMITTINGLAYER_H

#include "JITSymbol.h"
#include "LookasideRTDyldMM.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Mangler.h"
#include <list>

namespace llvm {

/// @brief Lazy-emitting IR layer.
///
///   This layer accepts sets of LLVM IR Modules (via addModuleSet), but does
/// not immediately emit them the layer below. Instead, emissing to the base
/// layer is deferred until the first time the client requests the address
/// (via JITSymbol::getAddress) for a symbol contained in this layer.
template <typename BaseLayerT> class LazyEmittingLayer {
public:
  typedef typename BaseLayerT::ModuleSetHandleT BaseLayerHandleT;

private:
  class EmissionDeferredSet {
  public:
    EmissionDeferredSet() : EmitState(NotEmitted) {}
    virtual ~EmissionDeferredSet() {}

    JITSymbol find(StringRef Name, bool ExportedSymbolsOnly, BaseLayerT &B) {
      switch (EmitState) {
      case NotEmitted:
        if (provides(Name, ExportedSymbolsOnly))
          return JITSymbol(
              [this,ExportedSymbolsOnly,Name,&B]() -> TargetAddress {
                if (this->EmitState == Emitting)
                  return 0;
                else if (this->EmitState != Emitted) {
                  this->EmitState = Emitting;
                  Handle = this->emit(B);
                  this->EmitState = Emitted;
                }
                return B.findSymbolIn(Handle, Name, ExportedSymbolsOnly)
                          .getAddress();
              });
        else
          return nullptr;
      case Emitting:
        // Calling "emit" can trigger external symbol lookup (e.g. to check for
        // pre-existing definitions of common-symbol), but it will never find in
        // this module that it would not have found already, so return null from
        // here.
        return nullptr;
      case Emitted:
        return B.findSymbolIn(Handle, Name, ExportedSymbolsOnly);
      }
      llvm_unreachable("Invalid emit-state.");
    }

    void removeModulesFromBaseLayer(BaseLayerT &BaseLayer) {
      if (EmitState != NotEmitted)
        BaseLayer.removeModuleSet(Handle);
    }

    template <typename ModuleSetT>
    static std::unique_ptr<EmissionDeferredSet>
    create(BaseLayerT &B, ModuleSetT Ms,
           std::unique_ptr<RTDyldMemoryManager> MM);

  protected:
    virtual bool provides(StringRef Name, bool ExportedSymbolsOnly) const = 0;
    virtual BaseLayerHandleT emit(BaseLayerT &BaseLayer) = 0;

  private:
    enum { NotEmitted, Emitting, Emitted } EmitState;
    BaseLayerHandleT Handle;
  };

  template <typename ModuleSetT>
  class EmissionDeferredSetImpl : public EmissionDeferredSet {
  public:
    EmissionDeferredSetImpl(ModuleSetT Ms,
                            std::unique_ptr<RTDyldMemoryManager> MM)
        : Ms(std::move(Ms)), MM(std::move(MM)) {}

  protected:
    BaseLayerHandleT emit(BaseLayerT &BaseLayer) override {
      // We don't need the mangled names set any more: Once we've emitted this
      // to the base layer we'll just look for symbols there.
      MangledNames.reset();
      return BaseLayer.addModuleSet(std::move(Ms), std::move(MM));
    }

    bool provides(StringRef Name, bool ExportedSymbolsOnly) const override {
      // FIXME: We could clean all this up if we had a way to reliably demangle
      //        names: We could just demangle name and search, rather than
      //        mangling everything else.

      // If we have already built the mangled name set then just search it.
      if (MangledNames) {
        auto VI = MangledNames->find(Name);
        if (VI == MangledNames->end())
          return false;
        return !ExportedSymbolsOnly || VI->second;
      }

      // If we haven't built the mangled name set yet, try to build it. As an
      // optimization this will leave MangledNames set to nullptr if we find
      // Name in the process of building the set.
      buildMangledNames(Name, ExportedSymbolsOnly);
      if (!MangledNames)
        return true;
      return false;
    }

  private:
    // If the mangled name of the given GlobalValue matches the given search
    // name (and its visibility conforms to the ExportedSymbolsOnly flag) then
    // just return 'true'. Otherwise, add the mangled name to the Names map and
    // return 'false'.
    bool addGlobalValue(StringMap<bool> &Names, const GlobalValue &GV,
                        const Mangler &Mang, StringRef SearchName,
                        bool ExportedSymbolsOnly) const {
      // Modules don't "provide" decls or common symbols.
      if (GV.isDeclaration() || GV.hasCommonLinkage())
        return false;

      // Mangle the GV name.
      std::string MangledName;
      {
        raw_string_ostream MangledNameStream(MangledName);
        Mang.getNameWithPrefix(MangledNameStream, &GV, false);
      }

      // Check whether this is the name we were searching for, and if it is then
      // bail out early.
      if (MangledName == SearchName)
        if (!ExportedSymbolsOnly || GV.hasDefaultVisibility())
          return true;

      // Otherwise add this to the map for later.
      Names[MangledName] = GV.hasDefaultVisibility();
      return false;
    }

    // Build the MangledNames map. Bails out early (with MangledNames left set
    // to nullptr) if the given SearchName is found while building the map.
    void buildMangledNames(StringRef SearchName,
                           bool ExportedSymbolsOnly) const {
      assert(!MangledNames && "Mangled names map already exists?");

      auto Names = llvm::make_unique<StringMap<bool>>();

      for (const auto &M : Ms) {
        Mangler Mang(M->getDataLayout());

        for (const auto &GV : M->globals())
          if (addGlobalValue(*Names, GV, Mang, SearchName, ExportedSymbolsOnly))
            return;

        for (const auto &F : *M)
          if (addGlobalValue(*Names, F, Mang, SearchName, ExportedSymbolsOnly))
            return;
      }

      MangledNames = std::move(Names);
    }

    ModuleSetT Ms;
    std::unique_ptr<RTDyldMemoryManager> MM;
    mutable std::unique_ptr<StringMap<bool>> MangledNames;
  };

  typedef std::list<std::unique_ptr<EmissionDeferredSet>> ModuleSetListT;

  BaseLayerT &BaseLayer;
  ModuleSetListT ModuleSetList;

public:
  /// @brief Handle to a set of loaded modules.
  typedef typename ModuleSetListT::iterator ModuleSetHandleT;

  /// @brief Construct a lazy emitting layer.
  LazyEmittingLayer(BaseLayerT &BaseLayer) : BaseLayer(BaseLayer) {}

  /// @brief Add the given set of modules to the lazy emitting layer.
  template <typename ModuleSetT>
  ModuleSetHandleT addModuleSet(ModuleSetT Ms,
                                std::unique_ptr<RTDyldMemoryManager> MM) {
    return ModuleSetList.insert(
        ModuleSetList.end(),
        EmissionDeferredSet::create(BaseLayer, std::move(Ms), std::move(MM)));
  }

  /// @brief Remove the module set represented by the given handle.
  ///
  ///   This method will free the memory associated with the given module set,
  /// both in this layer, and the base layer.
  void removeModuleSet(ModuleSetHandleT H) {
    (*H)->removeModulesFromBaseLayer(BaseLayer);
    ModuleSetList.erase(H);
  }

  /// @brief Search for the given named symbol.
  /// @param Name The name of the symbol to search for.
  /// @param ExportedSymbolsOnly If true, search only for exported symbols.
  /// @return A handle for the given named symbol, if it exists.
  JITSymbol findSymbol(const std::string &Name, bool ExportedSymbolsOnly) {
    // Look for the symbol among existing definitions.
    if (auto Symbol = BaseLayer.findSymbol(Name, ExportedSymbolsOnly))
      return Symbol;

    // If not found then search the deferred sets. If any of these contain a
    // definition of 'Name' then they will return a JITSymbol that will emit
    // the corresponding module when the symbol address is requested.
    for (auto &DeferredSet : ModuleSetList)
      if (auto Symbol = DeferredSet->find(Name, ExportedSymbolsOnly, BaseLayer))
        return Symbol;

    // If no definition found anywhere return a null symbol.
    return nullptr;
  }

  /// @brief Get the address of the given symbol in the context of the set of
  ///        compiled modules represented by the handle H.
  JITSymbol findSymbolIn(ModuleSetHandleT H, const std::string &Name,
                         bool ExportedSymbolsOnly) {
    return (*H)->find(Name, ExportedSymbolsOnly, BaseLayer);
  }
};

template <typename BaseLayerT>
template <typename ModuleSetT>
std::unique_ptr<typename LazyEmittingLayer<BaseLayerT>::EmissionDeferredSet>
LazyEmittingLayer<BaseLayerT>::EmissionDeferredSet::create(
    BaseLayerT &B, ModuleSetT Ms, std::unique_ptr<RTDyldMemoryManager> MM) {
  return llvm::make_unique<EmissionDeferredSetImpl<ModuleSetT>>(std::move(Ms),
                                                                std::move(MM));
}
}

#endif // LLVM_EXECUTIONENGINE_ORC_LAZYEMITTINGLAYER_H
