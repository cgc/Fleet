# Include directories
FLEET_INCLUDE=-I../../src/ -I../../src/Inference -I../../src/Hypotheses -I../../src/VirtualMachine -I../../src/Statistics -I../../src/Grammar

# Some standard/default flags
FLEET_FLAGS=-std=c++2a -Wall -fdiagnostics-color=auto -Wimplicit-fallthrough -Wall -Wextra -Wextra-semi -Wpedantic -Wvla -Wnull-dereference -Wswitch-enum -Wno-unused-parameter -fvar-tracking-assignments -Wduplicated-cond -Wduplicated-branches -Wsuggest-override -march=native -ftemplate-backtrace-limit=0 -fstack-protector-strong --param max-inline-insns-recursive=100000
## Might add -Werror ?

FLEET_LIBS=-lm -pthread

CLANG_FLAGS=-std=c++2a -Wall -fdiagnostics-color=auto -Wimplicit-fallthrough -Wall -Wextra -Wextra-semi -Wpedantic -Wvla -Wnull-dereference -Wswitch-enum -Wno-unused-parameter  -march=native 
