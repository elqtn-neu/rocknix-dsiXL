// dsi-lcd-color: dsi-lcd.frag with the DS screen's own colours first: duller, a little warmer (the profile of
// SuperDrastic's ds-*-color shaders and ROCKNIX's lcd1x+nds-color, the same polynomials). The DSi Menu's
// "DSi LCD + DS colors" (src/plat/lcd.c, mode 2) does the same on the CPU: keep the two in step.
//
// libdsflip (ROCKNIXDS) runs it on the GPU when the game's Screen setting is "DSi LCD": install.sh puts it in
// /storage/.config/drastic/shaders/. Each DS pixel becomes one whole-number block (4x4 on the RG DS Plus' 1024x768:
// the whole panel, no border), drawn the way a DSi's LCD shows it: red, green and blue upright stripes, a fine gap
// between columns and a darker one between rows. At 4x the stripes are 1 panel pixel each, the gaps the 4th.
// At 2x (the RG DS) there's no room for stripes: just the gaps, lighter.
// dsflip-viewport: integer
//
// Cost: a texture read and a few multiplies a pixel, like ds-grid-2x.
// highp: mediump on the Mali-G52 is fp16 and loses the cell phase near x=256. ESSL 1.00, no #version.

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec2 v_texcoord;
uniform sampler2D u_texture;
uniform vec2 u_texture_size;
uniform vec2 u_output_size;

const vec2 DS_SIZE = vec2(256.0, 192.0);
const float GAP_ROW = 0.78125;   // 200/256, as lcd.c
const float GAP_COL = 0.8984;    // 230/256
const float STRIPE  = 0.9609;    // 246/256: the other two colours in a stripe

// the DS screen's colours: to linear light, the DS's mix of the three, back
vec3 ndsColor(vec3 c)
{
    vec3 lin = clamp(((((0.0450218219) * c + -0.15107093) * c + 0.980162582) * c + 0.0161565261) * c + -0.000192440297, 0.0, 1.0);
    lin = mat3( 0.87,  0.10,  0.10,
                0.255, 0.645, 0.17,
               -0.125, 0.255, 0.73) * lin;
    vec3 t = sqrt(clamp(lin, 0.0, 1.0));
    return clamp(((((((0.397060658) * t + -1.36815007) * t + 1.87310197) * t + -1.321717) * t + 0.564332005) * t + 0.855903541) * t + -0.000458519754, 0.0, 1.0);
}

void main()
{
    float s = max(1.0, floor(min(u_output_size.x / DS_SIZE.x, u_output_size.y / DS_SIZE.y)));
    vec2 imgSize = DS_SIZE * s;
    vec2 origin = floor((u_output_size - imgSize) * 0.5);
    vec2 local = floor(v_texcoord * u_output_size) - origin;
    if (any(lessThan(local, vec2(0.0))) || any(greaterThanEqual(local, imgSize))) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    // one texel per DS pixel at 1x, one per output pixel at 2x 3D (512x384): nearest, exact at a whole scale
    vec2 texel = floor(local * u_texture_size / imgSize);
    vec4 c = SWIZ(texture2D(u_texture, (texel + 0.5) / u_texture_size));

    vec2 f = local - floor(local / s) * s;               // 0 .. s-1 inside the DS pixel
    vec3 m = vec3(1.0);
    if (s >= 3.0) {
        if (f.x >= s - 1.0) m = vec3(GAP_COL);
        else {
            float ch = floor(f.x * 3.0 / (s - 1.0));       // 0 red, 1 green, 2 blue
            m = vec3(STRIPE);
            if (ch < 0.5) m.r = 1.0; else if (ch < 1.5) m.g = 1.0; else m.b = 1.0;
        }
        if (f.y >= s - 1.0) m *= GAP_ROW;
    } else if (s >= 2.0) {
        if (f.x >= 1.0) m *= 0.94;
        if (f.y >= 1.0) m *= 0.86;
    }
    gl_FragColor = vec4(ndsColor(c.rgb) * m, c.a);
}
