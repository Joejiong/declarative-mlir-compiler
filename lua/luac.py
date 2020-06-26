#!python3
from antlr4 import *
from mlir import *

from LuaLexer import LuaLexer
from LuaListener import LuaListener
from LuaParser import LuaParser

import sys

def get_dialects(filename='lua.mlir'):
    m = parseSourceFile(filename)
    dialects = registerDynamicDialects(m)
    return dialects[0]

lua = get_dialects()

class Generator:
    ############################################################################
    # Constructor and Helpers
    ############################################################################

    def __init__(self, filename:str, stream:CommonTokenStream):
        self.filename = filename
        self.stream = stream

    @staticmethod
    def addEntryBlock(region:Region):
        assert region != None, "no region currently set"
        assert len(region) == 0, "region already has a block"

        # All main blocks will have zero arguments
        return region.addEntryBlock([])

    def getStartLoc(self, ctx:ParserRuleContext):
        a, b = ctx.getSourceInterval()
        startTok = self.stream.get(a)
        return FileLineColLoc(self.filename, startTok.line, startTok.column)

    def getEndLoc(self, ctx:ParserRuleContext):
        a, b = ctx.getSourceInterval()
        endTok = self.stream.get(b)
        return FileLineColLoc(self.filename, endTok.line, endTok.column)


    ############################################################################
    # AST Walker
    ############################################################################

    def chunk(self, ctx:LuaParser.ChunkContext):
        # Create the Lua module
        module = ModuleOp()
        main = FuncOp("main", FunctionType(), self.getStartLoc(ctx))
        module.append(main)

        # Set the current region
        self.region = main.getBody()

        # Process the block
        self.block(ctx.block())

        # Return the module
        assert verify(module), "module failed to verify"
        return module


    def block(self, ctx:LuaParser.BlockContext):
        # Add an entry block to the enclosing region and set it
        self.block = self.addEntryBlock(self.region)
        self.builder = Builder.atStart(self.block)

        # Process the statements
        for stat in ctx.stat():
            self.stat(stat)

        # Process the return statement or insert an empty terminator
        if ctx.retstat():
            raise NotImplementedError("return statements not handled")
        else:
            self.builder.create(ReturnOp, loc=self.getEndLoc(ctx))


    def stat(self, ctx:LuaParser.StatContext):
        if ctx.assignlist():
            self.assignlist(ctx.assignlist())
        elif ctx.functioncall():
            self.functioncall(ctx.functioncall())
        elif ctx.label():
            raise NotImplementedError("label not implemented")
        elif ctx.breakstmt():
            raise NotImplementedError("breakstmt not implemented")
        elif ctx.gotostmt():
            raise NotImplementedError("gotostmt not implemented")
        elif ctx.enclosedblock():
            raise NotImplementedError("enclosedblock not implemented")
        elif ctx.whileloop():
            raise NotImplementedError("whileloop not implemented")
        elif ctx.repeatloop():
            raise NotImplementedError("repeatloop not implemented")
        elif ctx.conditionalchain():
            raise NotImplementedError("conditionalchain not implemented")
        elif ctx.numericfor():
            raise NotImplementedError("numericfor not implemented")
        elif ctx.genericfor():
            raise NotImplementedError("genericfor not implemented")
        elif ctx.namedfunctiondef():
            raise NotImplementedError("namedfunctiondef not implemented")
        elif ctx.localnamedfunctiondef():
            raise NotImplementedError("localnamedfunctiondef not implemented")
        elif ctx.localvarlist():
            raise NotImplementedError("localvarlist not implemented")
        else:
            raise ValueError("Unknown StatContext case")


    def assignlist(self, ctx:LuaParser.AssignlistContext):
        varList = self.varlist(ctx.varlist())
        expList = self.explist(ctx.explist())

        # Assign via concat and unpack
        concat = self.builder.create(lua.concat, vals=expList,
                                     loc=self.getStartLoc(ctx))
        unpack = self.builder.create(lua.unpack, pack=concat.pack(),
                                     vals=[lua.ref()] * len(varList),
                                     loc=self.getStartLoc(ctx))

        vals = unpack.vals()
        assert len(vals) == len(varList)
        for i in range(0, len(varList)):
            self.builder.create(lua.assign, tgt=varList[i], val=vals[i],
                                loc=self.getStartLoc(ctx))


    def varlist(self, ctx:LuaParser.VarlistContext):
        varList = []
        for var in ctx.var():
            varList.append(self.var(var))
        return varList


    def explist(self, ctx:LuaParser.ExplistContext):
        expList = []
        for exp in ctx.exp():
            expList.append(self.exp(exp))
        return expList


    def nameAndArgs(self, ctx:LuaParser.NameAndArgsContext):
        assert ctx.NAME() == None, "colon operator unimplemented"
        return self.args(ctx.args())


    def args(self, ctx:LuaParser.ArgsContext):
        if ctx.explist():
            return self.explist(ctx.explist())
        elif ctx.tableconstructor():
            # Ensure return value is a list of values
            return [self.tableconstructor(ctx.tableconstructor())]
        elif ctx.string():
            return [self.string(ctx.string())]
        else:
            raise ValueError("Unknown ArgsContext case")


    def varOrExp(self, ctx:LuaParser.VarOrExpContext):
        if ctx.var():
            return self.var(ctx.var())
        elif ctx.exp():
            return self.exp(ctx.exp())
        else:
            raise ValueError("Unknown VarOrExpContext case")


    def var(self, ctx:LuaParser.VarContext):
        assert ctx.exp() == None and len(ctx.varSuffix()) == 0, \
            "only identifier variables are supported"

        alloc = self.builder.create(lua.get_or_alloc,
                                    var=StringAttr(ctx.NAME().getText()),
                                    loc=self.getStartLoc(ctx))
        return alloc.res()


    def exp(self, ctx:LuaParser.ExpContext):
        if ctx.nilvalue():
            return self.nilvalue(ctx.nilvalue())
        elif ctx.falsevalue():
            raise NotImplementedError("falsevalue not implemented")
        elif ctx.truevalue():
            raise NotImplementedError("truevalue not implemented")
        elif ctx.number():
            return self.number(ctx.number())
        elif ctx.string():
            raise NotImplementedError("string not implemented")
        elif ctx.elipsis():
            raise NotImplementedError("elipsis not implemented")
        elif ctx.functiondef():
            raise NotImplementedError("functiondef not implemented")
        elif ctx.prefixexp():
            return self.prefixexp(ctx.prefixexp())
        elif ctx.tableconstructor():
            raise NotImplementedError("tableconstructor not implemented")
        elif ctx.operatorUnary():
            raise NotImplementedError("operatorUnary not implemented")
        elif ctx.operatorPower():
            raise NotImplementedError("operatorPower not implemented")
        elif ctx.operatorMulDivMod():
            raise NotImplementedError("operatorMulDivMod not implemented")
        elif ctx.operatorAddSub():
            raise NotImplementedError("operatorAddSub not implemented")
        elif ctx.operatorStrcat():
            raise NotImplementedError("operatorStrcat not implemented")
        elif ctx.operatorComparison():
            raise NotImplementedError("operatorComparison not implemented")
        elif ctx.operatorAnd():
            raise NotImplementedError("operatorAnd not implemented")
        elif ctx.operatorOr():
            raise NotImplementedError("operatorOr not implemented")
        elif ctx.operatorBitwise():
            raise NotImplementedError("operatorBitwise not implemented")
        else:
            raise ValueError("Unknown ExpContext case")


    def nilvalue(self, ctx:LuaParser.NilvalueContext):
        nil = self.builder.create(lua.nil, loc=self.getStartLoc(ctx))
        return nil.res()


    def number(self, ctx:LuaParser.NumberContext):
        if ctx.INT():
            attr = I64Attr(int(ctx.INT().getText()))
        elif ctx.HEX():
            attr = I64Attr(int(ctx.HEX().getText(), 16))
        elif ctx.FLOAT():
            attr = F64Attr(float(ctx.FLOAT().getText()()))
        elif ctx.HEX_FLOAT():
            raise NotImplementedError("number HEX_FLOAT not implemented")
        else:
            raise NotImplementedError("Unknown NumberContext case")
        number = self.builder.create(lua.number, value=attr,
                                     loc=self.getStartLoc(ctx))
        return number.res()


    def prefixexp(self, ctx:LuaParser.PrefixexpContext):
        # A variable or expression, with optional trailing function calls
        # `myFcn (a, b, c) {a: b} "abc" {} "" (c, b, d)` is valid syntax
        val = self.varOrExp(ctx.varOrExp())
        allArgs = ctx.nameAndArgs()

        if len(allArgs) == 0:
            return val
        assert len(allArgs) == 1, "trailing function calls unimplemented"

        args = self.nameAndArgs(allArgs[0])
        concat = self.builder.create(lua.concat, vals=args,
                                     loc=self.getStartLoc(ctx))
        call = self.builder.create(lua.call, fcn=val, args=concat.pack(),
                                   loc=self.getStartLoc(ctx))
        # Return a value pack. This means that self.exp could return either a
        # value or value pack. Module verifier should catch accidental uses of
        # value pack as value.
        return call.rets()


    def functioncall(self, ctx:LuaParser.FunctioncallContext):
        # Identical to prefixexp, except len(allArgs) >= 1, and statement does
        # not return a value
        self.prefixexp(ctx)


    def operatorBinary(self, lhs:LuaParser.ExpContext, rhs:LuaParser.ExpContext, op:str):
        pass



def main():
    if len(sys.argv) != 2:
        print("Usage: luac.py <lua_file>")
        return
    filename = sys.argv[1]
    with open(filename, 'r') as file:
        contents = file.read()

    lexer = LuaLexer(InputStream(contents))
    stream = CommonTokenStream(lexer)
    parser = LuaParser(stream)

    generator = Generator(filename, stream)

    module = generator.chunk(parser.chunk())
    print(module)

if __name__ == '__main__':
    main()