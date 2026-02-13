//!HOOK MAIN
//!BIND HOOKED
//!BIND PREV
//!DESC grayscale -> blur -> motion detect

////////////////////////////////////////////////////////////////////////
// AUTO-GENERATED FILE — DO NOT EDIT
// sigma=4.00 radius=4.00 threshold=0.025 size=1280x720
////////////////////////////////////////////////////////////////////////

#define SIGMA 4.00
#define RADIUS 4.00
#define MOTION_THRESHOLD 0.025

#define get_weight(x) (exp(-(x)*(x)/(2.0*SIGMA*SIGMA)))

vec4 hook() {
    vec4 curr = linearize(textureLod(HOOKED_raw, HOOKED_pos, 0.0) * HOOKED_mul);

    float gray = dot(curr.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec4 gray_vec = vec4(gray, gray, gray, 0.0);

    vec4 csum = gray_vec;
    float wsum = 1.0;
    for (float i = 1.0; i <= RADIUS; ++i) {
        float w = get_weight(i);
        csum += (textureLod(HOOKED_raw, HOOKED_pos + vec2(0.0, -i)/HOOKED_size.xy, 0.0)
              + textureLod(HOOKED_raw, HOOKED_pos + vec2(0.0,  i)/HOOKED_size.xy, 0.0)) * w;
        wsum += 2.0 * w;
    }
    vec4 blur_y = csum / wsum;

    csum = blur_y;
    wsum = 1.0;
    for (float i = 1.0; i <= RADIUS; ++i) {
        float w = get_weight(i);
        csum += (textureLod(HOOKED_raw, HOOKED_pos + vec2(-i, 0.0)/HOOKED_size.xy, 0.0)
              + textureLod(HOOKED_raw, HOOKED_pos + vec2( i, 0.0)/HOOKED_size.xy, 0.0)) * w;
        wsum += 2.0 * w;
    }
    vec4 blur = csum / wsum;

    ivec3 pos = ivec3(HOOKED_pos * HOOKED_size, 0);
    vec4 prev = imageLoad(PREV, pos);
    float diff = abs(blur.r - prev.r);
    float motion = diff > MOTION_THRESHOLD ? 1.0 : 0.0;

    imageStore(PREV, pos, blur);

    return vec4(motion);
}

//!TEXTURE PREV
//!SIZE 1280 720 1
//!FORMAT r8
//!STORAGE
