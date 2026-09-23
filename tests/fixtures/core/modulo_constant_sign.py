for left, right in [(-27, 4), (27, -4), (-27, -4), (27, 4)]:
    print(left % right)

print(-27 % 4, 27 % -4, -27 % -4, 27 % 4)
print(b"=" * (-len(b"x" * 27) % 4))

import base64

signature = b"DVAWfe0DTPWGJj57ukjt0U6dL24"
print(len(base64.urlsafe_b64decode(signature + b"=" * (-len(signature) % 4))))
