#ifndef SAPERA_FG_H
#define SAPERA_FG_H
#include <functional>

namespace SaperaFG {
    typedef std::function<void(void)> VoidLambda;

    extern void Execute(const VoidLambda & messagePump = VoidLambda());
    extern void Execute(VoidLambda && messagePump);
}
#endif
