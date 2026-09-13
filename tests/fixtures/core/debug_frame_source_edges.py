import inspect
import linecache

GLOBAL_MARKER = 'global-value'

def outer(value):
    outer_local = value + 1
    return inner(outer_local)

def inner(value):
    frame = inspect.currentframe()
    same = inspect.currentframe() is frame
    before = frame.f_locals.get('later')
    later = value + 1
    refreshed = inspect.currentframe()
    info = inspect.getframeinfo(frame)
    outer_names = [item.frame.f_code.co_name for item in inspect.getouterframes(frame)[:3]]
    print('live', same, before, refreshed.f_locals['later'])
    print('names', frame.f_code.co_name, frame.f_back.f_code.co_name, outer_names)
    print('namespaces', frame.f_globals is globals(), frame.f_globals['GLOBAL_MARKER'], frame.f_builtins is __builtins__)
    print('source', info.function, info.filename.endswith('debug_frame_source_edges.py'), linecache.getline(__file__, frame.f_code.co_firstlineno).strip())
    print('code', frame.f_code.co_name, frame.f_code.co_qualname, frame.f_code.co_firstlineno, len(list(frame.f_code.co_lines())) > 0)
    return frame

saved = outer(40)
print('completed', saved.f_locals['value'], saved.f_locals['later'], saved.f_back.f_code.co_name)
try:
    saved.f_lineno = saved.f_lineno
except Exception as exc:
    print('set-line', type(exc).__name__, str(exc))
try:
    inspect.currentframe().clear()
except Exception as exc:
    print('clear-running', type(exc).__name__, str(exc))
saved.clear()
print('cleared', saved.f_locals)

