#version 460

layout(location = 0) flat in uint outDiscard;

void main() {
    if (outDiscard == 1) {
        discard;
    }

    gl_FragDepth = gl_FragCoord.z;
}
