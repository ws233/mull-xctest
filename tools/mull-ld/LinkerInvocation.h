#ifndef LinkerInvocation_h
#define LinkerInvocation_h

#include <llvm/ADT/StringRef.h>
#include <mull/Config/Configuration.h>
#include <mull/Diagnostics/Diagnostics.h>
#include <mull/Filters/Filters.h>
#include <mull/FunctionUnderTest.h>
#include <mull/MutationPoint.h>
#include <mull/MutationsFinder.h>
#include <mull/Parallelization/TaskExecutor.h>
#include <mull/Program/Program.h>
#include <vector>

namespace mull_xctest {
struct InvocationConfig {
  llvm::Optional<std::string> DumpLLVM;
  bool EnableSyntaxFilter;
};

class LinkerInvocation {
  std::vector<llvm::StringRef> inputObjects;
  mull::Diagnostics &diagnostics;
  const mull::Configuration &config;
  const InvocationConfig &invocationConfig;
  std::vector<std::string> originalArgs;
  mull::SingleTaskExecutor singleTask;
  struct mull::Filters &filters;
  mull::MutationsFinder &mutationsFinder;
  std::unique_ptr<mull::MutationFilter> syntaxFilterOwner;

public:
  LinkerInvocation(std::vector<llvm::StringRef> inputObjects,
                   struct mull::Filters &filters,
                   mull::MutationsFinder &mutationsFinder,
                   std::vector<std::string> originalArgs,
                   mull::Diagnostics &diagnostics,
                   const mull::Configuration &config,
                   const InvocationConfig &invocationConfig)
      : inputObjects(inputObjects), filters(filters),
        mutationsFinder(mutationsFinder), originalArgs(originalArgs),
        diagnostics(diagnostics), config(config), singleTask(diagnostics),
        invocationConfig(invocationConfig) {}
  void run();

private:
  std::vector<mull::MutationPoint *> findMutationPoints(mull::Program &program);
  void setupSyntaxFilter(std::vector<mull::MutationPoint *> &mutationPoints);
  std::vector<mull::MutationPoint *>
  filterMutations(std::vector<mull::MutationPoint *> mutationPoints);
  void selectInstructions(std::vector<mull::FunctionUnderTest> &functions);
  void applyMutation(mull::Program &program,
                     std::vector<mull::MutationPoint *> &mutationPoints);
  void link(std::vector<std::string> objectFiles);
};
} // namespace mull_xctest

#endif
