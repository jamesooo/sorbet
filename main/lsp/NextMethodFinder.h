#ifndef RUBY_TYPER_LSP_NEXTMETHODFINDER_H
#define RUBY_TYPER_LSP_NEXTMETHODFINDER_H

#include "ast/ast.h"

namespace sorbet::realmain::lsp {

class NextMethodFinder {
    const core::Loc queryLoc;

    core::MethodRef result_;
    std::vector<bool> ownerContainsQueryLoc;

public:
    NextMethodFinder(core::Loc queryLoc)
        : queryLoc(queryLoc), result_(core::Symbols::noMethod()), ownerContainsQueryLoc(std::vector<bool>{}) {}

    ast::ExpressionPtr preTransformClassDef(core::Context ctx, ast::ExpressionPtr tree);
    ast::ExpressionPtr postTransformClassDef(core::Context ctx, ast::ExpressionPtr tree);
    ast::ExpressionPtr preTransformMethodDef(core::Context ctx, ast::ExpressionPtr tree);

    const core::MethodRef result() const;
};
}; // namespace sorbet::realmain::lsp

#endif // RUBY_TYPER_LSP_NEXTMETHODFINDER_H
