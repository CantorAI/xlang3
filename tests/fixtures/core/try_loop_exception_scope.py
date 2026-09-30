class MarkerError(Exception):
    pass


try:
    try:
        for item in ['version']:
            try:
                if item != 'version':
                    raise MarkerError('mismatch')
                break
            except MarkerError:
                pass
    except MarkerError:
        pass
    else:
        raise MarkerError('after break')
except MarkerError:
    print('break caught by outer')
else:
    print('break escaped outer')

try:
    for item in [1]:
        try:
            if item == 1:
                continue
        except MarkerError:
            pass
    raise MarkerError('after continue')
except MarkerError:
    print('continue caught by outer')
else:
    print('continue escaped outer')
