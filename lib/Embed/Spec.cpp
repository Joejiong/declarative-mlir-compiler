#include "dmc/Embed/Constraints.h"
#include "dmc/Spec/SpecTypes.h"
#include "dmc/Spec/SpecAttrs.h"

using namespace mlir;

namespace dmc {

namespace detail {

/// Store the constraint expression.
struct PyTypeStorage : public TypeStorage {
  using KeyTy = StringRef;

  explicit PyTypeStorage(KeyTy key) : expr{key} {}
  bool operator==(KeyTy key) const { return key == expr; }
  static llvm::hash_code hashKey(KeyTy key) { return hash_value(key); }

  static PyTypeStorage *construct(TypeStorageAllocator &alloc, KeyTy key) {
    auto expr = alloc.copyInto(key);
    return new (alloc.allocate<PyTypeStorage>()) PyTypeStorage{expr};
  }

  StringRef expr;
};

} // end namespace detail

/// PyType implementation.
PyType PyType::getChecked(Location loc, StringRef expr) {
  return Base::getChecked(loc, Kind, expr);
}

LogicalResult PyType::verifyConstructionInvariants(Location loc,
                                                   StringRef expr) {
  return py::verifyTypeConstraint(loc, expr);
}

LogicalResult PyType::verify(Type ty) {
  return py::evalConstraintExpr(getImpl()->expr, ty);
}

void PyType::print(DialectAsmPrinter &printer) {
  printer << getTypeName() << "<\"" << getImpl()->expr << "\">";
}

} // end namespace dmc