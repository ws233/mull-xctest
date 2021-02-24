#include "LinkerInvocation.h"
#include <mull/FunctionUnderTest.h>
#include <mull/Program/Program.h>
#include <mull/Mutant.h>
#include <mull/MutationsFinder.h>
#include <mull/Mutators/MutatorsFactory.h>
#include <mull/Parallelization/TaskExecutor.h>
#include <mull/Parallelization/Parallelization.h>
#include <mull/Toolchain/Toolchain.h>
#include "MullXCTest/Tasks/ExtractEmbeddedFileTask.h"
#include "MullXCTest/Tasks/LoadBitcodeFromBufferTask.h"
#include <string>
#include <unordered_map>

using namespace mull_xctest;
using namespace mull;

void LinkerInvocation::run() {
    const auto workers = config.parallelization.workers;
    std::vector<std::unique_ptr<llvm::MemoryBuffer>> bitcodeBuffers;

    // Step 1: Extract bitcode section buffer from object files
    std::vector<mull_xctest::ExtractEmbeddedFileTask> extractTasks;
    for (int i = 0; i < workers; i++) {
      extractTasks.emplace_back(diagnostics);
    }
    TaskExecutor<mull_xctest::ExtractEmbeddedFileTask> extractExecutor(
        diagnostics, "Extracting embeded bitcode", inputObjects, bitcodeBuffers, std::move(extractTasks));
    extractExecutor.execute();

    // Step 2: Load LLVM bitcode module from extracted raw buffer
    std::vector<std::unique_ptr<llvm::LLVMContext>> contexts;
    std::vector<mull_xctest::LoadBitcodeFromBufferTask> tasks;
    for (int i = 0; i < workers; i++) {
      auto context = std::make_unique<llvm::LLVMContext>();
      tasks.emplace_back(diagnostics, *context);
      contexts.push_back(std::move(context));
    }
    std::vector<std::unique_ptr<mull::Bitcode>> bitcode;
    mull::TaskExecutor<mull_xctest::LoadBitcodeFromBufferTask> loadExecutor(
        diagnostics, "Loading bitcode files", bitcodeBuffers, bitcode, std::move(tasks));
    loadExecutor.execute();

    mull::Program program(std::move(bitcode));

    // Step 3: Find mutation points from LLVM modules
    auto mutationPoints = findMutationPoints(program);
    std::vector<std::unique_ptr<Mutant>> mutants;
    singleTask.execute("Deduplicate mutants", [&]() {
      std::unordered_map<std::string, std::vector<MutationPoint *>> mapping;
      for (MutationPoint *point : mutationPoints) {
        mapping[point->getUserIdentifier()].push_back(point);
      }
      for (auto &pair : mapping) {
        mutants.push_back(std::make_unique<Mutant>(pair.first, pair.second));
      }
      std::sort(std::begin(mutants), std::end(mutants), MutantComparator());
    });

    // Step 4. Apply mutations
    applyMutation(program, mutationPoints);

    // Step 5. Compile LLVM modules and link them
    mull::Toolchain toolchain(diagnostics, config);
    std::vector<OriginalCompilationTask> compilationTasks;
    compilationTasks.reserve(workers);
    for (int i = 0; i < workers; i++) {
      compilationTasks.emplace_back(toolchain);
    }
    std::vector<std::string> objectFiles;
    TaskExecutor<OriginalCompilationTask> mutantCompiler(diagnostics,
                                                         "Compiling original code",
                                                         program.bitcode(),
                                                         objectFiles,
                                                         std::move(compilationTasks));
    mutantCompiler.execute();

    std::string executable;
    singleTask.execute("Link mutated program",
                       [&]() { executable = toolchain.linker().linkObjectFiles(objectFiles); });

}

std::vector<MutationPoint *> LinkerInvocation::findMutationPoints(Program &program) {
    mull::MutatorsFactory factory(diagnostics);
    mull::MutationsFinder mutationsFinder(factory.mutators({}), config);
    std::vector<FunctionUnderTest> functionsUnderTest;
    for (auto &bitcode : program.bitcode()) {
      for (llvm::Function &function : *bitcode->getModule()) {
        functionsUnderTest.emplace_back(&function, bitcode.get());
      }
    }
    return mutationsFinder.getMutationPoints(diagnostics, program, functionsUnderTest);
}

void LinkerInvocation::applyMutation(Program &program, std::vector<MutationPoint *> &mutationPoints) {
    singleTask.execute("Prepare mutations", [&]() {
      for (auto point : mutationPoints) {
        point->getBitcode()->addMutation(point);
      }
    });

    auto workers = config.parallelization.workers;
    std::vector<int> devNull;
    TaskExecutor<CloneMutatedFunctionsTask> cloneFunctions(
        diagnostics,
        "Cloning functions for mutation",
        program.bitcode(),
        devNull,
        std::vector<CloneMutatedFunctionsTask>(workers));
    cloneFunctions.execute();

    std::vector<int> Nothing;
    TaskExecutor<DeleteOriginalFunctionsTask> deleteOriginalFunctions(
        diagnostics,
        "Removing original functions",
        program.bitcode(),
        Nothing,
        std::vector<DeleteOriginalFunctionsTask>(workers));
    deleteOriginalFunctions.execute();

    TaskExecutor<InsertMutationTrampolinesTask> redirectFunctions(
        diagnostics,
        "Redirect mutated functions",
        program.bitcode(),
        Nothing,
        std::vector<InsertMutationTrampolinesTask>(workers));
    redirectFunctions.execute();

    TaskExecutor<ApplyMutationTask> applyMutations(
        diagnostics, "Applying mutations", mutationPoints, Nothing, { ApplyMutationTask() });
    applyMutations.execute();
}
