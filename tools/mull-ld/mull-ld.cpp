#include "LinkerInvocation.h"
#include "MullXCTest/SwiftSupport/SyntaxMutationFinder.h"
#include "MullXCTest/SwiftSupport/SyntaxMutationFilter.h"
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <mull/Config/Configuration.h>
#include <mull/Diagnostics/Diagnostics.h>
#include <mull/Filters/FilePathFilter.h>
#include <mull/Filters/Filter.h>
#include <mull/Filters/Filters.h>
#include <mull/Filters/JunkMutationFilter.h>
#include <mull/Filters/NoDebugInfoFilter.h>
#include <mull/MutationsFinder.h>
#include <mull/Mutators/MutatorsFactory.h>
#include <mull/Parallelization/TaskExecutor.h>
#include <mull/Toolchain/Runner.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace llvm::cl;

OptionCategory MullLDCategory("mull-ld");

opt<std::string> Linker("linker", desc("Linker program"), value_desc("string"),
                        Optional, cat(MullLDCategory));

opt<bool> DebugEnabled("debug",
                       desc("Enables Debug Mode: more logs are printed"),
                       Optional, init(false), cat(MullLDCategory));

void extractBitcodeFiles(std::vector<std::string> &args,
                         std::vector<llvm::StringRef> &bitcodeFiles) {
  for (const auto &rawArg : args) {
    llvm::StringRef arg(rawArg);
    if (arg.endswith(".o")) {
      bitcodeFiles.push_back(arg);
    }
  }
}

static void validateInputFiles(const std::vector<llvm::StringRef> &inputFiles) {
  for (const auto inputFile : inputFiles) {
    if (access(inputFile.str().c_str(), R_OK) != 0) {
      perror(inputFile.str().c_str());
      exit(1);
    }
  }
}

static void validateConfiguration(const mull::Configuration &configuration,
                                  mull::Diagnostics &diags) {
  if (configuration.linker.empty()) {
    diags.error(
        "No linker specified. Please set --linker option in MULL_XCTEST_ARGS"
        "environment variable.");
  }
}

llvm::Optional<std::string> getLinkerPath(mull::Diagnostics &diagnositcs) {
  if (!Linker.empty()) {
    return std::string(Linker);
  }
  mull::Runner runner(diagnositcs);
  auto result =
      runner.runProgram("/usr/bin/xcrun", {"-find", "ld"}, {}, -1, true);
  if (result.status != mull::Passed) {
    diagnositcs.error("failed to run xcrun");
    return llvm::None;
  }
  std::string resultOutput = result.stdoutOutput;
  if (resultOutput.back() == '\n') {
    resultOutput.pop_back();
  }
  return resultOutput;
}

void bootstrapFilters(
    mull::Filters &filters,
    mull::Diagnostics &diagnostics,
    std::vector<std::unique_ptr<mull::Filter>> &filterStorage) {
  auto *noDebugInfoFilter = new mull::NoDebugInfoFilter;
  auto *filePathFilter = new mull::FilePathFilter;
  filterStorage.emplace_back(noDebugInfoFilter);
  filterStorage.emplace_back(filePathFilter);

  filters.mutationFilters.push_back(noDebugInfoFilter);
  filters.functionFilters.push_back(noDebugInfoFilter);
  filters.instructionFilters.push_back(noDebugInfoFilter);

  filters.mutationFilters.push_back(filePathFilter);
  filters.functionFilters.push_back(filePathFilter);

  using namespace mull_xctest::swift;
  SyntaxMutationFinder finder;
  auto storage = std::make_unique<SourceStorage>();

  auto *syntaxFilter = new SyntaxMutationFilter(diagnostics, std::move(storage));
  filterStorage.emplace_back(syntaxFilter);

  filters.mutationFilters.push_back(syntaxFilter);
}

void bootstrapConfiguration(mull::Configuration &configuration,
                            mull::Diagnostics &diagnostics) {
  if (const auto linker = getLinkerPath(diagnostics)) {
    configuration.linker = linker.getValue();
  } else {
    diagnostics.error("no real linker found");
  }
  configuration.debugEnabled = DebugEnabled;
  configuration.linkerTimeout = mull::MullDefaultLinkerTimeoutMilliseconds;
  configuration.timeout = mull::MullDefaultTimeoutMilliseconds;
}

int main(int argc, char **argv) {
  mull::Diagnostics diagnostics;
  std::vector<llvm::StringRef> inputObjects;
  std::vector<std::unique_ptr<llvm::MemoryBuffer>> bitcodeBuffers;

  bool validOptions = llvm::cl::ParseCommandLineOptions(
      1, argv, "", &llvm::errs(), "MULL_XCTEST_ARGS");
  if (!validOptions) {
    return 1;
  }

  if (DebugEnabled) {
    diagnostics.enableDebugMode();
  }

  std::vector<std::string> args(argv + 1, argv + argc);
  extractBitcodeFiles(args, inputObjects);
  validateInputFiles(inputObjects);

  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  std::vector<std::unique_ptr<mull::Filter>> filterStorage;
  mull::Filters filters;
  mull::Configuration configuration;

  bootstrapConfiguration(configuration, diagnostics);

  validateConfiguration(configuration, diagnostics);

  bootstrapFilters(filters, diagnostics, filterStorage);
  mull::MutatorsFactory factory(diagnostics);
  mull::MutationsFinder mutationsFinder(factory.mutators({"cxx_comparison"}),
                                        configuration);

  mull_xctest::LinkerInvocation invocation(
      inputObjects, filters, mutationsFinder, args, diagnostics, configuration);
  invocation.run();
  llvm::llvm_shutdown();
  return 0;
}
