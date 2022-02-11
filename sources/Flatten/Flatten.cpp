#include "Flatten.hpp"
#include <llvm-12/llvm/IR/BasicBlock.h>
#include <llvm-12/llvm/IR/Instructions.h>

#include "../CommonMiddleEnd.hpp"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/Local.h"

using namespace llvm;

namespace {

struct SaveOriginalBasicBlocksResult {
    bool success;
    std::vector<BasicBlock *> originalBasicBlocks = {};
};

SaveOriginalBasicBlocksResult saveOriginalBasicBlocks(Function &function) {
    std::vector<BasicBlock *> basicBlocks;

    for (auto &basicBlock : function) {
        const bool isExceptionHandlingBlock = basicBlock.isEHPad() || basicBlock.isLandingPad();
        if (isExceptionHandlingBlock) {
            errs() << "Lioness warning: " << demangle(std::string{function.getName()})
                   << " contains exception handling instructions. Such functions are not supported for flattening!\n";
            return {false};
        }
        const auto *basicBlockTerminator = basicBlock.getTerminator();
        const bool isTerminatedWithBranchOrReturnInst =
            isa<BranchInst>(basicBlockTerminator) || isa<ReturnInst>(basicBlockTerminator);
        if (!isTerminatedWithBranchOrReturnInst) {
            errs() << "Lioness: basic block terminated with not branch or return instruction!\n";
            return {false};
        }
        basicBlocks.push_back(&basicBlock);
    }

    return {true, std::move(basicBlocks)};
}

void splitFirstBasicBlockIfNeeded(BasicBlock *firstBB, std::vector<BasicBlock *> &basicBlocks) {
    auto firstBBTerminator = cast_or_null<BranchInst>(firstBB->getTerminator());
    if (!firstBBTerminator) {
        return;
    }
    if (!firstBBTerminator->isConditional() && firstBBTerminator->getNumSuccessors() <= 1) {
        return;
    }
    auto lastInstIterator = std::prev(firstBB->end());

    if (firstBB->size() > 1) {
        lastInstIterator = std::prev(lastInstIterator);
    }

    auto *newBasicBlock = firstBB->splitBasicBlock(lastInstIterator, "first_bb");
    basicBlocks.insert(basicBlocks.cbegin(), newBasicBlock);
}

void placeBasicBlocksInsideSwitch(SwitchInst *switchInstruction, BasicBlock *loopEndBB,
                                  const std::vector<BasicBlock *> &basicBlocks, PointerType *type) {
    size_t caseCounter = 0;
    for (auto *basicBlock : basicBlocks) {
        basicBlock->moveBefore(loopEndBB);

        auto *caseNumber = cast<ConstantInt>(ConstantInt::get(type->getElementType(), caseCounter));
        switchInstruction->addCase(caseNumber, basicBlock);
        caseCounter += 1;
    }
}

template <size_t BranchIndex> ConstantInt *getNextCaseNumber(SwitchInst *switchInstruction, BasicBlock *basicBlock) {
    auto *successor = basicBlock->getTerminator()->getSuccessor(BranchIndex);

    auto *nextCaseNumber = switchInstruction->findCaseDest(successor);
    if (!nextCaseNumber) {
        // next case == default
        nextCaseNumber = cast<ConstantInt>(
            ConstantInt::get(switchInstruction->getCondition()->getType(), switchInstruction->getNumCases() - 1));
    }

    return nextCaseNumber;
}

// Shamefully borrowed from ../Scalar/RegToMem.cpp :(
bool valueEscapes(Instruction *Inst) {
    BasicBlock *BB = Inst->getParent();
    for (auto UI = Inst->use_begin(), E = Inst->use_end(); UI != E; ++UI) {
        auto *I = cast<Instruction>(*UI);
        if (I->getParent() != BB || isa<PHINode>(I)) {
            return true;
        }
    }
    return false;
}

void fixStack(Function *f) {
    // Try to remove phi node and demote reg to stack
    std::vector<PHINode *> phiNodes;
    std::vector<Instruction *> registers;
    BasicBlock *bbEntry = &*f->begin();

    do {
        phiNodes.clear();
        registers.clear();

        for (auto &i : *f) {
            for (BasicBlock::iterator j = i.begin(); j != i.end(); ++j) {
                if (isa<PHINode>(j)) {
                    auto *phi = cast<PHINode>(j);
                    phiNodes.push_back(phi);
                    continue;
                }
                if (!(isa<AllocaInst>(j) && j->getParent() == bbEntry) &&
                    (valueEscapes(&*j) || j->isUsedOutsideOfBlock(&i))) {
                    registers.push_back(&*j);
                    continue;
                }
            }
        }
        for (auto &i : registers) {
            DemoteRegToStack(*i, f->begin()->getTerminator());
        }

        for (auto &i : phiNodes) {
            DemotePHIToStack(i, f->begin()->getTerminator());
        }

    } while (!registers.empty() || !phiNodes.empty());
}

void flattenFunction(Function &function) {
    //#ifndef LIONESS_STANDALONE
    //    std::unique_ptr<FunctionPass> switchLowerer{createLowerSwitchPass()};
    //    switchLowerer->runOnFunction(function);
    //#endif // LIONESS_STANDALONE

    auto [saveBBSuccess, originalBasicBlocks] = saveOriginalBasicBlocks(function);
    if (!saveBBSuccess) {
        return;
    }
    errs() << demangle(std::string{function.getName()})
           << " has this count of basic blocks: " << originalBasicBlocks.size() << '\n';
    if (!originalBasicBlocks.empty()) {
        // We cannot use the first basic block os the function in the flattening process:
        // In LLVM IR the first basic block of a function is special: it executed unconditionally and cannot have
        // predecessors.
        originalBasicBlocks.erase(originalBasicBlocks.begin());
    }

    if (originalBasicBlocks.empty()) {
        return;
    }

    auto *firstBB = &function.front();
    splitFirstBasicBlockIfNeeded(firstBB, originalBasicBlocks);

    firstBB->getTerminator()->eraseFromParent();

    auto *switchVariable = new AllocaInst(Type::getInt32Ty(function.getContext()), 0, "switch_variable", firstBB);
    new StoreInst(ConstantInt::get(Type::getInt32Ty(function.getContext()), 0), switchVariable, firstBB);

    // Create main loop:
    auto *loopStartBB = BasicBlock::Create(function.getContext(), "loop_start", &function, firstBB);
    auto *loopEndBB = BasicBlock::Create(function.getContext(), "loop_end", &function, firstBB);

    auto *switchVariableLoad =
        new LoadInst(switchVariable->getType()->getElementType(), switchVariable, "switch_variable", loopStartBB);

    firstBB->moveBefore(loopStartBB);
    BranchInst::Create(loopStartBB, firstBB);

    BranchInst::Create(loopStartBB, loopEndBB);

    auto *switchDefaultBB = BasicBlock::Create(function.getContext(), "switch_default", &function, loopEndBB);
    BranchInst::Create(loopEndBB, switchDefaultBB);

    // Create switch instruction
    auto *switchInstruction = SwitchInst::Create(&function.front(), switchDefaultBB, 0, loopStartBB);
    switchInstruction->setCondition(switchVariableLoad);

    function.front().getTerminator()->eraseFromParent();
    BranchInst::Create(loopStartBB, &function.front());

    placeBasicBlocksInsideSwitch(switchInstruction, loopEndBB, originalBasicBlocks, switchVariable->getType());

    for (auto *basicBlock : originalBasicBlocks) {
        const auto basicBlockNumSuccessors = basicBlock->getTerminator()->getNumSuccessors();
        const bool isReturnBB = basicBlockNumSuccessors == 0;
        if (isReturnBB) {
            continue;
        }

        const bool isNonConditionalJumpBB = basicBlockNumSuccessors == 1;
        if (isNonConditionalJumpBB) {
            auto *nextCaseNumber = getNextCaseNumber<0>(switchInstruction, basicBlock);
            basicBlock->getTerminator()->eraseFromParent();
            new StoreInst(nextCaseNumber, switchVariableLoad->getPointerOperand(), basicBlock);
            BranchInst::Create(loopEndBB, basicBlock);
            continue;
        }

        const bool isConditionalJumpBB = basicBlockNumSuccessors == 2;
        if (isConditionalJumpBB) {
            auto *trueCaseNumber = getNextCaseNumber<0>(switchInstruction, basicBlock);
            auto *falseCaseNumber = getNextCaseNumber<1>(switchInstruction, basicBlock);

            auto *branchInst = cast<BranchInst>(basicBlock->getTerminator());
            auto *selectInst = SelectInst::Create(branchInst->getCondition(), trueCaseNumber, falseCaseNumber, "",
                                                  basicBlock->getTerminator());

            basicBlock->getTerminator()->eraseFromParent();

            new StoreInst(selectInst, switchVariableLoad->getPointerOperand(), basicBlock);

            BranchInst::Create(loopEndBB, basicBlock);
        }
    }

    fixStack(&function);
}

struct Flatten : FunctionPass {
    Flatten() : FunctionPass(ID) {}

    bool runOnFunction(Function &function) override {
        if (shouldFlattenFunction(function)) {
            flattenFunction(function);
        }

        return false;
    }

    inline static char ID = 0;

  private:
    static bool shouldFlattenFunction(const Function &function) {
#ifdef LIONESS_STANDALONE
        return true;
#else
        return lioness::functionToAttributes.contains(std::string{function.getName()});
#endif // LIONESS_STANDALONE
    }
};

[[maybe_unused]] RegisterPass<Flatten> FlattenRegistration("flatten", "Control Flow Flattening Pass", false, false);

} // namespace
llvm::FunctionPass *lioness::createFlattenPass() { return new Flatten(); }
