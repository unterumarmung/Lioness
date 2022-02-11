#pragma once

#include "llvm/Pass.h"

namespace lioness {
llvm::FunctionPass *createFlattenPass();
}
