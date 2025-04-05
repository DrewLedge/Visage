// Jenkins one at a time hash by Bob Jenkins
uint jenkins(uint v) {
    uint hash = v;
    hash += hash << 10;
    hash ^= hash >> 6;
    hash += hash << 3;
    hash ^= hash >> 11;
    hash += hash << 15;

    return hash;
}

uint createSeed(uint val1, uint val2) {
    uint val = val1 * 0x9e3779b9 + val2;
    return jenkins(val);
}

// Numerical Recipes linear congruential generator
uint lcg(inout uint seed) {
    seed = seed * 1664525 + 1013904223;
    return seed;
}

float random(inout uint seed) {
    float s = float(lcg(seed));
    return s / 4294967296.0f;
}

vec2 jitter(inout uint seed) {
    float r = random(seed);
    float g = random(seed);
    return vec2(r, g);
}
