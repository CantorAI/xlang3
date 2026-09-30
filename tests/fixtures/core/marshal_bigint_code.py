import ast
import marshal


source = "answer = 123456789012345678901234567890\n"
code = compile(ast.parse(source), "<big integer>", "exec")
restored = marshal.loads(marshal.dumps(code))
namespace = {}
exec(restored, namespace)
print(namespace["answer"] == 123456789012345678901234567890)
