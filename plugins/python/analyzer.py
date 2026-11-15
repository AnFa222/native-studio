"""Persistent stdlib-only analyzer used by the Native Studio Python plugin."""

import ast
import builtins
import json
import sys


BUILTINS = set(dir(builtins)) | {
    "__annotations__", "__builtins__", "__cached__", "__file__",
    "__loader__", "__name__", "__package__", "__spec__",
}


def location(node):
    line = max(0, getattr(node, "lineno", 1) - 1)
    column = max(0, getattr(node, "col_offset", 0))
    end_column = getattr(node, "end_col_offset", column + 1)
    return line, column, max(1, end_column - column)


class Reporter:
    def __init__(self):
        self.items = []
        self.keys = set()

    def add(self, node, message, severity="warning"):
        line, column, length = location(node)
        key = (line, column, message)
        if key in self.keys:
            return
        self.keys.add(key)
        self.items.append({
            "line": line,
            "column": column,
            "length": length,
            "severity": severity,
            "message": message,
        })


class Binding:
    def __init__(self, node, kind):
        self.node = node
        self.kind = kind


class BindingCollector(ast.NodeVisitor):
    """Collect names local to one scope without entering nested scopes."""

    def __init__(self):
        self.bindings = {}
        self.globals = set()
        self.nonlocals = set()

    def bind(self, name, node, kind="assignment"):
        if name and name not in self.bindings:
            self.bindings[name] = Binding(node, kind)

    def bind_target(self, target, kind="assignment"):
        if isinstance(target, ast.Name):
            self.bind(target.id, target, kind)
        elif isinstance(target, (ast.Tuple, ast.List)):
            for element in target.elts:
                self.bind_target(element, kind)
        elif isinstance(target, ast.Starred):
            self.bind_target(target.value, kind)

    def visit_Name(self, node):
        if isinstance(node.ctx, (ast.Store, ast.Del)):
            self.bind(node.id, node)

    def visit_FunctionDef(self, node):
        self.bind(node.name, node, "function")
        for decorator in node.decorator_list:
            self.visit(decorator)
        for default in list(node.args.defaults) + [item for item in node.args.kw_defaults if item]:
            self.visit(default)

    visit_AsyncFunctionDef = visit_FunctionDef

    def visit_ClassDef(self, node):
        self.bind(node.name, node, "class")
        for decorator in node.decorator_list:
            self.visit(decorator)
        for base in node.bases:
            self.visit(base)

    def visit_Lambda(self, node):
        return

    def visit_Import(self, node):
        for alias in node.names:
            self.bind(alias.asname or alias.name.split(".")[0], alias, "import")

    def visit_ImportFrom(self, node):
        if node.module == "__future__":
            return
        for alias in node.names:
            if alias.name != "*":
                self.bind(alias.asname or alias.name, alias, "import")

    def visit_ExceptHandler(self, node):
        if node.name:
            self.bind(node.name, node)
        for statement in node.body:
            self.visit(statement)

    def visit_Global(self, node):
        self.globals.update(node.names)

    def visit_Nonlocal(self, node):
        self.nonlocals.update(node.names)

    def visit_MatchAs(self, node):
        if node.name:
            self.bind(node.name, node)
        if node.pattern:
            self.visit(node.pattern)

    def visit_MatchStar(self, node):
        if node.name:
            self.bind(node.name, node)

    def visit_MatchMapping(self, node):
        if node.rest:
            self.bind(node.rest, node)
        self.generic_visit(node)

    def visit_ListComp(self, node):
        return

    visit_SetComp = visit_ListComp
    visit_DictComp = visit_ListComp
    visit_GeneratorExp = visit_ListComp

    def finish(self):
        for name in self.globals | self.nonlocals:
            self.bindings.pop(name, None)
        return self.bindings


class Scope:
    def __init__(self, kind, node, parent, arguments=None):
        self.kind = kind
        self.node = node
        self.parent = parent
        collector = BindingCollector()
        body = getattr(node, "body", [])
        if isinstance(body, list):
            for statement in body:
                collector.visit(statement)
        self.bindings = collector.finish()
        self.globals = collector.globals
        self.nonlocals = collector.nonlocals
        self.used = set()
        if arguments:
            all_arguments = (
                list(arguments.posonlyargs) + list(arguments.args)
                + list(arguments.kwonlyargs)
            )
            if arguments.vararg:
                all_arguments.append(arguments.vararg)
            if arguments.kwarg:
                all_arguments.append(arguments.kwarg)
            for argument in all_arguments:
                self.bindings.setdefault(argument.arg, Binding(argument, "parameter"))


class SemanticAnalyzer(ast.NodeVisitor):
    def __init__(self, tree, reporter):
        self.reporter = reporter
        self.module_scope = Scope("module", tree, None)
        self.scope = self.module_scope
        self.scopes = [self.module_scope]

    def push_scope(self, kind, node, arguments=None):
        previous = self.scope
        self.scope = Scope(kind, node, previous, arguments)
        self.scopes.append(self.scope)
        return previous

    def pop_scope(self, previous):
        self.scope = previous

    def resolve(self, name, node):
        scope = self.scope
        if name in scope.globals:
            scope = self.module_scope
        elif name in scope.nonlocals:
            scope = scope.parent
            while scope and scope.kind == "class":
                scope = scope.parent

        current = scope
        while current:
            binding = current.bindings.get(name)
            if binding:
                current.used.add(name)
                binding_line = getattr(binding.node, "lineno", 0)
                use_line = getattr(node, "lineno", 0)
                if (current is self.scope and binding.kind not in ("parameter",)
                        and binding_line and use_line and use_line < binding_line):
                    self.reporter.add(node, f"Name '{name}' is used before assignment", "error")
                return
            current = current.parent
            if self.scope.kind == "function" and current and current.kind == "class":
                current = current.parent
        if name not in BUILTINS:
            self.reporter.add(node, f"Undefined name '{name}'", "error")

    def visit_Name(self, node):
        if isinstance(node.ctx, ast.Load):
            self.resolve(node.id, node)

    def visit_FunctionDef(self, node):
        for decorator in node.decorator_list:
            self.visit(decorator)
        for default in list(node.args.defaults) + [item for item in node.args.kw_defaults if item]:
            self.visit(default)
        all_arguments = list(node.args.posonlyargs) + list(node.args.args) + list(node.args.kwonlyargs)
        if node.args.vararg:
            all_arguments.append(node.args.vararg)
        if node.args.kwarg:
            all_arguments.append(node.args.kwarg)
        for argument in all_arguments:
            if argument.annotation:
                self.visit(argument.annotation)
        if node.returns:
            self.visit(node.returns)
        previous = self.push_scope("function", node, node.args)
        for statement in node.body:
            self.visit(statement)
        self.pop_scope(previous)

    visit_AsyncFunctionDef = visit_FunctionDef

    def visit_ClassDef(self, node):
        for decorator in node.decorator_list:
            self.visit(decorator)
        for base in node.bases:
            self.visit(base)
        for keyword in node.keywords:
            self.visit(keyword.value)
        previous = self.push_scope("class", node)
        for statement in node.body:
            self.visit(statement)
        self.pop_scope(previous)

    def visit_Lambda(self, node):
        for default in list(node.args.defaults) + [item for item in node.args.kw_defaults if item]:
            self.visit(default)
        previous = self.push_scope("function", node, node.args)
        self.visit(node.body)
        self.pop_scope(previous)

    def visit_ListComp(self, node):
        self._visit_comprehension(node.generators, [node.elt])

    def visit_SetComp(self, node):
        self._visit_comprehension(node.generators, [node.elt])

    def visit_GeneratorExp(self, node):
        self._visit_comprehension(node.generators, [node.elt])

    def visit_DictComp(self, node):
        self._visit_comprehension(node.generators, [node.key, node.value])

    def _visit_comprehension(self, generators, values):
        previous = self.scope
        temporary = Scope.__new__(Scope)
        temporary.kind = "comprehension"
        temporary.node = generators[0] if generators else None
        temporary.parent = previous
        temporary.bindings = {}
        temporary.globals = set()
        temporary.nonlocals = set()
        temporary.used = set()
        self.scopes.append(temporary)
        for generator in generators:
            self.scope = previous if generator is generators[0] else temporary
            self.visit(generator.iter)
            self.scope = temporary
            collector = BindingCollector()
            collector.bind_target(generator.target)
            temporary.bindings.update(collector.bindings)
            for condition in generator.ifs:
                self.visit(condition)
        self.scope = temporary
        for value in values:
            self.visit(value)
        self.scope = previous

    def finish(self):
        for scope in self.scopes:
            for name, binding in scope.bindings.items():
                if name in scope.used or name.startswith("_"):
                    continue
                if binding.kind == "import":
                    self.reporter.add(binding.node, f"Imported name '{name}' is never used")
                elif (scope.kind == "function" and binding.kind == "parameter"
                        and name not in ("self", "cls")):
                    self.reporter.add(binding.node, f"Parameter '{name}' is never used")
                elif scope.kind == "function" and binding.kind == "assignment":
                    self.reporter.add(binding.node, f"Local variable '{name}' is assigned but never used")


class StyleAndFlowAnalyzer(ast.NodeVisitor):
    UNUSED_EXPRESSIONS = (
        ast.Attribute, ast.BinOp, ast.BoolOp, ast.Compare, ast.Constant,
        ast.Dict, ast.JoinedStr, ast.Lambda, ast.List, ast.Name, ast.Set, ast.Tuple,
    )

    def __init__(self, reporter):
        self.reporter = reporter

    def check_body(self, owner, body):
        terminated = False
        for index, statement in enumerate(body):
            if terminated:
                self.reporter.add(statement, "Unreachable code")
                terminated = False
            if isinstance(statement, (ast.Return, ast.Raise, ast.Break, ast.Continue)):
                terminated = True
            if isinstance(statement, ast.Expr) and isinstance(statement.value, self.UNUSED_EXPRESSIONS):
                is_docstring = (
                    index == 0
                    and isinstance(owner, (ast.Module, ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef))
                    and isinstance(statement.value, ast.Constant)
                    and isinstance(statement.value.value, str)
                )
                if not is_docstring:
                    if (isinstance(statement.value, (ast.Constant, ast.JoinedStr))
                            and (isinstance(statement.value, ast.JoinedStr)
                                 or isinstance(getattr(statement.value, "value", None), str))):
                        message = "Standalone string literal has no effect"
                    else:
                        message = "Expression value is unused"
                    self.reporter.add(statement, message)

    def generic_visit(self, node):
        for field in ("body", "orelse", "finalbody"):
            body = getattr(node, field, None)
            if isinstance(body, list):
                self.check_body(node, body)
        super().generic_visit(node)

    def visit_ExceptHandler(self, node):
        if node.type is None:
            self.reporter.add(node, "Bare except catches system-exiting exceptions")
        self.check_body(node, node.body)
        self.generic_visit(node)

    def visit_FunctionDef(self, node):
        defaults = list(node.args.defaults) + [item for item in node.args.kw_defaults if item]
        for default in defaults:
            if isinstance(default, (ast.List, ast.Dict, ast.Set)):
                self.reporter.add(default, "Mutable default argument is shared between calls")
        self.generic_visit(node)

    visit_AsyncFunctionDef = visit_FunctionDef

    def visit_Compare(self, node):
        operands = [node.left] + list(node.comparators)
        if any(isinstance(item, ast.Constant) and item.value in (None, True, False)
               for item in operands) and any(isinstance(op, (ast.Eq, ast.NotEq)) for op in node.ops):
            self.reporter.add(node, "Use 'is' or 'is not' when comparing with None or booleans")
        self.generic_visit(node)

    def visit_Dict(self, node):
        seen = {}
        for key in node.keys:
            if isinstance(key, ast.Constant):
                try:
                    value = key.value
                    if value in seen:
                        self.reporter.add(key, f"Duplicate dictionary key {value!r}")
                    else:
                        seen[value] = key
                except TypeError:
                    pass
        self.generic_visit(node)

    def visit_ImportFrom(self, node):
        if any(alias.name == "*" for alias in node.names):
            self.reporter.add(node, "Wildcard import prevents reliable name analysis")
        self.generic_visit(node)


def syntax_diagnostic(error):
    message = getattr(error, "msg", None) or str(error) or "Invalid syntax"
    message = message[:1].upper() + message[1:]
    line = max(0, (getattr(error, "lineno", None) or 1) - 1)
    column = max(0, (getattr(error, "offset", None) or 1) - 1)
    end = getattr(error, "end_offset", None) or column + 2
    return [{
        "line": line,
        "column": column,
        "length": max(1, end - column - 1),
        "severity": "error",
        "message": message,
    }]


def analyze(source):
    try:
        tree = ast.parse(source, filename="<editor>", mode="exec", type_comments=True)
        compile(tree, "<editor>", "exec")
    except (SyntaxError, IndentationError, TabError) as error:
        return syntax_diagnostic(error)
    except (ValueError, TypeError) as error:
        return [{
            "line": 0, "column": 0, "length": 1,
            "severity": "error", "message": str(error),
        }]

    reporter = Reporter()
    semantics = SemanticAnalyzer(tree, reporter)
    semantics.visit(tree)
    semantics.finish()
    StyleAndFlowAnalyzer(reporter).visit(tree)
    reporter.items.sort(key=lambda item: (item["line"], item["column"], item["message"]))
    return reporter.items


def main():
    for request_line in sys.stdin:
        try:
            request = json.loads(request_line)
            response = analyze(request.get("source", ""))
        except Exception as error:  # Keep the service alive and let the plugin show the failure.
            response = [{
                "line": 0, "column": 0, "length": 1,
                "severity": "error", "message": f"Python analyzer failed: {error}",
            }]
        print(json.dumps(response, ensure_ascii=False, separators=(",", ":")), flush=True)


if __name__ == "__main__":
    main()
