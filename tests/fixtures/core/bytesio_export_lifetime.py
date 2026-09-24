import gc
import io
import sys


unraisable = []
previous_hook = sys.unraisablehook
sys.unraisablehook = lambda args: unraisable.append(type(args.exc_value).__name__)


def exported_view():
    stream = io.BytesIO(b"abc")
    view = stream.getbuffer()
    child = memoryview(view)
    try:
        stream.close()
    except BufferError:
        print("close blocked")
    view.release()
    return child


try:
    child = exported_view()
    gc.collect()
    print(bytes(child))
    print(unraisable)
    child.release()
    del child
    gc.collect()
    print(unraisable)
finally:
    sys.unraisablehook = previous_hook
