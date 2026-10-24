format_info = {
    "parens": "(value)",
    "rparen": "value)",
    "period": "value.",
}
patterns = {}
for key in format_info.keys():
    patterns[key] = key

print(
    "dict-keys-mapping-format",
    list(format_info.keys()),
    len(patterns),
    "%(parens)s %(rparen)s %(period)s" % patterns,
)
