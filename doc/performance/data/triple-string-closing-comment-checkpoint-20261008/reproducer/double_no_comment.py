def identity(value):
    return value

result = identity(
    """alpha
# interior hash, "double" and 'single'
omega""",
)
assert result == "alpha\n# interior hash, \"double\" and 'single'\nomega"
print("PASS double_no_comment")
