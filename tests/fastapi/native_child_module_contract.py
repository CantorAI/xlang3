"""A native child import must attach its module to the parent package."""

import rpds
import rpds.rpds

print(rpds.rpds.__name__)
print(hasattr(rpds.rpds, "__doc__"))
print(rpds.HashTrieMap is rpds.rpds.HashTrieMap)
