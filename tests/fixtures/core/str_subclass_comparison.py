class Label(str):
    pass


left = Label("alpha")
right = Label("beta")
print(left < right, left <= "alpha", "beta" > left)
print(",".join(sorted(["beta", left, Label("gamma"), "alpha"])))
