#include "xlang3/xapi.h"
#include "xlang3/xmodule.h"
#include "xlang3/xobject.h"
#include "xlang3/xvalue.h"

int main(void) {
  X3Value value = x3_value_int64(42);
  return value.tag == X3_TAG_INT64 ? 0 : 1;
}
