#ifndef MULL_XCTEST_TASKS_LOAD_BITCODE_FROM_BUFFER_TASK_H
#define MULL_XCTEST_TASKS_LOAD_BITCODE_FROM_BUFFER_TASK_H

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/MemoryBuffer.h>
#include "mull/Diagnostics/Diagnostics.h"
#include "mull/Parallelization/Progress.h"
#include "mull/Bitcode.h"
#include <vector>

namespace mull_xctest {


class LoadBitcodeFromBufferTask {
public:
  using In = std::vector<std::unique_ptr<llvm::MemoryBuffer>>;
  using Out = std::vector<std::unique_ptr<mull::Bitcode>>;
  using iterator = In::iterator;

  LoadBitcodeFromBufferTask(mull::Diagnostics &diagnostics, llvm::LLVMContext &context)
      : diagnostics(diagnostics), context(context) {}

  void operator()(iterator begin, iterator end, Out &storage, mull::progress_counter &counter);

private:
  mull::Diagnostics &diagnostics;
  llvm::LLVMContext &context;
};

}

#endif