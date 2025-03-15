// Tiny Encryption Algorithm: https://www.highperformancegraphics.org/previous/www_2010/media/GPUAlgorithms/HPG2010_GPUAlgorithms_Zafar.pdf
uint tea(uint v0, uint v1) {
    uint sum = 0;
    for (uint i = 0; i < 16; i++) {
        sum += 0x9E3779B9;
        v0 += ((v1 << 4) + 0xA341316C) ^ (v1 + sum) ^ ((v1 >> 5) + 0xC8013EA4);
        v1 += ((v0 << 4) + 0xAD90777D) ^ (v0 + sum) ^ ((v0 >> 5) + 0x7E95761E);
    }

    return v0;
}

uint createSeed(uint val1, uint val2) {
    return tea(val1, val2);
}

// Numerical Recipes linear congruential generator
uint lcg(inout uint seed) {
    uint mA = 1664525;
    uint iC = 1013904223;
    seed = (mA * seed + iC);
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
