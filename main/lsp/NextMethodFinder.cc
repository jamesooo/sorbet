#include "NextMethodFinder.h"
#include "core/GlobalState.h"

using namespace std;

namespace sorbet::realmain::lsp {

ast::ExpressionPtr NextMethodFinder::preTransformClassDef(core::Context ctx, ast::ExpressionPtr tree) {
    auto &classDef = ast::cast_tree_nonnull<ast::ClassDef>(tree);

    this->ownerContainsQueryLoc.emplace_back(core::Loc(ctx.file, classDef.loc).contains(this->queryLoc));

    return tree;
}

ast::ExpressionPtr NextMethodFinder::postTransformClassDef(core::Context ctx, ast::ExpressionPtr tree) {
    this->ownerContainsQueryLoc.pop_back();

    return tree;
}

ast::ExpressionPtr NextMethodFinder::preTransformMethodDef(core::Context ctx, ast::ExpressionPtr tree) {
    auto &methodDef = ast::cast_tree_nonnull<ast::MethodDef>(tree);
    ENFORCE(methodDef.symbol.exists());
    ENFORCE(methodDef.symbol != core::Symbols::todoMethod());

    ENFORCE(!this->ownerContainsQueryLoc.empty());

    if (!ownerContainsQueryLoc.back()) {
        // The queryLoc is not contained within our enclosing ClassDef, which means that this method
        // is in an entirely incorrect nesting scope.
        return tree;
    }

    auto currentMethod = methodDef.symbol;

    auto &currentMethodLocs = currentMethod.data(ctx)->locs();
    auto inFileOfQuery = [&](const auto &loc) { return loc.file() == this->queryLoc.file(); };
    auto maybeCurrentLoc = absl::c_find_if(currentMethodLocs, inFileOfQuery);
    if (maybeCurrentLoc == currentMethodLocs.end()) {
        return tree;
    }

    auto currentLoc = *maybeCurrentLoc;
    if (!currentLoc.exists()) {
        // Defensive in case location information is disabled (e.g., certain fuzzer modes)
        return tree;
    }

    ENFORCE(currentLoc.file() == this->queryLoc.file());

    if (currentLoc.beginPos() < queryLoc.beginPos()) {
        // Current method is before query, not after.
        return tree;
    }

    // Current method starts at or after query loc. Starting 'at' is fine, because it can happen in cases like this:
    //   |def foo; end

    if (this->result_.exists()) {
        // Method defs are not guaranteed to be sorted in order by their declLocs
        auto &resultLocs = this->result_.data(ctx)->locs();
        auto maybeResultLoc = absl::c_find_if(resultLocs, inFileOfQuery);
        ENFORCE(maybeResultLoc != resultLocs.end(), "Must exist, because otherwise it wouldn't have been the result_");
        auto resultLoc = *maybeResultLoc;
        if (currentLoc.beginPos() < resultLoc.beginPos()) {
            // Found a method defined after the query but earlier than previous result: overwrite previous result
            this->result_ = currentMethod;
            return tree;
        } else {
            // We've already found an earlier result, so the current is not the first
            return tree;
        }
    } else {
        // Haven't found a result yet, so this one is the best so far.
        this->result_ = currentMethod;
        return tree;
    }
}

const core::MethodRef NextMethodFinder::result() const {
    return this->result_;
}

}; // namespace sorbet::realmain::lsp
