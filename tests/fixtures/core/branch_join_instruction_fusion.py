class BranchBody:
    if True:
        def first(self):
            return "first"

        def second(self):
            return "second"
    else:
        def first(self):
            return "wrong"

        def second(self):
            return "wrong"

    first.__doc__ = "first-doc"
    second.__doc__ = "second-doc"


value = BranchBody()
print(value.first(), value.second())
print(BranchBody.first.__doc__, BranchBody.second.__doc__)
