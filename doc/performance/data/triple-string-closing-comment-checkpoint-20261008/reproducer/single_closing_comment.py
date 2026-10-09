def identity(value):
    return value

result = identity(
    '''alpha
# interior hash, "double" and 'single'
omega''',  # real closing-line comment
)
assert result == "alpha\n# interior hash, \"double\" and 'single'\nomega"
print("PASS single_closing_comment")
