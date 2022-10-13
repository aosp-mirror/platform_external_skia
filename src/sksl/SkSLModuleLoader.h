/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SKSL_MODULELOADER
#define SKSL_MODULELOADER

#include "src/sksl/SkSLBuiltinTypes.h"
#include <memory>

namespace SkSL {

class BuiltinMap;
class Compiler;
class ModifiersPool;
class SymbolTable;
class Type;

using BuiltinTypePtr = const std::unique_ptr<Type> BuiltinTypes::*;

class ModuleLoader {
private:
    struct Impl;
    Impl& fModuleLoader;

public:
    ModuleLoader(ModuleLoader::Impl&);
    ~ModuleLoader();

    // Acquires a mutex-locked reference to the singleton ModuleLoader. When the ModuleLoader is
    // allowed to fall out of scope, the mutex will be released.
    static ModuleLoader Get();

    // The built-in types and root module are universal, immutable, and shared by every Compiler.
    // They are created when the ModuleLoader is instantiated and never change.
    const BuiltinTypes& builtinTypes();
    const BuiltinMap*   rootModule();

    // This is used for testing purposes; it contains root types and public aliases (mat2 for
    // float2x2), and hides private types like sk_Caps.
    std::shared_ptr<SymbolTable>& rootSymbolTableWithPublicTypes();

    // This ModifiersPool is shared by every built-in module.
    ModifiersPool& coreModifiers();

    // These modules are loaded on demand; once loaded, they are kept for the lifetime of the
    // process.
    const BuiltinMap* loadSharedModule(SkSL::Compiler* compiler);
    const BuiltinMap* loadGPUModule(SkSL::Compiler* compiler);
    const BuiltinMap* loadVertexModule(SkSL::Compiler* compiler);
    const BuiltinMap* loadFragmentModule(SkSL::Compiler* compiler);
    const BuiltinMap* loadComputeModule(SkSL::Compiler* compiler);
    const BuiltinMap* loadGraphiteVertexModule(SkSL::Compiler* compiler);
    const BuiltinMap* loadGraphiteFragmentModule(SkSL::Compiler* compiler);

    const BuiltinMap* loadPublicModule(SkSL::Compiler* compiler);
    const BuiltinMap* loadPrivateRTShaderModule(SkSL::Compiler* compiler);

    // This unloads every module. It's useful primarily for benchmarking purposes.
    void unloadModules();
};

}  // namespace SkSL

#endif  // SKSL_MODULELOADER
