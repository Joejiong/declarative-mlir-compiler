# Declarative MLIR Compilers

[Design document](https://docs.google.com/document/d/1eAgIQZZ2dItJFSrCxemt7fwH0CD4w6_ueLKVl6UL-NU/edit?usp=sharing)

## Building DMC

Build requirements:

- `cmake >= 3.10`
- `python >= 3.6`

Arch Linux:

```bash
sudo pacman -Sy cmake python
```

MacOS:

```bash
brew install cmake python3
```

Clone the repo and its submodules. Configure CMake with whatever generator you
prefer and then build the target `gen`.

```bash
cmake --build $BINDIR --target gen
```

## Using DMC

Let's define a dead simple dialect.

```mlir
// toy.mlir
Dialect @toy {
  Op @add3(one: !dmc.Any, two: !dmc.Any, three: !dmc.Any) -> (res: !dmc.Any)
    traits [@SameType<"one", "two", "three", "res">]
}
```

Our Python script will need to load the dialect.

```python
from mlir import *

dialects = registerDynamicDialects(parseSourceFile("toy.mlir"))
toy = dialects[0]
```

Okay, let's generate a simple program.

```python
m = ModuleOp()
func = FuncOp("add3_impl", FunctionType([I64Type()] * 3, [I64Type()]))
m.append(func)

entry = func.addEntryBlock()
args = entry.args
b = Builder()
b.insertAtStart(entry)
add3 = b.create(toy.add3, one=args[0], two=args[1], three=args[2],
                res=I64Type())
b.create(ReturnOp, operands=[add3.res()])
verify(m)
print(m)
```

The output should be

```mlir
module {
  func @main(%arg0: i64, %arg1: i64, %arg2: i64) -> i64 {
    %0 = "toy.add3"(%arg0, %arg1, %arg2) : (i64, i64, i64) -> i64
    return %0 : i64
  }
}
```

## Building the Lua Compiler

The Lua compile requires `antlr >= 4`. On Arch, install the Pacman package
`antlr4`. On MacOS, install the Homebrew package `antlr`.

First, build the CMake targets `mlir`, `mlir-translate`, and `lua-parser`.

```bash
cmake --build $BINDIR -t mlir -t mlir-translate -t lua-parser
```

The Python shared library and autogenerated Lua parser must be added to your
`PYTHONPATH`. These can be found under `$BINDIR/lua/parser` and
`$BINDIR/lib/Python`.

```bash
export PATH=$PATH:$BINDIR/tools:$BINDIR/llvm-project/llvm/bin:$BINDIR/lua
export PYTHONPATH=$BINDIR/lua/parser:$BINDIR/lib/Python
```

## Using the Lua Compiler

Let's define a basic Lua file.

```lua
-- example.lua
function fib(n)
  if n == 0 then return 0 end
  if n == 1 then return 1 end
  return fib(n-1) + fib(n-2)
end

print("fib(5) = ", fib(5))
```

The Python script `lua/luac.py` calls the ANTLR parser and lowers the IR to
LLVM IR. The resulting file must be compiled (`-O2` strongly recommended) and
linked against a runtime `lua/impl.cpp` and the builtins `lua/builtins.cpp`.

```bash
python3 lua/luac.py example.lua > example.mlir
mlir-translate --mlir-to-llvmir example.mlir -o example.ll
clang++ example.ll lua/impl.cpp lua/builtins.cpp -O2 -std=c++17 -o example
```

Running (hopefully) produces the correct output:

```bash
me$ ./example
fib(5) = 5
```
