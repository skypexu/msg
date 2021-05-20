#include <unistd.h>

namespace msgr {

  // page size crap, see page.h
  int get_bits_of_(int v) {
    int n = 0;
    while (v) {
      n++;
      v = v >> 1;
    }
    return n;
  }

  unsigned page_size_ = sysconf(_SC_PAGESIZE);
  unsigned long page_mask_ = ~(unsigned long)(page_size_ - 1);
  unsigned page_shift_ = get_bits_of_(page_size_);

}
