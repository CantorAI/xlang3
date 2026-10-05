import sys

if sys.platform == 'darwin':
    import _scproxy
    settings = _scproxy._get_proxy_settings()
    proxies = _scproxy._get_proxies()
    assert isinstance(proxies, dict)
    assert all(isinstance(key, str) and isinstance(value, str) for key, value in proxies.items())
    if settings is not None:
        assert isinstance(settings, dict)
        assert isinstance(settings['exclude_simple'], bool)
        if 'exceptions' in settings:
            assert isinstance(settings['exceptions'], tuple)
            assert all(item is None or isinstance(item, str) for item in settings['exceptions'])
    for function in (_scproxy._get_proxy_settings, _scproxy._get_proxies):
        try:
            function(1)
        except TypeError:
            pass
        else:
            raise AssertionError('proxy function accepted an argument')
    import urllib.request
    assert isinstance(urllib.request.getproxies(), dict)
print('proxy API passed')
