#include "clang/AST/Attr.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"

using namespace clang;

namespace {

struct FlattenAttribute : public ParsedAttrInfo {
    FlattenAttribute() {
        OptArgs = 0;
        // GNU-style __attribute__(("example")) and C++-style [[plugin::example]] supported.
        static constexpr Spelling S[] = {{ParsedAttr::AS_GNU, "lioness.flatten"},
                                         {ParsedAttr::AS_CXX11, "lioness::flatten"}};
        Spellings = S;
    }

    bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr, const Decl *D) const override {
        // This attribute appertains to function only.
        if (!isa<FunctionDecl>(D)) {
            S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str) << Attr << "functions";
            return false;
        }
        return true;
    }

    AttrHandling handleDeclAttribute(Sema &S, Decl *D, const ParsedAttr &Attr) const override {
        // Check if the decl is at file scope.
        if (!D->getDeclContext()->isFileContext()) {
            unsigned ID = S.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error,
                                                             "'lioness::flatten' attribute only allowed at file scope");
            S.Diag(Attr.getLoc(), ID);
            return AttributeNotApplied;
        }

        if (Attr.getNumArgs() > 0) {
            unsigned ID = S.getDiagnostics().getCustomDiagID(
                DiagnosticsEngine::Error, "'lioness::flatten' attribute only accepts zero arguments");
            S.Diag(Attr.getLoc(), ID);
            return AttributeNotApplied;
        }

        // Attach an annotate attribute to the Decl.
        D->addAttr(AnnotateAttr::Create(S.Context, "lioness_flatten", nullptr, 0, Attr.getRange()));

        return AttributeApplied;
    }
};

[[maybe_unused]] ParsedAttrInfoRegistry::Add<FlattenAttribute> AddFlattenAttribute("lioness_flatten", "");

} // namespace