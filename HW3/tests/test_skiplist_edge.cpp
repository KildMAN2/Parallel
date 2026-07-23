// Verify the degenerate maxLevel=0 edge case no longer crashes
// (previously: std::vector<Node*> preds(0) -> preds[0] was out-of-bounds UB).
#include "../skip_list_par_impl.h"
#include <iostream>
#include <cassert>

int main() {
    SkipListImpl sl(0);   // degenerate: maxLevel <= 0
    sl.insert(5);
    sl.insert(3);
    sl.insert(8);
    assert(sl.search(5));
    assert(sl.search(3));
    assert(!sl.search(100));
    sl.remove(3);
    assert(!sl.search(3));
    assert(sl.search(5));
    std::cout << "maxLevel=0 EDGE CASE OK\n";
    return 0;
}
