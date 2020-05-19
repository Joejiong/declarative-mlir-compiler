#include "dmc/Spec/SpecDialect.h"
#include "dmc/Traits/Registry.h"

#include <mlir/IR/MLIRContext.h>
#include <mlir/Dialect/StandardOps/IR/Ops.h>

using namespace dmc;

namespace mlir {
namespace py {

/// Wrap the global MLIR context in a singleton class, using member objects
/// to ensure initialization order of dialect registrations. Static objects
/// are not guaranteed by the standard to be initialized in any order.
class GlobalContextHandle {
public:
  static GlobalContextHandle &instance() {
    static GlobalContextHandle instance;
    return instance;
  }

  MLIRContext *getContext() { return &context; }

private:
  /// Initiazation order is guaranteed.
  DialectRegistration<SpecDialect> specDialect;
  DialectRegistration<TraitRegistry> traitRegistry;
  DialectRegistration<StandardOpsDialect> standardOpsDialect;
  MLIRContext context;
};

MLIRContext *getMLIRContext() {
  return GlobalContextHandle::instance().getContext();
}

} // end namespace py
} // end namespace mlir