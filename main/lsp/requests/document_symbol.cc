#include "main/lsp/requests/document_symbol.h"
#include "core/lsp/QueryResponse.h"
#include "main/lsp/json_types.h"
#include "main/lsp/lsp.h"

using namespace std;

namespace sorbet::realmain::lsp {

std::unique_ptr<DocumentSymbol> symbolRef2DocumentSymbol(const core::GlobalState &gs, core::SymbolRef symRef,
                                                         core::FileRef filter);

void symbolRef2DocumentSymbolWalkMembers(const core::GlobalState &gs, core::SymbolRef sym, core::FileRef filter,
                                         vector<unique_ptr<DocumentSymbol>> &out) {
    for (auto mem : sym.data(gs)->membersStableOrderSlow(gs)) {
        auto ref = mem.second;
        if (mem.first != core::Names::attached() && mem.first != core::Names::singleton()) {
            bool foundThisFile = false;
            for (auto loc : ref.data(gs)->locs()) {
                foundThisFile = foundThisFile || loc.file() == filter;
            }
            if (!foundThisFile) {
                continue;
            }
            auto inner = symbolRef2DocumentSymbol(gs, ref, filter);
            if (inner) {
                out.push_back(move(inner));
            }
        }
    }
}

std::unique_ptr<DocumentSymbol> symbolRef2DocumentSymbol(const core::GlobalState &gs, core::SymbolRef symRef,
                                                         core::FileRef filter) {
    if (!symRef.exists()) {
        return nullptr;
    }
    auto sym = symRef.data(gs);
    if (!sym->loc().file().exists() || hideSymbol(gs, symRef)) {
        return nullptr;
    }
    auto kind = symbolRef2SymbolKind(gs, symRef);
    // TODO: this range should cover body. Currently it doesn't.
    auto range = Range::fromLoc(gs, sym->loc());
    auto selectionRange = Range::fromLoc(gs, sym->loc());
    if (range == nullptr || selectionRange == nullptr) {
        return nullptr;
    }

    string prefix;
    if (sym->owner.exists() && sym->owner.isClassOrModule() && sym->owner.data(gs)->attachedClass(gs).exists()) {
        prefix = "self.";
    }
    auto result = make_unique<DocumentSymbol>(prefix + sym->name.show(gs), kind, move(range), move(selectionRange));

    // Previous versions of VSCode have a bug that requires this non-optional field to be present.
    // This previously tried to include the method signature but due to issues where large signatures were not readable
    // when put on one line and given that currently details are only visible in the outline view but not seen in the
    // symbol search. Additionally, no other language server implementations we could find used this field.
    result->detail = "";

    vector<unique_ptr<DocumentSymbol>> children;
    symbolRef2DocumentSymbolWalkMembers(gs, symRef, filter, children);
    if (sym->isClassOrModule()) {
        auto singleton = sym->lookupSingletonClass(gs);
        if (singleton.exists()) {
            symbolRef2DocumentSymbolWalkMembers(gs, singleton, filter, children);
        }
    }
    result->children = move(children);
    return result;
}

DocumentSymbolTask::DocumentSymbolTask(const LSPConfiguration &config, MessageId id,
                                       std::unique_ptr<DocumentSymbolParams> params)
    : LSPRequestTask(config, move(id), LSPMethod::TextDocumentDocumentSymbol), params(move(params)) {}

bool DocumentSymbolTask::isDelayable() const {
    return true;
}

bool isOwnerInTheSameFile(const core::GlobalState &gs, core::SymbolRef ref, core::FileRef fref) {
    auto locs = ref.data(gs)->owner.data(gs)->locs();

    for (auto loc : locs) {
        if (loc.file() == fref) {
            return true;
        }
    }

    return false;
}

bool isRefInFile(const core::GlobalState &gs, core::SymbolRef ref, core::FileRef fref) {
    for (auto definitionLocation : ref.data(gs)->locs()) {
        if (definitionLocation.file() == fref) {
            return true;
        }
    }
    return false;
}

unique_ptr<ResponseMessage> DocumentSymbolTask::runRequest(LSPTypecheckerDelegate &typechecker) {
    auto response = make_unique<ResponseMessage>("2.0", id, LSPMethod::TextDocumentDocumentSymbol);
    if (!config.opts.lspDocumentSymbolEnabled) {
        response->error =
            make_unique<ResponseError>((int)LSPErrorCodes::InvalidRequest,
                                       "The `Document Symbol` LSP feature is experimental and disabled by default.");
        return response;
    }

    const core::GlobalState &gs = typechecker.state();
    vector<unique_ptr<DocumentSymbol>> result;
    string_view uri = params->textDocument->uri;
    auto fref = config.uri2FileRef(gs, uri);
    vector<pair<core::SymbolRef::Kind, u4>> symbolTypes = {
        {core::SymbolRef::Kind::ClassOrModule, gs.classAndModulesUsed()},
        {core::SymbolRef::Kind::Method, gs.methodsUsed()},
        {core::SymbolRef::Kind::FieldOrStaticField, gs.fieldsUsed()},
        {core::SymbolRef::Kind::TypeArgument, gs.typeArgumentsUsed()},
        {core::SymbolRef::Kind::TypeMember, gs.typeMembersUsed()},
    };
    for (auto [kind, used] : symbolTypes) {
        for (u4 idx = 1; idx < used; idx++) {
            core::SymbolRef ref(gs, kind, idx);
            // If owner of the ref is in the same file then
            // the ref will be added to the results during a processing of the owner.
            // That happens because the `symbolRef2DocumentSymbol` is recursively called for every child of the ref
            if (!hideSymbol(gs, ref) && (isRefInFile(gs, ref, fref) && !isOwnerInTheSameFile(gs, ref, fref))) {
                auto data = symbolRef2DocumentSymbol(gs, ref, fref);
                if (data) {
                    result.push_back(move(data));
                    break;
                }
            }
        }
    }
    response->result = move(result);
    return response;
}

} // namespace sorbet::realmain::lsp
