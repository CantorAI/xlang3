# Class-body loops must execute and write their targets and assignments into
# the class namespace. Pygments and docutils use this shape to build rules.
format_info = {"parens": "(value)", "rparen": "value)", "period": "value."}


class Rules:
    patterns = {}
    for key in format_info.keys():
        patterns[key] = key
    formatted = "%(parens)s %(rparen)s %(period)s" % patterns

    number_rules = {}
    for base in (2, 8, 10, 16):
        number_rules[base] = base
    selected = number_rules[2]


print(
    Rules.formatted,
    Rules.selected,
    Rules.base,
    sorted(Rules.number_rules.items()),
)
