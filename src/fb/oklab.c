#include <math.h>
#include <string.h>

#include "ash/fb/oklab.h"

static const float SRGB_TO_LINEAR[256] = {
    0.0000000000f, 0.0003035270f, 0.0006070540f, 0.0009105810f, 0.0012141080f, 0.0015176350f, 0.0018211619f, 0.0021246888f,
    0.0024282159f, 0.0027317430f, 0.0030352699f, 0.0033465356f, 0.0036765069f, 0.0040247170f, 0.0043914421f, 0.0047769533f,
    0.0051815170f, 0.0056053917f, 0.0060488326f, 0.0065120910f, 0.0069954102f, 0.0074990317f, 0.0080231922f, 0.0085681248f,
    0.0091340570f, 0.0097212177f, 0.0103298230f, 0.0109600937f, 0.0116122449f, 0.0122864870f, 0.0129830306f, 0.0137020806f,
    0.0144438436f, 0.0152085144f, 0.0159962922f, 0.0168073755f, 0.0176419523f, 0.0185002182f, 0.0193823613f, 0.0202885624f,
    0.0212190095f, 0.0221738834f, 0.0231533647f, 0.0241576303f, 0.0251868572f, 0.0262412224f, 0.0273208916f, 0.0284260381f,
    0.0295568332f, 0.0307134409f, 0.0318960287f, 0.0331047624f, 0.0343398079f, 0.0356013142f, 0.0368894450f, 0.0382043645f,
    0.0395462364f, 0.0409151986f, 0.0423114114f, 0.0437350273f, 0.0451862030f, 0.0466650836f, 0.0481718220f, 0.0497065634f,
    0.0512694679f, 0.0528606549f, 0.0544802807f, 0.0561284944f, 0.0578054339f, 0.0595112406f, 0.0612460710f, 0.0630100295f,
    0.0648032799f, 0.0666259527f, 0.0684781820f, 0.0703601092f, 0.0722718611f, 0.0742135793f, 0.0761853904f, 0.0781874284f,
    0.0802198276f, 0.0822827145f, 0.0843762159f, 0.0865004659f, 0.0886556059f, 0.0908417329f, 0.0930589810f, 0.0953074843f,
    0.0975873619f, 0.0998987406f, 0.1022417471f, 0.1046164930f, 0.1070231125f, 0.1094617173f, 0.1119324341f, 0.1144353822f,
    0.1169706732f, 0.1195384338f, 0.1221387982f, 0.1247718409f, 0.1274376959f, 0.1301364899f, 0.1328683347f, 0.1356333494f,
    0.1384316236f, 0.1412633061f, 0.1441284865f, 0.1470272839f, 0.1499598026f, 0.1529261619f, 0.1559264660f, 0.1589608639f,
    0.1620294005f, 0.1651322246f, 0.1682693958f, 0.1714410931f, 0.1746473908f, 0.1778884083f, 0.1811642349f, 0.1844749898f,
    0.1878207624f, 0.1912016720f, 0.1946178079f, 0.1980693042f, 0.2015562356f, 0.2050787061f, 0.2086368501f, 0.2122307271f,
    0.2158605307f, 0.2195262313f, 0.2232279778f, 0.2269658893f, 0.2307400703f, 0.2345506549f, 0.2383976579f, 0.2422811985f,
    0.2462013960f, 0.2501583695f, 0.2541521788f, 0.2581829131f, 0.2622507215f, 0.2663556635f, 0.2704978585f, 0.2746773660f,
    0.2788943350f, 0.2831487954f, 0.2874408960f, 0.2917706966f, 0.2961383164f, 0.3005438447f, 0.3049873710f, 0.3094689548f,
    0.3139887452f, 0.3185468316f, 0.3231432438f, 0.3277781308f, 0.3324515820f, 0.3371636569f, 0.3419144452f, 0.3467040956f,
    0.3515326977f, 0.3564002514f, 0.3613068759f, 0.3662526906f, 0.3712377846f, 0.3762622178f, 0.3813261092f, 0.3864295185f,
    0.3915725648f, 0.3967553079f, 0.4019778669f, 0.4072403014f, 0.4125427008f, 0.4178851545f, 0.4232677519f, 0.4286905527f,
    0.4341537058f, 0.4396572411f, 0.4452012479f, 0.4507858455f, 0.4564110637f, 0.4620770514f, 0.4677838385f, 0.4735315442f,
    0.4793202281f, 0.4851499796f, 0.4910208881f, 0.4969330430f, 0.5028865933f, 0.5088814497f, 0.5149177909f, 0.5209956765f,
    0.5271152258f, 0.5332764983f, 0.5394796133f, 0.5457245708f, 0.5520114899f, 0.5583404899f, 0.5647116303f, 0.5711249113f,
    0.5775805116f, 0.5840784907f, 0.5906189084f, 0.5972018838f, 0.6038274169f, 0.6104956269f, 0.6172066331f, 0.6239604354f,
    0.6307572126f, 0.6375969648f, 0.6444797516f, 0.6514056921f, 0.6583748460f, 0.6653873324f, 0.6724432111f, 0.6795425415f,
    0.6866854429f, 0.6938719153f, 0.7011020184f, 0.7083759308f, 0.7156936526f, 0.7230552435f, 0.7304608822f, 0.7379105687f,
    0.7454043627f, 0.7529423237f, 0.7605246305f, 0.7681512833f, 0.7758223414f, 0.7835379243f, 0.7912980318f, 0.7991028428f,
    0.8069523573f, 0.8148466945f, 0.8227858543f, 0.8307699561f, 0.8387991190f, 0.8468732834f, 0.8549926877f, 0.8631572723f,
    0.8713672161f, 0.8796223402f, 0.8879231811f, 0.8962693810f, 0.9046613574f, 0.9130986929f, 0.9215820432f, 0.9301108718f,
    0.9386858940f, 0.9473065734f, 0.9559735060f, 0.9646862745f, 0.9734454751f, 0.9822505713f, 0.9911022186f, 1.0000000000f,
};

static float cbrtf_est(float a)
{
    uint32_t u;
    memcpy(&u, &a, sizeof u);
    u = u / 3u + 709921077u;
    float x;
    memcpy(&x, &u, sizeof x);
    return (1.0f / 3.0f) * (a / (x * x) + (x + x));
}

static uint32_t linear_to_srgb(float c)
{
    float v = (c > 0.0031308f)
        ? 255.0f * 1.055f * powf(c, 1.0f / 2.4f) - 255.0f * 0.055f
        : 255.0f * 12.92f * c;
    return (uint32_t)v;
}

static float clamp01(float x)
{
    if (x < 0.0f)
        return 0.0f;
    if (x > 1.0f)
        return 1.0f;
    return x;
}

ash_rgba ash_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | 0xff000000u;
}

ash_rgba ash_rgba_make(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

uint32_t ash_rgba_r(ash_rgba c) { return c & 0xffu; }
uint32_t ash_rgba_g(ash_rgba c) { return (c >> 8) & 0xffu; }
uint32_t ash_rgba_b(ash_rgba c) { return (c >> 16) & 0xffu; }
uint32_t ash_rgba_a(ash_rgba c) { return (c >> 24) & 0xffu; }

ash_oklab ash_rgba_to_oklab(ash_rgba c)
{
    float r = SRGB_TO_LINEAR[ash_rgba_r(c)];
    float g = SRGB_TO_LINEAR[ash_rgba_g(c)];
    float b = SRGB_TO_LINEAR[ash_rgba_b(c)];
    float alpha = (float)ash_rgba_a(c) * (1.0f / 255.0f);

    float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;

    float l_ = cbrtf_est(l);
    float m_ = cbrtf_est(m);
    float s_ = cbrtf_est(s);

    ash_oklab o;
    o.l = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
    o.a = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
    o.b = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;
    o.alpha = alpha;
    return o;
}

ash_rgba ash_oklab_to_rgba(ash_oklab o)
{
    float l_ = o.l + 0.3963377774f * o.a + 0.2158037573f * o.b;
    float m_ = o.l - 0.1055613458f * o.a - 0.0638541728f * o.b;
    float s_ = o.l - 0.0894841775f * o.a - 1.2914855480f * o.b;

    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;

    float r = clamp01(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
    float g = clamp01(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
    float b = clamp01(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
    float alpha = clamp01(o.alpha);

    uint32_t rr = linear_to_srgb(r);
    uint32_t gg = linear_to_srgb(g);
    uint32_t bb = linear_to_srgb(b);
    uint32_t aa = (uint32_t)(alpha * 255.0f);

    return rr | (gg << 8) | (bb << 16) | (aa << 24);
}

float ash_rgba_lightness(ash_rgba c)
{
    return ash_rgba_to_oklab(c).l;
}

ash_rgba ash_rgba_blend(ash_rgba bottom, ash_rgba top)
{
    ash_oklab lo = ash_rgba_to_oklab(bottom);
    ash_oklab hi = ash_rgba_to_oklab(top);

    float top_a = hi.alpha;
    float bot_a = lo.alpha * (1.0f - top_a);
    float alpha = top_a + bot_a;

    ash_oklab out;
    out.l = hi.l * top_a + lo.l * bot_a;
    out.a = hi.a * top_a + lo.a * bot_a;
    out.b = hi.b * top_a + lo.b * bot_a;
    out.alpha = alpha;

    float inv = (alpha > 0.0f) ? (1.0f / alpha) : 0.0f;
    out.l *= inv;
    out.a *= inv;
    out.b *= inv;
    return ash_oklab_to_rgba(out);
}

void ash_contrast_init(ash_contrast *c, ash_rgba dark, ash_rgba light)
{
    float ld = ash_rgba_lightness(dark);
    float ll = ash_rgba_lightness(light);
    if (ld > ll) {
        ash_rgba t = dark;
        dark = light;
        light = t;
        float tf = ld;
        ld = ll;
        ll = tf;
    }
    c->dark = dark;
    c->light = light;
    c->threshold = (ld + ll) * 0.5f;
    memset(c->key, 0, sizeof c->key);
    memset(c->val, 0, sizeof c->val);
    memset(c->used, 0, sizeof c->used);
}

ash_rgba ash_contrasted(ash_contrast *c, ash_rgba color)
{
    uint64_t h = (uint64_t)color * 6364136223846793005ull;
    size_t idx = (size_t)(h >> 56);

    if (c->used[idx] && c->key[idx] == color)
        return c->val[idx];

    int is_dark = ash_rgba_lightness(color) < c->threshold;
    ash_rgba out = is_dark ? c->light : c->dark;
    c->key[idx] = color;
    c->val[idx] = out;
    c->used[idx] = 1;
    return out;
}
