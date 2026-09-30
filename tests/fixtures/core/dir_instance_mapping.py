class Dynamic:
    pass


instance = Dynamic()
instance.__dict__['mapped'] = 5
print('mapped' in dir(instance), 'mapped' in instance.__dir__())
del instance.mapped
print('mapped' in dir(instance))
