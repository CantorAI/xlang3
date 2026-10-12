"""Probe dictionary-view iteration in class scopes after suite measurement.

The Docutils import failure reaches a percent-format lookup after class-body
iteration should populate a plain dict. This probe compares class and function
scopes, retained views and list snapshots, without translating library code.
It is prepared diagnostic coverage, not a performance score.
"""
import json


class Struct:
    def __init__(self, **attributes):
        self.__dict__.update(attributes)


def function_scope():
    enum = Struct(formatinfo={'parens': '()', 'rparen': ')', 'period': '.'})
    enum.formats = enum.formatinfo.keys()
    pats = {}
    for format in enum.formats:
        pats[format] = enum.formatinfo[format]
    return pats, '%(parens)s|%(rparen)s|%(period)s' % pats


def class_view_scope():
    class Body:
        enum = Struct()
        enum.formatinfo = {'parens': '()', 'rparen': ')', 'period': '.'}
        enum.formats = enum.formatinfo.keys()
        pats = {}
        for format in enum.formats:
            pats[format] = enum.formatinfo[format]
        formatted = '%(parens)s|%(rparen)s|%(period)s' % pats
    return Body.pats, Body.formatted


def class_list_scope():
    class Body:
        enum = Struct()
        enum.formatinfo = {'parens': '()', 'rparen': ')', 'period': '.'}
        enum.formats = list(enum.formatinfo.keys())
        pats = {}
        for format in enum.formats:
            pats[format] = enum.formatinfo[format]
        formatted = '%(parens)s|%(rparen)s|%(period)s' % pats
    return Body.pats, Body.formatted


def run():
    rows = []
    for function in (function_scope, class_view_scope, class_list_scope):
        try:
            pats, formatted = function()
            assert pats == {'parens': '()', 'rparen': ')', 'period': '.'}
            assert formatted == '()|)|.'
        except Exception as error:
            rows.append({'case': function.__name__, 'status': 'failed',
                         'error_type': type(error).__name__, 'error': str(error)})
        else:
            rows.append({'case': function.__name__, 'status': 'passed',
                         'keys': list(pats), 'formatted': formatted})
    print(json.dumps({'purpose': 'correctness diagnosis only', 'rows': rows}))
    assert all(row['status'] == 'passed' for row in rows)


if __name__ == '__main__':
    run()
