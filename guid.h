#ifndef GUID_H
#define GUID_H

#include <combaseapi.h>
#include <stack>


using namespace std;

stack<GUID> genGuids();
bool compareGuidStack(stack<GUID> a, stack<GUID> b);

#endif // GUID_H

#pragma once
