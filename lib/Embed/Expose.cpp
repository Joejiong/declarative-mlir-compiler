#include "Scope.h"
#include "dmc/Embed/InMemoryDef.h"
#include "dmc/Dynamic/DynamicType.h"
#include "dmc/Dynamic/DynamicOperation.h"
#include "dmc/Dynamic/DynamicDialect.h"
#include "dmc/Traits/SpecTraits.h"
#include "dmc/Spec/SpecAttrs.h"
#include "dmc/Spec/SpecTypes.h"
#include "dmc/Spec/SpecRegion.h"
#include "dmc/Spec/SpecSuccessor.h"

#include <llvm/ADT/STLExtras.h>
#include <pybind11/pybind11.h>
#include <pybind11/embed.h>

using namespace pybind11;
using namespace llvm;
using namespace mlir;

namespace dmc {
namespace py {
namespace {

std::function<void(PythonGenStream::Line &)>
paramArgs(NamedParameterRange params) {
  return [params](PythonGenStream::Line &line) {
    interleaveComma(params, line, [&](auto param) {
      line << param.getName();
    });
  };
}

// TODO wish there was a Python API I could directly call, but one is not
// provided by pybind11, so I resort to codegen.
void exposeDynamicType(module &m, DynamicTypeImpl *impl) {
  InMemoryClass cls{impl->getName(), {"mlir.DynamicType"}, m};
  auto &s = cls.stream();
  s.line() << "def __init__(self, " << paramArgs(impl->getParamSpec())
      << "):" << incr; {
    s.line() << "super().__init__(mlir.build_dynamic_type(\""
        << impl->getDialect()->getNamespace() << "\", \"" << impl->getName()
        << "\", [" << paramArgs(impl->getParamSpec()) << "]))";
  } s.enddef();
  for (auto param : impl->getParamSpec()) {
    s.line() << "def " << param.getName() << "(self):" << incr; {
      s.line() << "return super().getParam(\"" << param.getName() << "\")";
    } s.enddef();
  }
}

struct Argument {
  const StringRef name, type, value;
};

struct ArgumentBuilder {
  void add(StringRef name, StringRef type) {
    args.push_back({name, type});
  }
  void add(StringRef name, StringRef value, StringRef type) {
    defArgs.push_back({name, type, value});
  }

  SmallVector<Argument, 8> args;
  SmallVector<Argument, 8> defArgs;
};

void exposeDynamicOp(module &m, DynamicOperation *impl) {
  auto *dialect = impl->getDialect();

  // Declare the class
  auto opName = impl->getName();
  InMemoryClass cls{opName.substr(opName.find('.') + 1),
                    {"mlir.Op", "mlir.OperationWrap"}, m};

  // Retrieve op traits
  auto &s = cls.stream();
  auto opType = impl->getTrait<TypeConstraintTrait>()->getOpType();
  auto opAttr = impl->getTrait<AttrConstraintTrait>()->getOpAttrs();
  auto opSucc = impl->getTrait<SuccessorConstraintTrait>()->getOpSuccessors();
  auto opRegion = impl->getTrait<RegionConstraintTrait>()->getOpRegions();

  // Collect arguments, checking for buildable types and attributes
  ArgumentBuilder b;
  for (auto &[name, type] : opType.getOperands()) {
    b.add(name, "mlir.Value");
  }
  for (auto &[name, type] : opType.getResults()) {
    if (auto *data = dialect->lookupTypeData(type);
        data && data->getBuilder()) {
      b.add(name, *data->getBuilder(), "mlir.Type");
    } else {
      b.add(name, "mlir.Type");
    }
  }
  for (auto &[name, attr] : opAttr) {
    if (auto *data = dialect->lookupAttributeData(attr);
        data && data->getBuilder()) {
      b.add(name, *data->getBuilder(), "mlir.Attribute");
    } else if (attr.isa<DefaultAttr>()) {
      std::string buf;
      llvm::raw_string_ostream getVal{buf};
      getVal << "mlir.get_default_attr_value(\"" << dialect->getNamespace()
          << "\", \"" << name.strref() << "\")";
      b.add(name, getVal.str(), "mlir.Attribute");
    } else {
      b.add(name, "mlir.Attribute");
    }
  }
  for (auto &[name, succ] : opSucc.getSuccessors()) {
    b.add(name, "mlir.Block");
  }
  auto numRegionsStr = std::to_string(opRegion.size());
  b.add("theNumRegions", numRegionsStr, "int");
  b.add("loc", "mlir.UnknownLoc()", "mlir.LocationAttr");

  // Declare the constructor
  auto allArgs = concat<Argument>(b.args, b.defArgs);
  auto args = make_range(std::begin(allArgs), std::end(allArgs));
  {
    auto line = s.line() << "def __init__(self, ";
    interleaveComma(args, line, [&](const Argument &a) {
      line << a.name << (a.value.empty() ? Twine{} : Twine{"="} + a.value);
    });
    line << "):" << incr;
  }

  // Insert type checks
  for (auto &a : args) {
    s.line() << "assert(isinstance(" << a.name << ", " << a.type << "))";
  }

  // Call to generic operation constructor
  {
    auto line = s.line() << "theOp = mlir.Operation(loc, \"" << opName << "\", [";
    interleaveComma(opType.getResults(), line,
                    [&](const NamedType &ty) { line << ty.name; });
    line << "], [";
    interleaveComma(opType.getOperands(), line,
                    [&](const NamedType &ty) { line << ty.name; });
    line << "], {";
    interleaveComma(opAttr, line, [&](const NamedAttribute &attr) {
      auto name = attr.first.strref();
      line << '"' << name << "\": " << name;
    });
    line << "}, [";
    interleaveComma(opSucc.getSuccessors(), line,
                    [&](const NamedConstraint &succ) { line << succ.name; });
    line << "], theNumRegions)";
  }

  // Call to parent class constructors
  s.line() << "mlir.Op.__init__(self, theOp)";
  s.line() << "mlir.OperationWrap.__init__(self, theOp)";
  s.enddef();

  // Define getters
  for (auto &[name, type] : opType.getOperands()) {
    auto getter = type.isa<VariadicType>() ? "getOperandGroup" : "getOperand";
    s.def(name + "(self)"); {
      s.line() << "return mlir.OperationWrap." << getter << "(self, \"" << name
        << "\")";
    } s.enddef();
  }
  for (auto &[name, type] : opType.getResults()) {
    auto getter = type.isa<VariadicType>() ? "getResultGroup" : "getResult";
    s.def(name + "(self)"); {
      s.line() << "return mlir.OperationWrap." << getter << "(self, \"" << name
          << "\")";
    } s.enddef();
  }
  for (auto &[name, attr] : opAttr) {
    s.def(name + "(self)"); {
      s.line() << "return mlir.Op.getAttr(self, \"" << name << "\")";
    } s.enddef();
  }
  for (auto &[name, succ] : opSucc.getSuccessors()) {
    auto getter = succ.isa<VariadicSuccessor>() ?
        "getSuccessor" : "getSuccessors";
    s.def(name + "(self)"); {
      s.line() << "return mlir.OperationWrap." << getter << "(self, \"" << name
          << "\")";
    } s.enddef();
  }
  for (auto &[name, region] : opRegion.getRegions()) {
    auto getter = region.isa<VariadicRegion>() ? "getRegion" : "getRegions";
    s.def(name + "(self)"); {
      s.line() << "return mlir.OperationWrap." << getter << "(self, \"" << name
          << "\")";
    } s.enddef();
  }
}

} // end anonymous namespace

void exposeDialectInternal(DynamicDialect *dialect) {
  auto m = reinterpret_borrow<module>(
      PyImport_AddModule(dialect->getNamespace().str().c_str()));
  ensureBuiltins(m);
  exec("import mlir", m.attr("__dict__"));
  auto regFcn = "register_internal_module_" + dialect->getNamespace().str();
  getInternalModule().def(regFcn.c_str(), [m]() { return m; });
  exec(dialect->getNamespace().str() + " = " + regFcn + "()",
       getInternalScope());
  exec(dialect->getNamespace().str() + " = mlir." + regFcn + "()",
       m.attr("__dict__"));
  for (auto *ty : dialect->getTypes()) {
    exposeDynamicType(m, ty);
  }
  for (auto *op : dialect->getOps()) {
    exposeDynamicOp(m, op);
  }
}

} // end namespace py
} // end namespace dmc
