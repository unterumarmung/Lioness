#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"

#include "nlohmann/json.hpp"

#include "Common.hpp"

#include <fstream>
#include <memory>
#include <utility>

using namespace nlohmann;
using namespace clang;

namespace {

class LionessFunctionsConsumer : public ASTConsumer {
    CompilerInstance &Instance;
    lioness::FunctionToAttributes functionDeclsWithAttributes;

    static std::string GetAttrName(const Attr *attr) {
        std::string attrName;
        llvm::raw_string_ostream ostream(attrName);
        attr->printPretty(ostream, LangOptions{});
        ostream.flush();

        const auto firstQuotationMark = attrName.find('"');
        const auto secondQuotationMark = attrName.find('"', firstQuotationMark + 1);

        return attrName.substr(firstQuotationMark + 1, secondQuotationMark - firstQuotationMark - 1);
    }

    static bool IsLionessAttribute(const Attr *attr) {
        if (attr == nullptr)
            return false;
        return GetAttrName(attr).find("lioness") != std::string::npos;
    }

    static bool HasLionessAttributes(const FunctionDecl *function) {
        const auto &attrs = function->getAttrs();
        bool hasAttribute = std::any_of(attrs.begin(), attrs.end(), IsLionessAttribute);
        return hasAttribute;
    }

    static std::string GetMangledName(const FunctionDecl *function) {
        auto &context = function->getASTContext();
        std::unique_ptr<MangleContext> mangleContext{context.createMangleContext()};

        if (!mangleContext->shouldMangleDeclName(function)) {
            return function->getNameInfo().getName().getAsString();
        }

        std::string mangledName;
        llvm::raw_string_ostream ostream(mangledName);

        mangleContext->mangleName(function, ostream);

        ostream.flush();

        return mangledName;
    }

  public:
    void AddFunction(const FunctionDecl *function) {
        std::vector<std::string> allAttributes;

        for (const auto *attr : function->getAttrs()) {
            if (attr && IsLionessAttribute(attr)) {
                allAttributes.push_back(GetAttrName(attr));
            }
        }

        functionDeclsWithAttributes[GetMangledName(function)] = std::move(allAttributes);
    }

    ~LionessFunctionsConsumer() override {
        json object(functionDeclsWithAttributes);
        std::ofstream file{"lioness_functions.json"};

        file << std::setw(4) << object;
    }

    explicit LionessFunctionsConsumer(CompilerInstance &Instance) : Instance(Instance) {}

    bool HandleTopLevelDecl(DeclGroupRef declGroup) override {
        for (auto *decl : declGroup) {
            if (const auto *function = dyn_cast<FunctionDecl>(decl); function && HasLionessAttributes(function)) {
                AddFunction(function);
            }
        }

        return true;
    }

    // This functionality is not properly testes yet and based on a clang example
    void HandleTranslationUnit(ASTContext &context) override {
        if (!Instance.getLangOpts().DelayedTemplateParsing)
            return;

        llvm::errs() << "Lioness warning: clang plugin can work unstable with late template parsing\n";

        // This demonstrates how to force instantiation of some templates in
        // -fdelayed-template-parsing mode. (Note: Doing this unconditionally for
        // all templates is similar to not using -fdelayed-template-parsing in the
        // first place.)
        // The advantage of doing this in HandleTranslationUnit() is that all
        // codegen (when using -add-plugin) is completely finished and this can't
        // affect the compiler output.
        struct Visitor : public RecursiveASTVisitor<Visitor> {
            explicit Visitor(LionessFunctionsConsumer *consumer) : consumer{consumer} {}
            bool VisitFunctionDecl(FunctionDecl *FD) const {
                if (FD->isLateTemplateParsed() && HasLionessAttributes(FD))
                    consumer->AddFunction(FD);
                return true;
            }

            LionessFunctionsConsumer *consumer;
        } v{this};
        v.TraverseDecl(context.getTranslationUnitDecl());
    }
};

class LionessJsonAction : public PluginASTAction {
  protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, llvm::StringRef) override {
        return std::make_unique<LionessFunctionsConsumer>(CI);
    }

    bool ParseArgs(const CompilerInstance &CI, const std::vector<std::string> &args) override {
        if (!args.empty() && args[0] == "help")
            PrintHelp(llvm::errs());

        return true;
    }

    void PrintHelp(llvm::raw_ostream &ros) {
        ros << "Plugin that emits json with mangled names and attributes for them";
    }
};

[[maybe_unused]] FrontendPluginRegistry::Add<LionessJsonAction>
    AddLionessJsonAction("emit-json", "Emit json with lioness attributes");

} // namespace
