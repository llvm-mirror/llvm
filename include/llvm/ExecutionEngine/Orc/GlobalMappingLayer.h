//===---- GlobalMappingLayer.h - Run all IR through a functor ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Convenience layer for injecting symbols that will appear in calls to
// findSymbol.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_GLOBALMAPPINGLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_GLOBALMAPPINGLAYER_H

#include "JITSymbol.h"
#include <map>

namespace llvm {
namespace orc {

/// @brief Global mapping layer.
///
///   This layer overrides the findSymbol method to first search a local symbol
/// table that the client can define. It can be used to inject new symbol
/// mappings into the JIT. Beware, however: symbols within a single IR module or
/// object file will still resolve locally (via RuntimeDyld's symbol table) -
/// such internal references cannot be overriden via this layer.
template <typename BaseLayerT>
class GlobalMappingLayer {
public:
  /// @brief Handle to a set of added modules.
  typedef typename BaseLayerT::ModuleSetHandleT ModuleSetHandleT;

  /// @brief Construct an GlobalMappingLayer with the given BaseLayer
  GlobalMappingLayer(BaseLayerT &BaseLayer) : BaseLayer(BaseLayer) {}

  /// @brief Add the given module set to the JIT.
  /// @return A handle for the added modules.
  template <typename ModuleSetT, typename MemoryManagerPtrT,
            typename SymbolResolverPtrT>
  ModuleSetHandleT addModuleSet(ModuleSetT Ms,
                                MemoryManagerPtrT MemMgr,
                                SymbolResolverPtrT Resolver) {
    return BaseLayer.addModuleSet(std::move(Ms), std::move(MemMgr),
                                  std::move(Resolver));
  }

  /// @brief Remove the module set associated with the handle H.
  void removeModuleSet(ModuleSetHandleT H) { BaseLayer.removeModuleSet(H); }

  /// @brief Manually set the address to return for the given symbol.
  void setGlobalMapping(const std::string &Name, TargetAddress Addr) {
    SymbolTable[Name] = Addr;
  }

  /// @brief Remove the given symbol from the global mapping.
  void eraseGlobalMapping(const std::string &Name) {
    SymbolTable.erase(Name);
  }

  /// @brief Search for the given named symbol.
  ///
  ///          This method will first search the local symbol table, returning
  ///        any symbol found there. If the symbol is not found in the local
  ///        table then this call will be passed through to the base layer.
  ///
  /// @param Name The name of the symbol to search for.
  /// @param ExportedSymbolsOnly If true, search only for exported symbols.
  /// @return A handle for the given named symbol, if it exists.
  JITSymbol findSymbol(const std::string &Name, bool ExportedSymbolsOnly) {
    auto I = SymbolTable.find(Name);
    if (I != SymbolTable.end())
      return JITSymbol(I->second, JITSymbolFlags::Exported);
    return BaseLayer.findSymbol(Name, ExportedSymbolsOnly);
  }

  /// @brief Get the address of the given symbol in the context of the set of
  ///        modules represented by the handle H. This call is forwarded to the
  ///        base layer's implementation.
  /// @param H The handle for the module set to search in.
  /// @param Name The name of the symbol to search for.
  /// @param ExportedSymbolsOnly If true, search only for exported symbols.
  /// @return A handle for the given named symbol, if it is found in the
  ///         given module set.
  JITSymbol findSymbolIn(ModuleSetHandleT H, const std::string &Name,
                         bool ExportedSymbolsOnly) {
    return BaseLayer.findSymbolIn(H, Name, ExportedSymbolsOnly);
  }

  /// @brief Immediately emit and finalize the module set represented by the
  ///        given handle.
  /// @param H Handle for module set to emit/finalize.
  void emitAndFinalize(ModuleSetHandleT H) {
    BaseLayer.emitAndFinalize(H);
  }

private:
  BaseLayerT &BaseLayer;
  std::map<std::string, TargetAddress> SymbolTable;
};

} // End namespace orc.
} // End namespace llvm.

#endif // LLVM_EXECUTIONENGINE_ORC_GLOBALMAPPINGLAYER_H
