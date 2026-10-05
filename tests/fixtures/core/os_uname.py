import os

if os.name == 'posix':
    result = os.uname()
    names = ('sysname', 'nodename', 'release', 'version', 'machine')
    assert isinstance(result, tuple)
    assert type(result) is os.uname_result
    assert len(result) == 5
    assert tuple(result) == tuple(getattr(result, name) for name in names)
    assert all(isinstance(item, str) for item in result)
    assert result.sysname and result.release and result.machine
    assert result[-1] == result.machine
    assert result[:2] == (result.sysname, result.nodename)
    assert os.uname_result.n_fields == os.uname_result.n_sequence_fields == 5
    assert os.uname_result.n_unnamed_fields == 0
    assert os.uname_result.__match_args__ == names
    expected = ('a', 'b', 'c', 'd', 'e')
    created = os.uname_result(list(expected))
    assert created == expected and not created != expected
    assert created == os.uname_result(iter(expected), {})
    assert repr(created) == "posix.uname_result(sysname='a', nodename='b', release='c', version='d', machine='e')"
    constructor, arguments = created.__reduce_ex__(2)
    assert constructor(*arguments) == created
    for name in names + ('extra',):
        try:
            setattr(created, name, 'changed')
        except AttributeError:
            pass
        else:
            raise AssertionError('uname result is mutable')
    for args in ((), ((1, 2),), ((1, 2, 3, 4, 5, 6),), (expected, None)):
        try:
            os.uname_result(*args)
        except TypeError:
            pass
        else:
            raise AssertionError('invalid uname_result arguments accepted')
    try:
        os.uname(1)
    except TypeError:
        pass
    else:
        raise AssertionError('uname arguments accepted')
else:
    assert not hasattr(os, 'uname')
print('uname API passed')
