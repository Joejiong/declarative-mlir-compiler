#include "Context.h"
#include "Location.h"
#include "dmc/Dynamic/DynamicContext.h"
#include "dmc/Dynamic/DynamicDialect.h"
#include "dmc/Dynamic/DynamicType.h"

#include <pybind11/pybind11.h>

using namespace pybind11;
using namespace mlir;

namespace dmc {
namespace py {

static Type buildDynamicType(
    std::string dialectName, std::string typeName,
    std::vector<Attribute> params, Location loc) {
  auto *dialect = mlir::py::getMLIRContext()->getRegisteredDialect(dialectName);
  if (!dialect)
    throw std::invalid_argument{"Unknown dialect name: " + dialectName};
  auto *dynDialect = dynamic_cast<DynamicDialect *>(dialect);
  if (!dynDialect)
    throw std::invalid_argument{"Not a dynamic dialect: " + dialectName};
  auto *impl = dynDialect->lookupType(typeName);
  if (!impl)
    throw std::invalid_argument{"Unknown type '" + typeName + "' in dialect '" +
                                dialectName + "'"};
  return DynamicType::getChecked(loc, impl, params);
}

void exposeDynamicTypes(module &m) {
  m.def("build_dynamic_type", &buildDynamicType,
        "dialectName"_a, "typeName"_a, "params"_a = std::vector<Attribute>{},
        "location"_a = mlir::py::getUnknownLoc());
}

} // end namespace py
} // end namespace dmc
