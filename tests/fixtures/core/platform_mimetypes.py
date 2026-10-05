import os
import mimetypes

if os.name != 'nt':
    try:
        import winreg
    except ModuleNotFoundError:
        pass
    else:
        raise AssertionError('Windows registry module exposed on POSIX')
assert mimetypes.guess_type('index.html')[0] == 'text/html'
assert mimetypes.guess_type('app.css')[0] == 'text/css'
assert mimetypes.guess_type('image.png')[0] == 'image/png'
print('platform MIME types: ok')
