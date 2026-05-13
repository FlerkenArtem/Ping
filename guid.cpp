#include "guid.h"

stack<GUID> genGuids() {
    stack<GUID> guids;
    for (int i = 0; i < 4; i++){
        GUID guid;
        if (CoCreateGuid(&guid) == S_OK){
            guids.push(guid);
        }
    }
    return guids;
}

bool compareGuidStack(stack<GUID> a, stack<GUID> b) {
    if (a.size() != b.size()){
        return false;
    }

    while (!a.empty() && !b.empty()) {
        if (!IsEqualGUID(a.top(), b.top())) {
            return false;
        }
        a.pop();
        b.pop();
    }

    return true;
}
