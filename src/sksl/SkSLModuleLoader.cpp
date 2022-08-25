/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/private/SkMutex.h"
#include "include/private/SkSLModifiers.h"
#include "include/sksl/SkSLPosition.h"
#include "src/sksl/SkSLBuiltinTypes.h"
#include "src/sksl/SkSLModifiersPool.h"
#include "src/sksl/SkSLModuleLoader.h"
#include "src/sksl/SkSLParsedModule.h"
#include "src/sksl/ir/SkSLSymbolTable.h"
#include "src/sksl/ir/SkSLType.h"
#include "src/sksl/ir/SkSLVariable.h"

#include <type_traits>

namespace SkSL {

#define TYPE(t) &BuiltinTypes::f ## t

static constexpr BuiltinTypePtr kRootTypes[] = {
    TYPE(Void),

    TYPE( Float), TYPE( Float2), TYPE( Float3), TYPE( Float4),
    TYPE(  Half), TYPE(  Half2), TYPE(  Half3), TYPE(  Half4),
    TYPE(   Int), TYPE(   Int2), TYPE(   Int3), TYPE(   Int4),
    TYPE(  UInt), TYPE(  UInt2), TYPE(  UInt3), TYPE(  UInt4),
    TYPE( Short), TYPE( Short2), TYPE( Short3), TYPE( Short4),
    TYPE(UShort), TYPE(UShort2), TYPE(UShort3), TYPE(UShort4),
    TYPE(  Bool), TYPE(  Bool2), TYPE(  Bool3), TYPE(  Bool4),

    TYPE(Float2x2), TYPE(Float2x3), TYPE(Float2x4),
    TYPE(Float3x2), TYPE(Float3x3), TYPE(Float3x4),
    TYPE(Float4x2), TYPE(Float4x3), TYPE(Float4x4),

    TYPE(Half2x2),  TYPE(Half2x3),  TYPE(Half2x4),
    TYPE(Half3x2),  TYPE(Half3x3),  TYPE(Half3x4),
    TYPE(Half4x2),  TYPE(Half4x3),  TYPE(Half4x4),

    TYPE(SquareMat), TYPE(SquareHMat),
    TYPE(Mat),       TYPE(HMat),

    // TODO(skia:12349): generic short/ushort
    TYPE(GenType),   TYPE(GenIType), TYPE(GenUType),
    TYPE(GenHType),   /* (GenSType)      (GenUSType) */
    TYPE(GenBType),
    TYPE(IntLiteral),
    TYPE(FloatLiteral),

    TYPE(Vec),     TYPE(IVec),     TYPE(UVec),
    TYPE(HVec),    TYPE(SVec),     TYPE(USVec),
    TYPE(BVec),

    TYPE(ColorFilter),
    TYPE(Shader),
    TYPE(Blender),
};

static constexpr BuiltinTypePtr kPrivateTypes[] = {
    TYPE(Sampler2D), TYPE(SamplerExternalOES), TYPE(Sampler2DRect),

    TYPE(SubpassInput), TYPE(SubpassInputMS),

    TYPE(Sampler),
    TYPE(Texture2D),
};

#undef TYPE

struct ModuleLoader::Impl {
    Impl();

    void makeRootSymbolTable();

    // This mutex is taken when ModuleLoader::Get is called, and released when the returned
    // ModuleLoader object falls out of scope.
    SkMutex fMutex;
    const BuiltinTypes fBuiltinTypes;
    ModifiersPool fCoreModifiers;

    ParsedModule fRootModule;
    std::shared_ptr<SymbolTable> fRootSymbolTableWithPublicTypes;
};

ModuleLoader ModuleLoader::Get() {
    static ModuleLoader::Impl* sModuleLoaderImpl = new ModuleLoader::Impl;
    return ModuleLoader(*sModuleLoaderImpl);
}

ModuleLoader::ModuleLoader(ModuleLoader::Impl& m) : fModuleLoader(m) {
    fModuleLoader.fMutex.acquire();
}

ModuleLoader::~ModuleLoader() {
    fModuleLoader.fMutex.release();
}

ModuleLoader::Impl::Impl() {
    this->makeRootSymbolTable();
}

void ModuleLoader::AddPublicTypeAliases(SkSL::SymbolTable* symbols,
                                        const SkSL::BuiltinTypes& types) {
    // Add some aliases to the runtime effect modules so that it's friendlier, and more like GLSL.
    symbols->addWithoutOwnership(types.fVec2.get());
    symbols->addWithoutOwnership(types.fVec3.get());
    symbols->addWithoutOwnership(types.fVec4.get());

    symbols->addWithoutOwnership(types.fIVec2.get());
    symbols->addWithoutOwnership(types.fIVec3.get());
    symbols->addWithoutOwnership(types.fIVec4.get());

    symbols->addWithoutOwnership(types.fBVec2.get());
    symbols->addWithoutOwnership(types.fBVec3.get());
    symbols->addWithoutOwnership(types.fBVec4.get());

    symbols->addWithoutOwnership(types.fMat2.get());
    symbols->addWithoutOwnership(types.fMat3.get());
    symbols->addWithoutOwnership(types.fMat4.get());

    symbols->addWithoutOwnership(types.fMat2x2.get());
    symbols->addWithoutOwnership(types.fMat2x3.get());
    symbols->addWithoutOwnership(types.fMat2x4.get());
    symbols->addWithoutOwnership(types.fMat3x2.get());
    symbols->addWithoutOwnership(types.fMat3x3.get());
    symbols->addWithoutOwnership(types.fMat3x4.get());
    symbols->addWithoutOwnership(types.fMat4x2.get());
    symbols->addWithoutOwnership(types.fMat4x3.get());
    symbols->addWithoutOwnership(types.fMat4x4.get());

    // Hide all the private symbols by aliasing them all to "invalid". This will prevent code from
    // using built-in names like `sampler2D` as variable names.
    for (BuiltinTypePtr privateType : ModuleLoader::PrivateTypeList()) {
        symbols->add(Type::MakeAliasType((types.*privateType)->name(), *types.fInvalid));
    }
    symbols->add(Type::MakeAliasType("sk_Caps", *types.fInvalid));
}

const BuiltinTypes& ModuleLoader::builtinTypes() {
    return fModuleLoader.fBuiltinTypes;
}

ModifiersPool& ModuleLoader::coreModifiers() {
    return fModuleLoader.fCoreModifiers;
}

const ParsedModule& ModuleLoader::rootModule() {
    return fModuleLoader.fRootModule;
}

std::shared_ptr<SymbolTable>& ModuleLoader::rootSymbolTableWithPublicTypes() {
    if (!fModuleLoader.fRootSymbolTableWithPublicTypes) {
        fModuleLoader.fRootSymbolTableWithPublicTypes =
                std::make_shared<SymbolTable>(this->rootModule().fSymbols, /*builtin=*/true);
        AddPublicTypeAliases(fModuleLoader.fRootSymbolTableWithPublicTypes.get(),
                             this->builtinTypes());
    }
    return fModuleLoader.fRootSymbolTableWithPublicTypes;
}

SkSpan<const BuiltinTypePtr> ModuleLoader::RootTypeList() {
    return kRootTypes;
}

SkSpan<const BuiltinTypePtr> ModuleLoader::PrivateTypeList() {
    return kPrivateTypes;
}

void ModuleLoader::Impl::makeRootSymbolTable() {
    fRootModule.fSymbols = std::make_shared<SymbolTable>(/*builtin=*/true);

    for (BuiltinTypePtr rootType : kRootTypes) {
        fRootModule.fSymbols->addWithoutOwnership((fBuiltinTypes.*rootType).get());
    }

    for (BuiltinTypePtr privateType : kPrivateTypes) {
        fRootModule.fSymbols->addWithoutOwnership((fBuiltinTypes.*privateType).get());
    }

    // sk_Caps is "builtin", but all references to it are resolved to Settings, so we don't need to
    // treat it as builtin (ie, no need to clone it into the Program).
    fRootModule.fSymbols->add(std::make_unique<Variable>(/*pos=*/Position(),
                                                         /*modifiersPosition=*/Position(),
                                                         fCoreModifiers.add(Modifiers{}),
                                                         "sk_Caps",
                                                         fBuiltinTypes.fSkCaps.get(),
                                                         /*builtin=*/false,
                                                         Variable::Storage::kGlobal));
}

}  // namespace SkSL
