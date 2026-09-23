class Base:
    @classmethod
    def owner(cls):
        return cls


class Middle(Base):
    @classmethod
    def owner(cls):
        return super().owner()


class Leaf(Middle):
    pass


print(Leaf.owner().__name__)
print(super(Middle, Leaf).owner().__name__)
sup = super(Middle, Leaf)
print(sup.__self__.__name__, sup.__self_class__.__name__, sup.__thisclass__.__name__)
