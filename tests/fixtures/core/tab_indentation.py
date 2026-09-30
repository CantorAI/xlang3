class TabIndented:
	"""A tab occupies the next eight-column indentation stop."""

	def value(self):
		if True:
			return 42


def mixed_consistently():
	if True:
	    return "mixed"


print(TabIndented().value())
print(mixed_consistently())
