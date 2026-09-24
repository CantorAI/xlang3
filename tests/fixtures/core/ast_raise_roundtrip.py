import ast


for source in (
    "raise ValueError('bad')",
    "raise ValueError('outer') from RuntimeError('inner')",
):
    tree = ast.parse(source)
    statement = tree.body[0]
    print("shape", type(statement).__name__,
          type(statement.exc).__name__,
          None if statement.cause is None else type(statement.cause).__name__)
    try:
        exec(compile(tree, "<ast-raise>", "exec"))
    except ValueError as error:
        print("raised", str(error),
              None if error.__cause__ is None else str(error.__cause__))
