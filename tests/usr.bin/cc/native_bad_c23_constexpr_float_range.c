/* a constexpr float's initializer keeps its value in float (C23 6.7.1p6): 1e40 is out of its range */
constexpr float x = 1e40;
