#include "Flatten/Flatten.hpp"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "nlohmann/json.hpp"

#include "CommonMiddleEnd.hpp"

#include <fstream>

using namespace nlohmann;
using namespace llvm;

namespace {

void parseJson() {
    std::ifstream file("lioness_functions.json");
    json object;
    file >> object;

    for (auto &function : object.items()) {
        for (auto &attribute : function.value()) {
            lioness::functionToAttributes[function.key()].push_back(attribute.get<std::string>());
        }
    }
}

void loadPass(const PassManagerBuilder &Builder, legacy::PassManagerBase &PM) {
    parseJson();
    PM.add(lioness::createFlattenPass());
}

[[maybe_unused]] RegisterStandardPasses foo(PassManagerBuilder::EP_EarlyAsPossible, loadPass);
} // namespace