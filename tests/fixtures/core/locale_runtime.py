import locale

assert locale.__file__.replace("\\", "/").endswith("/Lib/locale.py")
locale.setlocale(locale.LC_ALL, "C")
assert locale.format_string("%9.2f", 12345.67, grouping=True) == " 12345.67"
assert locale.format_string("%+d", 4200) == "+4200"
assert locale.strcoll("a", "b") < 0
assert locale.strxfrm("abc") == "abc"

null_rejected = False
try:
    locale.strcoll("a\0", "a")
except ValueError:
    null_rejected = True

category_rejected = False
try:
    locale.setlocale(12345)
except locale.Error:
    category_rejected = True

print("locale-runtime", bool(locale.getencoding()), null_rejected, category_rejected)
