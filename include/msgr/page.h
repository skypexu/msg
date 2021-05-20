#ifndef PAGE_60aae8bd80064b77965f8fcbc867e6bd_H
#define PAGE_60aae8bd80064b77965f8fcbc867e6bd_H

namespace msgr {

// these are in common/page.cc
extern unsigned page_size_;
extern unsigned long page_mask_;
extern unsigned page_shift_;

} // namespace msgr

#define MSGR_PAGE_SIZE     msgr::page_size_
#define MSGR_PAGE_MASK     msgr::page_mask_
#define MSGR_PAGE_SHIFT    msgr::page_shift_

#endif // PAGE_60aae8bd80064b77965f8fcbc867e6bd_H
